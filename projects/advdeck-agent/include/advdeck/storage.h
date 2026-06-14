// advdeck/storage.h
//
// Abstract storage interface and the host-test implementation.
//
// The interface is hardware-agnostic; the SD-backed implementation is in
// src/platform/sd_storage.cpp and is only compiled when ADVDECK_FIRMWARE
// is defined (so host tests do not pull in <SD.h> / <M5Cardputer.h>).
//
// Implementations MUST:
//   - Use LF line endings and UTF-8 for any text they write.
//   - Implement atomic write: write to <path>.tmp, fsync if supported,
//     then rename. The HostStorage impl does this with
//     std::filesystem::rename.

#ifndef ADVDECK_INCLUDE_ADVDECK_STORAGE_H_
#define ADVDECK_INCLUDE_ADVDECK_STORAGE_H_

#include <string>
#include <vector>

namespace advdeck {

class IStorage {
 public:
  virtual ~IStorage() = default;

  // Mount / initialize the backing store. Returns true on success.
  virtual bool mount() = 0;
  virtual bool is_mounted() const = 0;

  // True if `path` exists (file or directory).
  virtual bool exists(const std::string& path) const = 0;

  // Create `path` (and any missing parents). Returns "" on success,
  // error message on failure.
  virtual std::string ensure_dir(const std::string& path) = 0;

  // Atomic write: write to <path>.tmp, then rename. Returns "" on
  // success.
  virtual std::string write_file(const std::string& path,
                                 const std::string& data) = 0;

  // Read whole file. Returns "" if the file does not exist. On other
  // errors returns the empty string and a message can be obtained via
  // read_file_or(fallback).
  virtual std::string read_file(const std::string& path) = 0;

  // Read whole file; return `fallback` if missing or on read failure.
  virtual std::string read_file_or(const std::string& path,
                                   const std::string& fallback) = 0;

  // Immediate children of `path` (names only, not joined). Empty
  // vector for missing/non-directory paths. Order is unspecified.
  virtual std::vector<std::string> list_dir(const std::string& path) = 0;

  // Join two path components with '/'. Does not normalize or
  // canonicalize.
  virtual std::string join(const std::string& a, const std::string& b) = 0;

  // ISO8601 UTC mtime of `path` (e.g. "2026-06-14T15:30:00Z"), or
  // "" if missing or mtime is unavailable.
  //
  // Added in Phase 1 (deviation from PHASE-1-INTERFACES.md §3.2):
  // ProjectStore needs file mtimes to populate ProjectSummary. The
  // alternative was to expose the underlying real fs path to the
  // store, which would be a leaky abstraction.
  virtual std::string mtime_iso8601(const std::string& path) = 0;

  // Root path this storage was constructed with.
  virtual std::string root() const = 0;
};

// In-memory implementation for host tests. Backed by a temp directory
// on disk (so failures are observable). The root directory is created
// on construction; call mount() to ensure it is present after manual
// moves.
class HostStorage : public IStorage {
 public:
  explicit HostStorage(std::string root_path);

  bool mount() override;
  bool is_mounted() const override;
  bool exists(const std::string& path) const override;
  std::string ensure_dir(const std::string& path) override;
  std::string write_file(const std::string& path,
                         const std::string& data) override;
  std::string read_file(const std::string& path) override;
  std::string read_file_or(const std::string& path,
                           const std::string& fallback) override;
  std::vector<std::string> list_dir(const std::string& path) override;
  std::string join(const std::string& a, const std::string& b) override;
  std::string mtime_iso8601(const std::string& path) override;
  std::string root() const override;

 private:
  std::string root_;
  bool mounted_;
};

// SD-backed implementation. Compiled only when ADVDECK_FIRMWARE is
// defined. In Phase 1 it is a stub that returns an error from every
// method except the destructor; A01 / later phases will fill it in.
// Declared here so callers can construct it on firmware builds
// without conditional code.
#ifdef ADVDECK_FIRMWARE
class SdStorage : public IStorage {
 public:
  SdStorage();
  ~SdStorage() override;

  bool mount() override;
  bool is_mounted() const override;
  bool exists(const std::string& path) const override;
  std::string ensure_dir(const std::string& path) override;
  std::string write_file(const std::string& path,
                         const std::string& data) override;
  std::string read_file(const std::string& path) override;
  std::string read_file_or(const std::string& path,
                           const std::string& fallback) override;
  std::vector<std::string> list_dir(const std::string& path) override;
  std::string join(const std::string& a, const std::string& b) override;
  std::string mtime_iso8601(const std::string& path) override;
  std::string root() const override;
};
#endif  // ADVDECK_FIRMWARE

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_STORAGE_H_
