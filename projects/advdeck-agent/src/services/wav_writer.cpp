// src/services/wav_writer.cpp
//
// WavWriter implementation. See include/advdeck/wav_writer.h for
// the public contract.
//
// The header is written on create(). The data section is buffered
// in a std::vector<uint8_t> and flushed to the IStorage in
// chunks of >= 4 KiB. Finalize() patches the size fields.

#include "advdeck/wav_writer.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace advdeck {

namespace {

constexpr size_t kWavHeaderSize = 44;
// Flush threshold: when the buffer hits this size we sync it to
// disk. 4 KiB matches the M5Cardputer Mic's per-block read.
constexpr size_t kFlushThreshold = 4096;

void write_u32_le(uint8_t* dst, uint32_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFF);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  dst[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  dst[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void write_u16_le(uint8_t* dst, uint16_t v) {
  dst[0] = static_cast<uint8_t>(v & 0xFF);
  dst[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

// Build the standard 44-byte header. data_size is patched in on
// finalize; the rest is fixed.
void build_header(uint8_t hdr[kWavHeaderSize], const WavSpec& spec) {
  std::memset(hdr, 0, kWavHeaderSize);
  // "RIFF"
  hdr[0] = 'R'; hdr[1] = 'I'; hdr[2] = 'F'; hdr[3] = 'F';
  // file size minus 8 = 36 + data_size; placeholder 36.
  write_u32_le(hdr + 4, 36);
  // "WAVE"
  hdr[8]  = 'W'; hdr[9]  = 'A'; hdr[10] = 'V'; hdr[11] = 'E';
  // "fmt "
  hdr[12] = 'f'; hdr[13] = 'm'; hdr[14] = 't'; hdr[15] = ' ';
  // fmt chunk size = 16
  write_u32_le(hdr + 16, 16);
  // audio format = 1 (PCM)
  write_u16_le(hdr + 20, 1);
  write_u16_le(hdr + 22, spec.channels);
  write_u32_le(hdr + 24, spec.sample_rate);
  const uint32_t byte_rate =
      spec.sample_rate * spec.channels * (spec.bits_per_sample / 8);
  write_u32_le(hdr + 28, byte_rate);
  const uint16_t block_align =
      spec.channels * (spec.bits_per_sample / 8);
  write_u16_le(hdr + 32, block_align);
  write_u16_le(hdr + 34, spec.bits_per_sample);
  // "data"
  hdr[36] = 'd'; hdr[37] = 'a'; hdr[38] = 't'; hdr[39] = 'a';
  // data size placeholder
  write_u32_le(hdr + 40, 0);
}

class WavWriterImpl : public WavWriter {
 public:
  WavWriterImpl(IStorage& storage, std::string path, WavSpec spec)
      : storage_(storage), path_(std::move(path)), spec_(spec) {}

  std::string path() const override { return path_; }
  size_t bytes_written() const override { return data_bytes_; }

  // The static create() method finishes construction after this
  // returns. We split the work so the header bytes are written
  // and the buffer is initialized.
  std::string init(std::string* err) {
    uint8_t hdr[kWavHeaderSize];
    build_header(hdr, spec_);
    std::string body(reinterpret_cast<const char*>(hdr),
                     kWavHeaderSize);
    std::string e = storage_.write_file(path_, body);
    if (!e.empty()) {
      if (err) *err = e;
      return e;
    }
    return "";
  }

  std::string write_samples(const int16_t* samples, size_t n) override {
    if (finalized_) return "writer finalized";
    if (n == 0) return "";
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(samples);
    const size_t nbytes = n * sizeof(int16_t);
    // Append the new bytes to the buffer.
    buffer_.insert(buffer_.end(), bytes, bytes + nbytes);
    data_bytes_ += nbytes;
    if (buffer_.size() >= kFlushThreshold) {
      std::string e = flush_internal();
      if (!e.empty()) return e;
    }
    return "";
  }

  std::string flush(std::string* err) override {
    if (buffer_.empty()) return "";
    return flush_to_storage(err);
  }

  std::string finalize(std::string* err) override {
    if (finalized_) return "";
    // Flush any remaining buffered samples.
    if (!buffer_.empty()) {
      std::string e = flush_to_storage(err);
      if (!e.empty()) return e;
    }
    // Patch the WAV header in place. The current IStorage impls
    // are write_file (atomic, not in-place) and SD doesn't
    // expose random-access. We rewrite the whole file with the
    // updated header + the same body. To avoid a full re-read of
    // the file, we open the WAV file, prepend the new header,
    // and atomically rename. The host's IStorage has no
    // partial-rewrite API; for ~1 MB of recordings the extra
    // read is negligible, but a real firmware impl could keep
    // the file mmap-able and patch the two u32s in place.
    //
    // Practical alternative: re-read the whole file via the
    // storage abstraction and rewrite. That's what we do.
    std::string body = storage_.read_file(path_);
    if (body.size() < kWavHeaderSize) {
      if (err) *err = "wav file too short to finalize";
      return "wav file too short";
    }
    // bytes 4..7 = RIFF size = 36 + data_bytes_
    const uint32_t riff_size = 36 + static_cast<uint32_t>(data_bytes_);
    write_u32_le(reinterpret_cast<uint8_t*>(&body[0]) + 4, riff_size);
    // bytes 40..43 = data size
    write_u32_le(reinterpret_cast<uint8_t*>(&body[0]) + 40,
                 static_cast<uint32_t>(data_bytes_));
    std::string e = storage_.write_file(path_, body);
    if (!e.empty()) {
      if (err) *err = e;
      return e;
    }
    finalized_ = true;
    return "";
  }

 private:
  // flush_to_storage appends `buffer_` to the on-disk file by
  // rewriting the whole file with header + body. That is the
  // simplest correct behavior given the IStorage API: there is
  // no random-access write. For Phase 4 recording lengths
  // (15s-180s of 16 kHz mono = 480 KB - 5.6 MB) the rewrite
  // cost is acceptable. The header is unchanged on the second
  // and subsequent appends because data_bytes_ is patched in
  // finalize().
  std::string flush_to_storage(std::string* err) {
    if (buffer_.empty()) return "";
    // Read the existing on-disk content (header + any prior
    // body chunks), then write back header + prior + new.
    std::string existing = storage_.read_file(path_);
    if (existing.size() < kWavHeaderSize) {
      if (err) *err = "wav file disappeared mid-flush";
      return "wav file missing";
    }
    std::string body = std::move(existing);
    body.append(reinterpret_cast<const char*>(buffer_.data()),
                buffer_.size());
    std::string e = storage_.write_file(path_, body);
    if (!e.empty()) {
      if (err) *err = e;
      return e;
    }
    buffer_.clear();
    return "";
  }

  // flush_internal is the no-arg flavor used from write_samples.
  // We can't use a default-arg + return-error here without an
  // out-param dance; the trivial wrapper keeps the call sites
  // short.
  std::string flush_internal() {
    std::string e;
    std::string r = flush_to_storage(&e);
    return r.empty() ? r : r;
  }

  IStorage& storage_;
  std::string path_;
  WavSpec spec_;
  std::vector<uint8_t> buffer_;
  size_t data_bytes_ = 0;
  bool finalized_ = false;
};

}  // namespace

std::unique_ptr<WavWriter> WavWriter::create(
    IStorage& storage, const std::string& path, const WavSpec& spec,
    std::string* err) {
  auto w = std::unique_ptr<WavWriterImpl>(new WavWriterImpl(
      storage, path, spec));
  std::string e = w->init(err);
  if (!e.empty()) {
    if (err) *err = e;
    return nullptr;
  }
  return w;
}

}  // namespace advdeck
