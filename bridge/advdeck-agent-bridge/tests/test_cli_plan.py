"""Tests for ``advdeck-bridge plan --project <slug>``.

The ``plan`` subcommand:
* enqueues a request in pending.jsonl
* marks it in_flight
* runs the dry-run provider
* writes six artefacts plus a result manifest under outbox/results/<id>/

We exercise the real Click runner against a temp storage root.
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
from advdeck_bridge.queue import STATUS_IN_FLIGHT, load_all
from advdeck_bridge.runner import ARTIFACT_NAMES
from advdeck_bridge.validation import load_schema, validate


@pytest.fixture()
def storage_root(tmp_path: Path) -> Path:
    root = tmp_path / "advdeck"
    (root / "projects" / "demo").mkdir(parents=True)
    idea_path(root, "demo").write_text(
        "# Demo idea\n\nMake a thing that does a thing.\n",
        encoding="utf-8",
    )
    return root

def _run_plan(storage_root: Path, slug: str = "demo"):
    runner = CliRunner()
    return runner.invoke(
        main,
        ["plan", "--project", slug, "--storage-root", str(storage_root)],
        catch_exceptions=False,
    )


def test_plan_creates_outbox_results_dir(storage_root: Path) -> None:
    result = _run_plan(storage_root)
    assert result.exit_code == 0, result.stdout + result.stderr
    assert (outbox_dir(storage_root) / "results").is_dir()


def test_plan_writes_six_artefacts_and_a_manifest(storage_root: Path) -> None:
    _run_plan(storage_root)
    # Find the freshly-created request id from pending.jsonl
    rows = load_all(storage_root)
    assert rows, "expected a pending row to be created"
    target = result_dir(storage_root, rows[0].id)
    files = sorted(p.name for p in target.iterdir() if p.is_file())
    # 6 artefacts + result.json
    assert "result.json" in files
    for name in ARTIFACT_NAMES:
        assert name in files, f"missing artefact {name} in {files}"


def test_plan_manifest_validates_against_result_manifest_schema(storage_root: Path) -> None:
    _run_plan(storage_root)
    target = result_dir(storage_root, load_all(storage_root)[0].id)
    manifest = json.loads((target / "result.json").read_text(encoding="utf-8"))
    validate(manifest, load_schema("result-manifest"))


def test_plan_artefacts_validate_against_their_schemas(storage_root: Path) -> None:
    _run_plan(storage_root)
    target = result_dir(storage_root, load_all(storage_root)[0].id)
    tasks = json.loads((target / "tasks.json").read_text(encoding="utf-8"))
    cal = json.loads((target / "calendar-suggestions.json").read_text(encoding="utf-8"))
    validate(tasks, load_schema("tasks"))
    validate(cal, load_schema("calendar-suggestions"))


def test_plan_marks_request_in_flight_in_pending_jsonl(storage_root: Path) -> None:
    _run_plan(storage_root)
    rows = load_all(storage_root)
    assert len(rows) == 1
    assert rows[0].status == STATUS_IN_FLIGHT
    assert rows[0].project == "demo"
    assert rows[0].id.startswith("req-")
    # The request id matches the result directory.
    assert result_dir(storage_root, rows[0].id).is_dir()


def test_plan_appends_to_pending_jsonl(tmp_path: Path) -> None:
    """A second call writes a second row, with a fresh id."""
    root = tmp_path / "advdeck2"
    (root / "projects" / "alpha").mkdir(parents=True)
    idea_path(root, "alpha").write_text("# alpha\n", encoding="utf-8")

    r1 = _run_plan(root, "alpha")
    r2 = _run_plan(root, "alpha")
    assert r1.exit_code == 0
    assert r2.exit_code == 0

    rows = load_all(root)
    assert len(rows) == 2
    assert rows[0].id != rows[1].id
    # Same calendar day -> sequential numbers.
    assert rows[0].id.split("-")[1] == rows[1].id.split("-")[1]
    n0 = int(rows[0].id.rsplit("-", 1)[-1])
    n1 = int(rows[1].id.rsplit("-", 1)[-1])
    assert n1 == n0 + 1


def test_plan_fails_when_idea_missing(tmp_path: Path) -> None:
    root = tmp_path / "advdeck3"
    (root / "projects" / "ghost").mkdir(parents=True)  # no idea.md
    result = _run_plan(root, "ghost")
    assert result.exit_code != 0
    # Nothing was written under results/.
    assert not (outbox_dir(root) / "results").exists()


def test_plan_rejects_invalid_slug(storage_root: Path) -> None:
    """Slugs with bad characters are rejected by the queue's enqueue step."""
    result = _run_plan(storage_root, "Bad-SLUG")
    assert result.exit_code != 0


def test_plan_help_lists_required_options() -> None:
    runner = CliRunner()
    result = runner.invoke(main, ["plan", "--help"])
    assert result.exit_code == 0
    assert "--project" in result.output
    assert "--storage-root" in result.output


def test_pending_jsonl_is_well_formed_jsonl(storage_root: Path) -> None:
    """Every line in pending.jsonl is a JSON object on its own line."""
    _run_plan(storage_root)
    text = pending_path(storage_root).read_text(encoding="utf-8")
    lines = [l for l in text.splitlines() if l.strip()]
    assert lines
    for line in lines:
        obj = json.loads(line)
        assert "id" in obj
        assert "status" in obj
