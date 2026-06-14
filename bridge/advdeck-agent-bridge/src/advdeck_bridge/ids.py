"""Request id generation.

The firmware and the bridge both produce ids of the form
``req-YYYYMMDD-NNN`` (per PHASE-2-INTERFACES.md §4.1). The bridge only ever
generates ids in two cases:

* directly from the ``plan`` subcommand (no firmware pending row exists)
* as a fallback if ``run-once`` somehow sees a malformed line

The sequence number resets each calendar day. We compute the next sequence by
scanning the existing ``pending.jsonl`` (and any result directories under
``outbox/results/``) for the highest ``NNN`` already used today.
"""
from __future__ import annotations

from datetime import date
import re
from typing import Iterable

ID_PATTERN = re.compile(r"^req-(\d{8})-(\d{3,6})$")


def _today_compact(today: date | None = None) -> str:
    d = today or date.today()
    return d.strftime("%Y%m%d")


def highest_sequence_today(
    compact_today: str,
    *sources: Iterable[str],
) -> int:
    """Return the highest ``NNN`` seen in any of ``sources`` for ``compact_today``.

    Lines or filenames that don't match the ``req-YYYYMMDD-NNN`` pattern are
    ignored. Empty input returns 0.
    """
    highest = 0
    needle = f"req-{compact_today}-"
    for source in sources:
        for line in source.splitlines():
            for token in (line,):
                idx = token.find(needle)
                if idx < 0:
                    continue
                tail = token[idx + len(needle):].split()[0].split("/")[0]
                if tail.isdigit():
                    n = int(tail)
                    if n > highest:
                        highest = n
    return highest


def make_request_id(
    compact_today: str | None = None,
    pending_text: str = "",
    result_names: Iterable[str] = (),
) -> str:
    """Return a fresh ``req-YYYYMMDD-NNN`` id.

    Args:
        compact_today: ``YYYYMMDD`` for the day; defaults to today.
        pending_text: contents of ``outbox/pending.jsonl`` (may be empty).
        result_names: names of entries under ``outbox/results/`` (filenames
            or ids; the function only needs the textual token).
    """
    today = _today_compact() if compact_today is None else compact_today
    next_seq = highest_sequence_today(today, pending_text, *result_names) + 1
    return f"req-{today}-{next_seq:03d}"


def is_valid_request_id(value: str) -> bool:
    """Return True if ``value`` matches the canonical request id shape."""
    return bool(ID_PATTERN.match(value))
