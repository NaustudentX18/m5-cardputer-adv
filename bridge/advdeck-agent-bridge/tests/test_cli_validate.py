"""Tests for ``advdeck-bridge validate``.

The validate subcommand is a thin wrapper around
``jsonschema.Draft202012Validator`` exposed for the E2E test in Z02. Contract:

* exit 0 if the JSON file validates against the schema
* exit 1 if it does not
* emit ``ok`` to stdout on success
* emit a human-readable error to stderr on failure
"""
from __future__ import annotations

import json
from pathlib import Path

import pytest
from click.testing import CliRunner

from advdeck_bridge.cli import main
from advdeck_bridge.runner import ARTIFACT_NAMES
from advdeck_bridge.validation import load_schema


def _write_valid_tasks(path: Path) -> None:
    payload = {
        "version": 1,
        "tasks": [
            {
                "id": "t-1",
                "title": "Only task",
                "status": "todo",
            }
        ],
    }
    path.write_text(json.dumps(payload), encoding="utf-8")


def _write_invalid_tasks(path: Path) -> None:
    payload = {
        "version": 1,
        "tasks": [
            {
                "id": "t-1",
                "title": "Task with bad status",
                "status": "in-progress",  # not in the enum
            }
        ],
    }
    path.write_text(json.dumps(payload), encoding="utf-8")


@pytest.fixture()
def schemas_dir() -> Path:
    from advdeck_bridge.validation import SCHEMAS_DIR
    return SCHEMAS_DIR


def test_validate_exits_zero_on_valid_instance(tmp_path: Path, schemas_dir: Path) -> None:
    fixture = tmp_path / "tasks.json"
    schema = schemas_dir / "tasks.schema.json"
    _write_valid_tasks(fixture)
    runner = CliRunner()
    result = runner.invoke(
        main,
        ["validate", str(fixture), str(schema)],
        catch_exceptions=False,
    )
    assert result.exit_code == 0
    assert "ok" in result.stdout


def test_validate_exits_one_on_invalid_instance(tmp_path: Path, schemas_dir: Path) -> None:
    fixture = tmp_path / "tasks.json"
    schema = schemas_dir / "tasks.schema.json"
    _write_invalid_tasks(fixture)
    runner = CliRunner()
    result = runner.invoke(
        main,
        ["validate", str(fixture), str(schema)],
        catch_exceptions=False,
    )
    assert result.exit_code == 1
    # The error message is on stderr.
    assert result.stderr.strip()


def test_validate_works_against_result_manifest_schema(tmp_path: Path, schemas_dir: Path) -> None:
    fixture = tmp_path / "result.json"
    schema = schemas_dir / "result-manifest.schema.json"
    fixture.write_text(
        json.dumps(
            {
                "request_id": "req-20260614-001",
                "status": "ok",
                "artifacts": ["brief.md"],
                "warnings": [],
            }
        ),
        encoding="utf-8",
    )
    runner = CliRunner()
    result = runner.invoke(main, ["validate", str(fixture), str(schema)])
    assert result.exit_code == 0


def test_validate_works_against_calendar_schema(tmp_path: Path, schemas_dir: Path) -> None:
    fixture = tmp_path / "cal.json"
    schema = schemas_dir / "calendar-suggestions.schema.json"
    fixture.write_text(
        json.dumps(
            {
                "version": 1,
                "suggestions": [
                    {
                        "title": "Sometime",
                        "starts_at": "2026-06-21T09:00:00Z",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    runner = CliRunner()
    result = runner.invoke(main, ["validate", str(fixture), str(schema)])
    assert result.exit_code == 0


def test_validate_rejects_calendar_bad_datetime(tmp_path: Path, schemas_dir: Path) -> None:
    fixture = tmp_path / "cal.json"
    schema = schemas_dir / "calendar-suggestions.schema.json"
    fixture.write_text(
        json.dumps(
            {
                "version": 1,
                "suggestions": [{"title": "Sometime", "starts_at": "tomorrow at 9am"}],
            }
        ),
        encoding="utf-8",
    )
    runner = CliRunner()
    result = runner.invoke(main, ["validate", str(fixture), str(schema)])
    assert result.exit_code == 1


def test_validate_handles_missing_file_gracefully(tmp_path: Path, schemas_dir: Path) -> None:
    """Click's @click.Path(exists=True) catches this; we get a non-zero exit."""
    fixture = tmp_path / "does-not-exist.json"
    schema = schemas_dir / "tasks.schema.json"
    runner = CliRunner()
    result = runner.invoke(main, ["validate", str(fixture), str(schema)])
    assert result.exit_code != 0


def test_validate_against_artefact_filenames_round_trip(
    tmp_path: Path, schemas_dir: Path,
) -> None:
    """A result manifest whose artifacts list exactly matches the six names passes."""
    fixture = tmp_path / "result.json"
    schema = schemas_dir / "result-manifest.schema.json"
    fixture.write_text(
        json.dumps(
            {
                "request_id": "req-20260614-001",
                "status": "ok",
                "artifacts": list(ARTIFACT_NAMES),
                "warnings": [],
            }
        ),
        encoding="utf-8",
    )
    runner = CliRunner()
    result = runner.invoke(main, ["validate", str(fixture), str(schema)])
    assert result.exit_code == 0
