// advdeck/bridge_import.h
//
// BridgeImport reads a result manifest from
// <storage_root>/outbox/results/<request_id>/result.json, validates
// it, and copies the listed artifacts into the matching project
// folder. See PHASE-2-INTERFACES.md §5.3 and §6 for the full
// contract. The dry-run provider used in Phase 2 returns
// deterministic fixtures, so the review gate is auto-accepted
// here; the import body still has a TODO(phase-3) marker for the
// staging + review flow that real LLMs will need.

#ifndef ADVDECK_INCLUDE_ADVDECK_BRIDGE_IMPORT_H_
#define ADVDECK_INCLUDE_ADVDECK_BRIDGE_IMPORT_H_

#include <string>
#include <vector>

#include "advdeck/storage.h"

namespace advdeck {

struct ImportResult {
  bool ok = false;
  std::string request_id;
  std::vector<std::string> imported_files;  // absolute paths the artifacts landed at
  std::vector<std::string> warnings;
  std::string error_message;
  bool retryable = false;
};

class BridgeImport {
 public:
  BridgeImport(IStorage& storage, std::string storage_root = "/advdeck");

  // Look at outbox/results/<request_id>/result.json.
  //
  // If it's a result manifest, copy each listed artifact from
  // <storage_root>/outbox/results/<id>/<artifact> into
  // <storage_root>/projects/<slug>/<artifact>. Returns ok=true
  // with imported_files populated. Artifacts are restricted to
  // known filenames (brief.md, plan.md, tasks.json, tasks.md,
  // calendar-suggestions.json, agent-prompt.md) to keep the
  // copy target predictable.
  //
  // If it's an error manifest, returns ok=false with the error
  // message and retryable flag populated. The caller should then
  // call OutboxQueue::mark_terminal(id, "error", ...).
  //
  // The project slug is taken from the pending request in
  // <storage_root>/outbox/pending.jsonl. The caller can pre-load
  // the request and pass `project_slug` to skip the load
  // (useful for tests; pass "" to make the importer look it up).
  std::string import(const std::string& request_id,
                     ImportResult* out, std::string* err);

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_BRIDGE_IMPORT_H_
