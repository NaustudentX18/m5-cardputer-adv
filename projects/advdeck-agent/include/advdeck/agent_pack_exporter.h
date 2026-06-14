// advdeck/agent_pack_exporter.h
//
// A11 Agent Pack Export. Reads the six artefacts the bridge produced
// for a project (brief.md, plan.md, tasks.json, tasks.md,
// calendar-suggestions.json, agent-prompt.md) from the project
// folder, hashes them, and writes the canonical export/ folder that
// a fresh coding agent consumes.
//
// See PHASE-3-INTERFACES.md §3.1, §4.3, §4.4, §5.2 for the on-disk
// contract.
//
// The exporter never mutates the source project folder. The review
// gate (B3.1) decides whether the project folder's artefacts are
// authoritative; the exporter just packages what is on disk.

#ifndef ADVDECK_INCLUDE_ADVDECK_AGENT_PACK_EXPORTER_H_
#define ADVDECK_INCLUDE_ADVDECK_AGENT_PACK_EXPORTER_H_

#include <string>

#include "advdeck/storage.h"

namespace advdeck {

class AgentPackExporter {
 public:
  AgentPackExporter(IStorage& storage,
                    std::string storage_root = "/advdeck");

  // Read the six artefacts from <storage_root>/projects/<slug>/ and
  // the calendar-suggestions.json from the most recent
  // <storage_root>/outbox/results/<id>/. Write the export to
  // <storage_root>/projects/<slug>/export/ as:
  //   agent-pack.md
  //   agent-tasks.json
  //   README.md               (rendered from the bundled template)
  //   sources.json            (file index with SHA-256)
  //   export-info.json        (metadata; validates against
  //                            kAgentPackExportInfoSchema)
  //
  // The calendar section is omitted from agent-pack.md and
  // agent-tasks.json does NOT include calendar events when no
  // calendar-suggestions.json is available — a missing calendar is a
  // warning, not a failure.
  //
  // `request_id_or_null` is optional; pass nullptr if not associated
  // with a specific request (e.g. re-export of a previously imported
  // project).
  //
  // Returns the export root path on success, "" on failure (and
  // *err is set).
  std::string export_project(const std::string& project_slug,
                             const std::string& planner_provider,
                             const std::string& planner_version,
                             const std::string* request_id_or_null,
                             std::string* err);

  // <storage_root>/projects/<slug>/export
  std::string export_root(const std::string& project_slug) const;

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_AGENT_PACK_EXPORTER_H_
