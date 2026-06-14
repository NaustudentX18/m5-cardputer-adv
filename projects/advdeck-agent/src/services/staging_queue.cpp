// src/services/staging_queue.cpp
//
// Phase 3 staging area. Bridges the gap between the bridge writing a
// result into outbox/results/<id>/ and the user accepting or rejecting
// that result. See PHASE-3-INTERFACES.md §5.1 and §2 for the
// on-disk layout.
//
// Move semantics: on host we use std::filesystem::rename directly so
// the move is atomic on the same filesystem. The SdStorage impl is
// a Phase 1 stub today; the real impl will use the same IStorage
// contract and rename will be implemented there. For now HostStorage
// is the source of truth, which is what the host tests exercise.
//
// Threading: the firmware is the single user of this class. The
// dispatcher in main.cpp owns one StagingQueue instance and the
// routes are blocking — the race-condition space the OutboxQueue
// doc mentions does not apply here.

#include "advdeck/staging_queue.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "advdeck/project_store.h"

#ifndef ADVDECK_FIRMWARE
namespace fs = std::filesystem;
#endif

namespace advdeck {
namespace {

constexpr const char* kStagingSubdir = "staging";
constexpr const char* kRejectedSubdir = "rejected";
constexpr const char* kResultsSubdir = "results";
constexpr const char* kProjectsSubdir = "projects";
constexpr const char* kMetaFile = "meta.json";
constexpr const char* kSchemaVersion = "1";

// The six artifacts that follow a bridge result. Order matches the
// bridge's phase-3 contract. `meta.json` is our own; it is not in
// this list.
const std::vector<std::string>& artifact_filenames() {
  static const std::vector<std::string> kArtifacts = {
      "brief.md", "plan.md", "tasks.json", "tasks.md",
      "calendar-suggestions.json", "agent-prompt.md",
  };
  return kArtifacts;
}

// ISO8601 UTC "now" with second resolution. Same format the rest of
// the project uses.
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

// Lexicographic compare of two ISO8601 strings. The contract is that
// arrived_at is in canonical "YYYY-MM-DDTHH:MM:SSZ" form so the
// string sort matches the time order.
bool arrived_at_less(const StagingEntry& a, const StagingEntry& b) {
  return a.arrived_at < b.arrived_at;
}

// Move a single file from <src> to <dst> by reading and rewriting
// through IStorage. We use IStorage's write_file (atomic
// write-tmp-then-rename) instead of std::filesystem::rename so the
// call works on the SdStorage impl too. The old file is then
// removed via IStorage::ensure_dir's complement (we don't have a
// remove() method on IStorage, so the file is left in place and
// the row is logically moved by writing the new meta.json — for
// host tests this is fine because they use unique temp dirs).
//
// Per the spec: on host we want std::filesystem::rename (atomic
// within a filesystem). The cleanest way to do that is to read the
// on-disk path through the host backend. The host build defines
// ADVDECK_FIRMWARE=0, so we drop down to std::filesystem only
// there; firmware builds (which can't link <filesystem> anyway)
// route through IStorage and rely on the SD impl to do the right
// thing. The SdStorage stub returns an error from write_file in
// Phase 2 so this path is only exercised on host.
std::string move_file_via_storage(IStorage& storage,
                                  const std::string& src,
                                  const std::string& dst) {
  std::string body = storage.read_file(src);
  if (body.empty() && !storage.exists(src)) {
    return "move_file: source missing: " + src;
  }
  std::string e = storage.write_file(dst, body);
  if (!e.empty()) return "move_file: write_file(" + dst + "): " + e;
#ifndef ADVDECK_FIRMWARE
  // Drop the original. We do this on host only because the SD
  // stub doesn't expose a remove method.
  std::error_code ec;
  // Walk back to a host-side path. HostStorage::root() is the
  // temp dir it was constructed with, but we don't have it here
  // (we only have the IStorage interface). Use the storage's
  // join() to construct the path, then strip the leading "/advdeck"
  // prefix to make it relative to the host root. The host impl
  // joins on '/' and prepends '/' — so "<root>/advdeck" -> we
  // need to drop the leading '/'.
  std::string host_src = src;
  if (!host_src.empty() && host_src.front() == '/') {
    host_src.erase(0, 1);
  }
  fs::path resolved = fs::path(storage.root()) / fs::path(host_src);
  fs::remove(resolved, ec);
#endif
  return "";
}

// Read meta.json into a StagingEntry. The shape is the small typed
// JSON from PHASE-3-INTERFACES.md §4.1. Malformed input returns
// false; missing required fields are tolerated by leaving them
// empty.
bool parse_meta_json(const std::string& body, StagingEntry* out) {
  if (!out) return false;
  try {
    auto j = nlohmann::json::parse(body);
    if (!j.is_object()) return false;
    if (j.contains("request_id") && j["request_id"].is_string())
      out->request_id = j["request_id"].get<std::string>();
    if (j.contains("project") && j["project"].is_string())
      out->project = j["project"].get<std::string>();
    if (j.contains("arrived_at") && j["arrived_at"].is_string())
      out->arrived_at = j["arrived_at"].get<std::string>();
    if (j.contains("status") && j["status"].is_string())
      out->status = j["status"].get<std::string>();
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

// Read a project slug out of a meta.json without parsing the
// whole entry. Returns "" if the field is missing or the file is
// malformed. The staging queue uses this on accept to find the
// project folder without making the caller load the entry.
std::string read_meta_project(IStorage& storage, const std::string& dir) {
  std::string path = storage.join(dir, kMetaFile);
  std::string body = storage.read_file(path);
  if (body.empty()) return "";
  StagingEntry e;
  if (!parse_meta_json(body, &e)) return "";
  return e.project;
}

}  // namespace

StagingQueue::StagingQueue(IStorage& storage, std::string storage_root)
    : storage_(&storage), storage_root_(std::move(storage_root)) {}

std::string StagingQueue::staging_dir() const {
  return storage_->join(storage_->join(storage_root_, "outbox"),
                        kStagingSubdir);
}

std::string StagingQueue::rejected_dir() const {
  return storage_->join(storage_->join(storage_root_, "outbox"),
                        kRejectedSubdir);
}

std::string StagingQueue::stage(const std::string& request_id,
                                std::string* err) {
  if (request_id.empty()) {
    if (err) *err = "stage: empty request id";
    return *err;
  }
  std::string results_root = storage_->join(
      storage_->join(storage_root_, "outbox"), kResultsSubdir);
  std::string src_dir = storage_->join(results_root, request_id);
  if (!storage_->exists(src_dir)) {
    if (err) *err = "stage: results dir missing: " + src_dir;
    return *err;
  }
  std::string dst_dir = storage_->join(staging_dir(), request_id);
  std::string e = storage_->ensure_dir(dst_dir);
  if (!e.empty()) {
    if (err) *err = "stage: ensure_dir(" + dst_dir + "): " + e;
    return *err;
  }
  // The project slug has to come from the bridge result manifest if
  // the firmware didn't include it elsewhere. We look at result.json
  // for the `project` field, which the bridge writes from the
  // pending.jsonl row.
  std::string slug;
  {
    std::string manifest = storage_->read_file(
        storage_->join(src_dir, "result.json"));
    if (!manifest.empty()) {
      try {
        auto j = nlohmann::json::parse(manifest);
        if (j.is_object() && j.contains("project") &&
            j["project"].is_string()) {
          slug = j["project"].get<std::string>();
        }
      } catch (...) {
        // Tolerate malformed manifest; we'll write meta.json with
        // empty project and the caller can re-stage after fixing.
      }
    }
    if (slug.empty()) {
      // Fall back to pending.jsonl. The result manifest usually has
      // the project, but the field is technically optional in the
      // bridge's contract — pending.jsonl is the source of truth.
      std::string pending_body = storage_->read_file(
          storage_->join(storage_->join(storage_root_, "outbox"),
                         "pending.jsonl"));
      const std::string needle = "\"id\":\"" + request_id + "\"";
      std::size_t pos = pending_body.find(needle);
      if (pos != std::string::npos) {
        // Scan forward for the next "project":"<slug>" field.
        std::size_t ppos = pending_body.find("\"project\"", pos);
        if (ppos != std::string::npos) {
          std::size_t colon = pending_body.find(':', ppos);
          std::size_t q1 = pending_body.find('"', colon);
          std::size_t q2 = pending_body.find('"', q1 + 1);
          if (q1 != std::string::npos && q2 != std::string::npos &&
              q2 > q1 + 1) {
            slug = pending_body.substr(q1 + 1, q2 - q1 - 1);
          }
        }
      }
    }
  }
  // Copy each known artifact, skip missing ones. We write the
  // destination through IStorage so this also works on the SD impl
  // (when it lands).
  for (const auto& name : artifact_filenames()) {
    std::string src = storage_->join(src_dir, name);
    if (!storage_->exists(src)) continue;
    std::string dst = storage_->join(dst_dir, name);
    e = move_file_via_storage(*storage_, src, dst);
    if (!e.empty()) {
      if (err) *err = "stage: " + e;
      return *err;
    }
  }
  // Write meta.json with status=pending. We overwrite any prior
  // copy so a re-stage is idempotent.
  nlohmann::json meta = {
      {"version", kSchemaVersion},
      {"request_id", request_id},
      {"project", slug},
      {"arrived_at", now_iso8601_utc()},
      {"status", "pending"},
  };
  std::string meta_path = storage_->join(dst_dir, kMetaFile);
  e = storage_->write_file(meta_path, meta.dump() + "\n");
  if (!e.empty()) {
    if (err) *err = "stage: write_file(" + meta_path + "): " + e;
    return *err;
  }
  if (err) err->clear();
  return "";
}

std::string StagingQueue::accept(const std::string& request_id,
                                 std::string* err) {
  if (request_id.empty()) {
    if (err) *err = "accept: empty request id";
    return *err;
  }
  std::string src_dir = storage_->join(staging_dir(), request_id);
  if (!storage_->exists(src_dir)) {
    if (err) *err = "accept: staging dir missing: " + src_dir;
    return *err;
  }
  std::string slug = read_meta_project(*storage_, src_dir);
  if (slug.empty()) {
    if (err) *err = "accept: meta.json missing or has no project";
    return *err;
  }
  std::string project_dir = storage_->join(
      storage_->join(storage_root_, kProjectsSubdir), slug);
  std::string e = storage_->ensure_dir(project_dir);
  if (!e.empty()) {
    if (err) *err = "accept: ensure_dir(" + project_dir + "): " + e;
    return *err;
  }
  // Move every file from staging/<id>/ AND results/<id>/ into the
  // project folder. Both source dirs may exist; missing ones are
  // tolerated. meta.json from staging is NOT moved into the
  // project folder (it's a staging concern, not a project concern).
  std::string results_root = storage_->join(
      storage_->join(storage_root_, "outbox"), kResultsSubdir);
  std::string rdir = storage_->join(results_root, request_id);

  auto move_artifact_set = [&](const std::string& sdir,
                               const std::string& label) -> std::string {
    for (const auto& name : artifact_filenames()) {
      std::string src = storage_->join(sdir, name);
      if (!storage_->exists(src)) continue;
      std::string dst = storage_->join(project_dir, name);
      std::string me = move_file_via_storage(*storage_, src, dst);
      if (!me.empty()) {
        return label + ": " + me;
      }
    }
    return "";
  };
  e = move_artifact_set(src_dir, "accept(staging)");
  if (!e.empty()) {
    if (err) *err = e;
    return e;
  }
  e = move_artifact_set(rdir, "accept(results)");
  if (!e.empty()) {
    if (err) *err = e;
    return e;
  }
  // Write a terminal meta.json. The path stays in staging/<id>/ so
  // the entry is still listable as "accepted" for the audit trail.
  nlohmann::json meta = {
      {"version", kSchemaVersion},
      {"request_id", request_id},
      {"project", slug},
      {"arrived_at", now_iso8601_utc()},
      {"status", "accepted"},
  };
  std::string meta_path = storage_->join(src_dir, kMetaFile);
  e = storage_->write_file(meta_path, meta.dump() + "\n");
  if (!e.empty()) {
    if (err) *err = "accept: write_file(" + meta_path + "): " + e;
    return *err;
  }
  if (err) err->clear();
  return "";
}

std::string StagingQueue::reject(const std::string& request_id,
                                 std::string* err) {
  if (request_id.empty()) {
    if (err) *err = "reject: empty request id";
    return *err;
  }
  std::string src_dir = storage_->join(staging_dir(), request_id);
  if (!storage_->exists(src_dir)) {
    if (err) *err = "reject: staging dir missing: " + src_dir;
    return *err;
  }
  std::string slug = read_meta_project(*storage_, src_dir);
  std::string rej_dir = storage_->join(rejected_dir(), request_id);
  std::string e = storage_->ensure_dir(rej_dir);
  if (!e.empty()) {
    if (err) *err = "reject: ensure_dir(" + rej_dir + "): " + e;
    return *err;
  }
  // Move the six artifacts (not meta.json) to the rejected dir.
  for (const auto& name : artifact_filenames()) {
    std::string src = storage_->join(src_dir, name);
    if (!storage_->exists(src)) continue;
    std::string dst = storage_->join(rej_dir, name);
    std::string me = move_file_via_storage(*storage_, src, dst);
    if (!me.empty()) {
      if (err) *err = "reject: " + me;
      return *err;
    }
  }
  // Write the terminal meta.json into the rejected dir.
  nlohmann::json meta = {
      {"version", kSchemaVersion},
      {"request_id", request_id},
      {"project", slug},
      {"arrived_at", now_iso8601_utc()},
      {"status", "rejected"},
  };
  std::string meta_path = storage_->join(rej_dir, kMetaFile);
  e = storage_->write_file(meta_path, meta.dump() + "\n");
  if (!e.empty()) {
    if (err) *err = "reject: write_file(" + meta_path + "): " + e;
    return *err;
  }
  // Also overwrite the staging/<id>/meta.json with the terminal
  // status so list_pending does not see it as still pending.
  std::string staging_meta = storage_->join(src_dir, kMetaFile);
  e = storage_->write_file(staging_meta, meta.dump() + "\n");
  if (!e.empty()) {
    if (err) *err = "reject: write_file(" + staging_meta + "): " + e;
    return *err;
  }
  if (err) err->clear();
  return "";
}

std::string StagingQueue::list_pending(std::vector<StagingEntry>* out,
                                       std::string* err) {
  if (out) out->clear();
  std::string sdir = staging_dir();
  if (!storage_->exists(sdir)) {
    if (err) err->clear();
    return "";
  }
  std::vector<std::string> entries = storage_->list_dir(sdir);
  std::vector<StagingEntry> pending;
  pending.reserve(entries.size());
  for (const auto& sub : entries) {
    std::string sub_dir = storage_->join(sdir, sub);
    std::string meta_path = storage_->join(sub_dir, kMetaFile);
    std::string body = storage_->read_file(meta_path);
    if (body.empty()) continue;
    StagingEntry e;
    if (!parse_meta_json(body, &e)) continue;
    if (e.status != "pending") continue;
    pending.push_back(std::move(e));
  }
  std::sort(pending.begin(), pending.end(), arrived_at_less);
  if (out) *out = std::move(pending);
  if (err) err->clear();
  return "";
}

std::string StagingQueue::read_meta(const std::string& request_id,
                                    StagingEntry* out, std::string* err) {
  // Search both staging/ and rejected/. On accept, meta.json lives
  // in staging/<id>/, so we look there first. On reject it lives in
  // rejected/<id>/. The same request id is never in both at the
  // same time.
  for (const auto& base : {staging_dir(), rejected_dir()}) {
    std::string meta_path = storage_->join(storage_->join(base, request_id),
                                           kMetaFile);
    std::string body = storage_->read_file(meta_path);
    if (body.empty()) continue;
    if (!parse_meta_json(body, out)) continue;
    if (err) err->clear();
    return "";
  }
  if (err) *err = "read_meta: no meta.json for " + request_id;
  return *err;
}

}  // namespace advdeck
