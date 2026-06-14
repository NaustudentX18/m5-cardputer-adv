"""Tests for the dry-run provider.

The provider is the heart of Phase 2: every other piece (CLI, runner, E2E)
just shuttles its output. So we test the provider's contract directly:

* Every artefact is produced and non-empty.
* The two JSON artefacts validate against the Phase 2 schemas.
* The brief / plan / tasks.md / agent-prompt.md mention the project slug so
  the firmware's per-project rendering has something to display.
* Successive calls for the same project + id are deterministic (no flakiness).
"""
from __future__ import annotations

import json
from datetime import datetime, timezone

import pytest

from advdeck_bridge.providers import get_default_provider
from advdeck_bridge.providers.dry_run import FROZEN_AGENT_PACK
from advdeck_bridge.queue import PendingRequest
from advdeck_bridge.validation import load_schema, validate


def _make_request(project: str = "demo", request_id: str = "req-20260614-001") -> PendingRequest:
    return PendingRequest(
        id=request_id,
        project=project,
        type="plan_project",
        inputs=["idea.md"],
        created_at=datetime(2026, 6, 14, 12, 0, 0, tzinfo=timezone.utc)
        .isoformat()
        .replace("+00:00", "Z"),
        status="pending",
        attempts=0,
    )


def test_dry_run_provider_produces_all_six_artefacts() -> None:
    artefacts = get_default_provider().render(_make_request(), idea_text="ignored")
    assert artefacts.brief_md.strip()
    assert artefacts.plan_md.strip()
    assert artefacts.tasks_json.strip()
    assert artefacts.tasks_md.strip()
    assert artefacts.calendar_suggestions_json.strip()
    assert artefacts.agent_prompt_md.strip()
    # Warnings should not be empty (the provider is candid about being dry-run).
    assert artefacts.warnings


def test_dry_run_provider_tasks_json_validates() -> None:
    artefacts = get_default_provider().render(_make_request(), idea_text="ignored")
    tasks = json.loads(artefacts.tasks_json)
    validate(tasks, load_schema("tasks"))


def test_dry_run_provider_calendar_json_validates() -> None:
    artefacts = get_default_provider().render(_make_request(), idea_text="ignored")
    cal = json.loads(artefacts.calendar_suggestions_json)
    validate(cal, load_schema("calendar-suggestions"))


def test_dry_run_provider_tasks_use_closed_status_enum() -> None:
    """Every task in tasks.json uses one of todo/doing/done."""
    artefacts = get_default_provider().render(_make_request(), idea_text="ignored")
    tasks = json.loads(artefacts.tasks_json)["tasks"]
    assert tasks, "expected at least one task"
    for task in tasks:
        assert task["status"] in ("todo", "doing", "done"), task


def test_dry_run_provider_tasks_use_closed_role_enum() -> None:
    artefacts = get_default_provider().render(_make_request(), idea_text="ignored")
    tasks = json.loads(artefacts.tasks_json)["tasks"]
    allowed = {
        "executor", "planner", "architect", "writer",
        "reviewer", "debugger", "tester", "oracle", "designer",
    }
    for task in tasks:
        assert task["suggested_agent_role"] in allowed, task


def test_dry_run_provider_includes_project_slug_in_human_artefacts() -> None:
    artefacts = get_default_provider().render(_make_request("garden-watering"),
                                              idea_text="ignored")
    assert "garden-watering" in artefacts.brief_md
    assert "garden-watering" in artefacts.plan_md
    assert "garden-watering" in artefacts.tasks_md
    tasks = json.loads(artefacts.tasks_json)
    for task in tasks["tasks"]:
        assert "garden-watering" in task["id"]


def test_dry_run_provider_agent_prompt_is_frozen_template() -> None:
    """The agent-prompt.md artefact is the frozen template, byte-for-byte."""
    artefacts = get_default_provider().render(_make_request(), idea_text="ignored")
    assert artefacts.agent_prompt_md == FROZEN_AGENT_PACK.read_text(encoding="utf-8")


def test_dry_run_provider_is_deterministic_per_request_id() -> None:
    """Same id + project -> same bytes (timestamps are id-anchored)."""
    req = _make_request(project="repro", request_id="req-20260614-042")
    a = get_default_provider().render(req, idea_text="ignored")
    b = get_default_provider().render(req, idea_text="ignored")
    assert a.tasks_json == b.tasks_json
    assert a.calendar_suggestions_json == b.calendar_suggestions_json
    assert a.brief_md == b.brief_md
    assert a.plan_md == b.plan_md
    assert a.tasks_md == b.tasks_md


def test_dry_run_provider_ignores_idea_text() -> None:
    """Phase 2 dry-run: the user's idea does not influence the output."""
    req = _make_request(project="repro", request_id="req-20260614-007")
    a = get_default_provider().render(req, idea_text="first idea")
    b = get_default_provider().render(req, idea_text="totally different idea")
    assert a.tasks_json == b.tasks_json
    assert a.calendar_suggestions_json == b.calendar_suggestions_json


def test_dry_run_provider_calendar_timestamps_are_iso8601_utc() -> None:
    artefacts = get_default_provider().render(_make_request(), idea_text="ignored")
    cal = json.loads(artefacts.calendar_suggestions_json)
    for entry in cal["suggestions"]:
        # Must include a timezone marker. jsonschema date-time requires it.
        assert entry["starts_at"].endswith("Z"), entry
        if "ends_at" in entry:
            assert entry["ends_at"].endswith("Z"), entry


def test_dry_run_provider_brief_mentions_dry_run_warning() -> None:
    """Surface a warnings entry that tells the consumer this is dry-run output."""
    artefacts = get_default_provider().render(_make_request(), idea_text="ignored")
    joined = " ".join(artefacts.warnings).lower()
    assert "dry-run" in joined or "canned" in joined
