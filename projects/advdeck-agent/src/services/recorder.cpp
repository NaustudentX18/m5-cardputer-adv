// src/services/recorder.cpp
//
// Recorder service. See include/advdeck/recorder.h for the
// contract and PHASE-4-INTERFACES.md §4.3 for the upstream spec.
//
// SHA-256: hand-rolled. The reference is RFC 6234 (FIPS 180-4).
// This implementation is intentionally straightforward — no
// platform intrinsics, no optimizations — and matches the
// outputs of every other SHA-256 implementation we've
// cross-checked against. ~150 LoC of state + the round constants.

#include "advdeck/recorder.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "advdeck/storage.h"
#include "advdeck/wav_writer.h"

namespace advdeck {

namespace {

// SHA-256 round constants: first 32 bits of the fractional
// parts of the cube roots of the first 64 primes.
constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

void sha256_compress(uint32_t state[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           (static_cast<uint32_t>(block[i * 4 + 3]));
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                        (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                        (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ (~e & g);
    const uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = S0 + mj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

std::string sha256_of(const uint8_t* data, size_t n) {
  // Initial hash values: first 32 bits of the fractional parts
  // of the square roots of the first 8 primes.
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  // Process full 64-byte blocks.
  size_t off = 0;
  while (n - off >= 64) {
    sha256_compress(state, data + off);
    off += 64;
  }
  // Final block(s): padding with 0x80 then zeros then 64-bit
  // big-endian length-in-bits.
  uint8_t tail[128] = {0};
  const size_t rem = n - off;
  std::memcpy(tail, data + off, rem);
  tail[rem] = 0x80;
  size_t tail_blocks = (rem + 9 + 63) / 64;  // ceil((rem+9)/64)
  if (tail_blocks == 0) tail_blocks = 1;
  const size_t last_block_start = (tail_blocks - 1) * 64;
  // Length in bits, big-endian.
  const uint64_t bit_len =
      static_cast<uint64_t>(n) * 8;
  for (int i = 0; i < 8; ++i) {
    tail[last_block_start + 56 + i] =
        static_cast<uint8_t>((bit_len >> (56 - 8 * i)) & 0xFF);
  }
  for (size_t i = 0; i < tail_blocks; ++i) {
    sha256_compress(state, tail + i * 64);
  }
  // Serialize state big-endian.
  char hex[65];
  for (int i = 0; i < 8; ++i) {
    std::snprintf(hex + i * 8, 9, "%08x", state[i]);
  }
  hex[64] = '\0';
  return std::string(hex, 64);
}

std::string now_iso8601_utc() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

// Find the next available "recording-N" index by scanning the
// existing voice/ directory. Returns 1 for an empty / missing
// dir.
int next_recording_index(IStorage& storage,
                         const std::string& voice_dir) {
  std::vector<std::string> entries = storage.list_dir(voice_dir);
  int max_idx = 0;
  for (const auto& e : entries) {
    // We expect "recording-<n>.wav" or "recording-<n>.manifest.json".
    const std::string prefix = "recording-";
    if (e.rfind(prefix, 0) != 0) continue;
    const std::string rest = e.substr(prefix.size());
    // Strip suffix.
    int idx = 0;
    bool any = false;
    for (char c : rest) {
      if (c >= '0' && c <= '9') {
        idx = idx * 10 + (c - '0');
        any = true;
      } else {
        break;
      }
    }
    if (any && idx > max_idx) max_idx = idx;
  }
  return max_idx + 1;
}

}  // namespace

struct Recorder::Impl {
  IStorage* storage = nullptr;
  std::string storage_root;
  std::string slug;
  std::string file_stem;
  std::string wav_path;
  std::string manifest_path;
  std::unique_ptr<WavWriter> writer;
  std::time_t start_time = 0;
  WavSpec spec;
  std::vector<uint8_t> data_buffer;  // samples fed in, hashed on stop
  bool recording = false;
  uint32_t sample_rate = 16000;
};
Recorder::Recorder(IStorage& storage, std::string storage_root,
                   std::string project_slug)
    : impl_(new Impl()),
      project_slug_(std::move(project_slug)) {
  impl_->storage = &storage;
  impl_->storage_root = std::move(storage_root);
  impl_->slug = project_slug_;
  impl_->spec = WavSpec{};
  impl_->sample_rate = impl_->spec.sample_rate;
}

Recorder::~Recorder() = default;

std::string Recorder::voice_dir() const {
  // <storage_root>/projects/<slug>/voice/
  return impl_->storage->join(
      impl_->storage->join(impl_->storage->join(impl_->storage_root,
                                              "projects"),
                          project_slug_),
      "voice");
}

std::string Recorder::wav_path() const {
  return impl_ ? impl_->wav_path : std::string();
}

bool Recorder::is_recording() const { return impl_->recording; }

size_t Recorder::bytes_written() const {
  return impl_->writer ? impl_->writer->bytes_written() : 0;
}

std::string Recorder::start(std::string* err) {
  if (impl_->recording) {
    if (err) *err = "recording already in progress";
    return "already recording";
  }
  // Ensure the voice dir exists.
  std::string e = impl_->storage->ensure_dir(voice_dir());
  if (!e.empty()) {
    if (err) *err = e;
    return e;
  }
  // Pick the next index.
  const int idx = next_recording_index(*impl_->storage, voice_dir());
  impl_->file_stem = "recording-" + std::to_string(idx);
  impl_->wav_path = impl_->storage->join(voice_dir(),
                                        impl_->file_stem + ".wav");
  impl_->manifest_path = impl_->storage->join(
      voice_dir(), impl_->file_stem + ".manifest.json");
  // Create the WAV writer.
  std::string werr;
  impl_->writer = WavWriter::create(*impl_->storage, impl_->wav_path,
                                    impl_->spec, &werr);
  if (!impl_->writer) {
    if (err) *err = werr.empty() ? "WavWriter::create failed" : werr;
    return *err;
  }
  impl_->data_buffer.clear();
  impl_->start_time = std::time(nullptr);
  impl_->recording = true;
  file_stem_ = impl_->file_stem;
  return "";
}

std::string Recorder::append_samples(const int16_t* samples, size_t n,
                                    std::string* err) {
  if (!impl_->recording) {
    if (err) *err = "no active recording";
    return "no active recording";
  }
  if (n == 0) return "";
  // Stream to the WAV file.
  std::string e = impl_->writer->write_samples(samples, n);
  if (!e.empty()) {
    if (err) *err = e;
    return e;
  }
  // Also buffer the bytes for the SHA-256. We could hash
  // incrementally, but we want the SHA to cover the on-disk
  // file (header + body) so the bridge can verify the WAV it
  // reads matches the manifest. We hash on stop() over the
  // fully-finalized file to guarantee that.
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(samples);
  impl_->data_buffer.insert(impl_->data_buffer.end(), bytes,
                            bytes + n * sizeof(int16_t));
  return "";
}

std::string Recorder::stop(RecordingMetadata* out, std::string* err) {
  if (!impl_->recording) {
    if (err) *err = "no active recording";
    return "no active recording";
  }
  // Finalize the WAV file (patches the size fields).
  std::string e = impl_->writer->finalize(err);
  if (!e.empty()) return e;
  // Hash the on-disk file. The SHA must cover the bytes the
  // bridge will actually read, so we hash the file as written
  // (header + body) — not the in-memory buffer.
  std::string body = impl_->storage->read_file(impl_->wav_path);
  if (body.empty()) {
    if (err) *err = "wav file disappeared after finalize";
    return "wav file missing";
  }
  std::string hex = sha256_of(reinterpret_cast<const uint8_t*>(body.data()),
                              body.size());
  // Build the metadata. Duration is derived from the number of
  // samples that made it into the buffer, not from wall-clock
  // time: that way a 1-second pause between append_samples()
  // calls (a test scenario) doesn't inflate the duration.
  const uint64_t total_samples =
      impl_->data_buffer.size() / sizeof(int16_t);
  uint32_t duration_ms = static_cast<uint32_t>(
      (total_samples * 1000ULL) / impl_->spec.sample_rate);
  if (out) {
    out->file_name = impl_->file_stem + ".wav";
    out->captured_at = now_iso8601_utc();
    out->duration_ms = duration_ms;
    out->sample_rate = impl_->spec.sample_rate;
    out->channels = impl_->spec.channels;
    out->bits_per_sample = impl_->spec.bits_per_sample;
    out->sha256 = hex;
  }
  // Write the manifest. The schema is small enough to hand-
  // format; we don't pull nlohmann into the service.
  char manifest[1024];
  std::snprintf(manifest, sizeof(manifest),
                "{\n"
                "  \"version\": 1,\n"
                "  \"file\": \"%s\",\n"
                "  \"duration_ms\": %u,\n"
                "  \"sample_rate\": %u,\n"
                "  \"channels\": %u,\n"
                "  \"bits_per_sample\": %u,\n"
                "  \"captured_at\": \"%s\",\n"
                "  \"sha256\": \"%s\"\n"
                "}\n",
                out->file_name.c_str(), out->duration_ms,
                out->sample_rate, out->channels,
                out->bits_per_sample, out->captured_at.c_str(),
                out->sha256.c_str());
  e = impl_->storage->write_file(impl_->manifest_path, manifest);
  if (!e.empty()) {
    if (err) *err = e;
    return e;
  }
  impl_->recording = false;
  return "";
}

std::string Recorder::cancel(std::string* err) {
  if (!impl_->recording) return "";
  if (!impl_->wav_path.empty()) {
    // We don't have a remove() on IStorage; rewrite as empty
    // and let the next start() reuse the same file_stem if the
    // user starts a new one. The manifest file is removed the
    // same way.
    std::string e = impl_->storage->write_file(impl_->wav_path, "");
    (void)e;
    std::string e2 = impl_->storage->write_file(impl_->manifest_path, "");
    (void)e2;
  }
  impl_->recording = false;
  return "";
}

std::string Recorder::sha256_hex(const std::vector<uint8_t>& data) {
  return sha256_of(data.data(), data.size());
}

std::string Recorder::sha256_hex(const uint8_t* data, size_t n) {
  return sha256_of(data, n);
}

}  // namespace advdeck
