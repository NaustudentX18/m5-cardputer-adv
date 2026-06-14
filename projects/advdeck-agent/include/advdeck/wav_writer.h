// advdeck/wav_writer.h
//
// Streaming 16-bit PCM WAV writer. Per PHASE-4-INTERFACES.md §4.2.
//
// The writer:
//   - Writes a 44-byte standard PCM header on construction.
//   - Buffers append_samples() calls in memory; flushes to the
//     underlying IStorage in 4 KiB chunks (matches the M5Cardputer
//     Mic's `record_number * record_length * sizeof(int16_t) =
//     512*240*2 = 240 KiB` chunk size for the full buffer, but
//     our flush boundary is 4 KiB so the file is recoverable on
//     a power cycle sooner than that).
//   - On finalize() patches the data size in the WAV header
//     (offset 4: RIFF size, offset 40: data size).
//
// Pure C++17, no Arduino. The firmware build and the host build
// share the same .o; both link to the same WavWriter. The
// underlying IStorage is HostStorage on host and SdStorage on
// firmware — that is the entire point of the storage abstraction.

#ifndef ADVDECK_INCLUDE_ADVDECK_WAV_WRITER_H_
#define ADVDECK_INCLUDE_ADVDECK_WAV_WRITER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "advdeck/storage.h"

namespace advdeck {

struct WavSpec {
  uint32_t sample_rate = 16000;
  uint16_t channels = 1;        // 1 = mono
  uint16_t bits_per_sample = 16;
};

class WavWriter {
 public:
  // Create a writer for `path` with the given spec. Writes the WAV
  // header on construction. Returns "" on success, error message
  // on failure.
  static std::unique_ptr<WavWriter> create(
      IStorage& storage, const std::string& path, const WavSpec& spec,
      std::string* err);

  virtual ~WavWriter() = default;

  // Append a buffer of 16-bit PCM samples. Returns "" on success.
  // Internally buffers and flushes in 4 KiB chunks. The caller
  // MUST not retain `samples` past the return of this function.
  virtual std::string write_samples(const int16_t* samples,
                                    size_t n) = 0;

  // Finalize: patch the data size fields in the WAV header.
  // Idempotent. Must be called before the file is consumed.
  virtual std::string finalize(std::string* err) = 0;

  // Returns the absolute on-storage path of the WAV file.
  virtual std::string path() const = 0;

  // Returns the number of bytes written to the data section so far
  // (excluding the 44-byte header).
  virtual size_t bytes_written() const = 0;

  // Flush any buffered samples to the underlying file. Does not
  // patch the data-size fields; finalize() does that. Safe to
  // call multiple times.
  virtual std::string flush(std::string* err) = 0;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_WAV_WRITER_H_
