// src/services/agent_pack_exporter.cpp
//
// A11 Agent Pack Export implementation. See
// PHASE-3-INTERFACES.md §3.1, §4.3, §4.4, §5.2 for the on-disk
// contract; PHASE-3-INTERFACES.md §3.1 for the schema.
//
// The exporter is a pure packager: it reads from the project folder
// and (optionally) the most recent result dir, and writes a
// self-contained export/ folder. It does NOT validate the
// artefacts — that is the bridge's job before the project folder is
// written. It does compute SHA-256 of every artefact so a verifier
// (Z03) can re-check the export round-trips.

#include "advdeck/agent_pack_exporter.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace advdeck {
namespace {

// Names of the artefacts the exporter expects. Stable order so the
// emitted sources.json is diff-friendly.
constexpr const char* kSourceNames[] = {
    "brief.md",
    "plan.md",
    "tasks.json",
    "tasks.md",
    "agent-prompt.md",
};

// The calendar-suggestions file is loaded separately (it lives in
// the result dir, not the project folder).
constexpr const char* kCalendarName = "calendar-suggestions.json";

// Vendored README template path. The host build's CWD is the
// project root (projects/advdeck-agent); the firmware SD impl has
// no FS so we always use the inlined literal there.
constexpr const char* kExportReadmeTemplatePath =
    "templates/export-README.md";

// Inlined literal template. Kept in sync with the on-disk template
// at projects/advdeck-agent/templates/export-README.md. The
// `replace_placeholders` helper substitutes {{ var }} markers
// without pulling in a templating library.
constexpr const char* kExportReadmeTemplate =
    "# Agent pack export \u2014 {{ project_title }}\n"
    "\n"
    "> Project slug: `{{ project_slug }}`\n"
    "> Exported at: `{{ exported_at }}`\n"
    "> Planner provider: `{{ planner_provider }}`\n"
    "> Request id: `{{ request_id }}`\n"
    "\n"
    "---\n"
    "\n"
    "You are a fresh coding agent. The file `agent-pack.md` in this folder\n"
    "is the single source of truth for this project \u2014 read it top to\n"
    "bottom before doing anything else. The structured task list lives in\n"
    "`agent-tasks.json` next to it. `sources.json` is a machine-readable\n"
    "index of the four files in this folder with their SHA-256 hashes.\n"
    "`export-info.json` is the export-time metadata (planner version,\n"
    "exported_at, project slug, request id).\n"
    "\n"
    "Do not invent requirements that are not in the brief or plan. If a\n"
    "task's `acceptance_criteria` is ambiguous, ask before implementing.\n"
    "Match existing style; do not refactor adjacent code. Run the\n"
    "project's existing tests before declaring a task done.\n";

// 32-bit rotate-right. C++17 has no std::rotr.
inline std::uint32_t rotr32(std::uint32_t x, unsigned n) {
  return (x >> n) | (x << (32 - n));
}

// SHA-256 of `data`, returned as a lowercase hex string of length 64.
// Hand-rolled against a tiny reference: the firmware does not vendor
// OpenSSL, and the input sizes here are tiny so an unoptimised
// implementation is fine. We do not need a streaming API because
// each artefact is well under 64 KiB.
std::string sha256_hex(const std::string& data) {
  static const std::array<std::uint32_t, 64> kK = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  };
  std::uint32_t h[8] = {
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  };
  // Pre-processing: pad to a multiple of 64 bytes.
  std::string buf = data;
  const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8;
  buf.push_back(static_cast<char>(0x80));
  while (buf.size() % 64 != 56) buf.push_back(0);
  for (int i = 7; i >= 0; --i) {
    buf.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xff));
  }
  // Process each 512-bit block.
  for (std::size_t off = 0; off < buf.size(); off += 64) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(static_cast<unsigned char>(
                   buf[off + i * 4]))
                   << 24) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(
                   buf[off + i * 4 + 1]))
                   << 16) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(
                   buf[off + i * 4 + 2]))
                   << 8) |
             static_cast<std::uint32_t>(
                 static_cast<unsigned char>(buf[off + i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
      std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^
                         (w[i - 15] >> 3);
      std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^
                         (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      std::uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
      std::uint32_t ch = (e & f) ^ ((~e) & g);
      std::uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
      std::uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
      std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      std::uint32_t t2 = S0 + mj;
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  char hex[65];
  for (int i = 0; i < 8; ++i) {
    std::snprintf(hex + i * 8, 9, "%08x", h[i]);
  }
  hex[64] = '\0';
  return std::string(hex);
}

// Replace `{{ var }}` markers in `template_body` with the value from
// `vars[name]`. Unknown markers are left intact so a typo on the
// template side surfaces visibly. 30 lines max per the contract.
std::string replace_placeholders(
    const std::string& template_body,
    const std::vector<std::pair<std::string, std::string>>& vars) {
  std::string out = template_body;
  for (const auto& kv : vars) {
    const std::string needle = "{{ " + kv.first + " }}";
    std::size_t pos = 0;
    while ((pos = out.find(needle, pos)) != std::string::npos) {
      out.replace(pos, needle.size(), kv.second);
      pos += kv.second.size();
    }
  }
  return out;
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

// Project title from the slug. Same convention as the bridge's
// slug_to_title: capitalise each dash-separated word.
std::string slug_to_title(const std::string& slug) {
  std::string out;
  out.reserve(slug.size());
  bool at_word_start = true;
  for (char c : slug) {
    if (c == '-') {
      out.push_back(' ');
      at_word_start = true;
    } else if (at_word_start) {
      out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
      at_word_start = false;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

// Pick the most recent outbox/results/<id>/calendar-suggestions.json
// by mtime. Returns "" if no result dir has a calendar file.
std::string find_latest_calendar(IStorage& storage,
                                 const std::string& storage_root) {
  std::string results_dir = storage.join(
      storage.join(storage_root, "outbox"), "results");
  std::vector<std::string> dirs = storage.list_dir(results_dir);
  std::string best_path;
  std::string best_mtime;
  for (const auto& name : dirs) {
    std::string d = storage.join(results_dir, name);
    std::string cal = storage.join(d, kCalendarName);
    if (!storage.exists(cal)) continue;
    std::string mt = storage.mtime_iso8601(cal);
    if (mt.empty()) continue;
    if (best_mtime.empty() || mt > best_mtime) {
      best_mtime = mt;
      best_path = cal;
    }
  }
  return best_path;
}

}  // namespace

AgentPackExporter::AgentPackExporter(IStorage& storage,
                                     std::string storage_root)
    : storage_(&storage), storage_root_(std::move(storage_root)) {}

std::string AgentPackExporter::export_root(const std::string& project_slug) const {
  return storage_->join(
      storage_->join(storage_->join(storage_root_, "projects"), project_slug),
      "export");
}

std::string AgentPackExporter::export_project(
    const std::string& project_slug,
    const std::string& planner_provider,
    const std::string& planner_version,
    const std::string* request_id_or_null,
    std::string* err) {
  // 1. Locate the project folder. The exporter does not create it.
  std::string project_dir_path = storage_->join(
      storage_->join(storage_root_, "projects"), project_slug);
  if (!storage_->exists(project_dir_path)) {
    if (err) *err = "agent-pack: project folder missing: " + project_dir_path;
    return "";
  }

  // 2. Read the five source artefacts from the project folder. A
  //    missing source artefact is a warning, not a failure.
  std::vector<std::pair<std::string, std::string>> sources;  // (name, body)
  std::vector<std::string> warnings;
  for (const char* name : kSourceNames) {
    std::string p = storage_->join(project_dir_path, name);
    std::string body = storage_->read_file(p);
    if (body.empty() && !storage_->exists(p)) {
      warnings.push_back(std::string("missing source artefact: ") + name);
      continue;
    }
    sources.push_back({name, body});
  }

  // 3. Calendar: load the latest calendar-suggestions.json from
  //    outbox/results/. Missing -> omit the calendar section.
  std::string calendar_json;
  std::string cal_path = find_latest_calendar(*storage_, storage_root_);
  if (!cal_path.empty()) {
    calendar_json = storage_->read_file(cal_path);
  }
  bool has_calendar = !calendar_json.empty();

  // 4. Build agent-pack.md by stitching the artefacts.
  std::ostringstream pack;
  pack << "# Agent pack: " << slug_to_title(project_slug) << "\n\n";
  pack << "> Project slug: `" << project_slug << "`\n";
  pack << "> Planner provider: `" << planner_provider << "`\n";
  pack << "> Planner version: `" << planner_version << "`\n\n";
  pack << "---\n\n";
  for (const auto& kv : sources) {
    pack << "## " << kv.first << "\n\n";
    pack << kv.second;
    if (kv.second.empty() || kv.second.back() != '\n') pack << "\n";
    pack << "\n";
  }
  if (has_calendar) {
    pack << "## " << kCalendarName << "\n\n";
    pack << "```json\n" << calendar_json << "\n```\n\n";
  } else {
    pack << "## Calendar suggestions\n\n";
    pack << "_No calendar-suggestions.json was available at export time._\n\n";
  }
  std::string agent_pack_md = pack.str();

  // 5. Build agent-tasks.json: parse the source tasks.json (if
  //    present) and add a small metadata block.
  std::string agent_tasks_json;
  {
    nlohmann::json tj;
    tj["version"] = 1;
    tj["project_slug"] = project_slug;
    tj["tasks"] = nlohmann::json::array();
    auto tasks_src = std::find_if(
        sources.begin(), sources.end(),
        [](const std::pair<std::string, std::string>& kv) {
          return kv.first == "tasks.json";
        });
    if (tasks_src != sources.end()) {
      try {
        auto parsed = nlohmann::json::parse(tasks_src->second);
        if (parsed.is_object() && parsed.contains("tasks") &&
            parsed["tasks"].is_array()) {
          tj["tasks"] = parsed["tasks"];
        }
      } catch (const std::exception&) {
        warnings.push_back("tasks.json is not valid JSON; agent-tasks.json "
                           "written with empty task list");
      }
    }
    if (has_calendar) {
      try {
        auto parsed = nlohmann::json::parse(calendar_json);
        if (parsed.is_object() && parsed.contains("suggestions") &&
            parsed["suggestions"].is_array()) {
          tj["calendar_suggestions"] = parsed["suggestions"];
        }
      } catch (const std::exception&) {
        warnings.push_back("calendar-suggestions.json is not valid JSON; "
                           "omitted from agent-tasks.json");
      }
    }
    agent_tasks_json = tj.dump(2);
  }

  // 6. Compute SHA-256 of every source artefact.
  nlohmann::json hash_block = nlohmann::json::object();
  for (const auto& kv : sources) {
    hash_block[kv.first] = sha256_hex(kv.second);
  }
  if (has_calendar) {
    hash_block[kCalendarName] = sha256_hex(calendar_json);
  }

  // 7. Build the README from the vendored template.
  std::string template_body = kExportReadmeTemplate;
#ifndef ADVDECK_FIRMWARE
  for (const std::string& prefix :
       {"", "../", "../../", "../../../"}) {
    std::string candidate = prefix + kExportReadmeTemplatePath;
    std::ifstream f(candidate);
    if (f) {
      std::stringstream ss;
      ss << f.rdbuf();
      template_body = ss.str();
      break;
    }
  }
#endif

  std::string request_id = request_id_or_null ? *request_id_or_null : "";
  std::string rendered_readme = replace_placeholders(
      template_body,
      {
          {"project_slug", project_slug},
          {"project_title", slug_to_title(project_slug)},
          {"exported_at", now_iso8601_utc()},
          {"planner_provider", planner_provider},
          {"request_id", request_id},
      });

  // 8. Write the export files atomically.
  std::string out_dir = export_root(project_slug);
  std::string e = storage_->ensure_dir(out_dir);
  if (!e.empty()) {
    if (err) *err = "agent-pack: ensure_dir(" + out_dir + "): " + e;
    return "";
  }

  nlohmann::json sources_json;
  sources_json["version"] = 1;
  sources_json["project_slug"] = project_slug;
  sources_json["files"] = nlohmann::json::array();

  std::vector<std::pair<std::string, std::string>> export_files = {
      {"agent-pack.md", agent_pack_md},
      {"agent-tasks.json", agent_tasks_json},
      {"README.md", rendered_readme},
  };
  for (const auto& kv : export_files) {
    std::string p = storage_->join(out_dir, kv.first);
    e = storage_->write_file(p, kv.second);
    if (!e.empty()) {
      if (err) *err = "agent-pack: write_file(" + p + "): " + e;
      return "";
    }
    nlohmann::json entry = {
        {"path", kv.first},
        {"bytes", kv.second.size()},
        {"sha256", sha256_hex(kv.second)},
    };
    sources_json["files"].push_back(entry);
  // Also write the 5 source artefacts (brief.md, plan.md, tasks.json,
  // tasks.md, agent-prompt.md) into the export folder so the
  // export is self-contained and sources.json's "files" list
  // matches what is actually on disk.
  for (const auto& kv : sources) {
    std::string p = storage_->join(out_dir, kv.first);
    e = storage_->write_file(p, kv.second);
    if (!e.empty()) {
      if (err) *err = "agent-pack: write_file(" + p + "): " + e;
      return "";
    }
    sources_json["files"].push_back({
        {"path", kv.first},
        {"bytes", kv.second.size()},
        {"sha256", sha256_hex(kv.second)},
    });
  }
  if (has_calendar) {
    std::string p = storage_->join(out_dir, kCalendarName);
    e = storage_->write_file(p, calendar_json);
    if (!e.empty()) {
      if (err) *err = "agent-pack: write_file(" + p + "): " + e;
      return "";
    }
    sources_json["files"].push_back({
        {"path", kCalendarName},
        {"bytes", calendar_json.size()},
        {"sha256", sha256_hex(calendar_json)},
    });
  }

  }

  // 9. Write sources.json.
  std::string sources_path = storage_->join(out_dir, "sources.json");
  e = storage_->write_file(sources_path, sources_json.dump(2));
  if (!e.empty()) {
    if (err) *err = "agent-pack: write_file(" + sources_path + "): " + e;
    return "";
  }

  // 10. Write export-info.json.
  nlohmann::json info;
  info["version"] = 1;
  info["exported_at"] = now_iso8601_utc();
  info["project_slug"] = project_slug;
  info["planner_provider"] = planner_provider;
  info["planner_version"] = planner_version;
  if (!request_id.empty()) info["request_id"] = request_id;
  info["artifact_hashes"] = hash_block;
  std::string info_path = storage_->join(out_dir, "export-info.json");
  e = storage_->write_file(info_path, info.dump(2));
  if (!e.empty()) {
    if (err) *err = "agent-pack: write_file(" + info_path + "): " + e;
    return "";
  }

  // 11. Sidecar log for warnings.
  if (!warnings.empty()) {
    nlohmann::json wlog;
    wlog["warnings"] = warnings;
    storage_->write_file(storage_->join(out_dir, "warnings.json"),
                        wlog.dump(2));
  }
  return out_dir;
}

}  // namespace advdeck
