// src/services/reminder_watcher.cpp
//
// Implementation of the reminder watchdog. See reminder_watcher.h
// for the contract; see PHASE-5-INTERFACES.md §4.1 for the design
// rationale.
//
// Notes:
//   * Acks live at <root>/calendar/acks.json, JSONL, one line per
//     ack: {"id": "evt-...", "acked_at": "2026-..."}. The line is
//     appended (not rewritten) so the file is an append-only audit
//     trail.
//   * load_due() walks all events, filters by remind_at < now_iso
//     AND remind_at != "" (reminders are opt-in; a remind_at of "" is
//     the "no reminder" sentinel), drops ids that already have an
//     ack, and sorts the remainder by remind_at ascending.
//   * We reuse CalendarStore for events.json I/O so the file format
//     stays single-sourced.
//   * ISO8601 comparison uses the same lexicographic rule as
//     CalendarStore::iso_less: correct for the canonical Z form.

#include "advdeck/reminder_watcher.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "advdeck/calendar_store.h"

namespace advdeck {

ReminderWatcher::ReminderWatcher(IStorage& storage, std::string storage_root)
    : storage_(&storage), storage_root_(std::move(storage_root)) {}

std::string ReminderWatcher::acks_path() const {
  return storage_->join(storage_->join(storage_root_, "calendar"),
                        "acks.json");
}

std::set<std::string> ReminderWatcher::load_acked_ids(std::string* err) const {
  std::set<std::string> out;
  std::string path = acks_path();
  if (!storage_->exists(path)) {
    if (err) err->clear();
    return out;
  }
  std::string body = storage_->read_file(path);
  if (body.empty()) {
    if (err) err->clear();
    return out;
  }
  // Walk line by line. Each non-blank line is a JSON object with
  // at least an "id" field. Malformed lines are skipped (not an
  // error — the file is append-only and a partial write should not
  // brick the watcher).
  std::size_t i = 0;
  while (i < body.size()) {
    std::size_t j = body.find('\n', i);
    if (j == std::string::npos) j = body.size();
    std::string line = body.substr(i, j - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) {
      try {
        auto j2 = nlohmann::json::parse(line);
        if (j2.is_object() && j2.contains("id") &&
            j2["id"].is_string()) {
          out.insert(j2["id"].get<std::string>());
        }
      } catch (...) {
        // Skip malformed lines.
      }
    }
    i = j + 1;
  }
  if (err) err->clear();
  return out;
}

std::string ReminderWatcher::load_due(const std::string& now_iso,
                                      std::vector<PendingReminder>* out,
                                      std::string* err) {
  if (out) out->clear();
  if (err) err->clear();

  // Read events via CalendarStore.
  std::vector<Event> events;
  std::string lerr;
  std::string lpath = storage_->join(storage_->join(storage_root_, "calendar"),
                                     "events.json");
  // Use a transient CalendarStore on the same storage / root so we
  // share the on-disk format with the rest of the app.
  CalendarStore cs(*storage_, storage_root_);
  std::string lr = cs.load(&events, &lerr);
  if (!lr.empty()) {
    if (err) *err = lr;
    return lr;
  }
  (void)lpath;

  // Load acked ids.
  std::set<std::string> acked;
  std::string aerr;
  acked = load_acked_ids(&aerr);
  // aerr is "" on success; we ignore read errors and treat as
  // empty set so a corrupt acks file cannot brick the alert
  // pipeline (worst case: reminders re-fire once).

  // Filter.
  std::vector<PendingReminder> pending;
  pending.reserve(events.size());
  for (const auto& e : events) {
    if (e.remind_at.empty()) continue;            // no reminder set
    if (e.remind_at >= now_iso) continue;         // not yet due
    if (acked.count(e.id) != 0) continue;        // already acked
    PendingReminder r;
    r.event_id = e.id;
    r.project = e.project;
    r.title = e.title;
    r.remind_at = e.remind_at;
    pending.push_back(std::move(r));
  }
  // Sort by remind_at ascending. Ties (rare; suggest someone set
  // two reminders to the same second) broken by event_id for
  // determinism in tests.
  std::sort(pending.begin(), pending.end(),
            [](const PendingReminder& a, const PendingReminder& b) {
              if (a.remind_at == b.remind_at) return a.event_id < b.event_id;
              return a.remind_at < b.remind_at;
            });
  if (out) *out = std::move(pending);
  return "";
}

std::string ReminderWatcher::ack(const std::string& event_id,
                                 std::string* err) {
  if (err) err->clear();
  if (event_id.empty()) {
    if (err) *err = "ack: empty event_id";
    return *err;
  }

  std::set<std::string> acked;
  std::string lerr;
  acked = load_acked_ids(&lerr);
  if (acked.count(event_id) != 0) {
    // Idempotent: nothing to do.
    return "";
  }

  // ISO8601 UTC now. We use the same now_iso helper the calendar
  // store uses; this is a small inline copy to avoid pulling
  // <ctime> in the header.
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&t, &tm);
#else
  gmtime_r(&t, &tm);
#endif
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
  std::string acked_at(ts);

  nlohmann::json line;
  line["id"] = event_id;
  line["acked_at"] = acked_at;
  std::string body = line.dump();
  body.push_back('\n');

  // Read the existing acks file, append, write back. The acks file
  // is small (one line per ack ever); we keep the whole-file
  // read/append pattern consistent with the rest of the IStorage
  // facade.
  std::string path = acks_path();
  std::string existing;
  if (storage_->exists(path)) {
    existing = storage_->read_file(path);
  } else {
    // First ack: ensure the calendar dir exists.
    std::string dir = storage_->join(storage_root_, "calendar");
    std::string derr = storage_->ensure_dir(dir);
    if (!derr.empty()) {
      if (err) *err = "ensure_dir(" + dir + "): " + derr;
      return *err;
    }
  }
  existing += body;
  std::string werr = storage_->write_file(path, existing);
  if (!werr.empty()) {
    if (err) *err = "write_file(" + path + "): " + werr;
    return *err;
  }
  return "";
}

}  // namespace advdeck
