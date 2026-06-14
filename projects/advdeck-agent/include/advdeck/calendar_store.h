// advdeck/calendar_store.h
//
// CRUD over <root>/calendar/events.json. The calendar is global (not
// per-project) and stores both manually-entered events and bridge
// suggestions that the agent later accepts or rejects.

#ifndef ADVDECK_INCLUDE_ADVDECK_CALENDAR_STORE_H_
#define ADVDECK_INCLUDE_ADVDECK_CALENDAR_STORE_H_

#include <string>
#include <vector>

#include "advdeck/storage.h"

namespace advdeck {

struct Event {
  std::string id;          // "evt-YYYYMMDD-NNN" (NNN = 000-999; -XX suffix if overflow)
  std::string title;
  std::string starts_at;   // ISO8601 with TZ (canonical: "...Z")
  std::string ends_at;     // may be empty
  std::string remind_at;   // may be empty
  std::string source;      // "manual" | "bridge"
  std::string project;     // may be empty
  std::string status;      // "accepted" | "suggested" | "rejected"
};

class CalendarStore {
 public:
  // The CalendarStore owns the calendar root; default "/advdeck" matches
  // the SD layout from PHASE-1-INTERFACES.md §2. The file lives at
  // <root>/calendar/events.json.
  CalendarStore(IStorage& storage, std::string root = "/advdeck");

  // File: <root>/calendar/events.json
  // Schema: top-level { "version": 1, "events": [ Event, ... ] }
  // Atomic write. Malformed JSON returns a recoverable error and moves
  // the bad file aside to `events.json.bad.<unix-timestamp>`.

  // Returns "" on success and fills *out. Returns "" with an empty
  // *out on missing file (a fresh calendar). On parse failure returns
  // an error message; the bad file has been moved aside and *out is
  // empty.
  std::string load(std::vector<Event>* out, std::string* err);

  // Atomic write of the given events. *events is taken as the new
  // authoritative state; existing id collisions on disk are not a
  // concern because save() overwrites the file. Returns "" on
  // success.
  std::string save(const std::vector<Event>& events, std::string* err);

  // Generate a fresh, date-unique id for `e.starts_at` (or today if
  // empty), append the event, persist, and return the new id. On
  // failure returns "" and sets *err. The id format is
  // "evt-YYYYMMDD-NNN" where NNN is the smallest free 000-999 slot
  // for that date; if all 1000 are taken a 2-letter random suffix is
  // appended (e.g. "evt-20260614-1A").
  std::string add_event(const Event& e, Event* out_added, std::string* err);

  // Remove the event with the given id. Returns "" on success, an
  // error message ("event not found: <id>") if no such id exists.
  std::string delete_event(const std::string& id, std::string* err);

  // Sorted, filtered view: events whose starts_at >= now_iso, OR
  // events with no starts_at past now but with remind_at >= now_iso
  // (so a reminder set before the start is still surfaced). Sorted
  // ascending by starts_at, with remind_at-only events placed after
  // events that have a starts_at. Returns "" on success.
  std::string upcoming(const std::string& now_iso,
                       std::vector<Event>* out, std::string* err);

  // Logical path of the events file: <root>/calendar/events.json.
  std::string events_path() const {
    return storage_->join(calendar_dir(), "events.json");
  }

 private:
  // <root>/calendar
  std::string calendar_dir() const {
    return storage_->join(root_, "calendar");
  }

  IStorage* storage_;
  std::string root_;
};

// Lexicographic compare of two ISO8601 strings. Correct only for the
// canonical UTC-with-T-Z form ("YYYY-MM-DDTHH:MM:SSZ") that this
// project writes; local-time offsets or date-only strings are not
// handled. Documented as a known limitation; A12 will switch to a
// proper parser if we ever accept non-Z timestamps.
bool iso_less(const std::string& a, const std::string& b);

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_CALENDAR_STORE_H_
