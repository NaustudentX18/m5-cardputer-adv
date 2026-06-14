"""Tests for ``advdeck-bridge run-once``.

This subcommand is the bridge-side of the firmware handoff: the firmware
writes a ``status: pending`` row to pending.jsonl, the bridge picks the first
one, marks it ``in_flight``, runs the dry-run provider, and writes the result
dir. We simulate the firmware by writing the JSONL line directly.
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest
from click.testing import CliRunner

from advdeck_bridge.cli import main
from advdeck_bridge.paths import (
    idea_path,
    outbox_dir,
    pending_path,
    result_dir,
)
from advdeck_bridge.queue import (
    STATUS_IN_FLIGHT,
    STATUS_PENDING,
    load_all,
)
from advdeck_bridge.runner import ARTIFACT_NAMES
from advdeck_bridge.validation import load_schema, validate


@pytest.fixture()
def firmware_like_storage(tmp_path: Path) -> Path:
    """A storage root pre-populated as the firmware would leave it."""
    root = tmp_path / "advdeck"
    (root / "projects" / "demo").mkdir(parents=True)
    idea_path(root, "demo").write_text("# demo\n", encoding="utf-8")
    # Write one pending row (status: pending).
    pending_path(root).parent.mkdir(parents=True, exist_ok=True)
    pending_path(root).write_text(
        json.dumps(
            {
                "id": "req-20260614-001",
                "project": "demo",
                "type": "plan_project",
                "inputs": ["idea.md"],
                "created_at": "2026-06-14T12:00:00Z",
                "status": "pending",
                "attempts": 0,
            }
        )
        + "\n",
        encoding="utf-8",
    )
    return root

def _run(root: Path, *extra: str):
    return CliRunner().invoke(
        main,
        ["run-once", "--storage-root", str(root), *extra],
        catch_exceptions=False,
    )
def test_run_once_picks_first_pending_and_writes_result(firmware_like_storage: Path) -> None:
    result = _run(firmware_like_storage)
    assert result.exit_code == 0, result.stdout + result.stderr

    target = result_dir(firmware_like_storage, "req-20260614-001")
    files = sorted(p.name for p in target.iterdir() if p.is_file())
    assert "result.json" in files
    for name in ARTIFACT_NAMES:
        assert name in files, f"missing artefact {name} in {files}"


def test_run_once_marks_request_in_flight(firmware_like_storage: Path) -> None:
    _run(firmware_like_storage)
    rows = load_all(firmware_like_storage)
    assert len(rows) == 1
    assert rows[0].status == STATUS_IN_FLIGHT
    # attempts was bumped to record the bridge's work.
    assert rows[0].attempts == 1


def test_run_once_manifest_validates(firmware_like_storage: Path) -> None:
    _run(firmware_like_storage)
    manifest_path = result_dir(firmware_like_storage, "req-20260614-001") / "result.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    validate(manifest, load_schema("result-manifest"))
    assert manifest["status"] == "ok"
    assert sorted(manifest["artifacts"]) == sorted(ARTIFACT_NAMES)


def test_run_once_picks_first_in_order(tmp_path: Path) -> None:
    """If pending.jsonl has 2 pending rows, the first one is processed first."""
    root = tmp_path / "advdeck"
    (root / "projects" / "alpha").mkdir(parents=True)
    (root / "projects" / "beta").mkdir(parents=True)
    idea_path(root, "alpha").write_text("# alpha\n", encoding="utf-8")
    idea_path(root, "beta").write_text("# beta\n", encoding="utf-8")
    pending_path(root).parent.mkdir(parents=True, exist_ok=True)
    pending_path(root).write_text(
        "\n".join([
            json.dumps({
                "id": "req-20260614-001",
                "project": "alpha",
                "type": "plan_project",
                "inputs": ["idea.md"],
                "created_at": "2026-06-14T12:00:00Z",
                "status": "pending",
                "attempts": 0,
            }),
            json.dumps({
                "id": "req-20260614-002",
                "project": "beta",
                "type": "plan_project",
                "inputs": ["idea.md"],
                "created_at": "2026-06-14T12:01:00Z",
                "status": "pending",
                "attempts": 0,
            }),
        ])
        + "\n",
        encoding="utf-8",
    )

    result = _run(root)
    assert result.exit_code == 0, result.stdout + result.stderr
    assert (result_dir(root, "req-20260614-001") / "result.json").exists()
    assert not (result_dir(root, "req-20260614-002") / "result.json").exists()

    rows = load_all(root)
    statuses = {r.id: r.status for r in rows}
    assert statuses["req-20260614-001"] == STATUS_IN_FLIGHT
    assert statuses["req-20260614-002"] == STATUS_PENDING


def test_run_once_skips_non_pending_rows(tmp_path: Path) -> None:
    """A row with status=done is left alone; no result dir is written for it."""
    root = tmp_path / "advdeck"
    (root / "projects" / "alpha").mkdir(parents=True)
    idea_path(root, "alpha").write_text("# alpha\n", encoding="utf-8")
    pending_path(root).parent.mkdir(parents=True, exist_ok=True)
    pending_path(root).write_text(
        json.dumps(
            {
                "id": "req-20260614-009",
                "project": "alpha",
                "type": "plan_project",
                "inputs": ["idea.md"],
                "created_at": "2026-06-14T12:00:00Z",
                "status": "done",
                "attempts": 1,
            }
        )
        + "\n",
        encoding="utf-8",
    )

    result = _run(root)
    assert result.exit_code == 0
    # No result dir was written; outbox/results/ might not even exist.
    if (outbox_dir(root) / "results").exists():
        assert not (result_dir(root, "req-20260614-009") / "result.json").exists()


def test_run_once_no_pending_prints_message(tmp_path: Path) -> None:
    root = tmp_path / "advdeck"
    (root / "projects" / "alpha").mkdir(parents=True)
    idea_path(root, "alpha").write_text("# alpha\n", encoding="utf-8")
    # No pending.jsonl at all.
    result = _run(root)
    assert result.exit_code == 0
    assert "no pending" in result.stdout.lower()


def test_run_once_dry_run_does_not_mutate_state(firmware_like_storage: Path) -> None:
    result = _run(firmware_like_storage, "--dry-run")
    assert result.exit_code == 0

    # pending.jsonl is unchanged.
    text = pending_path(firmware_like_storage).read_text(encoding="utf-8")
    obj = json.loads(text.splitlines()[0])
    assert obj["status"] == STATUS_PENDING
    assert obj["attempts"] == 0

    # No result dir was created.
    assert not (outbox_dir(firmware_like_storage) / "results").exists() \
        or not list((outbox_dir(firmware_like_storage) / "results").iterdir())


def test_run_once_writes_error_manifest_when_idea_missing(tmp_path: Path) -> None:
    root = tmp_path / "advdeck"
    (root / "projects" / "ghost").mkdir(parents=True)  # no idea.md
    pending_path(root).parent.mkdir(parents=True, exist_ok=True)
    pending_path(root).write_text(
        json.dumps(
            {
                "id": "req-20260614-101",
                "project": "ghost",
                "type": "plan_project",
                "inputs": ["idea.md"],
                "created_at": "2026-06-14T12:00:00Z",
                "status": "pending",
                "attempts": 0,
            }
        )
        + "\n",
        encoding="utf-8",
    )
    result = _run(root)
    assert result.exit_code != 0
    err = json.loads(
        (result_dir(root, "req-20260614-101") / "result.json").read_text(encoding="utf-8")
    )
    assert err["status"] == "error"
    assert err["request_id"] == "req-20260614-101"
    assert err["error_code"] in {"invalid_ai_output", "storage_error", "bridge_timeout", "unknown"}
    assert err["message"]
    assert isinstance(err["retryable"], bool)
