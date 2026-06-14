"""Read and mutate ``outbox/pending.jsonl``.

The JSONL file is the canonical bridge queue (PHASE-2-INTERFACES.md §4.2).
Each line is a ``PendingBridgeRequest``. Phase 2 is single-writer:

* the firmware appends new pending rows
* the bridge reads and in-place edits a row to ``in_flight`` (or marks it
  ``error``/``done`` if the bridge itself is what terminates it)
* the firmware does the rest

In-place edits are done by reading the whole file, rewriting the line that
matches the target ``id``, and atomically swapping the file on disk. We never
delete lines from the bridge — the firmware owns compaction.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Iterator

from .ids import make_request_id
from .paths import pending_path

STATUS_PENDING = "pending"
STATUS_IN_FLIGHT = "in_flight"
STATUS_DONE = "done"
STATUS_ERROR = "error"

VALID_STATUSES = {STATUS_PENDING, STATUS_IN_FLIGHT, STATUS_DONE, STATUS_ERROR}


class QueueError(RuntimeError):
    """Raised when a pending.jsonl line is malformed or unwriteable."""


@dataclass(frozen=True)
class PendingRequest:
    """In-memory view of one pending.jsonl line."""

    id: str
    project: str
    type: str
    inputs: list[str]
    created_at: str
    status: str
    attempts: int

    @classmethod
    def from_dict(cls, data: dict) -> "PendingRequest":
        for required in ("id", "project", "type", "inputs", "created_at", "status"):
            if required not in data:
                raise QueueError(f"missing required field {required!r} in {data!r}")
        if data["status"] not in VALID_STATUSES:
            raise QueueError(f"invalid status {data['status']!r} in {data!r}")
        attempts = int(data.get("attempts", 0))
        if attempts < 0:
            raise QueueError(f"attempts must be >= 0 (got {attempts}) in {data!r}")
        return cls(
            id=str(data["id"]),
            project=str(data["project"]),
            type=str(data["type"]),
            inputs=[str(x) for x in data["inputs"]],
            created_at=str(data["created_at"]),
            status=str(data["status"]),
            attempts=attempts,
        )

    def to_jsonl(self) -> str:
        """Serialise to a single JSONL line (no trailing newline)."""
        payload = {
            "id": self.id,
            "project": self.project,
            "type": self.type,
            "inputs": list(self.inputs),
            "created_at": self.created_at,
            "status": self.status,
            "attempts": self.attempts,
        }
        return json.dumps(payload, separators=(",", ":"), ensure_ascii=False)

    def with_status(self, status: str, *, bump_attempts: bool = False) -> "PendingRequest":
        if status not in VALID_STATUSES:
            raise QueueError(f"invalid status {status!r}")
        return PendingRequest(
            id=self.id,
            project=self.project,
            type=self.type,
            inputs=list(self.inputs),
            created_at=self.created_at,
            status=status,
            attempts=self.attempts + 1 if bump_attempts else self.attempts,
        )


def _iter_lines(text: str) -> Iterator[tuple[int, str]]:
    for idx, raw in enumerate(text.splitlines(keepends=False)):
        if raw.strip():
            yield idx, raw


def load_all(storage_root: Path) -> list[PendingRequest]:
    """Read every row in pending.jsonl. Missing file -> empty list."""
    path = pending_path(storage_root)
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8")
    out: list[PendingRequest] = []
    for _, line in _iter_lines(text):
        try:
            data = json.loads(line)
        except json.JSONDecodeError as exc:
            raise QueueError(f"malformed JSONL at {path}: {exc}") from exc
        out.append(PendingRequest.from_dict(data))
    return out


def load_all_text(storage_root: Path) -> str:
    """Read pending.jsonl as text. Missing file -> empty string.

    Used by id generation so we can scan for the highest sequence in use
    today without re-parsing the JSON.
    """
    path = pending_path(storage_root)
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8")


def find_first_pending(storage_root: Path) -> PendingRequest | None:
    """Return the first row with status ``pending`` (file order)."""
    for row in load_all(storage_root):
        if row.status == STATUS_PENDING:
            return row
    return None


def _atomic_write(path: Path, content: str) -> None:
    """Write ``content`` to ``path`` via a sibling temp file + os.replace."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(content, encoding="utf-8")
    tmp.replace(path)


def _format_jsonl(rows: Iterable[PendingRequest]) -> str:
    return "".join(row.to_jsonl() + "\n" for row in rows)


def update_status(
    storage_root: Path,
    request_id: str,
    new_status: str,
    *,
    bump_attempts: bool = False,
) -> PendingRequest:
    """Rewrite pending.jsonl with the row matching ``request_id`` set to ``new_status``.

    Returns the new in-memory row. Raises ``QueueError`` if the id is absent
    or the status transition is illegal.
    """
    if new_status not in VALID_STATUSES:
        raise QueueError(f"invalid status {new_status!r}")
    rows = load_all(storage_root)
    new_rows: list[PendingRequest] = []
    updated: PendingRequest | None = None
    for row in rows:
        if row.id == request_id:
            candidate = row.with_status(new_status, bump_attempts=bump_attempts)
            new_rows.append(candidate)
            updated = candidate
        else:
            new_rows.append(row)
    if updated is None:
        raise QueueError(f"request id {request_id!r} not found in pending.jsonl")
    _atomic_write(pending_path(storage_root), _format_jsonl(new_rows))
    return updated


def enqueue(
    storage_root: Path,
    project: str,
    request_type: str,
    inputs: list[str],
    *,
    now: datetime | None = None,
) -> PendingRequest:
    """Append a new pending row and return it.

    Used by the ``plan`` subcommand when the user calls the bridge directly
    (no firmware enqueue happened). The id is generated against the
    current state of pending.jsonl and ``outbox/results/``.
    """
    if not inputs:
        raise QueueError("inputs must contain at least one filename")
    if not project:
        raise QueueError("project must be non-empty")
    if request_type != "plan_project":
        raise QueueError(f"unsupported request type {request_type!r}")

    results_dir = storage_root / "outbox" / "results"
    result_names: list[str] = []
    if results_dir.is_dir():
        result_names = [p.name for p in results_dir.iterdir() if p.is_dir()]

    ts = now or datetime.now(timezone.utc)
    request_id = make_request_id(
        compact_today=ts.strftime("%Y%m%d"),
        pending_text=load_all_text(storage_root),
        result_names=result_names,
    )
    created_at = ts.replace(microsecond=0).isoformat().replace("+00:00", "Z")
    row = PendingRequest(
        id=request_id,
        project=project,
        type=request_type,
        inputs=list(inputs),
        created_at=created_at,
        status=STATUS_PENDING,
        attempts=0,
    )
    path = pending_path(storage_root)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as f:
        f.write(row.to_jsonl() + "\n")
    return row
