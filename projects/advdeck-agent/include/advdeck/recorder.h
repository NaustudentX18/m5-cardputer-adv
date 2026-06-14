// advdeck/recorder.h
//
// Recorder service. Per PHASE-4-INTERFACES.md §4.3.
//
// Owns the WavWriter, the SHA-256 hasher, the start time, and
// the file name selection logic. On host the start() and
// append_samples() methods are the same code path as on
// firmware; only the actual mic capture (M5Cardputer.Mic) is
// different — and the test framework drives append_samples()
// directly with synthetic buffers, so the host impl is fully
// exercised end-to-end.
//
// SHA-256: we hand-roll a small implementation (see
// src/services/recorder.cpp). It is public-domain, ~80 lines,
// has no dependencies, and matches the spec the bridge's
// `transcribe` step needs. Vendoring a one-header library would
// also be fine, but adds a third-party surface for ~150 LoC of
// code. The recorder's needs are simple: a hash over the
// finalized WAV file (header + samples). Streaming is convenient
// but not required, so the impl buffers the data in a single
// vector during the recording and hashes on stop().

#ifndef ADVDECK_INCLUDE_ADVDECK_RECORDER_H_
#define ADVDECK_INCLUDE_ADVDECK_RECORDER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "advdeck/storage.h"
#include "advdeck/wav_writer.h"

namespace advdeck {

struct RecordingMetadata {
  std::string file_name;       // "recording-1.wav"
  std::string captured_at;     // ISO8601
  uint32_t duration_ms = 0;
  uint32_t sample_rate = 16000;
  uint16_t channels = 1;
  uint16_t bits_per_sample = 16;
  std::string sha256;          // hex
};

class Recorder {
 public:
  // Construct a recorder that will write to
  // <storage_root>/projects/<slug>/voice/recording-<n>.wav. The
  // next available n is computed by listing the voice/ dir. The
  // manifest is written to
  // voice/recording-<n>.manifest.json on finalize.
  Recorder(IStorage& storage, std::string storage_root,
           std::string project_slug);
  ~Recorder();

  // Begin recording. The mic is platform-specific; on host this
  // is a no-op (the test framework drives append_samples()).
  // Returns "" on success.
  std::string start(std::string* err);

  // Append samples. The Recorder buffers in memory and
  // periodically flushes to the WAV file. On host the
  // implementation is identical to firmware; only the caller
  // differs.
  std::string append_samples(const int16_t* samples, size_t n,
                             std::string* err);

  // Stop, finalize, write the manifest. Returns "" on success
  // and populates *out. err is populated with "no active
  // recording" if start was not called.
  std::string stop(RecordingMetadata* out, std::string* err);

  // Cancel an in-progress recording: delete the partial WAV
  // and manifest files. No-op if not recording.
  std::string cancel(std::string* err);

  // True if start() has been called and stop()/cancel() has not.
  bool is_recording() const;

  // Total bytes written to the WAV data section so far.
  size_t bytes_written() const;

  // The path to the WAV file. Empty until start() succeeds.
  std::string wav_path() const;

  // The project slug.
  const std::string& project_slug() const { return project_slug_; }

  // The voice directory the recorder writes to.
  std::string voice_dir() const;

  // The currently-targeted file name (without the .wav/.json
  // suffix). Computed at start() so it stays stable for the
  // whole recording.
  const std::string& file_stem() const { return file_stem_; }

  // Compute the SHA-256 of `data` as a lowercase hex string.
  // Exposed for the host test that wants to verify the
  // hash-on-stop matches a hand-computed value.
  static std::string sha256_hex(const std::vector<uint8_t>& data);

  // Compute the SHA-256 of a range of bytes. Same algorithm,
  // different API for the test.
  static std::string sha256_hex(const uint8_t* data, size_t n);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string project_slug_;
  std::string file_stem_;  // e.g. "recording-1"
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_RECORDER_H_
