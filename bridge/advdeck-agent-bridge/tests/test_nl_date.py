"""Tests for ``advdeck_bridge.nl_date.parse_nl_date``.

Per PHASE-5-INTERFACES.md §7.2 the bridge needs eight tests:

1. "tomorrow at 9am"       -> tomorrow's 09:00:00
2. "tomorrow at 9:30"      -> tomorrow's 09:30:00
3. "in 3 days"             -> +3 days, 12:00:00
4. "next Monday at 2pm"    -> next Monday 14:00:00
5. "Monday at 2pm"         -> upcoming Monday 14:00:00
6. "in 2 hours"            -> +2 hours
7. "now"                   -> current time
8. "garbage nonsense"      -> "" (unparseable)

We pin ``now_iso`` to a known Wednesday so the relative-day
arithmetic is deterministic. The chosen reference is
2026-06-10T08:00:00Z (Wednesday in 2026). Use the same in
``test_cli_calendar_export.py`` when testing the end-to-end
pipeline.
"""
from __future__ import annotations

import pytest

from advdeck_bridge.nl_date import parse_nl_date

# Reference "now" used by the deterministic tests below. 2026-06-10
# is a Wednesday; tests can rely on that weekday to compute the
# offset to Monday or Sunday.
NOW_ISO = "2026-06-10T08:00:00Z"  # Wednesday 2026-06-10 08:00 UTC


# ---------------------------------------------------------------------------
# 1. "tomorrow at 9am" -> tomorrow 09:00:00
# ---------------------------------------------------------------------------


def test_tomorrow_at_9am() -> None:
    assert parse_nl_date("tomorrow at 9am", now_iso=NOW_ISO) == "2026-06-11T09:00:00Z"


def test_tomorrow_at_9am_with_extra_whitespace() -> None:
    assert parse_nl_date("  tomorrow   at   9am  ", now_iso=NOW_ISO) == "2026-06-11T09:00:00Z"


def test_tomorrow_with_no_time_defaults_to_9am() -> None:
    """The "tomorrow" pattern without "at HH" defaults to 09:00.

    This is the calendar convention most users expect when they
    say "tomorrow" without a time: 9am, the start of the workday.
    """
    assert parse_nl_date("tomorrow", now_iso=NOW_ISO) == "2026-06-11T09:00:00Z"


# ---------------------------------------------------------------------------
# 2. "tomorrow at 9:30" -> tomorrow 09:30:00
# ---------------------------------------------------------------------------


def test_tomorrow_at_9_30() -> None:
    """24-hour time without am/pm."""
    assert parse_nl_date("tomorrow at 9:30", now_iso=NOW_ISO) == "2026-06-11T09:30:00Z"


def test_tomorrow_at_9_30am() -> None:
    """12-hour time with explicit am."""
    assert parse_nl_date("tomorrow at 9:30am", now_iso=NOW_ISO) == "2026-06-11T09:30:00Z"


def test_tomorrow_at_9pm() -> None:
    """12-hour pm is correctly mapped past noon."""
    assert parse_nl_date("tomorrow at 9pm", now_iso=NOW_ISO) == "2026-06-11T21:00:00Z"


def test_tomorrow_at_12am() -> None:
    """12am is midnight, not noon."""
    assert parse_nl_date("tomorrow at 12am", now_iso=NOW_ISO) == "2026-06-11T00:00:00Z"


def test_tomorrow_at_12pm() -> None:
    """12pm is noon."""
    assert parse_nl_date("tomorrow at 12pm", now_iso=NOW_ISO) == "2026-06-11T12:00:00Z"


# ---------------------------------------------------------------------------
# 3. "in 3 days" -> +3 days, 12:00:00
# ---------------------------------------------------------------------------


def test_in_three_days() -> None:
    """The "in N days" pattern defaults to noon (12:00:00)."""
    assert parse_nl_date("in 3 days", now_iso=NOW_ISO) == "2026-06-13T12:00:00Z"


def test_in_one_day_singular() -> None:
    """Singular "day" is accepted as well as "days"."""
    assert parse_nl_date("in 1 day", now_iso=NOW_ISO) == "2026-06-11T12:00:00Z"


def test_in_zero_days_is_unparseable() -> None:
    """A magnitude of zero is meaningless; we reject it."""
    assert parse_nl_date("in 0 days", now_iso=NOW_ISO) == ""


# ---------------------------------------------------------------------------
# 4. "next Monday at 2pm" -> next Monday 14:00:00
# ---------------------------------------------------------------------------


def test_next_monday_at_2pm() -> None:
    """Reference is Wed 2026-06-10; next Monday is 2026-06-15."""
    assert parse_nl_date("next Monday at 2pm", now_iso=NOW_ISO) == "2026-06-15T14:00:00Z"


def test_next_sunday_at_9am() -> None:
    """Reference is Wed; next Sunday is the upcoming one (2026-06-14)."""
    assert parse_nl_date("next Sunday at 9am", now_iso=NOW_ISO) == "2026-06-14T09:00:00Z"


def test_next_wednesday_at_noon() -> None:
    """Reference is Wed; "next Wednesday" is the FOLLOWING Wednesday."""
    assert parse_nl_date("next Wednesday at 12pm", now_iso=NOW_ISO) == "2026-06-17T12:00:00Z"


def test_next_weekday_with_no_time_defaults_to_noon() -> None:
    assert parse_nl_date("next Friday", now_iso=NOW_ISO) == "2026-06-12T12:00:00Z"


# ---------------------------------------------------------------------------
# 5. "Monday at 2pm" -> upcoming Monday 14:00:00
# ---------------------------------------------------------------------------


def test_monday_at_2pm() -> None:
    """No "next" -- returns the upcoming Monday (which may be today)."""
    assert parse_nl_date("Monday at 2pm", now_iso=NOW_ISO) == "2026-06-15T14:00:00Z"


def test_monday_with_no_time_defaults_to_noon() -> None:
    assert parse_nl_date("Monday", now_iso=NOW_ISO) == "2026-06-15T12:00:00Z"


def test_sunday_at_5pm() -> None:
    """Reference Wed; upcoming Sunday is 2026-06-14 (4 days from Wed)."""
    assert parse_nl_date("Sunday at 5pm", now_iso=NOW_ISO) == "2026-06-14T17:00:00Z"


# ---------------------------------------------------------------------------
# 6. "in 2 hours" -> +2 hours
# ---------------------------------------------------------------------------


def test_in_two_hours() -> None:
    """Adding 2 hours to 08:00 UTC gives 10:00 UTC."""
    assert parse_nl_date("in 2 hours", now_iso=NOW_ISO) == "2026-06-10T10:00:00Z"


def test_in_one_hour_singular() -> None:
    assert parse_nl_date("in 1 hour", now_iso=NOW_ISO) == "2026-06-10T09:00:00Z"


def test_in_24_hours_rolls_over_a_day() -> None:
    """24 hours from 08:00 Wed = 08:00 Thu."""
    assert parse_nl_date("in 24 hours", now_iso=NOW_ISO) == "2026-06-11T08:00:00Z"


# ---------------------------------------------------------------------------
# 7. "now" -> current time
# ---------------------------------------------------------------------------


def test_now_returns_now_iso() -> None:
    assert parse_nl_date("now", now_iso=NOW_ISO) == NOW_ISO


def test_now_is_case_insensitive() -> None:
    assert parse_nl_date("NOW", now_iso=NOW_ISO) == NOW_ISO
    assert parse_nl_date("Now", now_iso=NOW_ISO) == NOW_ISO


# ---------------------------------------------------------------------------
# 8. "garbage nonsense" -> ""
# ---------------------------------------------------------------------------


def test_garbage_returns_empty_string() -> None:
    assert parse_nl_date("garbage nonsense", now_iso=NOW_ISO) == ""


def test_empty_string_returns_empty_string() -> None:
    assert parse_nl_date("", now_iso=NOW_ISO) == ""


def test_whitespace_only_returns_empty_string() -> None:
    assert parse_nl_date("   \t  ", now_iso=NOW_ISO) == ""


def test_unknown_weekday_returns_empty_string() -> None:
    """Made-up day names are rejected."""
    assert parse_nl_date("Funday at 3pm", now_iso=NOW_ISO) == ""


def test_time_without_date_anchor_returns_empty_string() -> None:
    """A bare time has no date anchor and is unparseable."""
    assert parse_nl_date("at 9am", now_iso=NOW_ISO) == ""


# ---------------------------------------------------------------------------
# Timezone handling for now_iso
# ---------------------------------------------------------------------------


def test_now_iso_with_offset_is_handled() -> None:
    """The reference time can be a +HH:MM offset; the output is UTC.

    Input ``+10:00`` means the local time in Sydney; we render
    back as UTC. The arithmetic uses the absolute moment in time,
    not the local clock, so an "in 2 hours" offset is 2 actual
    hours later regardless of timezone.
    """
    # 18:00 +10:00 == 08:00 UTC. Reference matches NOW_ISO's instant.
    result = parse_nl_date("in 2 hours", now_iso="2026-06-10T18:00:00+10:00")
    assert result == "2026-06-10T10:00:00Z"


def test_invalid_now_iso_returns_empty() -> None:
    """A bogus now_iso does not crash; we return "" as the unparseable sentinel."""
    assert parse_nl_date("now", now_iso="") == ""
    assert parse_nl_date("tomorrow at 9am", now_iso="not a date") == ""
