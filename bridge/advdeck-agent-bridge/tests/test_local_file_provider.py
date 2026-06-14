"""Tests for ``LocalFileProvider`` (C1.1).

Covers the 5-test contract from the assignment:
  1. complete artifacts dir produces a ProviderArtifacts with the
     expected fields
  2. missing brief.md produces empty string + warning
  3. missing tasks.json produces empty tasks_json string
  4. render of a known dir matches a sha256 snapshot
  5. provider protocol surface is correct (renders the right 6 fields)
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

from advdeck_bridge.providers import (
    ProviderArtifacts,
    ProviderError,
    get_provider,
)
from advdeck_bridge.providers.local_file import (
    ARTIFACT_NAMES,
    LocalFileProvider,
    is_valid_artifacts_dir,
)
from advdeck_bridge.queue import PendingRequest


# Map artefact filename -> dataclass field name on ProviderArtifacts.
_FIELD_FOR: dict[str, str] = {
    "brief.md": "brief_md",
    "plan.md": "plan_md",
    "tasks.json": "tasks_json",
    "tasks.md": "tasks_md",
    "calendar-suggestions.json": "calendar_suggestions_json",
    "agent-prompt.md": "agent_prompt_md",
}


def _make_request(project: str = "demo") -> PendingRequest:
    return PendingRequest(
        id="req-20260614-001",
        project=project,
        type="plan_project",
        inputs=["idea.md"],
        created_at="2026-06-14T12:00:00Z",
        status="pending",
        attempts=0,
    )


def _write_complete_dir(path: Path) -> None:
    (path / "brief.md").write_text("# Brief\nbody\n", encoding="utf-8")
    (path / "plan.md").write_text("# Plan\n1. step\n", encoding="utf-8")
    (path / "tasks.json").write_text(
        json.dumps({"version": 1, "tasks": []}, indent=2),
        encoding="utf-8",
    )
    (path / "tasks.md").write_text("- [ ] t1\n", encoding="utf-8")
    (path / "calendar-suggestions.json").write_text(
        json.dumps({"version": 1, "suggestions": []}, indent=2),
        encoding="utf-8",
    )
    (path / "agent-prompt.md").write_text("# Agent\n", encoding="utf-8")


def test_complete_artifacts_dir_produces_all_six_fields(tmp_path: Path) -> None:
    _write_complete_dir(tmp_path)
    provider = LocalFileProvider(artifacts_dir=tmp_path)
    artefacts = provider.render(_make_request(), idea_text="ignored")
    assert isinstance(artefacts, ProviderArtifacts)
    assert artefacts.brief_md == "# Brief\nbody\n"
    assert artefacts.plan_md == "# Plan\n1. step\n"
    assert artefacts.tasks_md == "- [ ] t1\n"
    assert artefacts.agent_prompt_md == "# Agent\n"
    # JSON artefacts round-trip
    tasks = json.loads(artefacts.tasks_json)
    assert tasks == {"version": 1, "tasks": []}
    cal = json.loads(artefacts.calendar_suggestions_json)
    assert cal == {"version": 1, "suggestions": []}
    assert artefacts.warnings == []


def test_missing_brief_md_produces_empty_string_and_warning(tmp_path: Path) -> None:
    _write_complete_dir(tmp_path)
    (tmp_path / "brief.md").unlink()
    provider = LocalFileProvider(artifacts_dir=tmp_path)
    artefacts = provider.render(_make_request(), idea_text="ignored")
    assert artefacts.brief_md == ""
    assert any("brief.md" in w for w in artefacts.warnings)
    # The other five fields are still present.
    assert artefacts.plan_md == "# Plan\n1. step\n"
    assert artefacts.agent_prompt_md == "# Agent\n"


def test_missing_tasks_json_produces_empty_string(tmp_path: Path) -> None:
    _write_complete_dir(tmp_path)
    (tmp_path / "tasks.json").unlink()
    provider = LocalFileProvider(artifacts_dir=tmp_path)
    artefacts = provider.render(_make_request(), idea_text="ignored")
    assert artefacts.tasks_json == ""
    assert any("tasks.json" in w for w in artefacts.warnings)


def test_render_of_known_dir_matches_sha256_snapshot(tmp_path: Path) -> None:
    """Two consecutive renders produce the same SHA-256 (no nondeterminism)."""
    _write_complete_dir(tmp_path)
    provider = LocalFileProvider(artifacts_dir=tmp_path)
    a1 = provider.render(_make_request(), idea_text="ignored")
    a2 = provider.render(_make_request(), idea_text="ignored")

    def _digest(a: ProviderArtifacts) -> str:
        h = hashlib.sha256()
        for name in ARTIFACT_NAMES:
            h.update(name.encode("utf-8"))
            h.update(b"=")
            h.update(getattr(a, _FIELD_FOR[name]).encode("utf-8"))
            h.update(b"\n---\n")
        return h.hexdigest()

    d1 = _digest(a1)
    d2 = _digest(a2)
    assert len(d1) == 64
    assert d1 == d2


def test_provider_protocol_surface_renders_six_fields(tmp_path: Path) -> None:
    """ProviderArtifacts has exactly the six artefact fields + warnings."""
    _write_complete_dir(tmp_path)
    provider = LocalFileProvider(artifacts_dir=tmp_path)
    artefacts = provider.render(_make_request(), idea_text="ignored")
    artefact_field_names = {
        "brief_md",
        "plan_md",
        "tasks_json",
        "tasks_md",
        "calendar_suggestions_json",
        "agent_prompt_md",
        "warnings",
    }
    assert set(artefacts.__dataclass_fields__) == artefact_field_names
    for name in ARTIFACT_NAMES:
        assert isinstance(getattr(artefacts, _FIELD_FOR[name]), str)


# ----- factory + convenience helpers ---------------------------------


def test_get_provider_local_file_via_factory(tmp_path: Path) -> None:
    _write_complete_dir(tmp_path)
    p = get_provider("local-file", artifacts_dir=tmp_path)
    assert isinstance(p, LocalFileProvider)
    artefacts = p.render(_make_request(), idea_text="ignored")
    assert artefacts.brief_md == "# Brief\nbody\n"


def test_get_provider_local_file_missing_artifacts_dir_raises() -> None:
    with pytest.raises(ProviderError):
        get_provider("local-file")  # type: ignore[call-arg]


def test_local_file_provider_missing_dir_raises() -> None:
    with pytest.raises(FileNotFoundError):
        LocalFileProvider(artifacts_dir=Path("/nonexistent/_dir"))


def test_is_valid_artifacts_dir_true_with_one_artefact(tmp_path: Path) -> None:
    (tmp_path / "brief.md").write_text("x", encoding="utf-8")
    assert is_valid_artifacts_dir(tmp_path) is True


def test_is_valid_artifacts_dir_false_when_empty(tmp_path: Path) -> None:
    assert is_valid_artifacts_dir(tmp_path) is False
