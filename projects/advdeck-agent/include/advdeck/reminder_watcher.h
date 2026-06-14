// advdeck/reminder_watcher.h
//
// Per PHASE-5-INTERFACES.md §4.1. Reads calendar/events.json via
// CalendarStore, filters events whose `remind_at` is in the past,
// drops ones already acked, and returns the survivors sorted by
// `remind_at` ascending. Acks are recorded in calendar/acks.json
// (JSONL, one {id, acked_at} per line) so the same reminder will
// not re-fire until the user re-acks the event.

#ifndef ADVDECK_INCLUDE_ADVDECK_REMINDER_WATCHER_H_
#define ADVDECK_INCLUDE_ADVDECK_REMINDER_WATCHER_H_

#include <set>
#include <string>
#include <vector>

#include "advdeck/storage.h"

namespace advdeck {

struct PendingReminder {
  std::string event_id;
  std::string project;    // may be empty for global events
  std::string title;
  std::string remind_at;  // ISO8601
};

class ReminderWatcher {
 public:
  ReminderWatcher(IStorage& storage, std::string storage_root = "/advdeck");

  // Load all events from calendar/events.json, filter to those
  // whose remind_at is in the past and not yet acked, and return
  // them sorted by remind_at ascending. The watcher's clock is
  // `now_iso` (the caller — main.cpp — passes the device clock in
  // ISO8601 form). Returns "" on success; on parse failure
  // forwards CalendarStore's error string.
  std::string load_due(const std::string& now_iso,
                       std::vector<PendingReminder>* out,
                       std::string* err);

  // Mark a reminder as acked. Appends one JSONL line to
  // calendar/acks.json. Idempotent: acking the same id twice is a
  // no-op (we check for the id before writing). Returns "" on
  // success.
  std::string ack(const std::string& event_id, std::string* err);

  // Returns the storage path of calendar/acks.json.
  std::string acks_path() const;

 private:
  // Read the acks file and return the set of acked event ids.
  // Missing or empty file returns an empty set.
  std::set<std::string> load_acked_ids(std::string* err) const;

  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_REMINDER_WATCHER_H_
