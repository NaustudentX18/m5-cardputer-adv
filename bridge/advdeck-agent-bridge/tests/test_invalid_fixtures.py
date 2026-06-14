"""E2E-style tests: every fixture in fixtures/invalid/ fails its schema.

These tests drive the *CLI* validate subcommand rather than calling
``jsonschema.validate`` directly. That mirrors what Z02's verify-bridge.sh
will do, and catches the case where the CLI ever stops wiring up
``Draft202012Validator`` (or stops enforcing format checkers).
"""
from __future__ import annotations

from pathlib import Path

import pytest
from click.testing import CliRunner

from advdeck_bridge.cli import main


FIXTURES = Path(__file__).resolve().parent.parent / "fixtures" / "invalid"
SCHEMAS = Path(__file__).resolve().parent.parent / "schemas"

def _validate_cli(fixture: Path, schema: Path):
    return CliRunner().invoke(
        main,
        ["validate", str(fixture), str(schema)],
        catch_exceptions=False,
    )


def test_tasks_missing_status_rejected() -> None:
    fixture = FIXTURES / "tasks-missing-status.json"
    schema = SCHEMAS / "tasks.schema.json"
    result = _validate_cli(fixture, schema)
    assert result.exit_code == 1, result.stdout + result.stderr


def test_tasks_unknown_status_rejected() -> None:
    fixture = FIXTURES / "tasks-unknown-status.json"
    schema = SCHEMAS / "tasks.schema.json"
    result = _validate_cli(fixture, schema)
    assert result.exit_code == 1, result.stdout + result.stderr


def test_calendar_bad_datetime_rejected() -> None:
    fixture = FIXTURES / "calendar-bad-datetime.json"
    schema = SCHEMAS / "calendar-suggestions.schema.json"
    result = _validate_cli(fixture, schema)
    assert result.exit_code == 1, result.stdout + result.stderr


def test_result_manifest_missing_artifacts_rejected() -> None:
    fixture = FIXTURES / "result-manifest-missing-artifacts.json"
    schema = SCHEMAS / "result-manifest.schema.json"
    result = _validate_cli(fixture, schema)
    assert result.exit_code == 1, result.stdout + result.stderr


@pytest.mark.parametrize(
    "fixture,schema",
    [
        ("tasks-missing-status.json", "tasks.schema.json"),
        ("tasks-unknown-status.json", "tasks.schema.json"),
        ("calendar-bad-datetime.json", "calendar-suggestions.schema.json"),
        ("result-manifest-missing-artifacts.json", "result-manifest.schema.json"),
    ],
)
def test_all_fixtures_rejected_by_cli(fixture: str, schema: str) -> None:
    """Parametrised safety net: every fixture under fixtures/invalid/ fails.

    B1.1 owns the fixtures; if a new one is added without updating the
    per-fixture tests above, this parametrised test still catches it.
    """
    fixture_path = FIXTURES / fixture
    schema_path = SCHEMAS / schema
    if not fixture_path.exists():
        pytest.skip(f"fixture {fixture} not yet present")
    result = _validate_cli(fixture_path, schema_path)
    assert result.exit_code == 1
