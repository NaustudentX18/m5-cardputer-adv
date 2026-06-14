// src/platform/sd_storage.cpp
//
// SD-backed IStorage. Phase 1 stub: every method except the destructor
// returns an error. The real implementation (SD.h + M5Cardputer.h) will
// be added by A01 / later phases. This file MUST compile on host
// (without Arduino headers) so the contract surface is testable.

#include "advdeck/storage.h"

namespace advdeck {

#ifdef ADVDECK_FIRMWARE

SdStorage::SdStorage() = default;
SdStorage::~SdStorage() = default;

namespace {
constexpr const char* kStubErr = "SdStorage not implemented in Phase 1";
}

bool SdStorage::mount() { return false; }
bool SdStorage::is_mounted() const { return false; }
bool SdStorage::exists(const std::string&) const { return false; }
std::string SdStorage::ensure_dir(const std::string&) { return kStubErr; }
std::string SdStorage::write_file(const std::string&,
                                  const std::string&) { return kStubErr; }
std::string SdStorage::read_file(const std::string&) { return ""; }
std::string SdStorage::read_file_or(const std::string&,
                                   const std::string& fallback) {
  return fallback;
}
std::vector<std::string> SdStorage::list_dir(const std::string&) { return {}; }
std::string SdStorage::join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return a.back() == '/' ? a + b : a + "/" + b;
}
std::string SdStorage::mtime_iso8601(const std::string&) { return ""; }
std::string SdStorage::root() const { return "/advdeck"; }

#else  // host build: no SdStorage symbol.

#endif  // ADVDECK_FIRMWARE

}  // namespace advdeck
