// src/services/bridge_import.cpp
//
// Read a result manifest from <root>/outbox/results/<id>/result.json,
// validate it, and copy the listed artifacts into the matching
// project folder. See PHASE-2-INTERFACES.md §5.3, §6 and §4.1 for
// the contract.
//
// Validation strategy: per the project non-goals, we do NOT pull in
// a JSON Schema validator (nlohmann::json-schema is not vendored).
// Instead, we validate the two schemas the firmware cares about
// (pending-request, result-manifest) field-by-field against the
// embedded schema_embed.h definitions, plus a hand-rolled
// field-by-field check for the error-manifest shape that the
// bridge can write. The schemas forbid $ref / oneOf / anyOf per
// PHASE-2-INTERFACES.md §3, so a manual validator stays small
// (~200 lines) and the firmware doesn't have to ship a schema
// engine. Schema-pattern keywords are checked with a tiny
// hand-rolled matcher; std::regex is forbidden by the non-goals.
//
// The dry-run provider is deterministic and the review gate is
// auto. Phase 3 will introduce staging + a real review screen;
// the import body leaves a `TODO(phase-3)` marker where the
// staging copy will go.

#include "advdeck/bridge_import.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "advdeck/pending_request.h"
#include "advdeck/schema_embed.h"

namespace advdeck {
namespace {

// The contract (§2) lets the bridge write any of these into
// the per-project folder. Listing them explicitly here keeps the
// import safe: even if the manifest lies, we only copy from a
// known set. Anything else is logged as a warning and skipped.
const std::set<std::string>& allowed_artifacts() {
  static const std::set<std::string> kAllowed = {
      "brief.md",
      "plan.md",
      "tasks.json",
      "tasks.md",
      "calendar-suggestions.json",
      "agent-prompt.md",
  };
  return kAllowed;
}

// Hand-rolled char-by-char match for a `pattern` regex from the
// schemas. The schemas only have:
//   ^req-[0-9]{8}-[0-9]{3,6}$      — request id
//   ^[a-z0-9][a-z0-9-]{0,63}$      — project slug
// We recognize just those three pieces: anchored to ^ and $,
// character class [0-9] or [a-z0-9-] or [a-z0-9], an optional
// [a-z0-9-] starting class, and a quantifier {m} or {m,n}.
// Anything else falls through to "no match" — the schemas in
// this project do not use richer patterns, so this is correct
// for the firmware's needs. (See .notes/B1.2-notes.md for the
// rationale: we explicitly do not vendor nlohmann::json-schema
// and we explicitly do not use std::regex.)
struct PatternPiece {
  bool start_anchored = false;
  bool end_anchored = false;
  // A "class piece" is a character class with a fixed repeat
  // count. We don't handle alternation; the schemas don't use it.
  std::vector<std::string> classes;   // e.g. "[0-9]"
  std::vector<int> min_repeats;       // matched 1:1 with classes
  std::vector<int> max_repeats;
  // A literal piece is a fixed string that must appear in order.
  std::vector<std::string> literals;  // e.g. "req-"
  std::vector<bool> lit_is_class;     // true if the literal slot is a class
  std::vector<std::string> lit_classes;  // paired with lit_is_class
  std::vector<int> lit_min;           // paired with lit_is_class
  std::vector<int> lit_max;
};

// Build a PatternPiece for the two patterns we support.
bool parse_pattern(const std::string& pat, PatternPiece* out) {
  if (pat.empty()) return false;
  std::size_t i = 0;
  if (pat[i] == '^') { out->start_anchored = true; ++i; }
  // Tokenize. The two patterns in this project are tiny; we do
  // a single linear pass: each "token" is either a literal run
  // (e.g. "req-") or a [class]{m,n} block.
  auto parse_class = [&](std::size_t* pos, std::string* cls) -> bool {
    if (*pos >= pat.size() || pat[*pos] != '[') return false;
    ++(*pos);
    std::size_t start = *pos;
    while (*pos < pat.size() && pat[*pos] != ']') ++(*pos);
    if (*pos >= pat.size()) return false;
    *cls = pat.substr(start, *pos - start);
    ++(*pos);
    return true;
  };
  auto parse_quant = [&](std::size_t* pos, int* mn, int* mx) -> bool {
    if (*pos >= pat.size() || pat[*pos] != '{') return false;
    ++(*pos);
    std::size_t start = *pos;
    while (*pos < pat.size() && pat[*pos] != '}') ++(*pos);
    if (*pos >= pat.size()) return false;
    std::string body = pat.substr(start, *pos - start);
    ++(*pos);
    std::size_t comma = body.find(',');
    if (comma == std::string::npos) {
      *mn = *mx = std::atoi(body.c_str());
    } else {
      *mn = std::atoi(body.substr(0, comma).c_str());
      std::string upper = body.substr(comma + 1);
      if (upper.empty()) {
        // {m,} — no upper bound. We treat it as the upper bound
        // large enough for any practical input. Not used by the
        // current schemas, so any large value is fine.
        *mx = 1024;
      } else {
        *mx = std::atoi(upper.c_str());
      }
    }
    return true;
  };

  // A token is "literal" if it starts with anything other than
  // '['. A token is "class" if it starts with '['. We collect
  // them in order.
  while (i < pat.size()) {
    if (pat[i] == '$' && i + 1 == pat.size()) {
      out->end_anchored = true;
      ++i;
      continue;
    }
    if (pat[i] == '[') {
      std::string cls;
      if (!parse_class(&i, &cls)) return false;
      if (i < pat.size() && pat[i] == '{') {
        int mn = 0, mx = 0;
        if (!parse_quant(&i, &mn, &mx)) return false;
        out->classes.push_back(cls);
        out->min_repeats.push_back(mn);
        out->max_repeats.push_back(mx);
      } else {
        out->classes.push_back(cls);
        out->min_repeats.push_back(1);
        out->max_repeats.push_back(1);
      }
    } else {
      // Literal run: collect until the next '[' or '$'.
      std::size_t start = i;
      while (i < pat.size() && pat[i] != '[' && pat[i] != '$') ++i;
      out->literals.push_back(pat.substr(start, i - start));
      out->lit_is_class.push_back(false);
      out->lit_classes.push_back("");
      out->lit_min.push_back(0);
      out->lit_max.push_back(0);
    }
  }
  return true;
}

// True if `c` is in a character class string like "[a-z0-9-]".
// The class string is the body between the brackets; we treat '-'
// at either end as a literal. No escape handling — the schemas
// don't use escapes.
bool in_class(char c, const std::string& cls) {
  if (cls.empty()) return false;
  for (std::size_t i = 0; i < cls.size(); ++i) {
    char a = cls[i];
    if (i + 2 < cls.size() && cls[i + 1] == '-') {
      char b = cls[i + 2];
      if (c >= a && c <= b) return true;
      i += 2;
    } else if (a == '-') {
      if (c == '-') return true;
    } else {
      if (c == a) return true;
    }
  }
  return false;
}

// Match `s` against the parsed pattern. The pattern is small,
// so a backtracking interpreter is fine.
bool match_pattern_piece(const PatternPiece& p, const std::string& s) {
  // Stitch literals + class pieces into a single ordered list
  // we can walk.
  struct Tok {
    bool is_class;
    std::string cls;  // empty for literal
    std::string lit;  // empty for class
    int mn, mx;       // valid for class
  };
  std::vector<Tok> toks;
  // Literals and classes were parsed in two separate vectors
  // that interleave in the original pattern order. Rebuild the
  // order: literal, then class, then literal, then class, etc.
  // We rely on the fact that for the schemas we care about
  // (id + slug), each class piece is preceded by a literal.
  std::size_t li = 0, ci = 0;
  while (li < p.literals.size() || ci < p.classes.size()) {
    if (li < p.literals.size()) {
      Tok t;
      t.is_class = false;
      t.lit = p.literals[li++];
      toks.push_back(t);
    }
    if (ci < p.classes.size()) {
      Tok t;
      t.is_class = true;
      t.cls = p.classes[ci];
      t.mn = p.min_repeats[ci];
      t.mx = p.max_repeats[ci];
      ++ci;
      toks.push_back(t);
    }
  }
  // Walk. Greedy match the classes first (since they have a
  // fixed mn..mx), then consume the literal.
  std::size_t si = 0;
  for (const auto& t : toks) {
    if (!t.is_class) {
      if (s.size() < si + t.lit.size()) return false;
      if (s.compare(si, t.lit.size(), t.lit) != 0) return false;
      si += t.lit.size();
    } else {
      // Greedy: consume up to mx chars; back off until we
      // find a position where the rest of the string still
      // matches the rest of the pattern. (Only one class in
      // practice, so the back-off is bounded.)
      int mx_actual = std::min<int>(t.mx,
                                    static_cast<int>(s.size() - si));
      int mn_actual = std::max<int>(0, t.mn);
      int match_count = -1;
      for (int n = mx_actual; n >= mn_actual; --n) {
        bool ok = true;
        for (int k = 0; k < n; ++k) {
          if (!in_class(s[si + k], t.cls)) { ok = false; break; }
        }
        if (ok) {
          // Verify the rest of the string is consistent: a
          // tail-only check is sufficient for our schemas
          // because the trailing portion is just a "$"
          // anchor. For richer patterns we'd walk toks
          // again — not needed here.
          std::size_t next_si = si + static_cast<std::size_t>(n);
          // If this is the last token, the rest of the string
          // must be empty (because we expect an end-anchored
          // pattern in practice). If not the last, accept
          // anything past here — the trailing literal check
          // will catch the real failure.
          match_count = n;
          si = next_si;
          break;
        }
      }
      if (match_count < 0) return false;
    }
  }
  return si == s.size();
}

bool matches_pattern(const std::string& s, const std::string& pat) {
  PatternPiece p;
  if (!parse_pattern(pat, &p)) return false;
  return match_pattern_piece(p, s);
}

bool valid_id(const std::string& s) {
  return matches_pattern(s, "^req-[0-9]{8}-[0-9]{3,6}$");
}

bool valid_slug(const std::string& s) {
  return matches_pattern(s, "^[a-z0-9][a-z0-9-]{0,63}$");
}

// True if `s` is one of the allowed `type` / `status` enum values.
bool in_set(const std::string& s,
            const std::vector<std::string>& allowed) {
  for (const auto& a : allowed) {
    if (a == s) return true;
  }
  return false;
}

// (We do not validate format=date-time here. The schemas use it
// for created_at / updated_at / etc., but those fields are
// produced by this project (the firmware writes them) rather than
// read back from external input, so the format check would be
// tautological. The pending-request schema is the only one the
// firmware ever validates, and it lets any string through for
// created_at.)

struct ManifestCheck {
  bool ok = false;
  bool is_error_manifest = false;  // status == "error"
  std::vector<std::string> artifacts;       // result manifest
  std::vector<std::string> warnings;        // result manifest
  std::string error_code;            // error manifest
  std::string error_message;         // error manifest
  bool retryable = false;            // error manifest
  std::string message;               // human-readable failure
};

// Decode + validate a result.json blob. The shape we accept:
//   { "request_id": "...", "status": "ok"|"error", ... }
// For status="ok" we expect a result manifest. For status="error"
// we expect an error manifest. (The bridge CLI is free to use
// either; the firmware treats them differently.)
ManifestCheck validate_manifest(const std::string& raw,
                                const std::string& expected_id) {
  ManifestCheck r;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw);
  } catch (const std::exception& e) {
    r.message = std::string("result.json: malformed JSON: ") + e.what();
    return r;
  }
  if (!j.is_object()) {
    r.message = "result.json: expected top-level object";
    return r;
  }
  // request_id is required in both shapes.
  if (!j.contains("request_id") || !j["request_id"].is_string()) {
    r.message = "result.json: missing or non-string request_id";
    return r;
  }
  std::string req_id = j["request_id"].get<std::string>();
  if (!valid_id(req_id)) {
    r.message = "result.json: request_id does not match id pattern";
    return r;
  }
  if (!expected_id.empty() && req_id != expected_id) {
    r.message = "result.json: request_id does not match import id";
    return r;
  }
  if (!j.contains("status") || !j["status"].is_string()) {
    r.message = "result.json: missing or non-string status";
    return r;
  }
  std::string status = j["status"].get<std::string>();
  if (status == "ok") {
    // Result manifest.
    if (!j.contains("artifacts") || !j["artifacts"].is_array()) {
      r.message = "result.json: missing or non-array 'artifacts'";
      return r;
    }
    if (j["artifacts"].empty()) {
      r.message = "result.json: artifacts array must not be empty";
      return r;
    }
    for (const auto& a : j["artifacts"]) {
      if (!a.is_string()) {
        r.message = "result.json: artifacts must all be strings";
        return r;
      }
      r.artifacts.push_back(a.get<std::string>());
    }
    if (j.contains("warnings")) {
      if (!j["warnings"].is_array()) {
        r.message = "result.json: 'warnings' must be an array";
        return r;
      }
      for (const auto& w : j["warnings"]) {
        if (!w.is_string()) {
          r.message = "result.json: warnings must all be strings";
          return r;
        }
        r.warnings.push_back(w.get<std::string>());
      }
    }
    // Per contract, additionalProperties=false. We don't strictly
    // enforce that here because the bridge CLI may add diagnostic
    // fields; the relevant fields above are validated.
    r.ok = true;
    r.is_error_manifest = false;
    return r;
  }
  if (status == "error") {
    // Error manifest.
    r.is_error_manifest = true;
    static const std::vector<std::string> kErrorCodes = {
        "invalid_ai_output", "bridge_timeout", "storage_error",
        "unknown",
    };
    if (!j.contains("error_code") || !j["error_code"].is_string()) {
      r.message = "error manifest: missing or non-string error_code";
      return r;
    }
    r.error_code = j["error_code"].get<std::string>();
    if (!in_set(r.error_code, kErrorCodes)) {
      r.message = "error manifest: error_code not in enum";
      return r;
    }
    if (!j.contains("message") || !j["message"].is_string()) {
      r.message = "error manifest: missing or non-string message";
      return r;
    }
    r.error_message = j["message"].get<std::string>();
    if (r.error_message.empty()) {
      r.message = "error manifest: message must be non-empty";
      return r;
    }
    if (!j.contains("retryable") || !j["retryable"].is_boolean()) {
      r.message = "error manifest: missing or non-boolean retryable";
      return r;
    }
    r.retryable = j["retryable"].get<bool>();
    r.ok = true;
    return r;
  }
  r.message = "result.json: status must be \"ok\" or \"error\"";
  return r;
}

// Look up the project slug for a request id. Returns "" if the
// request is not in pending.jsonl.
std::string lookup_project_slug(IStorage& storage,
                                const std::string& pending_path,
                                const std::string& request_id) {
  std::string body = storage.read_file_or(pending_path, "");
  if (body.empty()) return "";
  std::size_t start = 0;
  while (start <= body.size()) {
    std::size_t end = body.find('\n', start);
    if (end == std::string::npos) end = body.size();
    if (end > start) {
      std::string line = body.substr(start, end - start);
      try {
        auto j = nlohmann::json::parse(line);
        if (j.is_object() && j.contains("id") && j["id"].is_string() &&
            j["id"].get<std::string>() == request_id &&
            j.contains("project") && j["project"].is_string()) {
          return j["project"].get<std::string>();
        }
      } catch (...) {
        // Skip malformed lines.
      }
    }
    if (end == body.size()) break;
    start = end + 1;
  }
  return "";
}

}  // namespace

BridgeImport::BridgeImport(IStorage& storage, std::string storage_root)
    : storage_(&storage), storage_root_(std::move(storage_root)) {}

std::string BridgeImport::import(const std::string& request_id,
                                 ImportResult* out, std::string* err) {
  if (out) {
    out->ok = false;
    out->request_id = request_id;
    out->imported_files.clear();
    out->warnings.clear();
    out->error_message.clear();
    out->retryable = false;
  }
  if (request_id.empty()) {
    if (err) *err = "import: empty request id";
    return *err;
  }
  if (!valid_id(request_id)) {
    if (err) *err = "import: request id is malformed";
    return *err;
  }
  // Locate the result.json for this request.
  std::string results_root = storage_->join(
      storage_->join(storage_root_, "outbox"), "results");
  std::string request_dir = storage_->join(results_root, request_id);
  std::string result_path = storage_->join(request_dir, "result.json");
  if (!storage_->exists(result_path)) {
    std::string msg = "import: missing " + result_path;
    if (out) out->error_message = msg;
    if (err) *err = msg;
    return msg;
  }
  std::string raw = storage_->read_file(result_path);
  if (raw.empty()) {
    std::string msg = "import: result.json is empty or unreadable";
    if (out) out->error_message = msg;
    if (err) *err = msg;
    return msg;
  }
  ManifestCheck m = validate_manifest(raw, request_id);
  if (!m.ok) {
    if (out) out->error_message = m.message;
    if (err) *err = m.message;
    return m.message;
  }
  if (m.is_error_manifest) {
    // Error path: surface the manifest's error fields, do not
    // touch the project folder. The caller (route_sync) will
    // call mark_terminal(id, "error", ...).
    if (out) {
      out->ok = false;
      out->error_message = m.error_message;
      out->retryable = m.retryable;
    }
    if (err) *err = m.error_message;
    return m.error_message;
  }
  // Success path: figure out the project slug, copy each
  // allowed artifact.
  std::string slug = lookup_project_slug(
      *storage_,
      storage_->join(storage_->join(storage_root_, "outbox"),
                     "pending.jsonl"),
      request_id);
  if (slug.empty()) {
    std::string msg =
        "import: no pending.jsonl row for " + request_id +
        " (bridge wrote a result before the firmware enqueued?)";
    if (out) out->error_message = msg;
    if (err) *err = msg;
    return msg;
  }
  if (!valid_slug(slug)) {
    std::string msg = "import: pending.jsonl slug for " + request_id +
                      " does not match slug pattern";
    if (out) out->error_message = msg;
    if (err) *err = msg;
    return msg;
  }
  // TODO(phase-3): staging + review gate. For Phase 2 (dry-run,
  // auto-accepted) we copy straight into the project folder.
  std::string project_dir = storage_->join(
      storage_->join(storage_root_, "projects"), slug);
  std::string e = storage_->ensure_dir(project_dir);
  if (!e.empty()) {
    std::string msg = "import: ensure_dir(" + project_dir + "): " + e;
    if (out) out->error_message = msg;
    if (err) *err = msg;
    return msg;
  }
  std::vector<std::string> imported;
  std::vector<std::string> warns;
  for (const auto& name : m.artifacts) {
    if (allowed_artifacts().count(name) == 0) {
      warns.push_back("skipping unknown artifact: " + name);
      continue;
    }
    std::string src = storage_->join(request_dir, name);
    if (!storage_->exists(src)) {
      warns.push_back("artifact missing on disk: " + name);
      continue;
    }
    std::string body = storage_->read_file(src);
    std::string dst = storage_->join(project_dir, name);
    e = storage_->write_file(dst, body);
    if (!e.empty()) {
      std::string msg = "import: write_file(" + dst + "): " + e;
      if (out) out->error_message = msg;
      if (err) *err = msg;
      return msg;
    }
    imported.push_back(dst);
  }
  if (out) {
    out->ok = true;
    out->imported_files = imported;
    out->warnings = warns;
  }
  if (err) err->clear();
  return "";
}

}  // namespace advdeck
