"""Tests for ``advdeck_bridge.ics_export`` (Phase 5, E1.2).

Per PHASE-5-INTERFACES.md §7.2 the bridge needs five tests:

1. empty event list produces BEGIN/END with no VEVENT (or an error)
2. single event produces a valid VEVENT with UID, DTSTAMP, DTSTART, SUMMARY
3. multiple events each get a unique UID
4. line folding kicks in for SUMMARY longer than 75 chars
5. DTSTART with timezone is preserved
"""
from __future__ import annotations

import pytest

from advdeck_bridge.ics_export import (
    EXPORTABLE_STATUSES,
    IcsExporter,
    MAX_LINE_OCTETS,
    PRODID,
    validate,
)


def _split_lines(ics: str) -> list[str]:
    """Split an .ics text into its unfolded content lines."""
    return ics.replace("\r\n ", "").replace("\r\n", "\n").split("\n")


# ---------------------------------------------------------------------------
# Test 1: empty event list -> BEGIN/END shell, no VEVENT
# ---------------------------------------------------------------------------


def test_empty_event_list_produces_calendar_shell() -> None:
    """An empty events list yields a valid VCALENDAR with no VEVENT.

    This is the documented choice in the module: returning a
    shell rather than raising lets the user's ``git diff`` show
    clearly that nothing was scheduled.
    """
    ics = IcsExporter().build([])
    assert ics.startswith("BEGIN:VCALENDAR\r\n")
    assert ics.rstrip("\r\n").endswith("END:VCALENDAR")
    # No VEVENT blocks.
    assert "BEGIN:VEVENT" not in ics
    assert "END:VEVENT" not in ics
    # And the structural validator is happy (empty calendar is allowed).
    assert validate(ics) == []


# ---------------------------------------------------------------------------
# Test 2: single event has UID, DTSTAMP, DTSTART, SUMMARY
# ---------------------------------------------------------------------------


def test_single_event_has_required_vevent_fields() -> None:
    """A single event yields a VEVENT with the four required fields."""
    exporter = IcsExporter()
    ics = exporter.build([
        {
            "id": "evt-20260614-001",
            "title": "Standup",
            "starts_at": "2026-06-14T09:00:00Z",
        }
    ])
    # Required properties per RFC 5545 + our spec.
    assert "UID:evt-20260614-001@advdeck-agent" in ics
    assert "DTSTAMP:" in ics
    assert "DTSTART:20260614T090000Z" in ics
    assert "SUMMARY:Standup" in ics
    # No DTEND/DESCRIPTION/LOCATION when not provided.
    assert "DTEND:" not in ics
    assert "DESCRIPTION:" not in ics
    assert "LOCATION:" not in ics
    # Stable PRODID per spec.
    assert f"PRODID:{PRODID}" in ics
    assert "VERSION:2.0" in ics
    # Structural validation passes.
    assert validate(ics) == []


# ---------------------------------------------------------------------------
# Test 3: multiple events each get a unique UID
# ---------------------------------------------------------------------------


def test_multiple_events_each_get_unique_uid() -> None:
    """Each event produces a unique, stable UID."""
    exporter = IcsExporter()
    ics = exporter.build([
        {
            "id": "evt-20260614-001",
            "title": "Morning standup",
            "starts_at": "2026-06-14T09:00:00Z",
        },
        {
            "id": "evt-20260614-002",
            "title": "Lunch with Alex",
            "starts_at": "2026-06-14T12:30:00Z",
        },
        {
            "id": "evt-20260615-001",
            "title": "Project review",
            "starts_at": "2026-06-15T15:00:00Z",
        },
    ])
    # Each UID appears once.
    assert ics.count("UID:evt-20260614-001@advdeck-agent") == 1
    assert ics.count("UID:evt-20260614-002@advdeck-agent") == 1
    assert ics.count("UID:evt-20260615-001@advdeck-agent") == 1
    # SUMMARY for each event is present.
    assert "SUMMARY:Morning standup" in ics
    assert "SUMMARY:Lunch with Alex" in ics
    assert "SUMMARY:Project review" in ics
    # Three VEVENT blocks.
    assert ics.count("BEGIN:VEVENT") == 3
    # No structural errors.
    assert validate(ics) == []


# ---------------------------------------------------------------------------
# Test 4: line folding kicks in for SUMMARY > 75 chars
# ---------------------------------------------------------------------------


def test_long_summary_is_line_folded_per_rfc5545() -> None:
    """A SUMMARY longer than 75 octets is folded with CRLF + space."""
    long_title = "x" * 100  # 100 ASCII chars -> 100 octets
    ics = IcsExporter().build([
        {
            "id": "evt-20260614-001",
            "title": long_title,
            "starts_at": "2026-06-14T09:00:00Z",
        }
    ])
    # Every content line (after CRLF) must be <= 75 octets.
    for raw in ics.split("\r\n"):
        assert len(raw.encode("utf-8")) <= MAX_LINE_OCTETS, (
            f"line exceeds 75 octets: {raw!r}"
        )
    # The folded form uses CRLF + single-space continuation.
    # Unfolding must give us back the full SUMMARY value.
    unfolded = ics.replace("\r\n ", "")
    assert f"SUMMARY:{long_title}" in unfolded
    # And the validator is happy.
    assert validate(ics) == []


# ---------------------------------------------------------------------------
# Test 5: DTSTART with timezone is preserved
# ---------------------------------------------------------------------------


def test_dtstart_with_timezone_offset_is_preserved() -> None:
    """A starts_at with a +HH:MM offset is rendered as DTSTART with offset."""
    ics = IcsExporter().build([
        {
            "id": "evt-20260614-001",
            "title": "Sydney call",
            "starts_at": "2026-06-14T09:00:00+10:00",
        }
    ])
    # The offset survives the round-trip. RFC 5545 requires the
    # basic form (no colon) for the offset.
    assert "DTSTART:20260614T090000+1000" in ics
    # And the structural check passes.
    assert validate(ics) == []


# ---------------------------------------------------------------------------
# Extra: validate() catches malformed input
# ---------------------------------------------------------------------------


def test_validate_rejects_missing_begin_vcalendar() -> None:
    bad = "VERSION:2.0\r\nPRODID:x\r\nEND:VCALENDAR\r\n"
    errors = validate(bad)
    assert any("BEGIN:VCALENDAR" in e for e in errors)


def test_validate_rejects_empty() -> None:
    assert validate("") != []
    assert validate("   \r\n") != []


def test_validate_rejects_missing_required_field() -> None:
    """A VEVENT with no UID is reported as invalid."""
    ics = (
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//AdvDeck//x//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "DTSTAMP:20260614T090000Z\r\n"
        "DTSTART:20260614T090000Z\r\n"
        "SUMMARY:no uid here\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n"
    )
    errors = validate(ics)
    assert any("UID" in e for e in errors)


def test_status_filter_skips_suggested_events() -> None:
    """Only "accepted" status events are exported; "suggested" are skipped."""
    ics = IcsExporter().build([
        {
            "id": "evt-1",
            "title": "Accepted",
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
    assert "SUMMARY:Accepted" in ics
    assert "Still a suggestion" not in ics
    assert ics.count("BEGIN:VEVENT") == 1


def test_optional_fields_round_trip() -> None:
    """DTEND, DESCRIPTION and LOCATION are written when present."""
    ics = IcsExporter().build([
        {
            "id": "evt-1",
            "title": "Workshop",
            "starts_at": "2026-06-14T09:00:00Z",
            "ends_at": "2026-06-14T12:00:00Z",
            "description": "Bring a laptop; please.",
            "location": "Room 4B",
        }
    ])
    assert "DTEND:20260614T120000Z" in ics
    # The semicolon must be escaped per RFC 5545 §3.3.11.
    assert "DESCRIPTION:Bring a laptop\\; please." in ics
    assert "LOCATION:Room 4B" in ics
    assert validate(ics) == []


def test_exportable_statuses_constant_is_exposed() -> None:
    """The module exposes the exportable-statuses set for tests."""
    assert "accepted" in EXPORTABLE_STATUSES


def test_text_escaping_in_summary() -> None:
    """Commas, semicolons, and backslashes are escaped in TEXT properties."""
    ics = IcsExporter().build([
        {
            "id": "evt-1",
            "title": "Lunch, drinks; plans",
            "starts_at": "2026-06-14T12:00:00Z",
            "description": "Backslash: \\ and newline:\nfoo",
        }
    ])
    # Comma, semicolon escaped.
    assert "SUMMARY:Lunch\\, drinks\\; plans" in ics
    # Backslash escaped (doubled) and newline as literal \n.
    assert "DESCRIPTION:Backslash: \\\\ and newline:\\nfoo" in ics
    assert validate(ics) == []
