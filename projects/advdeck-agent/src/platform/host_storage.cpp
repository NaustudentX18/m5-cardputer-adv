// src/platform/host_storage.cpp
//
// Host-side IStorage backed by std::filesystem on a real on-disk temp
// directory. The root is created on construction; mount() is idempotent
// and exists() / list_dir() / read_file() all use std::filesystem.
//
// This translation unit uses <filesystem>, which is not available in
// the Arduino/ESP32 toolchain. It is excluded from the firmware build
// by build_src_filter in platformio.ini AND by the ADVDECK_FIRMWARE
// guard below as defense in depth.

#ifndef ADVDECK_FIRMWARE

#include "advdeck/storage.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace advdeck {
namespace {

std::string fs_error(const std::error_code& ec) {
  std::ostringstream os;
  os << "filesystem error: " << ec.message() << " [" << ec.value() << "]";
  return os.str();
}
// Translate a logical (storage-rooted, forward-slash) path to a real
// filesystem path. All paths are relative to `root`, even if they
// look absolute: on the firmware side the SD card is mounted at
// "/advdeck/...", so a path like "/advdeck/projects/foo" must land
// under our host temp dir, not at the host filesystem root.
fs::path resolve_under(const std::string& root, const std::string& path) {
  std::string rel = path;
  // Strip a single leading '/' so the SD-style absolute path
  // "/advdeck/projects/foo" becomes "advdeck/projects/foo" relative
  // to our temp root.
  if (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) {
    rel.erase(0, 1);
  }
  fs::path p(rel);
  return (fs::path(root) / p).lexically_normal();
}
// Format a std::filesystem::file_time_type as an ISO8601 UTC string
// (YYYY-MM-DDTHH:MM:SSZ). On glibc 14 the file_clock shares
// CLOCK_REALTIME with system_clock; we just take seconds-since-epoch
// and format with gmtime. Sub-second precision is dropped.
std::string file_time_to_iso8601_utc(std::filesystem::file_time_type ft) {
  using Sec = std::chrono::duration<std::time_t, std::ratio<1>>;
  std::time_t t =
      std::chrono::duration_cast<Sec>(ft.time_since_epoch()).count();
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

}  // namespace
HostStorage::HostStorage(std::string root_path)
    : root_(std::move(root_path)), mounted_(false) {
  // Best-effort create the root now so the storage is usable without an
  // explicit mount() call. mount() is still the documented entry point.
  std::error_code ec;
  fs::create_directories(root_, ec);
  if (!ec) {
    mounted_ = true;
  }
}

bool HostStorage::mount() {
  std::error_code ec;
  fs::create_directories(root_, ec);
  if (ec) return false;
  mounted_ = true;
  return true;
}

bool HostStorage::is_mounted() const { return mounted_; }

bool HostStorage::exists(const std::string& path) const {
  if (path == root_) return true;
  std::error_code ec;
  return fs::exists(resolve_under(root_, path), ec);
}

std::string HostStorage::ensure_dir(const std::string& path) {
  std::error_code ec;
  fs::create_directories(resolve_under(root_, path), ec);
  if (ec) return fs_error(ec);
  return "";
}

std::string HostStorage::write_file(const std::string& path,
                                    const std::string& data) {
  fs::path target = resolve_under(root_, path);
  fs::path tmp = target;
  tmp += ".tmp";

  // Ensure parent directory exists.
  if (target.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    if (ec) return fs_error(ec);
  }

  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      return std::string("open failed: ") + std::strerror(errno);
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out.good()) {
      return std::string("write failed: ") + std::strerror(errno);
    }
    out.flush();
    if (!out.good()) {
      return std::string("flush failed: ") + std::strerror(errno);
    }
  }

  std::error_code ec;
  fs::rename(tmp, target, ec);
  if (ec) {
    // Clean up the tmp file before reporting.
    std::error_code ec2;
    fs::remove(tmp, ec2);
    return fs_error(ec);
  }
  return "";
}

std::string HostStorage::read_file(const std::string& path) {
  fs::path p = resolve_under(root_, path);
  std::error_code ec;
  if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) {
    return "";
  }
  std::ifstream in(p, std::ios::binary);
  if (!in.is_open()) return "";
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

std::string HostStorage::read_file_or(const std::string& path,
                                      const std::string& fallback) {
  std::string s = read_file(path);
  return s.empty() && !exists(path) ? fallback : s;
}

std::vector<std::string> HostStorage::list_dir(const std::string& path) {
  std::vector<std::string> out;
  fs::path p = resolve_under(root_, path);
  std::error_code ec;
  if (!fs::exists(p, ec) || !fs::is_directory(p, ec)) {
    return out;
  }
  for (auto it = fs::directory_iterator(p, ec);
       !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    out.push_back(it->path().filename().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string HostStorage::join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  char last = a.back();
  if (last == '/' || last == '\\') return a + b;
  return a + "/" + b;
}

std::string HostStorage::root() const { return root_; }

std::string HostStorage::mtime_iso8601(const std::string& path) {
  std::error_code ec;
  fs::file_time_type t = fs::last_write_time(resolve_under(root_, path), ec);
  if (ec) return "";
  return file_time_to_iso8601_utc(t);
}

}  // namespace advdeck

#endif  // !ADVDECK_FIRMWARE
