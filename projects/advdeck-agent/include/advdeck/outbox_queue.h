// advdeck/outbox_queue.h
//
// Append-only queue of pending bridge requests. See
// PHASE-2-INTERFACES.md §4.1 for the full lifecycle and §5.2 for
// the C++ interface contract.
//
// On-disk layout (under `storage_root`):
//   <storage_root>/outbox/pending.jsonl   — one PendingRequest per line
//   <storage_root>/outbox/results/        — per-request staging dir
//
// The queue is single-writer (the firmware) with the bridge
// reading + in-place editing status. The firmware rewrites the
// whole JSONL when it transitions a row to in_flight / done /
// error; see mark_in_flight() and mark_terminal().

#ifndef ADVDECK_INCLUDE_ADVDECK_OUTBOX_QUEUE_H_
#define ADVDECK_INCLUDE_ADVDECK_OUTBOX_QUEUE_H_

#include <string>
#include <vector>

#include "advdeck/pending_request.h"
#include "advdeck/storage.h"

namespace advdeck {

class OutboxQueue {
 public:
  OutboxQueue(IStorage& storage, std::string storage_root = "/advdeck");

  // Append a new pending request. Generates the id, builds a
  // PendingRequest with status="pending", and appends a JSON line
  // to <storage_root>/outbox/pending.jsonl. Returns the generated
  // id, or "" on failure (and *err is set). `inputs` filenames
  // are stored verbatim — the bridge resolves them against the
  // project folder at processing time.
  std::string enqueue(const std::string& project_slug,
                      const std::string& request_type,
                      const std::vector<std::string>& inputs,
                      std::string* err);

  // Read all rows (any status). *out is cleared first. Returns ""
  // on success. The file missing is not an error: *out is empty
  // and the function returns "".
  std::string load_all(std::vector<PendingRequest>* out, std::string* err);

  // Flip an existing request to status="in_flight". The JSONL is
  // rewritten atomically. Returns "" on success. If the id is not
  // found, *err is set to "request not found: <id>".
  std::string mark_in_flight(const std::string& id, std::string* err);

  // Flip an existing request to a terminal status ("done" or
  // "error"). Increments `attempts` to reflect the final attempt
  // count. The JSONL is rewritten atomically. If `final_status`
  // is not "done" or "error", *err is set and no write happens.
  std::string mark_terminal(const std::string& id,
                            const std::string& final_status,
                            std::string* err);

  // Mark an errored request as pending again so the bridge picks it
  // up on the next run-once. No-op (with *err set to "already
  // pending") if the request is already pending or in_flight.
  // Returns "" on success, an error message otherwise. The JSONL is
  // rewritten atomically. `created_at` is reset to <now> and
  // `attempts` is reset to 0.
  std::string retry(const std::string& id, std::string* err);

  // Compact `done` rows out of pending.jsonl whose results dir
  // mtime is older than `days_threshold` days. Sets *removed to the
  // number of rows dropped and returns "" on success. The threshold
  // is in days; B2.1's sync UI uses 7. The mtime comes from
  // IStorage::mtime_iso8601; rows whose result dir is missing are
  // dropped conservatively (treated as old — the bridge has cleaned
  // them up).
  std::string compact_done(int days_threshold, int* removed,
                           std::string* err);

  // <storage_root>/outbox/pending.jsonl
  std::string pending_path() const;

  // <storage_root>/outbox/results
  std::string results_dir() const;

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_OUTBOX_QUEUE_H_
