// advdeck/bridge_import.h
//
// BridgeImport reads a result manifest from
// <storage_root>/outbox/results/<request_id>/result.json, validates
// it, and stages the listed artifacts for the user to review. See
// PHASE-3-INTERFACES.md §5.3 for the Phase 3 contract.
//
// In Phase 2, `import` copied the artifacts straight into the
// project folder. Phase 3 changes the flow to stage-then-review:
//   - `stage_only` copies artifacts to <storage_root>/outbox/staging/<id>/
//     and writes meta.json with status=pending. The project folder is
//     NOT touched.
//   - `import` is a thin shim: it calls stage_only then
//     StagingQueue::accept. It is kept only because verify.sh's
//     end-to-end test in Phase 2 still expects the one-shot path.
//     New code MUST call stage_only and let the user accept or
//     reject via the review screen.

#ifndef ADVDECK_INCLUDE_ADVDECK_BRIDGE_IMPORT_H_
#define ADVDECK_INCLUDE_ADVDECK_BRIDGE_IMPORT_H_

#include <string>
#include <vector>

#include "advdeck/storage.h"

namespace advdeck {

struct ImportResult {
  bool ok = false;
  std::string request_id;
  std::vector<std::string> imported_files;  // staging paths on stage_only
  std::vector<std::string> warnings;
  std::string error_message;
  bool retryable = false;
};

class BridgeImport {
 public:
  BridgeImport(IStorage& storage, std::string storage_root = "/advdeck");

  // Phase 3 entry point. Look at outbox/results/<request_id>/result.json.
  //
  // If it's a result manifest, call StagingQueue::stage(request_id),
  // which copies each listed artifact from
  // <storage_root>/outbox/results/<id>/<artifact> into
  // <storage_root>/outbox/staging/<id>/<artifact> and writes
  // meta.json with status=pending. Returns ok=true with
  // `imported_files` set to the staging paths. The project folder is
  // NOT touched.
  //
  // If it's an error manifest, returns ok=false with the error
  // message and retryable flag populated. The caller should then
  // call OutboxQueue::mark_terminal(id, "error", ...).
  //
  // The project slug is taken from the result manifest's `project`
  // field (the bridge writes it from the pending request) or, as a
  // fallback, from <storage_root>/outbox/pending.jsonl. The caller
  // can pre-load the request and pass `project_slug` to skip the
  // load (useful for tests; pass "" to make the importer look it up).
  std::string stage_only(const std::string& request_id,
                         ImportResult* out, std::string* err);

  // DEPRECATED (Phase 2 shim, kept for verify.sh end-to-end test).
  // Calls stage_only(request_id) then StagingQueue::accept(request_id).
  // On a successful accept, `imported_files` is rewritten to be the
  // project-folder paths the artifacts landed at (i.e. the Phase 2
  // behavior). New code MUST call stage_only and let the user
  // accept or reject via the review screen.
  std::string import(const std::string& request_id,
                     ImportResult* out, std::string* err);

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_BRIDGE_IMPORT_H_
