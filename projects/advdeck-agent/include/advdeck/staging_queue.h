// advdeck/staging_queue.h
//
// Staging area for bridge-produced artifacts awaiting user review.
// See PHASE-3-INTERFACES.md §5.1 for the C++ interface contract and
// §2 for the on-disk layout.
//
// On-disk layout (under `storage_root`):
//   <storage_root>/outbox/results/<id>/...   — bridge writes here
//   <storage_root>/outbox/staging/<id>/      — copy of the result dir
//     meta.json
//     brief.md, plan.md, tasks.json, tasks.md,
//     calendar-suggestions.json, agent-prompt.md
//   <storage_root>/outbox/rejected/<id>/     — on user reject
//     meta.json + the same six artifacts
//
// On accept, the files are moved into the project folder at
// <storage_root>/projects/<slug>/. The result dir is also drained
// (the "source of truth" for the project is the project folder).
// `idea.md` is never touched by this code path.

#ifndef ADVDECK_INCLUDE_ADVDECK_STAGING_QUEUE_H_
#define ADVDECK_INCLUDE_ADVDECK_STAGING_QUEUE_H_

#include <string>
#include <vector>

#include "advdeck/storage.h"

namespace advdeck {

struct StagingEntry {
  std::string request_id;
  std::string project;
  std::string arrived_at;
  std::string status;        // "pending" | "accepted" | "rejected"
};

class StagingQueue {
 public:
  StagingQueue(IStorage& storage, std::string storage_root = "/advdeck");

  // Stage a result that's already in outbox/results/<id>/. Copies
  // the artifacts into outbox/staging/<id>/, writes meta.json, leaves
  // the source result dir alone (the user can still mark done
  // afterward). The list of copied files is the Phase 3 contract set
  // (brief.md, plan.md, tasks.json, tasks.md, calendar-suggestions.json,
  // agent-prompt.md); missing source files are skipped silently.
  std::string stage(const std::string& request_id, std::string* err);

  // Accept or reject a pending entry. On accept, moves every file
  // from staging/<id>/ into <project_dir>/, and from results/<id>/
  // into the project folder as well (so the "source of truth" for
  // the project is the project folder). On reject, moves from
  // staging/<id>/ into rejected/<id>/. Either way, writes a new
  // meta.json with the terminal status. Returns "" on success.
  std::string accept(const std::string& request_id, std::string* err);
  std::string reject(const std::string& request_id, std::string* err);

  // List pending entries (status: pending), sorted by arrived_at
  // ascending. Returns "" on success.
  std::string list_pending(std::vector<StagingEntry>* out,
                            std::string* err);

  // Read the meta.json for one entry.
  std::string read_meta(const std::string& request_id, StagingEntry* out,
                        std::string* err);

  // Path of the staging dir. <storage_root>/outbox/staging
  std::string staging_dir() const;
  // Path of the rejected dir. <storage_root>/outbox/rejected
  std::string rejected_dir() const;

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_STAGING_QUEUE_H_
