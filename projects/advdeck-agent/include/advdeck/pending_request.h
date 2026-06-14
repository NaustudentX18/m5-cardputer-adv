// advdeck/pending_request.h
//
// The on-disk shape of one row in /advdeck/outbox/pending.jsonl
// (PHASE-2-INTERFACES.md §5.1). The bridge CLI and the firmware
// share this struct via the JSONL file. Status is one of:
//   "pending"    — newly enqueued, not yet picked up
//   "in_flight"  — the bridge has claimed it and is processing
//   "done"       — the firmware successfully imported the result
//   "error"      — see the bridge-import.log entry for the cause
//
// The id format is "req-YYYYMMDD-NNN" where NNN is a 3-6 digit
// per-day sequence (000-999999). The sequence is recomputed on
// every generate_request_id() call by scanning the existing
// JSONL for the same date prefix, so a fresh process sees the
// same numbering as a long-running one.

#ifndef ADVDECK_INCLUDE_ADVDECK_PENDING_REQUEST_H_
#define ADVDECK_INCLUDE_ADVDECK_PENDING_REQUEST_H_

#include <string>
#include <vector>

#include "advdeck/storage.h"

namespace advdeck {

struct PendingRequest {
  std::string id;                  // "req-YYYYMMDD-NNN"
  std::string project;             // slug, matches ^[a-z0-9][a-z0-9-]{0,63}$
  std::string type;                // "plan_project" for Phase 2
  std::vector<std::string> inputs; // filenames, resolved against the project
  std::string created_at;          // ISO8601 UTC, e.g. "2026-06-14T15:30:00Z"
  std::string status;              // "pending" | "in_flight" | "done" | "error"
  int attempts = 0;                // monotonically increasing on each retry
};

// Generate the next fresh request id for the current date. Scans
// `outbox_pending_path` (logical SD path) and picks
// "req-YYYYMMDD-NNN" with NNN one above the highest existing id for
// today. If the path is missing or unreadable, NNN starts at 1.
// Returns "" on I/O failure and sets *err.
//
// The function does not validate the file's contents beyond the id
// prefix; any unparseable line is skipped. This keeps the queue
// resilient to bridge-side edits that briefly produce invalid JSONL.
std::string generate_request_id(IStorage& storage,
                                const std::string& outbox_pending_path,
                                std::string* err);

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_PENDING_REQUEST_H_
