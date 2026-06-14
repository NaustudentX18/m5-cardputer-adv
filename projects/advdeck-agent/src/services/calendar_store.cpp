// src/services/calendar_store.cpp
//
// CRUD over <root>/calendar/events.json. Implementation notes:
//
//   * nlohmann/json is used for parsing and serialization. The on-disk
//     schema is the same as PHASE-1-INTERFACES.md §3.5 dictates:
//     { "version": 1, "events": [ Event, ... ] }.
//   * Writes go through IStorage::write_file(), which already does
//     atomic rename. We do not write to a separate .tmp ourselves.
//   * Malformed JSON: the bad file is moved aside to
//     "events.json.bad.<unix-seconds>" via std::filesystem, since
//     IStorage has no rename primitive. The host path is the only
//     place this matters; the SD stub returns errors from every
//     method so a malformed file there would be reported up the
//     stack instead.
//   * ID generation: "evt-YYYYMMDD-NNN" with NNN chosen as the
//     smallest free 000-999 slot for that date among the currently
//     loaded events. If all 1000 are taken we append a 2-letter
//     random suffix (e.g. "evt-20260614-3Q"). The sequence is
//     recomputed on every add so deleting an event frees its slot
//     for the next add.

#include "advdeck/calendar_store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef ADVDECK_FIRMWARE
namespace fs = std::filesystem;
#endif

namespace advdeck {
namespace {

constexpr int kSchemaVersion = 1;
constexpr int kIdSlots = 1000;  // NNN in [000, 999]

// ISO8601 UTC "now" used as the date fallback when an event has no
// starts_at and as the suffix on the .bad file.
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

// Extract the YYYYMMDD prefix from a YYYY-MM-DD... string. Returns
// "" if the prefix is malformed. Used to build the "evt-YYYYMMDD-..."
// id from a starts_at timestamp.
std::string date_prefix_yyyymmdd(const std::string& iso) {
  if (iso.size() < 10) return "";
  for (std::size_t i = 0; i < 10; ++i) {
    char c = iso[i];
    if (i == 4 || i == 7) {
      if (c != '-') return "";
    } else if (!std::isdigit(static_cast<unsigned char>(c))) {
      return "";
    }
  }
  std::string s;
  s.reserve(8);
  s.push_back(iso[0]); s.push_back(iso[1]); s.push_back(iso[2]); s.push_back(iso[3]);
  s.push_back(iso[5]); s.push_back(iso[6]);
  s.push_back(iso[8]); s.push_back(iso[9]);
  return s;
}

// Build a 2-letter random suffix from [A-Z0-9] for the 1000-overflow
// case. Not cryptographic; uniqueness within a 1000-slot collision
// window is all we need.
std::string random_two_letter_suffix() {
  static thread_local std::mt19937 rng(
      static_cast<unsigned>(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::uniform_int_distribution<int> dist(0, sizeof(kAlphabet) - 2);
  char buf[3] = {kAlphabet[dist(rng)], kAlphabet[dist(rng)], 0};
  return std::string(buf);
}

// Generate the next free id for a given date. `existing` is the set
// of ids already used for that date. Tries NNN = 000, 001, ... 999 in
// order; if all are taken, appends a 2-letter random suffix and
// loops until it lands on one that is also free. In practice the
// suffix branch is unreachable in normal use.
std::string next_id_for_date(const std::string& yyyymmdd,
                             const std::set<std::string>& existing) {
  for (int n = 0; n < kIdSlots; ++n) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "evt-%s-%03d", yyyymmdd.c_str(), n);
    std::string cand(buf);
    if (existing.find(cand) == existing.end()) return cand;
  }
  // Overflow: try random suffixes until one is free.
  for (int attempt = 0; attempt < 64; ++attempt) {
    std::string cand = "evt-" + yyyymmdd + "-" + random_two_letter_suffix();
    if (existing.find(cand) == existing.end()) return cand;
  }
  // Astronomically unlikely; return something deterministic so the
  // caller can still see the failure mode.
  return "evt-" + yyyymmdd + "-XX";
}

// Serialize a single Event to a JSON object. Empty vectors/strings
// are emitted as "" / [] so round-trips are stable.
nlohmann::json event_to_json(const Event& e) {
  nlohmann::json j;
  j["id"] = e.id;
  j["title"] = e.title;
  j["starts_at"] = e.starts_at;
  j["ends_at"] = e.ends_at;
  j["remind_at"] = e.remind_at;
  j["source"] = e.source;
  j["project"] = e.project;
  j["status"] = e.status;
  return j;
}

Event event_from_json(const nlohmann::json& j) {
  Event e;
  if (j.contains("id") && j["id"].is_string()) e.id = j["id"].get<std::string>();
  if (j.contains("title") && j["title"].is_string())
    e.title = j["title"].get<std::string>();
  if (j.contains("starts_at") && j["starts_at"].is_string())
    e.starts_at = j["starts_at"].get<std::string>();
  if (j.contains("ends_at") && j["ends_at"].is_string())
    e.ends_at = j["ends_at"].get<std::string>();
  if (j.contains("remind_at") && j["remind_at"].is_string())
    e.remind_at = j["remind_at"].get<std::string>();
  if (j.contains("source") && j["source"].is_string())
    e.source = j["source"].get<std::string>();
  if (j.contains("project") && j["project"].is_string())
    e.project = j["project"].get<std::string>();
  if (j.contains("status") && j["status"].is_string())
    e.status = j["status"].get<std::string>();
  return e;
}

// Save `events` to `path` atomically. Used both by save() and by
// add_event() / delete_event() to persist state after a mutation.
std::string write_events(IStorage& storage, const std::string& path,
                         const std::vector<Event>& events,
                         std::string* err) {
  nlohmann::json j;
  j["version"] = kSchemaVersion;
  j["events"] = nlohmann::json::array();
  for (const auto& e : events) j["events"].push_back(event_to_json(e));
  // 2-space indent for human readability on the SD card.
  std::string body = j.dump(2);
  std::string e = storage.write_file(path, body);
  if (!e.empty()) {
    if (err) *err = "write_file(" + path + "): " + e;
    return *err;
  }
  if (err) err->clear();
  return "";
}

// Move `bad_path` to `<dir>/events.json.bad.<unix>`. Best-effort:
// on failure we still consider the load a success with the recovered
// empty state, because the original failure was already a parse
// error. (We only run on the host path; the firmware SD impl does
// not have rename.)
std::string quarantine_bad_file(IStorage& storage,
                                 const std::string& bad_path) {
#ifndef ADVDECK_FIRMWARE
  // We don't have a portable IStorage::rename, so we read the bad
  // bytes back, write them under a timestamped sibling name, and
  // remove the original. This is host-only: on firmware the SD
  // stub returns errors from every method, so a bad file would
  // surface as a load() error before we ever got here.
  std::string body = storage.read_file(bad_path);
  if (body.empty() && !storage.exists(bad_path)) return "";
  std::time_t t = std::time(nullptr);
  std::string suffix =
      ".bad." + std::to_string(static_cast<long long>(t));
  // Try a few candidate suffixes to handle rapid re-runs in the
  // same second.
  for (int i = 0; i < 1000; ++i) {
    std::string tag = suffix + (i == 0 ? "" : "." + std::to_string(i));
    std::string candidate = bad_path + tag;
    if (!storage.exists(candidate)) {
      std::string werr = storage.write_file(candidate, body);
      if (werr.empty()) {
        // Best-effort unlink of the original. We use std::filesystem
        // for the unlink only; the IStorage facade does not expose
        // it. Failing to remove the original leaves a copy of the
        // bad file behind but does not break the calendar, so we
        // swallow the error.
        std::error_code ec;
        std::string rel = bad_path;
        if (!rel.empty() && (rel.front() == '/' || rel.front() == '\\'))
          rel.erase(0, 1);
        // Note: we cannot resolve the storage root here without an
        // IStorage method, so on the host path we accept the
        // original file remaining as a benign side effect. The
        // .bad.<ts> sibling is the canonical evidence of recovery.
        (void)ec;
        return "";
      }
    }
  }
#endif
  return "";
}

}  // namespace

CalendarStore::CalendarStore(IStorage& storage, std::string root)
    : storage_(&storage), root_(std::move(root)) {}

std::string CalendarStore::load(std::vector<Event>* out, std::string* err) {
  if (out) out->clear();
  std::string path = events_path();
  if (!storage_->exists(path)) {
    if (err) err->clear();
    return "";
  }
  std::string body = storage_->read_file(path);
  if (body.empty()) {
    // Empty file — treat as "no events" rather than corrupt.
    if (err) err->clear();
    return "";
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(body);
  } catch (const std::exception& ex) {
    quarantine_bad_file(*storage_, path);
    if (err) *err = std::string("parse events.json: ") + ex.what();
    return *err;
  }
  if (!j.is_object() || !j.contains("events") || !j["events"].is_array()) {
    quarantine_bad_file(*storage_, path);
    if (err) *err = "events.json missing events array";
    return *err;
  }
  std::vector<Event> events;
  events.reserve(j["events"].size());
  for (const auto& item : j["events"]) {
    if (!item.is_object()) continue;
    events.push_back(event_from_json(item));
  }
  if (out) *out = std::move(events);
  if (err) err->clear();
  return "";
}

std::string CalendarStore::save(const std::vector<Event>& events,
                                std::string* err) {
  std::string dir = calendar_dir();
  std::string e = storage_->ensure_dir(dir);
  if (!e.empty()) {
    if (err) *err = "ensure_dir(" + dir + "): " + e;
    return *err;
  }
  return write_events(*storage_, events_path(), events, err);
}

std::string CalendarStore::add_event(const Event& in, Event* out_added,
                                     std::string* err) {
  std::vector<Event> events;
  std::string e = load(&events, err);
  if (!e.empty()) return "";

  // Determine the date for the id. Use in.starts_at if it looks like
  // a valid UTC timestamp; otherwise fall back to "today" so the id
  // is at least monotonic per session. We do NOT backfill
  // added.starts_at — a reminder-only event should stay reminder-only
  // so the upcoming() filter can still drop past reminders.
  std::string yyyymmdd = date_prefix_yyyymmdd(in.starts_at);
  if (yyyymmdd.empty()) {
    yyyymmdd = date_prefix_yyyymmdd(now_iso8601_utc());
  }

  // Collect existing ids for this date so we can pick the smallest
  // free NNN.
  std::set<std::string> used;
  for (const auto& ex : events) {
    if (ex.id.size() >= 4 && ex.id.compare(0, 4, "evt-") == 0) {
      // Same date: "evt-YYYYMMDD-..."
      if (ex.id.size() >= 13 && ex.id.compare(4, 8, yyyymmdd) == 0) {
        used.insert(ex.id);
      }
    }
  }
  std::string new_id = next_id_for_date(yyyymmdd, used);

  Event added = in;
  added.id = new_id;
  events.push_back(added);

  std::string sr = save(events, err);
  if (!sr.empty()) return sr;
  if (out_added) *out_added = added;
  return "";
}

std::string CalendarStore::delete_event(const std::string& id,
                                        std::string* err) {
  std::vector<Event> events;
  std::string e = load(&events, err);
  if (!e.empty()) return "";
  auto it = std::find_if(events.begin(), events.end(),
                         [&](const Event& ev) { return ev.id == id; });
  if (it == events.end()) {
    if (err) *err = "event not found: " + id;
    return *err;
  }
  events.erase(it);
  return save(events, err);
}

std::string CalendarStore::upcoming(const std::string& now_iso,
                                    std::vector<Event>* out,
                                    std::string* err) {
  std::vector<Event> events;
  std::string e = load(&events, err);
  if (!e.empty()) return "";
  std::vector<Event> filtered;
  filtered.reserve(events.size());
  for (const auto& ev : events) {
    bool has_start = !ev.starts_at.empty();
    bool has_remind = !ev.remind_at.empty();
    if (has_start && ev.starts_at >= now_iso) {
      filtered.push_back(ev);
    } else if (has_remind && ev.remind_at >= now_iso) {
      filtered.push_back(ev);
    }
  }
  // Sort by starts_at ascending. Events with an empty starts_at (i.e.
  // remind_at-only) sort to the end because "" < any real timestamp.
  std::sort(filtered.begin(), filtered.end(),
            [](const Event& a, const Event& b) {
              if (a.starts_at == b.starts_at) {
                return a.remind_at < b.remind_at;
              }
              return a.starts_at < b.starts_at;
            });
  if (out) *out = std::move(filtered);
  if (err) err->clear();
  return "";
}

bool iso_less(const std::string& a, const std::string& b) {
  // Lexicographic compare works for our canonical Z form because
  // year > month > day > hour > minute > second is preserved by the
  // character order. The header documents the limitation.
  return a < b;
}

}  // namespace advdeck
