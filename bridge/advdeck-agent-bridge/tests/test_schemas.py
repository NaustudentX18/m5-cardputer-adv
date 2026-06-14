"""Schema acceptance and rejection tests for the Phase 2 bridge schemas.

Loads each schema once from disk via pathlib, then exercises a minimal valid
example and every fixture in fixtures/invalid/ that targets that schema.
"""
import json
from pathlib import Path

import jsonschema
from jsonschema import Draft202012Validator
import pytest

BRIDGE_ROOT = Path(__file__).resolve().parent.parent
SCHEMAS_DIR = BRIDGE_ROOT / "schemas"
FIXTURES_DIR = BRIDGE_ROOT / "fixtures" / "invalid"

# The Phase 2 schemas declare `format: date-time` but Draft 2020-12 does not
# require validators to check it. We opt in here so the bad-datetime fixture
# actually fails. The bridge/firmware may use a different check; this
# FormatChecker is purely a host-side guard for the pytest gate.
_FORMAT_CHECKER = Draft202012Validator.FORMAT_CHECKER


def _validate(instance, schema):
    jsonschema.validate(
        instance=instance,
        schema=schema,
        format_checker=_FORMAT_CHECKER,
    )


def _load_schema(name: str) -> dict:
    with (SCHEMAS_DIR / f"{name}.schema.json").open() as f:
        return json.load(f)


def _load_fixture(name: str) -> dict:
    with (FIXTURES_DIR / name).open() as f:
        return json.load(f)


# Minimal valid examples -----------------------------------------------------

MINIMAL_PENDING_REQUEST = {
    "id": "req-20260614-001",
    "project": "garden-watering",
    "type": "plan_project",
    "inputs": ["idea.md"],
    "created_at": "2026-06-14T09:00:00+10:00",
    "status": "pending",
    "attempts": 0,
}

MINIMAL_RESULT_MANIFEST = {
    "request_id": "req-20260614-001",
    "status": "ok",
    "artifacts": ["brief.md", "plan.md", "tasks.json", "tasks.md"],
    "warnings": [],
}

MINIMAL_ERROR_MANIFEST = {
    "request_id": "req-20260614-002",
    "status": "error",
    "error_code": "storage_error",
    "message": "out of disk space",
    "retryable": True,
}

MINIMAL_TASKS = {
    "version": 1,
    "tasks": [
        {
            "id": "t-1",
            "title": "Do the thing",
            "status": "todo",
        }
    ],
}

MINIMAL_CALENDAR = {
    "version": 1,
    "suggestions": [
        {
            "title": "Check sensor calibration",
            "starts_at": "2026-06-21T09:00:00+10:00",
        }
    ],
}


# Acceptance tests -----------------------------------------------------------

def test_pending_request_validates() -> None:
    _validate(MINIMAL_PENDING_REQUEST, _load_schema("pending-request"))


def test_result_manifest_validates() -> None:
    _validate(MINIMAL_RESULT_MANIFEST, _load_schema("result-manifest"))


def test_error_manifest_validates() -> None:
    _validate(MINIMAL_ERROR_MANIFEST, _load_schema("error-manifest"))


def test_tasks_validates() -> None:
    _validate(MINIMAL_TASKS, _load_schema("tasks"))


def test_calendar_suggestions_validates() -> None:
    _validate(MINIMAL_CALENDAR, _load_schema("calendar-suggestions"))


# Rejection tests ------------------------------------------------------------

def test_tasks_missing_status_rejected() -> None:
    schema = _load_schema("tasks")
    with pytest.raises(jsonschema.ValidationError):
        _validate(_load_fixture("tasks-missing-status.json"), schema)


def test_tasks_unknown_status_rejected() -> None:
    schema = _load_schema("tasks")
    with pytest.raises(jsonschema.ValidationError):
        _validate(_load_fixture("tasks-unknown-status.json"), schema)


def test_calendar_bad_datetime_rejected() -> None:
    schema = _load_schema("calendar-suggestions")
    with pytest.raises(jsonschema.ValidationError):
        _validate(_load_fixture("calendar-bad-datetime.json"), schema)


def test_result_manifest_missing_artifacts_rejected() -> None:
    schema = _load_schema("result-manifest")
    with pytest.raises(jsonschema.ValidationError):
        _validate(_load_fixture("result-manifest-missing-artifacts.json"), schema)
