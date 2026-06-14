#!/usr/bin/env python3
# scripts/build_schema_embed.py
#
# Generate include/advdeck/schema_embed.h from the vendored JSON
# schemas the firmware cares about (pending-request, result-manifest,
# agent-pack-export-info).
#
# We can't load the JSON files at runtime on the firmware (no SD in
# Phase 2 — it's a stub), and the contract says no $ref / oneOf / anyOf,
# so a literal C string embedding is the simplest correct path. The
# C++ side parses + validates with nlohmann::json.
#
# Usage:
#   scripts/build_schema_embed.py
#       --schema-dir projects/advdeck-agent/schemas
#       --out include/advdeck/schema_embed.h
#
# Exits 0 on success. Exits non-zero with a clear message on failure.

from __future__ import annotations

import argparse
import pathlib
import sys


# The schemas the firmware embeds. Keep this list in sync with
# PHASE-3-INTERFACES.md §3.1 / §5 and the firmware-side code that
# references the corresponding k<Name>Schema symbols.
SCHEMA_FILES = [
    "pending-request.schema.json",
    "result-manifest.schema.json",
    "agent-pack-export-info.schema.json",
    "recording-manifest.schema.json",
]

# Map the file basename to the C++ symbol we expose.
SYMBOL_NAMES = {
    "pending-request.schema.json": "kPendingRequestSchema",
    "result-manifest.schema.json": "kResultManifestSchema",
    "agent-pack-export-info.schema.json": "kAgentPackExportInfoSchema",
    "recording-manifest.schema.json": "kRecordingManifestSchema",
}


def c_string_escape(s: str) -> str:
    """Produce a valid C string literal body (no surrounding quotes)."""
    out = []
    for ch in s:
        code = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif code < 0x20 or code == 0x7F:
            out.append(f"\\x{code:02x}")
        else:
            # Encode anything above ASCII as UTF-8 bytes. The byte
            # values go through the same printable-ASCII escape so
            # the resulting literal is a valid C string regardless
            # of the source encoding.
            try:
                ch.encode("ascii")
                out.append(ch)
            except UnicodeEncodeError:
                for b in ch.encode("utf-8"):
                    out.append(f"\\x{b:02x}")
    return "".join(out)


def render_header(schema_dir: pathlib.Path) -> str:
    parts = [
        "// include/advdeck/schema_embed.h",
        "//",
        "// GENERATED FILE - DO NOT EDIT BY HAND.",
        "//",
        "// Source: the vendored JSON schemas under",
        "//   projects/advdeck-agent/schemas/",
        "// Regenerate with:",
        "//   python3 scripts/build_schema_embed.py",
        "//   --schema-dir projects/advdeck-agent/schemas",
        "//   --out include/advdeck/schema_embed.h",
        "//",
        "// This header embeds the JSON Schemas as C string literals so the",
        "// firmware can validate bridge results without an SD-backed file",
        "// load (the SD impl is a stub in Phase 2/3).",
        "//",
        "#ifndef ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_",
        "#define ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_",
        "",
        'extern "C" {',
        "",
    ]

    for filename in SCHEMA_FILES:
        path = schema_dir / filename
        if not path.is_file():
            raise FileNotFoundError(
                f"schema file missing: {path} - see PHASE-3-INTERFACES.md §3"
            )
        body = path.read_text(encoding="utf-8")
        symbol = SYMBOL_NAMES[filename]
        parts.append(f"// {filename}")
        parts.append(
            f"inline constexpr const char* {symbol} = "
            f"\"{c_string_escape(body)}\";"
        )
        parts.append("")

    parts.append('}  // extern "C"')
    parts.append("")
    parts.append("#endif  // ADVDECK_INCLUDE_ADVDECK_SCHEMA_EMBED_H_")
    parts.append("")
    return "\n".join(parts)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--schema-dir",
        required=True,
        type=pathlib.Path,
        help="Directory containing the vendored *.schema.json files",
    )
    p.add_argument(
        "--out",
        required=True,
        type=pathlib.Path,
        help="Output header path (will be overwritten)",
    )
    args = p.parse_args()

    if not args.schema_dir.is_dir():
        print(f"schema dir not found: {args.schema_dir}", file=sys.stderr)
        return 2

    try:
        rendered = render_header(args.schema_dir)
    except (OSError, FileNotFoundError) as e:
        print(f"failed to render header: {e}", file=sys.stderr)
        return 1

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(rendered, encoding="utf-8")
    print(f"wrote {args.out} ({len(rendered)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
