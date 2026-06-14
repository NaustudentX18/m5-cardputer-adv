// test/host/test_wav_writer.cpp
//
// Host tests for the WavWriter service. We drive the writer
// against HostStorage on a temp dir and assert the resulting
// file is a well-formed WAV (parseable header + correct
// samples).

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/storage.h"
#include "advdeck/wav_writer.h"

namespace fs = std::filesystem;

namespace {

fs::path make_temp_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_d11_wav_" + std::to_string(::getpid()) +
                   "_" + std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return base;
}

advdeck::HostStorage fresh_storage() {
  return advdeck::HostStorage(make_temp_root().string());
}

uint16_t read_u16_le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32_le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void write_1s_mono_16khz() {
  auto s = fresh_storage();
  advdeck::WavSpec spec;
  spec.sample_rate = 16000;
  spec.channels = 1;
  spec.bits_per_sample = 16;
  std::string err;
  auto w = advdeck::WavWriter::create(
      s, "/advdeck/test_1s.wav", spec, &err);
  EXPECT_EQ(std::string(""), err);
  EXPECT_TRUE(w != nullptr);
  std::vector<int16_t> samples(16000);
  for (int i = 0; i < 16000; ++i) {
    samples[i] = static_cast<int16_t>(i * 4);
  }
  err = w->write_samples(samples.data(), samples.size());
  EXPECT_EQ(std::string(""), err);
  err = w->finalize(&err);
  EXPECT_EQ(std::string(""), err);
  // Read the file back and check.
  std::string body = s.read_file("/advdeck/test_1s.wav");
  EXPECT_EQ(static_cast<size_t>(44 + 32000), body.size());
  // Header sanity.
  EXPECT_EQ(0, std::memcmp(body.data(), "RIFF", 4));
  EXPECT_EQ(0, std::memcmp(body.data() + 8, "WAVE", 4));
  EXPECT_EQ(0, std::memcmp(body.data() + 12, "fmt ", 4));
  EXPECT_EQ(0, std::memcmp(body.data() + 36, "data", 4));
}

void write_half_second_mono_16khz() {
  auto s = fresh_storage();
  advdeck::WavSpec spec;
  std::string err;
  auto w = advdeck::WavWriter::create(
      s, "/advdeck/half.wav", spec, &err);
  EXPECT_TRUE(w != nullptr);
  std::vector<int16_t> samples(8000, 0);
  err = w->write_samples(samples.data(), samples.size());
  EXPECT_EQ(std::string(""), err);
  err = w->finalize(&err);
  EXPECT_EQ(std::string(""), err);
  std::string body = s.read_file("/advdeck/half.wav");
  EXPECT_EQ(static_cast<size_t>(44 + 16000), body.size());
}

void write_zero_second_file() {
  auto s = fresh_storage();
  advdeck::WavSpec spec;
  std::string err;
  auto w = advdeck::WavWriter::create(
      s, "/advdeck/zero.wav", spec, &err);
  EXPECT_TRUE(w != nullptr);
  err = w->finalize(&err);
  EXPECT_EQ(std::string(""), err);
  std::string body = s.read_file("/advdeck/zero.wav");
  EXPECT_EQ(static_cast<size_t>(44), body.size());
  // data size field should be 0.
  EXPECT_EQ(0u, read_u32_le(reinterpret_cast<const uint8_t*>(body.data()) + 40));
}

void write_multiple_chunks_across_flushes() {
  auto s = fresh_storage();
  advdeck::WavSpec spec;
  std::string err;
  auto w = advdeck::WavWriter::create(
      s, "/advdeck/multi.wav", spec, &err);
  EXPECT_TRUE(w != nullptr);
  // 4 KiB = 2048 int16 samples. 5 chunks => 10240 samples =>
  // 20480 data bytes.
  std::vector<int16_t> chunk(2048, 0x1234);
  for (int i = 0; i < 5; ++i) {
    err = w->write_samples(chunk.data(), chunk.size());
    EXPECT_EQ(std::string(""), err);
  }
  err = w->finalize(&err);
  EXPECT_EQ(std::string(""), err);
  std::string body = s.read_file("/advdeck/multi.wav");
  EXPECT_EQ(static_cast<size_t>(44 + 10240 * 2), body.size());
}

void finalize_fills_data_size_correctly() {
  auto s = fresh_storage();
  advdeck::WavSpec spec;
  std::string err;
  auto w = advdeck::WavWriter::create(
      s, "/advdeck/finalize.wav", spec, &err);
  EXPECT_TRUE(w != nullptr);
  std::vector<int16_t> samples(2000, 0x7F);
  err = w->write_samples(samples.data(), samples.size());
  EXPECT_EQ(std::string(""), err);
  err = w->finalize(&err);
  EXPECT_EQ(std::string(""), err);
  std::string body = s.read_file("/advdeck/finalize.wav");
  // data size = 4000 bytes.
  const uint32_t data_size =
      read_u32_le(reinterpret_cast<const uint8_t*>(body.data()) + 40);
  EXPECT_EQ(4000u, data_size);
  // RIFF size = 36 + 4000 = 4036.
  const uint32_t riff_size =
      read_u32_le(reinterpret_cast<const uint8_t*>(body.data()) + 4);
  EXPECT_EQ(4036u, riff_size);
}

void header_is_parseable() {
  auto s = fresh_storage();
  advdeck::WavSpec spec;
  spec.sample_rate = 16000;
  spec.channels = 1;
  spec.bits_per_sample = 16;
  std::string err;
  auto w = advdeck::WavWriter::create(
      s, "/advdeck/hdr.wav", spec, &err);
  EXPECT_TRUE(w != nullptr);
  std::vector<int16_t> samples(16000, 0);
  err = w->write_samples(samples.data(), samples.size());
  EXPECT_EQ(std::string(""), err);
  err = w->finalize(&err);
  EXPECT_EQ(std::string(""), err);
  std::string body = s.read_file("/advdeck/hdr.wav");
  const uint8_t* b = reinterpret_cast<const uint8_t*>(body.data());
  EXPECT_EQ(16u, read_u32_le(b + 16));   // fmt chunk size
  EXPECT_EQ(1u, read_u16_le(b + 20));    // PCM format
  EXPECT_EQ(1u, read_u16_le(b + 22));    // channels
  EXPECT_EQ(16000u, read_u32_le(b + 24)); // sample rate
  EXPECT_EQ(32000u, read_u32_le(b + 28)); // byte rate
  EXPECT_EQ(2u, read_u16_le(b + 32));    // block align
  EXPECT_EQ(16u, read_u16_le(b + 34));   // bits per sample
}

void file_data_size_round_trips() {
  auto s = fresh_storage();
  advdeck::WavSpec spec;
  std::string err;
  auto w = advdeck::WavWriter::create(
      s, "/advdeck/round.wav", spec, &err);
  EXPECT_TRUE(w != nullptr);
  // 1234 samples => 2468 data bytes.
  std::vector<int16_t> samples(1234);
  for (int i = 0; i < 1234; ++i) {
    samples[i] = static_cast<int16_t>(i);
  }
  err = w->write_samples(samples.data(), samples.size());
  EXPECT_EQ(std::string(""), err);
  err = w->finalize(&err);
  EXPECT_EQ(std::string(""), err);
  std::string body = s.read_file("/advdeck/round.wav");
  const uint8_t* b = reinterpret_cast<const uint8_t*>(body.data());
  EXPECT_EQ(2468u, read_u32_le(b + 40));
  // Bytes 44..(44+2468) should equal the LE-encoded samples.
  for (int i = 0; i < 1234; ++i) {
    const uint8_t* sp = b + 44 + i * 2;
    const int16_t v = static_cast<int16_t>(read_u16_le(sp));
    EXPECT_EQ(samples[i], v);
  }
}

void sample_bytes_are_written_correctly() {
  // A tiny, distinctive sample pattern. Verify each byte lands
  // at the right offset.
  auto s = fresh_storage();
  advdeck::WavSpec spec;
  std::string err;
  auto w = advdeck::WavWriter::create(
      s, "/advdeck/bytes.wav", spec, &err);
  EXPECT_TRUE(w != nullptr);
  // Use values that are NOT all-zero so a bug that writes
  // zeros (or memcpy's the wrong range) is caught.
  const int16_t pattern[] = {0x0102, 0x0304, 0x0506, 0x0708,
                             0x090a, static_cast<int16_t>(0xfffd),
                             static_cast<int16_t>(0x7fff),
                             static_cast<int16_t>(0x8000)};
  err = w->write_samples(pattern, 8);
  EXPECT_EQ(std::string(""), err);
  err = w->finalize(&err);
  EXPECT_EQ(std::string(""), err);
  std::string body = s.read_file("/advdeck/bytes.wav");
  EXPECT_EQ(static_cast<size_t>(44 + 16), body.size());
  const uint8_t* b = reinterpret_cast<const uint8_t*>(body.data());
  // Little-endian: 0x0102 -> 0x02, 0x01.
  EXPECT_EQ(0x02, b[44 + 0]);
  EXPECT_EQ(0x01, b[44 + 1]);
  EXPECT_EQ(0x04, b[44 + 2]);
  EXPECT_EQ(0x03, b[44 + 3]);
  EXPECT_EQ(0x06, b[44 + 4]);
  EXPECT_EQ(0x05, b[44 + 5]);
  EXPECT_EQ(0x08, b[44 + 6]);
  EXPECT_EQ(0x07, b[44 + 7]);
  EXPECT_EQ(0x0a, b[44 + 8]);
  EXPECT_EQ(0x09, b[44 + 9]);
  // 0xfffd -> 0xfd, 0xff
  EXPECT_EQ(0xfd, b[44 + 10]);
  EXPECT_EQ(0xff, b[44 + 11]);
  // 0x7fff -> 0xff, 0x7f
  EXPECT_EQ(0xff, b[44 + 12]);
  EXPECT_EQ(0x7f, b[44 + 13]);
  // 0x8000 -> 0x00, 0x80
  EXPECT_EQ(0x00, b[44 + 14]);
  EXPECT_EQ(0x80, b[44 + 15]);
}

}  // namespace

ADVDECK_REGISTER_TEST(wav_write_1s_mono_16khz, write_1s_mono_16khz);
ADVDECK_REGISTER_TEST(wav_write_half_second_mono_16khz,
                      write_half_second_mono_16khz);
ADVDECK_REGISTER_TEST(wav_write_zero_second_file, write_zero_second_file);
ADVDECK_REGISTER_TEST(wav_write_multiple_chunks_across_flushes,
                      write_multiple_chunks_across_flushes);
ADVDECK_REGISTER_TEST(wav_finalize_fills_data_size_correctly,
                      finalize_fills_data_size_correctly);
ADVDECK_REGISTER_TEST(wav_header_is_parseable, header_is_parseable);
ADVDECK_REGISTER_TEST(wav_file_data_size_round_trips,
                      file_data_size_round_trips);
ADVDECK_REGISTER_TEST(wav_sample_bytes_are_written_correctly,
                      sample_bytes_are_written_correctly);
