# Phase 5 Internal Interface Contract (for swarm)

> **Audience:** AdvDeck Agent swarm agents working on Phase 5. Read this FIRST.
> **Purpose:** Define the shared C++ + Python surface that the swarm depends on.
> **Status:** Authoritative for `phase-5-calendar-polish` branch.

## 1. What Phase 5 means

From `roadmap/advdeck-agent-plan.md` §Phase 5: "Calendar And Reminder Intelligence." Outcome: plans can produce local reminders and calendar exports.

Validation:
- manual event persists
- generated suggestion requires acceptance
- accepted event appears in local calendar
- `.ics` opens in a normal calendar app

Phase 5 also closes the deferred A12 UX polish from Phase 3 — the 240x135 screen needs a coherent pass before MVP is "ready for humans."

## 2. SD layout (no changes from Phase 4)

```
/advdeck/
  config.json
  inbox/
  projects/<slug>/...
  calendar/
    events.json                 # global calendar; Phase 1 had this
  outbox/
    pending.jsonl
    results/<id>/...
    staging/<id>/...
    rejected/<id>/...
  logs/
    bridge-import.log
```

## 3. New schema (one)

### 3.1 `ics-export.schema.json` (new)

Authoritative: the `.ics` file the bridge produces. Lives at `bridge/advdeck-agent-bridge/schemas/ics-export.schema.json` and a byte-identical copy at `projects/advdeck-agent/schemas/`.

This isn't a typical JSON Schema; it's a description of the iCalendar (RFC 5545) text format. The schema is a **structural** schema that says "the file must be a UTF-8 text, must start with `BEGIN:VCALENDAR` and end with `END:VCALENDAR`, must contain at least one `VEVENT` block, and the VEVENT must have `UID`, `DTSTAMP`, `DTSTART`, `SUMMARY`." The JSON Schema shape:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "IcsExportSpec",
  "type": "object",
  "required": ["version", "min_rfc5545_conformance"],
  "properties": {
    "version": { "type": "string", "const": "1.0" },
    "min_rfc5545_conformance": { "type": "boolean", "const": true },
    "prodid": { "type": "string" },
    "line_folding": { "type": "boolean", "const": true },
    "max_line_length": { "type": "number", "const": 75 }
  },
  "additionalProperties": false
}
```

The actual `.ics` text is generated and validated by string-level checks (line lengths, BEGIN/END pairing, required VEVENT fields). A test parses the `.ics` text and asserts the structure.

## 4. C++ interface surface

All headers in `projects/advdeck-agent/include/advdeck/`. Namespace `advdeck`. Pure C++17. No Arduino headers in services. Host tests in `test/host/` use the existing `IStorage`.

### 4.1 `reminder_watcher.h` (new — E1.1 owns)
```cpp
namespace advdeck {

struct PendingReminder {
  std::string event_id;
  std::string project;          // may be empty for global events
  std::string title;
  std::string remind_at;        // ISO8601
};

class ReminderWatcher {
 public:
  ReminderWatcher(IStorage& storage, std::string storage_root = "/advdeck");

  // Load all events from calendar/events.json and return the ones
  // whose remind_at is in the past and not yet acked. Sorted by
  // remind_at ascending.
  std::string load_due(const std::string& now_iso,
                        std::vector<PendingReminder>* out,
                        std::string* err);

  // Mark a reminder as acked. Writes a small `acks.json` to the
  // calendar/ dir. Acks are timestamped; the same reminder id
  // will not appear again until the user re-acks the event.
  std::string ack(const std::string& event_id, std::string* err);

  // Returns the path of the acks file.
  std::string acks_path() const;

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck
```

### 4.2 `Ctx` extension
Add a `ReminderWatcher* reminder = nullptr` field for the route handlers. Same null-lazy-init pattern as the other Ctx pointers.

### 4.3 UI surface (E1.1)
- New `Route::Calendar` already exists. Extend it: when the user picks an event in the list and presses Enter, open a new `route_calendar_editor_impl(Ctx& ctx, const std::string& event_id)` for edit. From the editor, 'n' creates a new event with the next available id, 'a' accepts (saves), 'd' deletes.
- New `Route::Reminder` enum value. `route_reminder_alert_impl(Ctx& ctx)` is the modal screen. It shows the event title, the remind_at time, the project (if any), and binds 'a' to ack, 's' to snooze (5 minutes), Esc to dismiss. The dispatcher in `main.cpp`'s `loop()` calls `ReminderWatcher::load_due` every 5 seconds and pops the alert route when there are pending reminders.
- The home menu gains a "Calendar" entry that's already wired from Phase 1. The reminder alert is automatic (not a menu entry).

## 5. Bridge additions (E1.2)

### 5.1 New subcommand
- `advdeck-bridge calendar export --storage-root <path> --out <ics_path>` — produces an `.ics` file from `<storage_root>/calendar/events.json`. The file is RFC 5545 compliant (BEGIN:VCALENDAR / END:VCALENDAR with at least one VEVENT).

### 5.2 New module
- `bridge/advdeck-agent-bridge/src/advdeck_bridge/ics_export.py` — `IcsExporter` class with `build(events: list[dict]) -> str` returning the `.ics` text. Single-tenant (no Prodid/UID cross-referencing needed).
- `bridge/advdeck-agent-bridge/src/advdeck_bridge/nl_date.py` — `parse_nl_date(text: str, *, now: str) -> str` returns ISO8601 or "" if unparseable. Handles: "tomorrow at 9am", "tomorrow at 9:30", "in 3 days", "next Monday at 2pm", "Monday at 2pm", "in 2 hours". Lexer is a tiny 50-line state machine. Reference `now` is in ISO8601.

### 5.3 New CLI subcommand
- `advdeck-bridge nl-date --text "tomorrow at 9am"` — parses and prints the ISO8601 result. Used for testing the parser end-to-end.

## 6. UI polish (E1.3 — A12)

A12 is a polish pass. Concrete deliverables:

### 6.1 Status bar (E1.3)
- Always show: battery %, SD status, bridge status, current time
- Top 12 px of the 135-px screen
- Update every 5 seconds (the existing `StatusBar::draw` is fine; just make sure it includes the time)

### 6.2 Footer (E1.3)
- Bottom 12 px of the screen
- Show 3-4 key labels specific to the current route
- E.g. home menu: `[r] record [p] projects [c] calendar [esc] exit`
- E.g. project detail: `[e] edit [t] tasks [d] calendar [esc] back`

### 6.3 Scrolling (E1.3)
- For long content (tasks, calendar events, recordings), add a vertical scroll indicator on the right edge: a filled bar showing the current viewport position in the full content.
- All list views get a `std::string render_scrollable_list(...)` helper.

### 6.4 Keybinding map (E1.3)
- Add a `Help` route (`Route::Help`) — pressing '?' from the home menu shows a keybinding map. Single screen, no scrolling.

### 6.5 Error states (E1.3)
- When a route can't proceed (e.g. no project selected), show a clear "no project" message and bounce back to home. Don't show stack traces or cryptic messages.

### 6.6 New file
- `projects/advdeck-agent/include/advdeck/ui/layout.h` + `src/ui/layout.cpp` — constants for the 240x135 layout: `kTopBarHeight = 12`, `kFooterHeight = 12`, `kContentTop = 14`, `kContentBottom = 122`, `kScrollBarWidth = 4`. Plus a `RenderScrollIndicator(int content_height, int total_height, int scroll_y)` helper.

## 7. Tests

### 7.1 Firmware tests
- `test/host/test_reminder_watcher.cpp` — 5 tests:
  - load_due returns events with remind_at in the past, sorted ascending
  - load_due ignores events with remind_at in the future
  - ack writes to acks.json
  - load_due after ack does not return the same event
  - load_due returns events with no project
- `test/host/test_calendar_editor.cpp` — 3 tests:
  - render_calendar_editor_with_event_id_shows_event_details
  - render_calendar_editor_new_event_picks_next_id
  - render_calendar_editor_after_delete_doesnt_show_event
- `test/host/test_reminder_alert.cpp` — 2 tests:
  - render_reminder_alert_shows_event_title_and_time
  - render_reminder_alert_after_ack_returns_empty
- `test/host/test_ui_layout.cpp` — 4 tests:
  - layout_constants_are_correct
  - render_scroll_indicator_zero_content_returns_empty
  - render_scroll_indicator_at_top_returns_top_marker
  - render_scroll_indicator_at_bottom_returns_bottom_marker
- `test/host/test_help_route.cpp` — 1 test:
  - render_help_route_lists_common_keys

### 7.2 Bridge tests
- `tests/test_ics_export.py` — 5 tests:
  - empty event list produces BEGIN/END with no VEVENT (or an error — your call, document it)
  - single event produces a valid VEVENT with UID, DTSTAMP, DTSTART, SUMMARY
  - multiple events each get a unique UID
  - line folding kicks in for SUMMARY longer than 75 chars
  - DTSTART with timezone is preserved
- `tests/test_nl_date.py` — 8 tests:
  - "tomorrow at 9am" → tomorrow's 09:00:00
  - "tomorrow at 9:30" → tomorrow's 09:30:00
  - "in 3 days" → +3 days, 12:00:00 (default)
  - "next Monday at 2pm" → next Monday 14:00:00
  - "Monday at 2pm" → upcoming Monday 14:00:00
  - "in 2 hours" → +2 hours
  - "now" → current time
  - "garbage nonsense" → "" (unparseable)
- `tests/test_cli_calendar_export.py` — 3 tests:
  - `calendar export` writes a valid .ics file
  - `calendar export` exits 1 with a clear message if the calendar dir is missing
  - `nl-date` exits 0 and prints the ISO8601 for known inputs

### 7.3 Z05 end-to-end (E2.1)
`tests/test_e2e_calendar_to_export.py` — 3 tests:
- `test_e2e_calendar_export_produces_ics_openable_by_python_ical_library` — uses the `ics` Python lib to read the .ics file the bridge produces. Asserts all events are present with the right title and start time.
- `test_e2e_calendar_export_includes_accepted_bridge_suggestion` — creates a project, runs a planner with calendar suggestions, accepts one via the firmware's `CalendarStore::add_event` (or by replicating the action in Python), then exports. Asserts the accepted event is in the .ics.
- `test_e2e_nl_date_appears_in_calendar_event` — uses `advdeck-bridge nl-date` to parse "tomorrow at 9am", then constructs a calendar event with that start time, exports, and asserts the .ics contains the right DTSTART.

## 8. Build commands

From `projects/advdeck-agent/`:
```bash
/home/pi/.platformio/penv/bin/platformio run
make -C test/host test
```

From `bridge/advdeck-agent-bridge/`:
```bash
.venv/bin/python -m pytest tests/ -v
```

End-to-end:
```bash
./projects/advdeck-agent/test/host/verify.sh
```

## 9. Coding rules

Same as Phases 1-4: C++17, no Arduino headers in services, no exceptions in firmware, Python 3.11+ for the bridge, type hints on all public functions, all tests under the existing test frameworks.

## 10. What's NOT in Phase 5

- Real LLM provider for the bridge (Phase 3 already has `OpenAIProvider`; Phase 5 just adds the nl-date parser and .ics exporter)
- Voice capture (Phase 4)
- C5 companion integration (Phase 7)
- Powered-off reminder reliability (the plan explicitly disclaims this; "MVP only guarantees alerts while app is running")
- Wi-Fi / SoftAP / radio (later)

This batch closes the MVP. After Phase 5 the project is "ready for humans" per the stop condition in `roadmap/advdeck-agent-plan.md`.
