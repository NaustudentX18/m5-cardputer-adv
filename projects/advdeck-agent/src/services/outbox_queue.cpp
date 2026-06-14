// src/services/outbox_queue.cpp
//
// Implementation of OutboxQueue. The queue owns no in-memory state
// beyond a pointer to IStorage; every operation reads the JSONL,
// mutates in memory, and writes it back. See
// PHASE-2-INTERFACES.md §4.1 / §5.2 for the on-disk protocol.
//
// Concurrency note (per §4.2): the firmware is the single writer of
// pending.jsonl. The bridge is a reader that mutates the file in
// place (in_flight transition); if both the firmware and the
// bridge run at the same time, the firmware's read-modify-write
// here can clobber a bridge-side status flip. The cardputer UI
// gates the bridge loop on a user action, so concurrent writes
// are not a Phase 2 concern; the comment is here so a future
// reader doesn't add background work without re-reading the
// contract.

#include "advdeck/outbox_queue.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace advdeck {
namespace {

constexpr const char* kOutboxSubdir = "outbox";
constexpr const char* kPendingFile = "pending.jsonl";
constexpr const char* kResultsSubdir = "results";

// 4-digit year + 2-digit month + 2-digit day. Used in both id
// generation and the date-prefix scan.
std::string today_yyyymmdd_utc() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[16];
  std::strftime(buf, sizeof(buf), "%Y%m%d", &tm);
  return std::string(buf);
}

// ISO8601 UTC "now" with second resolution. Matches the format the
// rest of the project uses for created_at / updated_at.
std::string now_iso8601_utc() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

// True if `c` is an ASCII digit.
bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

// Parse the NNN portion of a "req-YYYYMMDD-NNN" id. Returns -1 if
// the id is malformed or the suffix is not a non-empty digit run in
// the 3-6 character range. Matches the schema's pattern.
int parse_seq_suffix(const std::string& id, const std::string& yyyymmdd) {
  const std::string prefix = "req-" + yyyymmdd + "-";
  if (id.size() <= prefix.size()) return -1;
  if (id.compare(0, prefix.size(), prefix) != 0) return -1;
  const std::string tail = id.substr(prefix.size());
  if (tail.size() < 3 || tail.size() > 6) return -1;
  for (char c : tail) {
    if (!is_digit(c)) return -1;
  }
  // Avoid std::stoi's UB-on-overflow: atoi would too, but atoi
  // returns 0 on failure and we control the input width. 6 digits
  // fits in int with margin to spare.
  int v = 0;
  for (char c : tail) {
    v = v * 10 + (c - '0');
  }
  return v;
}

// Read the whole pending.jsonl into a vector of PendingRequest.
// Unparseable lines are skipped silently — the bridge might
// briefly leave a partial edit in place and we don't want a single
// bad line to brick the queue. Returns the raw lines (without the
// trailing newline) alongside, in case the caller wants to rewrite
// the file preserving the exact byte order.
struct JsonlRow {
  PendingRequest req;
  std::string raw;       // original line without trailing \n
  bool ok = false;
};

std::vector<JsonlRow> read_rows(IStorage& storage,
                                const std::string& path) {
  std::vector<JsonlRow> out;
  std::string body = storage.read_file_or(path, "");
  if (body.empty()) return out;
  std::size_t start = 0;
  while (start <= body.size()) {
    std::size_t end = body.find('\n', start);
    if (end == std::string::npos) end = body.size();
    if (end > start) {
      std::string line = body.substr(start, end - start);
      JsonlRow row;
      row.raw = line;
      try {
        auto j = nlohmann::json::parse(line);
        if (j.is_object()) {
          auto& o = j;
          if (o.contains("id") && o["id"].is_string())
            row.req.id = o["id"].get<std::string>();
          if (o.contains("project") && o["project"].is_string())
            row.req.project = o["project"].get<std::string>();
          if (o.contains("type") && o["type"].is_string())
            row.req.type = o["type"].get<std::string>();
          if (o.contains("inputs") && o["inputs"].is_array()) {
            for (const auto& v : o["inputs"]) {
              if (v.is_string())
                row.req.inputs.push_back(v.get<std::string>());
            }
          }
          if (o.contains("created_at") && o["created_at"].is_string())
            row.req.created_at = o["created_at"].get<std::string>();
          if (o.contains("status") && o["status"].is_string())
            row.req.status = o["status"].get<std::string>();
          if (o.contains("attempts") && o["attempts"].is_number()) {
            row.req.attempts = o["attempts"].get<int>();
          }
          row.ok = true;
        }
      } catch (const std::exception&) {
        // Skip malformed lines; row.ok stays false.
      }
      out.push_back(std::move(row));
    }
    if (end == body.size()) break;
    start = end + 1;
  }
  return out;
}

// Serialise the request back to a JSON object with stable field
// order. The bridge CLI and the E2E test rely on the field order
// being predictable; nlohmann::json preserves insertion order in
// its underlying map.
nlohmann::json request_to_json(const PendingRequest& r) {
  nlohmann::json j;
  j["id"] = r.id;
  j["project"] = r.project;
  j["type"] = r.type;
  j["inputs"] = r.inputs;
  j["created_at"] = r.created_at;
  j["status"] = r.status;
  j["attempts"] = r.attempts;
  return j;
}

// Rewrite pending.jsonl with the supplied rows. Empty file is
// represented as the empty string (no trailing newline), matching
// the convention the bridge CLI uses.
std::string write_rows(IStorage& storage, const std::string& path,
                       const std::vector<PendingRequest>& rows,
                       std::string* err) {
  if (rows.empty()) {
    std::string e = storage.write_file(path, "");
    if (!e.empty()) {
      if (err) *err = e;
      return e;
    }
    if (err) err->clear();
    return "";
  }
  std::ostringstream os;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i > 0) os << '\n';
    os << request_to_json(rows[i]).dump();
  }
  os << '\n';
  std::string e = storage.write_file(path, os.str());
  if (!e.empty()) {
    if (err) *err = e;
    return e;
  }
  if (err) err->clear();
  return "";
}

}  // namespace

std::string generate_request_id(IStorage& storage,
                                const std::string& outbox_pending_path,
                                std::string* err) {
  std::string yyyymmdd = today_yyyymmdd_utc();
  std::vector<JsonlRow> rows = read_rows(storage, outbox_pending_path);
  int max_seq = 0;
  for (const auto& r : rows) {
    if (!r.ok) continue;
    int n = parse_seq_suffix(r.req.id, yyyymmdd);
    if (n > max_seq) max_seq = n;
  }
  // Schema allows 3-6 digit NNN. After 999999 we can't continue
  // the sequence without breaking the schema. This is well past
  // any realistic daily request rate; the dry-run provider in
  // Phase 2 only generates a handful per session. Bump to 7+ digits
  // past the cap — the schema rejects it, which is the right
  // failure mode: the firmware operator sees the bridge stop
  // accepting new requests for the day and can address it.
  if (max_seq >= 999999) {
    if (err) *err = "request sequence exhausted for " + yyyymmdd;
    return "";
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "req-%s-%06d", yyyymmdd.c_str(),
                max_seq + 1);
  if (err) err->clear();
  return std::string(buf);
}

OutboxQueue::OutboxQueue(IStorage& storage, std::string storage_root)
    : storage_(&storage), storage_root_(std::move(storage_root)) {}

std::string OutboxQueue::pending_path() const {
  return storage_->join(storage_->join(storage_root_, kOutboxSubdir),
                        kPendingFile);
}

std::string OutboxQueue::results_dir() const {
  // <storage_root>/outbox/results — the parent of each
  // <request_id>/ subdir the bridge writes. The bridge layout
  // (§2) puts per-request files under results/<id>/result.json.
  return storage_->join(storage_->join(storage_root_, kOutboxSubdir),
                        kResultsSubdir);
}

std::string OutboxQueue::enqueue(const std::string& project_slug,
                                 const std::string& request_type,
                                 const std::vector<std::string>& inputs,
                                 std::string* err) {
  if (project_slug.empty()) {
    if (err) *err = "enqueue: empty project slug";
    return "";
  }
  if (request_type.empty()) {
    if (err) *err = "enqueue: empty request type";
    return "";
  }
  if (inputs.empty()) {
    if (err) *err = "enqueue: inputs must contain at least one filename";
    return "";
  }
  // Make sure the outbox dir exists before we try to append.
  std::string outbox_dir = storage_->join(storage_root_, kOutboxSubdir);
  std::string e = storage_->ensure_dir(outbox_dir);
  if (!e.empty()) {
    if (err) *err = "ensure_dir(outbox): " + e;
    return "";
  }
  std::string path = pending_path();
  std::string id = generate_request_id(*storage_, path, err);
  if (id.empty()) {
    // err is already set by generate_request_id.
    return "";
  }
  PendingRequest r;
  r.id = id;
  r.project = project_slug;
  r.type = request_type;
  r.inputs = inputs;
  r.created_at = now_iso8601_utc();
  r.status = "pending";
  r.attempts = 0;
  // Append, not rewrite: read what's there, add a row, write back.
  std::vector<JsonlRow> rows = read_rows(*storage_, path);
  std::vector<PendingRequest> as_requests;
  as_requests.reserve(rows.size() + 1);
  for (const auto& row : rows) {
    if (row.ok) as_requests.push_back(row.req);
  }
  as_requests.push_back(r);
  e = write_rows(*storage_, path, as_requests, err);
  if (!e.empty()) return "";
  if (err) err->clear();
  return id;
}

std::string OutboxQueue::load_all(std::vector<PendingRequest>* out,
                                  std::string* err) {
  if (out) out->clear();
  std::vector<JsonlRow> rows = read_rows(*storage_, pending_path());
  if (out) {
    out->reserve(rows.size());
    for (const auto& r : rows) {
      if (r.ok) out->push_back(r.req);
    }
  }
  if (err) err->clear();
  return "";
}

std::string OutboxQueue::mark_in_flight(const std::string& id,
                                        std::string* err) {
  std::vector<JsonlRow> rows = read_rows(*storage_, pending_path());
  std::vector<PendingRequest> as_requests;
  as_requests.reserve(rows.size());
  bool found = false;
  for (auto& row : rows) {
    if (!row.ok) continue;
    if (row.req.id == id) {
      row.req.status = "in_flight";
      found = true;
    }
    as_requests.push_back(row.req);
  }
  if (!found) {
    if (err) *err = "request not found: " + id;
    return *err;
  }
  std::string e = write_rows(*storage_, pending_path(), as_requests, err);
  if (!e.empty()) return e;
  if (err) err->clear();
  return "";
}

std::string OutboxQueue::mark_terminal(const std::string& id,
                                      const std::string& final_status,
                                      std::string* err) {
  if (final_status != "done" && final_status != "error") {
    if (err) *err = "mark_terminal: final_status must be \"done\" or \"error\"";
    return *err;
  }
  std::vector<JsonlRow> rows = read_rows(*storage_, pending_path());
  std::vector<PendingRequest> as_requests;
  as_requests.reserve(rows.size());
  bool found = false;
  for (auto& row : rows) {
    if (!row.ok) continue;
    if (row.req.id == id) {
      row.req.status = final_status;
      // attempts counts the *failed* attempts; the current attempt
      // is implicit. So on a successful terminal flip we keep
      // attempts as-is, on an error we increment. Matches what
      // the bridge CLI logs in its own per-request metadata.
      if (final_status == "error") {
        row.req.attempts = row.req.attempts + 1;
      }
      found = true;
    }
    as_requests.push_back(row.req);
  }
  if (!found) {
    if (err) *err = "request not found: " + id;
    return *err;
  }
  std::string e = write_rows(*storage_, pending_path(), as_requests, err);
  if (!e.empty()) return e;
  if (err) err->clear();
  return "";
}

}  // namespace advdeck
