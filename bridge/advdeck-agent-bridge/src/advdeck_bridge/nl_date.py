"""Parse short English natural-language date expressions into ISO8601.

Phase 5 (E1.2) per PHASE-5-INTERFACES.md §5.2.

Why hand-rolled instead of pulling in ``python-dateutil`` or
``dateparser``?

* ``python-dateutil`` is a transitive of ``jsonschema`` (used by
  the validation layer) but we never call it; pulling it in
  directly would entrench that coupling and bring in pytz.
* ``dateparser`` is a 5MB+ dependency for a feature we need
  to handle in roughly ten specific input shapes.
* The MVP surface is tiny and we want the behaviour to be 100%
  deterministic and testable against a fixed ``now_iso`` argument.

Date math uses only the standard library ``datetime`` module;
``now_iso`` is a keyword-only argument so test code can pin
time without monkey-patching.

Supported patterns
------------------

* ``"now"``                            -> current time
* ``"tomorrow at 9am"``                -> next day, 09:00:00
* ``"tomorrow at 9:30"``               -> next day, 09:30:00
* ``"tomorrow at 9:30am"``             -> next day, 09:30:00
* ``"in 3 days"``                      -> +N days, 12:00:00 (default time)
* ``"in 2 hours"``                     -> +N hours, default minute/second 0
* ``"Monday at 2pm"``                  -> upcoming Monday 14:00:00
* ``"next Monday at 2pm"``             -> Monday strictly in the future
* ``"Sunday"`` / ``"Sunday at 5pm"``   -> upcoming Sunday (with time)

Patterns that return ``""`` (unparseable)
-----------------------------------------

* empty / whitespace-only input
* strings with no recognisable date marker (e.g. ``"garbage"``)
* weekday names not in the English set
* time strings without a date anchor (e.g. ``"at 9am"`` alone)
* offset magnitudes outside 1..999 (e.g. ``"in 0 days"``)
"""
from __future__ import annotations

import re
from datetime import datetime, timedelta, timezone

# Weekday names -> Python weekday() index (Monday=0, Sunday=6).
WEEKDAYS: dict[str, int] = {
    "monday": 0,
    "tuesday": 1,
    "wednesday": 2,
    "thursday": 3,
    "friday": 4,
    "saturday": 5,
    "sunday": 6,
}

# "in 3 days" / "in 1 day" / "in 2 hours" / "in 1 hour".
_IN_RE = re.compile(r"^in\s+(\d{1,3})\s+(day|days|hour|hours)$", re.IGNORECASE)

# "tomorrow [at HH[:MM][am|pm]]" -- "at" and the time are optional.
_TOMORROW_RE = re.compile(
    r"^tomorrow(?:\s+at\s+(\d{1,2})(?::(\d{2}))?\s*(am|pm)?)?$",
    re.IGNORECASE,
)

# "next <weekday> [at HH[:MM][am|pm]]".
_NEXT_WEEKDAY_RE = re.compile(
    r"^next\s+(monday|tuesday|wednesday|thursday|friday|saturday|sunday)"
    r"(?:\s+at\s+(\d{1,2})(?::(\d{2}))?\s*(am|pm)?)?$",
    re.IGNORECASE,
)

# "<weekday> [at HH[:MM][am|pm]]" -- no "next".
_WEEKDAY_RE = re.compile(
    r"^(monday|tuesday|wednesday|thursday|friday|saturday|sunday)"
    r"(?:\s+at\s+(\d{1,2})(?::(\d{2}))?\s*(am|pm)?)?$",
    re.IGNORECASE,
)


def _normalize(s: str) -> str:
    """Trim and collapse runs of whitespace to a single space."""
    return re.sub(r"\s+", " ", s.strip())


def _parse_time(hour_str: str, minute_str: str | None, ampm: str | None) -> tuple[int, int] | None:
    """Parse a time-of-day from a 12- or 24-hour string.

    Returns ``(hour, minute)`` on success or ``None`` if the
    combination is not a valid clock time.
    """
    if hour_str is None:
        return None
    try:
        hour = int(hour_str)
    except ValueError:
        return None
    minute = int(minute_str) if minute_str else 0
    if ampm:
        ampm_lower = ampm.lower()
        if hour < 1 or hour > 12:
            return None
        if ampm_lower == "am":
            if hour == 12:
                hour = 0
        else:  # pm
            if hour != 12:
                hour += 12
    else:
        if hour < 0 or hour > 23:
            return None
    if minute < 0 or minute > 59:
        return None
    return hour, minute


def _apply_time(base: datetime, hhmm: tuple[int, int]) -> datetime:
    """Return ``base`` with the given time-of-day, preserving tzinfo."""
    return base.replace(hour=hhmm[0], minute=hhmm[1], second=0, microsecond=0)


def _parse_iso(now_iso: str) -> datetime:
    """Parse ``now_iso`` into an aware datetime.

    Accepts ``YYYY-MM-DDTHH:MM:SSZ`` and offset forms like
    ``YYYY-MM-DDTHH:MM:SS+10:00``. A naive timestamp is treated
    as UTC (the project's convention).
    """
    if not now_iso:
        raise ValueError("now_iso must not be empty")
    if now_iso.endswith("Z"):
        return datetime.strptime(now_iso, "%Y-%m-%dT%H:%M:%SZ").replace(
            tzinfo=timezone.utc
        )
    try:
        return datetime.fromisoformat(now_iso)
    except ValueError:
        pass
    return datetime.strptime(now_iso, "%Y-%m-%dT%H:%M:%S").replace(tzinfo=timezone.utc)


def _format_iso(dt: datetime) -> str:
    """Render an aware datetime as ``YYYY-MM-DDTHH:MM:SSZ``."""
    utc = dt.astimezone(timezone.utc)
    return utc.strftime("%Y-%m-%dT%H:%M:%SZ")


def _next_weekday(now: datetime, target_weekday: int) -> datetime:
    """Return the next occurrence of ``target_weekday`` at 12:00:00.

    "next" implies strictly in the future, so when ``now`` is
    already the target weekday we jump a full week.
    """
    delta_days = (target_weekday - now.weekday()) % 7
    if delta_days == 0:
        delta_days = 7
    return (now + timedelta(days=delta_days)).replace(
        hour=12, minute=0, second=0, microsecond=0
    )


def _upcoming_weekday(now: datetime, target_weekday: int) -> datetime:
    """Return the upcoming ``target_weekday`` at 12:00:00 (may be today)."""
    delta_days = (target_weekday - now.weekday()) % 7
    return (now + timedelta(days=delta_days)).replace(
        hour=12, minute=0, second=0, microsecond=0
    )


def parse_nl_date(text: str, *, now_iso: str) -> str:
    """Return an ISO8601 UTC timestamp for ``text``, or ``""`` if unparseable.

    ``text`` is a short English expression. ``now_iso`` is the
    reference time; it is required (keyword-only) so test code
    can pin the clock.
    """
    try:
        now = _parse_iso(now_iso)
    except ValueError:
        return ""

    normalised = _normalize(text)
    if not normalised:
        return ""

    lower = normalised.lower()

    # "now"
    if lower == "now":
        return _format_iso(now)

    # "in N days" / "in N hours"
    m = _IN_RE.match(normalised)
    if m:
        magnitude = int(m.group(1))
        if magnitude < 1:
            return ""
        unit = m.group(2).lower()
        if unit.startswith("day"):
            # "in N days" defaults to noon on the target day.
            target = (now + timedelta(days=magnitude)).replace(
                hour=12, minute=0, second=0, microsecond=0
            )
            return _format_iso(target)
        return _format_iso(now + timedelta(hours=magnitude))

    # "tomorrow [at ...]"
    m = _TOMORROW_RE.match(normalised)
    if m:
        target = (now + timedelta(days=1)).replace(
            hour=9, minute=0, second=0, microsecond=0
        )
        if m.group(1):
            hhmm = _parse_time(m.group(1), m.group(2), m.group(3))
            if hhmm is None:
                return ""
            target = _apply_time(target, hhmm)
        return _format_iso(target)

    # "next <weekday> [at ...]"
    m = _NEXT_WEEKDAY_RE.match(normalised)
    if m:
        weekday = WEEKDAYS[m.group(1).lower()]
        target = _next_weekday(now, weekday)
        if m.group(2):
            hhmm = _parse_time(m.group(2), m.group(3), m.group(4))
            if hhmm is None:
                return ""
            target = _apply_time(target, hhmm)
        return _format_iso(target)

    # "<weekday> [at ...]"
    m = _WEEKDAY_RE.match(normalised)
    if m:
        weekday = WEEKDAYS[m.group(1).lower()]
        target = _upcoming_weekday(now, weekday)
        if m.group(2):
            hhmm = _parse_time(m.group(2), m.group(3), m.group(4))
            if hhmm is None:
                return ""
            target = _apply_time(target, hhmm)
        return _format_iso(target)

    # Unrecognised.
    return ""
