// test/host/test_recorder.cpp
//
// Host tests for the Recorder service. The recorder is
// stateful: start() -> append_samples()* -> stop() -> produces
// a WAV file in voice/ and a manifest.json in the same dir.

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/recorder.h"
#include "advdeck/storage.h"
#include "advdeck/wav_writer.h"

namespace fs = std::filesystem;

namespace {

fs::path make_temp_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_d11_rec_" + std::to_string(::getpid()) +
                   "_" + std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return base;
}

advdeck::HostStorage fresh_storage() {
  return advdeck::HostStorage(make_temp_root().string());
}

void recorder_start_stop_writes_manifest_with_correct_fields() {
  auto s = fresh_storage();
  advdeck::Recorder r(s, "/advdeck", "p1");
  std::string err;
  std::string e = r.start(&err);
  EXPECT_EQ(std::string(""), e);
  EXPECT_EQ(std::string(""), err);
  EXPECT_TRUE(r.is_recording());
  // 1 second of 16 kHz mono = 16000 samples.
  std::vector<int16_t> samples(16000);
  for (int i = 0; i < 16000; ++i) {
    samples[i] = static_cast<int16_t>((i % 100) * 100);
  }
  e = r.append_samples(samples.data(), samples.size(), &err);
  EXPECT_EQ(std::string(""), e);
  EXPECT_EQ(std::string(""), err);
  advdeck::RecordingMetadata md;
  e = r.stop(&md, &err);
  EXPECT_EQ(std::string(""), e);
  EXPECT_EQ(std::string(""), err);
  EXPECT_TRUE(!r.is_recording());
  EXPECT_EQ(std::string("recording-1.wav"), md.file_name);
  EXPECT_EQ(1000u, md.duration_ms);
  EXPECT_EQ(16000u, md.sample_rate);
  EXPECT_EQ(1u, md.channels);
  EXPECT_EQ(16u, md.bits_per_sample);
  EXPECT_EQ(64u, md.sha256.size());
  // The manifest file must exist with the right name.
  std::string manifest_path =
      s.join(r.voice_dir(), "recording-1.manifest.json");
  std::string body = s.read_file(manifest_path);
  EXPECT_TRUE(body.find("\"file\": \"recording-1.wav\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"duration_ms\": 1000") != std::string::npos);
  EXPECT_TRUE(body.find("\"sample_rate\": 16000") != std::string::npos);
  EXPECT_TRUE(body.find("\"sha256\": \"" + md.sha256 + "\"") !=
              std::string::npos);
}
void recorder_two_recordings_get_distinct_filenames() {
  auto s = fresh_storage();
  advdeck::Recorder r1(s, "/advdeck", "p1");
  std::string err;
  r1.start(&err);
  std::vector<int16_t> samples1(8000);
  for (int i = 0; i < 8000; ++i) samples1[i] = static_cast<int16_t>(i);
  r1.append_samples(samples1.data(), samples1.size(), &err);
  advdeck::RecordingMetadata md1;
  r1.stop(&md1, &err);
  // The voice dir now has recording-1.wav. A second recorder
  // on the same project should pick recording-2.wav.
  advdeck::Recorder r2(s, "/advdeck", "p1");
  r2.start(&err);
  std::vector<int16_t> samples2(8000);
  for (int i = 0; i < 8000; ++i) samples2[i] = static_cast<int16_t>(i + 1);
  r2.append_samples(samples2.data(), samples2.size(), &err);
  advdeck::RecordingMetadata md2;
  r2.stop(&md2, &err);
  EXPECT_EQ(std::string("recording-1.wav"), md1.file_name);
  EXPECT_EQ(std::string("recording-2.wav"), md2.file_name);
  EXPECT_TRUE(md1.sha256 != md2.sha256);
}

void recorder_append_samples_computes_correct_duration_ms() {
  auto s = fresh_storage();
  advdeck::Recorder r(s, "/advdeck", "p1");
  std::string err;
  r.start(&err);
  // 0.5s at 16kHz = 8000 samples.
  std::vector<int16_t> samples(8000, 0);
  r.append_samples(samples.data(), samples.size(), &err);
  advdeck::RecordingMetadata md;
  r.stop(&md, &err);
  EXPECT_EQ(500u, md.duration_ms);
}

void recorder_stop_creates_voice_dir_if_missing() {
  auto s = fresh_storage();
  // The voice dir does not exist yet.
  advdeck::Recorder r(s, "/advdeck", "fresh");
  std::string err;
  r.start(&err);
  EXPECT_EQ(std::string(""), err);
  std::vector<int16_t> samples(1600, 0);  // 100ms
  r.append_samples(samples.data(), samples.size(), &err);
  advdeck::RecordingMetadata md;
  r.stop(&md, &err);
  EXPECT_EQ(std::string(""), err);
  EXPECT_TRUE(s.exists(r.voice_dir()));
  EXPECT_TRUE(s.exists(s.join(r.voice_dir(), "recording-1.wav")));
}

void recorder_manifest_validates_against_schema() {
  // The schema lives in the same dir as the test runner (the
  // makefile chdir's). The bridge's copy is the same file; the
  // firmware vendored copy is also the same. We compare the
  // keys present in the manifest against the schema's required
  // keys; this is a structural check that catches field-rename
  // bugs without dragging in a JSON-Schema validator.
  auto s = fresh_storage();
  advdeck::Recorder r(s, "/advdeck", "p1");
  std::string err;
  r.start(&err);
  std::vector<int16_t> samples(16000, 0);
  r.append_samples(samples.data(), samples.size(), &err);
  advdeck::RecordingMetadata md;
  r.stop(&md, &err);
  std::string manifest_path =
      s.join(r.voice_dir(), "recording-1.manifest.json");
  std::string body = s.read_file(manifest_path);
  // The schema (per PHASE-4-INTERFACES.md §3.1) requires
  //   version, file, duration_ms, sample_rate, captured_at, sha256
  // and (optionally) channels, bits_per_sample.
  EXPECT_TRUE(body.find("\"version\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"file\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"duration_ms\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"sample_rate\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"captured_at\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"sha256\"") != std::string::npos);
  // version must be 1.
  EXPECT_TRUE(body.find("\"version\": 1") != std::string::npos);
  // file must match recording-N.wav.
  EXPECT_TRUE(body.find("\"file\": \"recording-1.wav\"") != std::string::npos);
  // captured_at must look like a date-time (2026-06-14T...).
  EXPECT_TRUE(body.find("\"captured_at\": \"20") != std::string::npos);
  EXPECT_TRUE(body.find("Z\"") != std::string::npos);
  // sha256 must be 64 hex chars.
  const std::string sha_tag = "\"sha256\": \"";
  const size_t sha_pos = body.find(sha_tag);
  EXPECT_TRUE(sha_pos != std::string::npos);
  const size_t sha_start = sha_pos + sha_tag.size();
  std::string sha = body.substr(sha_start, 64);
  EXPECT_EQ(64u, sha.size());
  for (char c : sha) {
    const bool is_hex =
        (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    EXPECT_TRUE(is_hex);
  }
}

void recorder_manifest_sha256_matches_file_bytes() {
  // The SHA recorded in the manifest must match the SHA of the
  // bytes of recording-1.wav as stored on disk. The bridge uses
  // this to detect tampered or partial WAVs.
  auto s = fresh_storage();
  advdeck::Recorder r(s, "/advdeck", "p1");
  std::string err;
  r.start(&err);
  std::vector<int16_t> samples(16000, 0);
  for (int i = 0; i < 16000; ++i) {
    samples[i] = static_cast<int16_t>(i);
  }
  r.append_samples(samples.data(), samples.size(), &err);
  advdeck::RecordingMetadata md;
  r.stop(&md, &err);
  std::string body = s.read_file(s.join(r.voice_dir(), "recording-1.wav"));
  EXPECT_TRUE(!body.empty());
  // Recompute the SHA from the on-disk bytes.
  std::string expected = advdeck::Recorder::sha256_hex(
      reinterpret_cast<const uint8_t*>(body.data()), body.size());
  EXPECT_EQ(md.sha256, expected);
}

}  // namespace

ADVDECK_REGISTER_TEST(recorder_start_stop_writes_manifest_with_correct_fields,
                      recorder_start_stop_writes_manifest_with_correct_fields);
ADVDECK_REGISTER_TEST(recorder_two_recordings_get_distinct_filenames,
                      recorder_two_recordings_get_distinct_filenames);
ADVDECK_REGISTER_TEST(recorder_append_samples_computes_correct_duration_ms,
                      recorder_append_samples_computes_correct_duration_ms);
ADVDECK_REGISTER_TEST(recorder_stop_creates_voice_dir_if_missing,
                      recorder_stop_creates_voice_dir_if_missing);
ADVDECK_REGISTER_TEST(recorder_manifest_validates_against_schema,
                      recorder_manifest_validates_against_schema);
ADVDECK_REGISTER_TEST(recorder_manifest_sha256_matches_file_bytes,
                      recorder_manifest_sha256_matches_file_bytes);
