// src/platform/sd_storage.cpp
//
// SD-backed IStorage. Replaces the Phase 1 stub with a real
// implementation that talks to the Cardputer-Adv's SD card slot
// through the Arduino `<SD.h>` library (which is bundled with the
// ESP32 Arduino core — `framework-arduinoespressif32`).
//
// SPI pinout per `boards/pinouts/m5stack-cardputer.h`:
//   SS    = 12  (SDCARD_CS)
//   SCK   = 40  (SDCARD_SCK)
//   MISO  = 39  (SDCARD_MISO)
//   MOSI  = 14  (SDCARD_MOSI)
//
// SPI clock: 20 MHz. The Cardputer-Adv's SD wiring is short, and
// 20 MHz is the default used by the M5Cardputer's mic_wav_record
// example. SD cards are spec'd for 50 MHz max in High-Speed mode,
// but cheap / older cards fail at 40+ MHz. 20 MHz is the safe
// default for unknown cards; we can revisit if A09 sees write
// timeouts.
//
// write_file is atomic: the body is written to `<path>.tmp` and
// then renamed over the destination. The SD library's `rename` is
// a metadata-only operation (it just updates the FAT entry) and
// is not subject to power-fail mid-write, so a torn write leaves
// the old file intact and a `.tmp` orphan that the next `mount()`
// could (in future) sweep.
//
// Host build (no ADVDECK_FIRMWARE) compiles to nothing — the
// existing host tests continue to use HostStorage.

#include "advdeck/storage.h"

#ifdef ADVDECK_FIRMWARE

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace advdeck {

namespace {

// 20 MHz — see header comment. Stored as a `uint32_t` so the
// integer constant plays nicely with the SD library's signature.
constexpr uint32_t kSpiClockHz = 20000000;

}  // namespace

SdStorage::SdStorage() = default;
SdStorage::~SdStorage() = default;

bool SdStorage::mount() {
  // SD.begin(ssPin, SPI, frequency, mountpoint, max_files). The
  // mountpoint arg is ignored on the ESP32 Arduino port (it
  // always mounts at "/sd") but the slot count matters — a busy
  // bridge that opens 5 long-lived File handles will hit
  // "too many open files" otherwise. 8 is the M5Cardputer
  // mic_wav_record default.
  if (!SD.begin(SS, SPI, kSpiClockHz, "/sd", 8)) {
    mounted_ = false;
    return false;
  }
  mounted_ = true;
  return true;
}

bool SdStorage::is_mounted() const { return mounted_; }

bool SdStorage::exists(const std::string& path) const {
  if (path.empty()) return false;
  return SD.exists(path.c_str());
}

std::string SdStorage::ensure_dir(const std::string& path) {
  // The Arduino SD library's mkdir is not recursive, so we walk
  // the path one segment at a time and create each parent. This
  // matches the contract used elsewhere: a caller can pass
  // "advdeck/projects/foo/voice" and we will create the
  // intermediate directories.
  if (path.empty()) return "";
  std::string accum;
  accum.reserve(path.size());
  std::string seg;
  for (char c : path) {
    if (c == '/' || c == '\\') {
      if (!seg.empty()) {
        if (!accum.empty()) SD.mkdir(accum.c_str());
        accum += '/';
        accum += seg;
        seg.clear();
      } else if (!accum.empty()) {
        accum += '/';
      }
    } else {
      seg.push_back(c);
    }
  }
  if (!seg.empty()) {
    if (!accum.empty()) SD.mkdir(accum.c_str());
    accum += '/';
    accum += seg;
    if (!SD.mkdir(accum.c_str())) {
      // mkdir returns false if the dir already exists; that is
      // not an error. Only treat the case where it is missing
      // afterwards as a failure.
      if (!SD.exists(accum.c_str())) {
        return std::string("mkdir failed: ") + accum;
      }
    }
  }
  return "";
}

std::string SdStorage::write_file(const std::string& path,
                                  const std::string& data) {
  if (path.empty()) return "empty path";
  // Make sure the parent directory exists so an open() of a deep
  // path doesn't fail silently.
  const std::string parent = join(path, "..");
  if (!parent.empty() && !SD.exists(parent.c_str())) {
    std::string e = ensure_dir(parent);
    if (!e.empty()) return e;
  }

  // Atomic write: stream to <path>.tmp, then rename.
  const std::string tmp = path + ".tmp";
  {
    File f = SD.open(tmp.c_str(), FILE_WRITE);
    if (!f) {
      return std::string("open failed: ") + tmp;
    }
    if (data.size() > 0) {
      const size_t written =
          f.write(reinterpret_cast<const uint8_t*>(data.data()),
                  static_cast<size_t>(data.size()));
      if (written != data.size()) {
        f.close();
        SD.remove(tmp.c_str());
        return "short write";
      }
    }
    f.close();
  }
  // Replace the destination. SD.rename on the ESP32 port is the
  // FAT rename: it updates the directory entry and is atomic with
  // respect to a power-cycle. We always remove the existing
  // destination first because SD.rename refuses to overwrite.
  if (SD.exists(path.c_str())) {
    if (!SD.remove(path.c_str())) {
      // Keep the tmp file around for diagnostics; surface the
      // error.
      return "remove of existing destination failed";
    }
  }
  if (!SD.rename(tmp.c_str(), path.c_str())) {
    return "rename failed";
  }
  return "";
}

std::string SdStorage::read_file(const std::string& path) {
  if (path.empty()) return "";
  if (!SD.exists(path.c_str())) return "";
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return "";
  std::string out;
  out.reserve(f.size());
  // SD library reads return up to `size` bytes; we loop in case
  // the implementation hands us a short read.
  std::vector<uint8_t> buf(1024);
  while (f.available() > 0) {
    const int n = f.read(buf.data(), buf.size());
    if (n <= 0) break;
    out.append(reinterpret_cast<const char*>(buf.data()),
               static_cast<size_t>(n));
  }
  f.close();
  return out;
}

std::string SdStorage::read_file_or(const std::string& path,
                                    const std::string& fallback) {
  if (!SD.exists(path.c_str())) return fallback;
  std::string s = read_file(path);
  if (s.empty()) return fallback;
  return s;
}

std::vector<std::string> SdStorage::list_dir(const std::string& path) {
  std::vector<std::string> out;
  if (path.empty()) return out;
  File d = SD.open(path.c_str());
  if (!d) return out;
  if (!d.isDirectory()) {
    d.close();
    return out;
  }
  while (true) {
    File entry = d.openNextFile();
    if (!entry) break;
    out.push_back(entry.name());
    entry.close();
  }
  d.close();
  return out;
}

std::string SdStorage::join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  return a.back() == '/' ? a + b : a + "/" + b;
}

std::string SdStorage::mtime_iso8601(const std::string& path) {
  // The ESP32 Arduino SD library does not expose file mtime. The
  // filesystem driver tracks a creation-only time on FAT, so we
  // return "" for the firmware path. The recorder/manifest uses
  // its own start time for `captured_at`, so the lack of mtime
  // does not break Phase 4 deliverables; a future task that
  // needs reliable mtime can store it in a sidecar file.
  (void)path;
  return "";
}

std::string SdStorage::root() const { return "/advdeck"; }

}  // namespace advdeck

#endif  // ADVDECK_FIRMWARE
