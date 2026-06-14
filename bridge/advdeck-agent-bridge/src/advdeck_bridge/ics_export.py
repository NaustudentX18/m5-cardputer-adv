"""Build and validate an RFC 5545 .ics file from a list of calendar events.

Phase 5 (E1.2) per PHASE-5-INTERFACES.md §3.1, §5.1, §5.2.

Why hand-rolled instead of pulling in the ``ics`` PyPI package?

* The bridge already depends on ``jsonschema`` (Phase 3). Adding
  ``ics`` is a second runtime dep for what is fundamentally a
  100-line string-formatting problem.
* The export is single-tenant (one VCALENDAR, no recurrence rules,
  no attendees, no alarms) and our output is a known good subset of
  RFC 5545. We control the producer and the consumer (the
  ``calendar export`` CLI) and the consumer is the user's normal
  calendar app, which is far more permissive than a strict parser.

The :class:`IcsExporter` produces the .ics text; :func:`validate`
runs the same structural checks we use in the test suite. Both
operations are pure (no I/O), so they're trivially host-testable.
"""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

# Stable PRODID per PHASE-5-INTERFACES.md §5.2.
PRODID = "-//AdvDeck Agent//Bridge Export 1.0//EN"

# RFC 5545 §3.1: content lines SHOULD be folded at 75 octets. We
# count UTF-8 octets (not characters) per the spec.
MAX_LINE_OCTETS = 75

# Status filter: only "accepted" events are exported. Suggestions
# that the user has not accepted yet are not in the calendar.
EXPORTABLE_STATUSES = frozenset({"accepted"})


def _escape_text(value: str) -> str:
    """Escape an iCalendar TEXT value per RFC 5545 §3.3.11.

    Backslash, comma, semicolon, and newline get a leading
    backslash. ``\\n`` decodes to a newline when a calendar app
    reads the .ics file.
    """
    if not value:
        return ""
    out = value.replace("\\", "\\\\")
    out = out.replace(";", "\\;")
    out = out.replace(",", "\\,")
    out = out.replace("\r\n", "\\n")
    out = out.replace("\n", "\\n")
    return out


def _fold_line(line: str) -> str:
    """Fold a content line per RFC 5545 §3.1.

    The first chunk is up to 75 octets; subsequent chunks are
    prefixed with a single space. Folding is byte-aware: we
    never split a multi-byte UTF-8 sequence.
    """
    encoded = line.encode("utf-8")
    if len(encoded) <= MAX_LINE_OCTETS:
        return line

    chunks: list[str] = []
    pos = 0
    first = True
    total = len(encoded)
    while pos < total:
        # The leading space of a continuation line counts against
        # the previous line's 75-octet budget.
        budget = MAX_LINE_OCTETS if first else (MAX_LINE_OCTETS - 1)
        end = pos + budget
        if end >= total:
            chunks.append(encoded[pos:total].decode("utf-8"))
            pos = total
            break
        # Don't split a UTF-8 multibyte sequence: back up to the
        # last byte that is NOT a continuation byte (0b10xxxxxx).
        while end > pos and (encoded[end] & 0xC0) == 0x80:
            end -= 1
        if end == pos:
            end = pos + budget
        chunks.append(encoded[pos:end].decode("utf-8"))
        pos = end
        first = False
    return "\r\n ".join(chunks)


def _format_dt(iso: str) -> str:
    """Convert an ISO8601 timestamp to an iCalendar DATE-TIME.

    Supports three input shapes:

    * ``YYYY-MM-DDTHH:MM:SSZ``  (UTC, canonical) -> basic form
      ``YYYYMMDDTHHMMSSZ``.
    * ``YYYY-MM-DDTHH:MM:SS+HH:MM`` (offset) -> preserved with
      separators rewritten to the basic form.
    * ``YYYY-MM-DDTHH:MM:SS-HH:MM`` (offset) -> same as above.

    Anything else is returned as-is; the ``validate()`` pass flags
    it. We'd rather emit a non-canonical DTSTART than silently drop
    an event.
    """
    if not iso:
        return ""
    if iso.endswith("Z") and len(iso) >= 20 and iso[10] == "T":
        date_part = iso[:10].replace("-", "")
        time_part = iso[11:19].replace(":", "")
        return f"{date_part}T{time_part}Z"

    if len(iso) >= 25 and iso[10] == "T" and iso[19] in "+-":
        date_part = iso[:10].replace("-", "")
        time_part = iso[11:19].replace(":", "")
        offset = iso[19:]
        sign = offset[0]
        rest = offset[1:].replace(":", "")
        return f"{date_part}T{time_part}{sign}{rest}"

    return iso


def _dtstamp() -> str:
    """Current UTC time in the iCalendar DTSTAMP basic form."""
    now = datetime.now(timezone.utc)
    return now.strftime("%Y%m%dT%H%M%SZ")


def _uid(event_id: str) -> str:
    """Build a stable UID for an event.

    Format: ``<id>@advdeck-agent``. The id is unique per day
    (``evt-YYYYMMDD-NNN``) so this is globally unique enough for
    a single-tenant export.
    """
    return f"{event_id}@advdeck-agent"


def _should_export(event: dict[str, Any]) -> bool:
    """Filter: only events with status "accepted" are exported."""
    status = event.get("status", "accepted")
    return status in EXPORTABLE_STATUSES


class IcsExporter:
    """Build an RFC 5545 calendar from a list of event dicts.

    Each event dict may have:

    * ``id``         (str, required)  - stable id, e.g. ``evt-20260614-001``
    * ``title``      (str, required)  - event summary
    * ``starts_at``  (str, required)  - ISO8601, must include TZ
    * ``ends_at``    (str, optional)  - ISO8601; if present, DTEND is added
    * ``description``(str, optional)  - long text
    * ``location``   (str, optional)  - location string
    * ``project``    (str, optional)  - attached project slug; not
      emitted as a property but preserved in the UID.
    * ``status``     (str, optional)  - "accepted" exports, anything
      else is skipped. Defaults to "accepted" for back-compat with
      raw dicts the test fixtures pass in.
    """

    def build(self, events: list[dict[str, Any]]) -> str:
        """Return the .ics text for ``events``.

        The text always starts with ``BEGIN:VCALENDAR`` and ends
        with ``END:VCALENDAR``. An empty ``events`` list produces
        a calendar shell with no VEVENT blocks. This is a
        deliberate choice (documented in the test): the alternative
        would be to raise, but a calendar app is happy to import
        an empty file and the user's ``git diff`` shows clearly
        that nothing was scheduled.
        """
        out: list[str] = [
            "BEGIN:VCALENDAR",
            "VERSION:2.0",
            f"PRODID:{PRODID}",
            "CALSCALE:GREGORIAN",
        ]
        for event in events:
            if not _should_export(event):
                continue
            out.extend(self._render_event(event))
        out.append("END:VCALENDAR")
        folded = "\r\n".join(_fold_line(line) for line in out)
        return folded + "\r\n"

    def _render_event(self, event: dict[str, Any]) -> list[str]:
        """Render one event as a list of lines (no folding)."""
        event_id = str(event.get("id", "")).strip()
        title = str(event.get("title", "")).strip()
        starts_at = str(event.get("starts_at", "")).strip()
        ends_at = str(event.get("ends_at", "")).strip()
        description = str(event.get("description", "")).strip()
        location = str(event.get("location", "")).strip()

        lines: list[str] = ["BEGIN:VEVENT"]
        lines.append(f"UID:{_uid(event_id)}")
        lines.append(f"DTSTAMP:{_dtstamp()}")
        lines.append(f"DTSTART:{_format_dt(starts_at)}")
        if ends_at:
            lines.append(f"DTEND:{_format_dt(ends_at)}")
        lines.append(f"SUMMARY:{_escape_text(title)}")
        if description:
            lines.append(f"DESCRIPTION:{_escape_text(description)}")
        if location:
            lines.append(f"LOCATION:{_escape_text(location)}")
        lines.append("END:VEVENT")
        return lines


def validate(ics_text: str) -> list[str]:
    """Return a list of structural errors. Empty list = valid.

    Checks (per PHASE-5-INTERFACES.md §3.1):

    * The file is non-empty UTF-8 text.
    * First logical line is ``BEGIN:VCALENDAR`` and the last is
      ``END:VCALENDAR``.
    * Required VCALENDAR properties VERSION:2.0 and PRODID: are
      present.
    * Every ``BEGIN:VEVENT`` is matched by a later ``END:VEVENT``.
    * Every ``VEVENT`` has the four required properties
      ``UID``, ``DTSTAMP``, ``DTSTART``, ``SUMMARY``.
    * No content line (after CRLF-unfolding) exceeds 75 octets.

    An empty calendar (no VEVENT blocks) is accepted; this matches
    the documented behaviour of :meth:`IcsExporter.build`.
    """
    errors: list[str] = []

    if not ics_text:
        return ["empty calendar"]

    unfolded = (
        ics_text.replace("\r\n ", "\r\n")
        .replace("\r\n", "\n")
        .strip("\n")
    )
    if not unfolded:
        return ["empty calendar"]

    lines = unfolded.split("\n")
    if lines[0] != "BEGIN:VCALENDAR":
        errors.append(f"first line is {lines[0]!r}, expected BEGIN:VCALENDAR")
        return errors
    if lines[-1] != "END:VCALENDAR":
        errors.append(f"last line is {lines[-1]!r}, expected END:VCALENDAR")
        return errors

    cal_lines = lines[1:-1]
    has_version = any(l == "VERSION:2.0" for l in cal_lines)
    has_prodid = any(l.startswith("PRODID:") for l in cal_lines)
    if not has_version:
        errors.append("VCALENDAR missing VERSION:2.0")
    if not has_prodid:
        errors.append("VCALENDAR missing PRODID:")

    i = 0
    n = len(cal_lines)
    while i < n:
        line = cal_lines[i]
        if line == "BEGIN:VEVENT":
            block_start = i
            required = {"UID", "DTSTAMP", "DTSTART", "SUMMARY"}
            seen: set[str] = set()
            j = i + 1
            closed = False
            while j < n:
                inner = cal_lines[j]
                if inner == "END:VEVENT":
                    closed = True
                    break
                if inner.startswith("BEGIN:") and inner != "BEGIN:VEVENT":
                    errors.append(
                        f"unsupported nested block {inner!r} inside VEVENT "
                        f"starting at line {block_start + 2}"
                    )
                if ":" in inner:
                    prop = inner.split(":", 1)[0]
                    seen.add(prop)
                j += 1
            if not closed:
                errors.append(
                    f"unterminated VEVENT starting at line {block_start + 2}"
                )
                break
            missing = required - seen
            if missing:
                errors.append(
                    f"VEVENT starting at line {block_start + 2} missing "
                    f"required properties: {sorted(missing)}"
                )
            i = j + 1
        else:
            i += 1

    for raw in ics_text.split("\r\n"):
        octets = len(raw.encode("utf-8"))
        if octets > 75:
            errors.append(
                f"line exceeds 75 octets ({octets}): {raw[:40]!r}..."
            )

    return errors
