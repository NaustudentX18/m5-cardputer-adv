// src/services/bridge_log.cpp
//
// Append-only JSONL log of bridge events. The log line is built
// field-by-field: we don't use nlohmann::json here because the
// caller already provides the `details` payload as a pre-serialized
// JSON object (so it can carry any shape without this layer having
// to know about it). The log rotates at 1 MiB by moving the
// current file aside to <path>.1 — single-rotation keeps the
// implementation small for Phase 2.

#include "advdeck/bridge_log.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#ifndef ADVDECK_FIRMWARE
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace advdeck {
namespace {

constexpr const char* kLogsSubdir = "logs";
constexpr const char* kLogFile = "bridge-import.log";

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

// Escape a string for inclusion in a JSON string literal. Mirrors
// the encoder in scripts/build_schema_embed.py, scoped to the
// control chars + quote + backslash we expect for timestamps,
// request_ids, and short event names.
std::string json_string_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    unsigned char u = static_cast<unsigned char>(c);
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (u < 0x20) {
          char esc[8];
          std::snprintf(esc, sizeof(esc), "\\u%04x", u);
          out += esc;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace

BridgeLog::BridgeLog(IStorage& storage, std::string storage_root)
    : storage_(&storage), storage_root_(std::move(storage_root)) {}

std::string BridgeLog::log_path() const {
  return storage_->join(storage_->join(storage_root_, kLogsSubdir),
                        kLogFile);
}

std::string BridgeLog::log_event(const std::string& request_id,
                                 const std::string& event_name,
                                 const std::string& details_json,
                                 std::string* err) {
  if (event_name.empty()) {
    if (err) *err = "log_event: empty event name";
    return *err;
  }
  // Validate that details_json looks like a JSON object: starts
  // with '{' and ends with '}' (after optional whitespace). This
  // is a coarse but adequate check — the caller is the same
  // firmware code path, so we know the shape.
  std::size_t first = details_json.find_first_not_of(" \t\n\r");
  std::size_t last = details_json.find_last_not_of(" \t\n\r");
  if (first == std::string::npos || last == std::string::npos ||
      details_json[first] != '{' || details_json[last] != '}') {
    if (err) *err = "log_event: details_json must be a JSON object";
    return *err;
  }

  std::string line;
  line.reserve(64 + details_json.size() + request_id.size() +
               event_name.size());
  line += "{\"ts\":\"";
  line += now_iso8601_utc();
  line += "\",\"request_id\":\"";
  line += json_string_escape(request_id);
  line += "\",\"event\":\"";
  line += json_string_escape(event_name);
  line += "\",\"details\":";
  line += details_json;
  line += "}\n";

  std::string dir = storage_->join(storage_root_, kLogsSubdir);
  std::string e = storage_->ensure_dir(dir);
  if (!e.empty()) {
    if (err) *err = "ensure_dir(logs): " + e;
    return *err;
  }
  std::string path = log_path();

  // Rotation: if the existing file + this line exceeds the
  // threshold, move it to <path>.1 and start a new file. The
  // single-rotation shape keeps the host test path simple
  // (std::filesystem::rename) and the firmware path equally
  // small (just truncate, the SD stub returns errors and
  // log_event surfaces them).
  std::uint64_t existing_size = 0;
  std::string existing = storage_->read_file_or(path, "");
  if (!existing.empty()) {
    existing_size = static_cast<std::uint64_t>(existing.size());
  }
  if (existing_size + line.size() > kRotateBytes && existing_size > 0) {
#ifndef ADVDECK_FIRMWARE
    // On the host path we can rename. We resolve the storage
    // root via IStorage::root() and strip the leading slash the
    // storage layer uses for logical SD paths.
    try {
      std::string rel = path;
      if (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
      fs::path src = fs::path(storage_->root()) / rel;
      fs::path dst = src;
      dst += ".1";
      std::error_code ec;
      fs::rename(src, dst, ec);
      // If rename fails, fall through and just append — losing
      // rotation is better than dropping the log line.
    } catch (...) {
      // Swallow rotation errors. The next line below still
      // attempts the append.
    }
#else
    // Firmware path: truncate the file. The SD stub is not
    // expected to get this far; if it does, we drop the old
    // content so we don't blow the size budget.
    storage_->write_file(path, "");
#endif
  }

  // Append: read the existing content (which is small after
  // rotation), concatenate, write back. The storage layer
  // does the atomic rename for us.
  std::string body = storage_->read_file_or(path, "");
  body += line;
  e = storage_->write_file(path, body);
  if (!e.empty()) {
    if (err) *err = "write_file(" + path + "): " + e;
    return *err;
  }
  if (err) err->clear();
  return "";
}

}  // namespace advdeck
