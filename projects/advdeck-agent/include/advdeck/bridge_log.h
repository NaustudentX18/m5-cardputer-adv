// advdeck/bridge_log.h
//
// Append-only JSONL log of every bridge import attempt. Lives at
// <storage_root>/logs/bridge-import.log. The dry-run provider's
// deterministic behavior means the log is mostly diagnostic in
// Phase 2; real LLM providers (Phase 3+) will need it for
// postmortems.
//
// Each line is a JSON object with at least:
//   {"ts":"<ISO8601 UTC>","request_id":"<req-...>","event":"<name>","details":<obj>}
//
// `event` values we emit:
//   "enqueue"       — OutboxQueue wrote a new request
//   "import"        — BridgeImport started processing a result
//   "import_ok"     — artifacts copied successfully
//   "import_error"  — see details.message
//   "mark_terminal" — OutboxQueue flipped a row to done/error
//
// The log rotates on size: when the file would exceed 1 MiB after
// the append, the existing file is moved aside to
// `<path>.1` and a fresh file is started. This keeps the log
// manageable on a tiny SD card and matches what a Phase 3 device
// could plausibly want to keep around for postmortems.

#ifndef ADVDECK_INCLUDE_ADVDECK_BRIDGE_LOG_H_
#define ADVDECK_INCLUDE_ADVDECK_BRIDGE_LOG_H_

#include <string>

#include "advdeck/storage.h"

namespace advdeck {

class BridgeLog {
 public:
  BridgeLog(IStorage& storage, std::string storage_root = "/advdeck");

  // Append one event line to logs/bridge-import.log. `details_json`
  // MUST be a valid JSON object (e.g. "{}") — it is spliced in
  // verbatim. `event_name` MUST be a non-empty JSON-safe string.
  // Returns "" on success, an error message on I/O failure.
  std::string log_event(const std::string& request_id,
                       const std::string& event_name,
                       const std::string& details_json,
                       std::string* err);

  // <storage_root>/logs/bridge-import.log
  std::string log_path() const;

  // Rotation threshold in bytes. Default is 1 MiB.
  static constexpr std::size_t kRotateBytes = 1u * 1024u * 1024u;

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_BRIDGE_LOG_H_
