#include <cstdio>
//
// Host tests for advdeck::BridgeLog. We exercise the basic
// append, the JSON shape of the emitted line, the validation
// of the details payload, and the size-based rotation.

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "advdeck/bridge_log.h"
#include "advdeck/expect.h"
#include "advdeck/storage.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_b12_bl_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

void log_event_appends_jsonl_line() {
  auto s = make_storage_with_unique_root();
  advdeck::BridgeLog log(s, "/advdeck");
  std::string err;
  std::string e = log.log_event("req-20260614-001", "import", "{\"ok\":true}",
                                 &err);
  EXPECT_EQ(std::string(""), e);
  EXPECT_EQ(std::string(""), err);
  std::string body = s.read_file(log.log_path());
  EXPECT_TRUE(!body.empty());
  EXPECT_EQ('\n', body.back());
  EXPECT_EQ(1, static_cast<int>(
                     std::count(body.begin(), body.end(), '\n')));
  std::string one = body;
  if (!one.empty() && one.back() == '\n') one.pop_back();
  auto j = nlohmann::json::parse(one);
  EXPECT_TRUE(j.is_object());
  EXPECT_TRUE(j.contains("ts"));
  EXPECT_TRUE(j.contains("request_id"));
  EXPECT_TRUE(j.contains("event"));
  EXPECT_TRUE(j.contains("details"));
  EXPECT_EQ(std::string("req-20260614-001"),
            j["request_id"].get<std::string>());
  EXPECT_EQ(std::string("import"), j["event"].get<std::string>());
  EXPECT_TRUE(j["details"].is_object());
  EXPECT_EQ(true, j["details"]["ok"].get<bool>());
  EXPECT_TRUE(j["ts"].is_string());
  EXPECT_EQ('Z', j["ts"].get<std::string>().back());
}

void log_event_appends_multiple_lines() {
  auto s = make_storage_with_unique_root();
  advdeck::BridgeLog log(s, "/advdeck");
  std::string err;
  log.log_event("req-1", "import", "{}", &err);
  log.log_event("req-2", "import", "{}", &err);
  log.log_event("req-3", "import", "{}", &err);
  std::string body = s.read_file(log.log_path());
  EXPECT_EQ(3, static_cast<int>(
                     std::count(body.begin(), body.end(), '\n')));
}

ADVDECK_REGISTER_TEST(bridge_log_log_event_appends_jsonl_line,
                       log_event_appends_jsonl_line);
ADVDECK_REGISTER_TEST(bridge_log_log_event_appends_multiple_lines,
                       log_event_appends_multiple_lines);

void log_event_validates_event_name() {
  auto s = make_storage_with_unique_root();
  advdeck::BridgeLog log(s, "/advdeck");
  std::string err;
  std::string e = log.log_event("req-1", "", "{}", &err);
  EXPECT_TRUE(!e.empty());
  EXPECT_TRUE(err.find("empty event name") != std::string::npos);
  EXPECT_TRUE(!s.exists(log.log_path()));
}

void log_event_validates_details_shape() {
  auto s = make_storage_with_unique_root();
  advdeck::BridgeLog log(s, "/advdeck");
  std::string err;
  std::string e = log.log_event("req-1", "import", "[]", &err);
  EXPECT_TRUE(!e.empty());
  EXPECT_TRUE(err.find("JSON object") != std::string::npos);
  err.clear();
  e = log.log_event("req-1", "import", "not-json", &err);
  EXPECT_TRUE(!e.empty());
  err.clear();
  e = log.log_event("req-1", "import", "{\"k\":1}", &err);
  EXPECT_EQ(std::string(""), e);
}

void log_event_rotates_after_threshold() {
  auto s = make_storage_with_unique_root();
  advdeck::BridgeLog log(s, "/advdeck");
  std::string err;
  // Pre-populate with a file just under the 1 MiB threshold so
  // the next log line pushes us over and forces a rotation. We
  // use 50 bytes of headroom — well below the 1 MiB threshold
  // but small enough that the new line (~120 bytes of JSON)
  const std::size_t headroom = 50;
  std::string existing(advdeck::BridgeLog::kRotateBytes - headroom, 'x');
  std::string path = log.log_path();
  std::string we = s.write_file(path, existing);
  EXPECT_EQ(std::string(""), we);
  std::string e = log.log_event("req-rotate", "import", "{}", &err);
  EXPECT_EQ(std::string(""), e);
  EXPECT_TRUE(s.exists(path + ".1"));
  EXPECT_TRUE(s.exists(path));
  EXPECT_EQ(existing.size(), s.read_file(path + ".1").size());
  // The main log now holds only the new line, which is well
  // under the threshold. The exact line size depends on the
  // timestamp + request_id, so we just check it's not a million
  // bytes.
  EXPECT_TRUE(s.read_file(path).size() < 1024);
  EXPECT_TRUE(s.read_file(path).find("req-rotate") != std::string::npos);
}

ADVDECK_REGISTER_TEST(bridge_log_log_event_validates_details_shape,
                       log_event_validates_details_shape);
ADVDECK_REGISTER_TEST(bridge_log_log_event_validates_event_name,
                       log_event_validates_event_name);
ADVDECK_REGISTER_TEST(bridge_log_log_event_rotates_after_threshold,
                       log_event_rotates_after_threshold);

}  // namespace
