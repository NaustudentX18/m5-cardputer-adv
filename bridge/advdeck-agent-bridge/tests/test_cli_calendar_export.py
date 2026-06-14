"""Tests for ``advdeck-bridge calendar export`` and ``advdeck-bridge nl-date``.

Per PHASE-5-INTERFACES.md §7.2 the bridge needs three CLI tests:

1. ``calendar export`` writes a valid .ics file
2. ``calendar export`` exits 1 with a clear message if the calendar
   dir is missing
3. ``nl-date`` exits 0 and prints the ISO8601 for known inputs
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest
from click.testing import CliRunner

from advdeck_bridge.cli import main
from advdeck_bridge.ics_export import IcsExporter, validate as ics_validate


# ---------------------------------------------------------------------------
# Test 1: calendar export writes a valid .ics file
# ---------------------------------------------------------------------------


def _write_events_json(root: Path, events: list[dict]) -> Path:
    """Helper: write a valid events.json fixture and return its path."""
    calendar_dir = root / "calendar"
    calendar_dir.mkdir(parents=True, exist_ok=True)
    path = calendar_dir / "events.json"
    path.write_text(
        json.dumps({"version": 1, "events": events}, indent=2),
        encoding="utf-8",
    )
    return path


def test_calendar_export_writes_valid_ics(tmp_path: Path) -> None:
    """``calendar export`` produces a structurally valid .ics file."""
    root = tmp_path / "advdeck"
    _write_events_json(root, [
        {
            "id": "evt-20260614-001",
            "title": "Standup",
            "starts_at": "2026-06-14T09:00:00Z",
            "status": "accepted",
        },
    ])
    out = tmp_path / "out.ics"

    runner = CliRunner()
    result = runner.invoke(
        main,
        ["calendar", "export", "--storage-root", str(root), "--out", str(out)],
    )

    assert result.exit_code == 0, result.stdout + result.stderr
    assert out.is_file()
    body = out.read_bytes().decode("utf-8")
    # The exporter wrote a valid VCALENDAR.
    assert body.startswith("BEGIN:VCALENDAR\r\n")
    assert body.rstrip("\r\n").endswith("END:VCALENDAR")
    assert "SUMMARY:Standup" in body
    # And the structural validator agrees.
    assert ics_validate(body) == []


def test_calendar_export_handles_empty_calendar(tmp_path: Path) -> None:
    """An events.json with an empty events list produces a shell .ics."""
    root = tmp_path / "advdeck"
    _write_events_json(root, [])
    out = tmp_path / "empty.ics"

    runner = CliRunner()
    result = runner.invoke(
        main,
        ["calendar", "export", "--storage-root", str(root), "--out", str(out)],
    )

    assert result.exit_code == 0, result.stdout + result.stderr
    body = out.read_bytes().decode("utf-8")
    assert "BEGIN:VCALENDAR" in body
    assert "BEGIN:VEVENT" not in body
    assert ics_validate(body) == []


def test_calendar_export_skips_suggested_events(tmp_path: Path) -> None:
    """Only "accepted" events are exported; "suggested" are dropped."""
    root = tmp_path / "advdeck"
    _write_events_json(root, [
        {
            "id": "evt-1",
            "title": "Accepted one",
            "starts_at": "2026-06-14T09:00:00Z",
            "status": "accepted",
        },
        {
            "id": "evt-2",
            "title": "Still a suggestion",
            "starts_at": "2026-06-14T10:00:00Z",
            "status": "suggested",
        },
    ])
    out = tmp_path / "filtered.ics"

    runner = CliRunner()
    result = runner.invoke(
        main,
        ["calendar", "export", "--storage-root", str(root), "--out", str(out)],
    )

    assert result.exit_code == 0, result.stdout + result.stderr
    body = out.read_bytes().decode("utf-8")
    assert "SUMMARY:Accepted one" in body
    assert "Still a suggestion" not in body


# ---------------------------------------------------------------------------
# Test 2: calendar export exits 1 on missing calendar dir
# ---------------------------------------------------------------------------


def test_calendar_export_exits_1_when_calendar_dir_missing(tmp_path: Path) -> None:
    """No calendar dir at all -> exit 1 with a clear, actionable message."""
    root = tmp_path / "advdeck"  # exists but no calendar subdir
    root.mkdir()
    out = tmp_path / "out.ics"

    runner = CliRunner()
    result = runner.invoke(
        main,
        ["calendar", "export", "--storage-root", str(root), "--out", str(out)],
    )

    assert result.exit_code == 1
    # The error goes to stderr.
    assert "calendar export" in result.stderr
    assert "events.json" in result.stderr
    # And nothing was written.
    assert not out.exists()


def test_calendar_export_exits_1_when_events_json_missing(tmp_path: Path) -> None:
    """An empty calendar dir (no events.json) is treated the same as a missing dir."""
    root = tmp_path / "advdeck"
    (root / "calendar").mkdir(parents=True)
    out = tmp_path / "out.ics"

    runner = CliRunner()
    result = runner.invoke(
        main,
        ["calendar", "export", "--storage-root", str(root), "--out", str(out)],
    )

    assert result.exit_code == 1
    assert "events.json" in result.stderr


def test_calendar_export_exits_1_on_malformed_json(tmp_path: Path) -> None:
    """A malformed events.json is reported clearly, not silently ignored."""
    root = tmp_path / "advdeck"
    (root / "calendar").mkdir(parents=True)
    (root / "calendar" / "events.json").write_text("{ not valid json", encoding="utf-8")
    out = tmp_path / "out.ics"

    runner = CliRunner()
    result = runner.invoke(
        main,
        ["calendar", "export", "--storage-root", str(root), "--out", str(out)],
    )

    assert result.exit_code == 1
    assert "not valid JSON" in result.stderr


# ---------------------------------------------------------------------------
# Test 3: nl-date exits 0 with correct ISO output
# ---------------------------------------------------------------------------


def test_nl_date_exits_zero_and_prints_iso8601() -> None:
    """``nl-date`` with a known input prints the expected ISO8601."""
    runner = CliRunner()
    result = runner.invoke(
        main,
        [
            "nl-date",
            "--text", "tomorrow at 9am",
            "--now", "2026-06-10T08:00:00Z",
        ],
    )
    assert result.exit_code == 0, result.stdout + result.stderr
    assert result.stdout.strip() == "2026-06-11T09:00:00Z"


def test_nl_date_prints_empty_string_for_garbage() -> None:
    """Unparseable input prints an empty line and exits 0 (per design)."""
    runner = CliRunner()
    result = runner.invoke(
        main,
        [
            "nl-date",
            "--text", "garbage nonsense",
            "--now", "2026-06-10T08:00:00Z",
        ],
    )
    assert result.exit_code == 0
    assert result.stdout == "\n"  # one empty line


def test_nl_date_defaults_now_to_current_utc() -> None:
    """With no --now, the parser uses current UTC and 'now' returns it.

    We don't pin a specific timestamp; we just check that the
    output is a non-empty ISO8601 string ending in 'Z'.
    """
    runner = CliRunner()
    result = runner.invoke(main, ["nl-date", "--text", "now"])
    assert result.exit_code == 0
    out = result.stdout.strip()
    assert out.endswith("Z")
    assert "T" in out
    assert len(out) == 20  # YYYY-MM-DDTHH:MM:SSZ


def test_nl_date_handles_all_spec_patterns() -> None:
    """Quick smoke for every pattern listed in PHASE-5-INTERFACES.md §5.2."""
    cases = [
        ("tomorrow at 9am", "2026-06-11T09:00:00Z"),
        ("tomorrow at 9:30", "2026-06-11T09:30:00Z"),
        ("tomorrow at 9:30am", "2026-06-11T09:30:00Z"),
        ("in 3 days", "2026-06-13T12:00:00Z"),
        ("next Monday at 2pm", "2026-06-15T14:00:00Z"),
        ("Monday at 2pm", "2026-06-15T14:00:00Z"),
        ("in 2 hours", "2026-06-10T10:00:00Z"),
        ("now", "2026-06-10T08:00:00Z"),
    ]
    runner = CliRunner()
    for text, expected in cases:
        result = runner.invoke(
            main,
            [
                "nl-date",
                "--text", text,
                "--now", "2026-06-10T08:00:00Z",  # Wednesday
            ],
        )
        assert result.exit_code == 0, f"{text!r} failed: {result.stdout}{result.stderr}"
        assert result.stdout.strip() == expected, (
            f"{text!r} -> {result.stdout.strip()!r}, expected {expected!r}"
        )


# ---------------------------------------------------------------------------
# End-to-end: nl-date feeds a calendar export
# ---------------------------------------------------------------------------


def test_nl_date_feeds_into_calendar_export(tmp_path: Path) -> None:
    """An event whose starts_at came from ``nl-date`` survives the round trip.

    The chain we test:

        nl-date "tomorrow at 9am"  ->  ISO timestamp
        build an event with that timestamp
        write events.json
        calendar export            ->  .ics file with that DTSTART
    """
    root = tmp_path / "advdeck"
    _write_events_json(root, [])
    out = tmp_path / "out.ics"

    runner = CliRunner()
    parsed = runner.invoke(
        main,
        [
            "nl-date",
            "--text", "tomorrow at 9am",
            "--now", "2026-06-10T08:00:00Z",
        ],
    )
    assert parsed.exit_code == 0
    iso = parsed.stdout.strip()
    assert iso == "2026-06-11T09:00:00Z"

    _write_events_json(root, [
        {
            "id": "evt-20260611-001",
            "title": "Derived from nl-date",
            "starts_at": iso,
            "status": "accepted",
        },
    ])

    result = runner.invoke(
        main,
        ["calendar", "export", "--storage-root", str(root), "--out", str(out)],
    )
    assert result.exit_code == 0, result.stdout + result.stderr
    body = out.read_bytes().decode("utf-8")
    assert "DTSTART:20260611T090000Z" in body
    assert "SUMMARY:Derived from nl-date" in body
    assert ics_validate(body) == []


# ---------------------------------------------------------------------------
# Help text sanity (so a help-change breaks the test instead of slipping by)
# ---------------------------------------------------------------------------


def test_calendar_group_help_lists_export() -> None:
    runner = CliRunner()
    result = runner.invoke(main, ["calendar", "--help"])
    assert result.exit_code == 0
    assert "export" in result.output


def test_nl_date_help_lists_options() -> None:
    runner = CliRunner()
    result = runner.invoke(main, ["nl-date", "--help"])
    assert result.exit_code == 0
    assert "--text" in result.output
    assert "--now" in result.output
