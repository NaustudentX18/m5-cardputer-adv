"""Tests for ``advdeck_bridge.export`` and the ``export`` CLI subcommand.

The export module is the host-side rebuild of the C++
``AgentPackExporter``'s ``export/`` folder. It must:

* read the source artefacts from the project folder (or, if not
  imported yet, from the most recent result dir),
* write the same five files the firmware writes (plus an optional
  ``warnings.json``),
* produce a ``sources.json`` whose SHA-256 hashes match the actual
  bytes,
* validate ``export-info.json`` against the
  ``agent-pack-export-info`` schema,
* support a ``github-issues`` format that fans tasks out into one
  ``gh issue create --input``-shaped JSON per task, in dependency
  order.
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

import pytest
from click.testing import CliRunner

from advdeck_bridge import runner as bridge_runner
from advdeck_bridge.cli import main
from advdeck_bridge.export import (
    SUPPORTED_FORMATS,
    ExportError,
    _dependency_order,
    _slug_to_title,
    _task_to_github_issue,
    export_project,
)
from advdeck_bridge.queue import STATUS_IN_FLIGHT, enqueue, load_all, update_status
from advdeck_bridge.validation import load_schema, validate


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _write(path: Path, body: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(body, encoding="utf-8")


def _make_project_with_plan(tmp_path: Path, slug: str = "demo") -> tuple[Path, str]:
    """Build a storage root with a project that has a completed plan.

    The dry-run provider writes the six artefacts to
    ``outbox/results/<request_id>/`` (NOT the project folder) — the
    firmware's ``BridgeImport`` is what copies them across. Our
    exporter falls back to the latest result dir, so this helper
    exercises that path.
    """
    root = tmp_path / "advdeck"
    _write(root / "projects" / slug / "idea.md",
           "# Demo Project\n\nDemo idea body.\n")
    req = enqueue(root, project=slug, request_type="plan_project", inputs=["idea.md"])
    update_status(root, req.id, STATUS_IN_FLIGHT)
    rows = [r for r in load_all(root) if r.id == req.id]
    bridge_runner.run_for_request(root, rows[0])
    return root, req.id


# ---------------------------------------------------------------------------
# Module: dependency order + helpers
# ---------------------------------------------------------------------------


def test_dependency_order_handles_chain() -> None:
    a = {"id": "a", "title": "A", "dependencies": []}
    b = {"id": "b", "title": "B", "dependencies": ["a"]}
    c = {"id": "c", "title": "C", "dependencies": ["b"]}
    out = _dependency_order([c, a, b])
    assert [t["id"] for t in out] == ["a", "b", "c"]


def test_dependency_order_handles_diamond() -> None:
    a = {"id": "a", "title": "A", "dependencies": []}
    b = {"id": "b", "title": "B", "dependencies": ["a"]}
    c = {"id": "c", "title": "C", "dependencies": ["a"]}
    d = {"id": "d", "title": "D", "dependencies": ["b", "c"]}
    out = _dependency_order([d, c, b, a])
    assert [t["id"] for t in out] == ["a", "b", "c", "d"]


def test_dependency_order_no_cycles_does_not_recurse_forever() -> None:
    a = {"id": "a", "title": "A", "dependencies": ["b"]}
    b = {"id": "b", "title": "B", "dependencies": ["a"]}
    out = _dependency_order([a, b])
    assert {t["id"] for t in out} == {"a", "b"}


def test_slug_to_title_known_values() -> None:
    assert _slug_to_title("garden-watering") == "Garden Watering"
    assert _slug_to_title("") == ""
    assert _slug_to_title("single") == "Single"


def test_task_to_github_issue_shapes_payload() -> None:
    task = {
        "id": "wire-x",
        "title": "Wire X",
        "objective": "Connect X to GPIO 5",
        "files_or_modules": ["src/x.cpp"],
        "acceptance_criteria": ["X reads correctly"],
        "validation": ["make test"],
        "dependencies": ["setup-board"],
        "risk": "medium",
        "suggested_agent_role": "executor",
        "status": "todo",
    }
    issue = _task_to_github_issue(task, project_title="X", project_slug="x", brief_md=None)
    assert issue["title"] == "Wire X"
    assert "Connect X to GPIO 5" in issue["body"]
    assert "src/x.cpp" in issue["body"]
    assert "role:executor" in issue["labels"]
    assert "status:todo" in issue["labels"]
    assert "has-deps" in issue["labels"]
    assert "risk:medium" in issue["labels"]


# ---------------------------------------------------------------------------
# export_project: agent-pack format
# ---------------------------------------------------------------------------


def test_export_project_writes_all_files(tmp_path: Path) -> None:
    root, _req = _make_project_with_plan(tmp_path)
    out = tmp_path / "export"
    result = export_project(root, "demo", out)

    expected = {
        "agent-pack.md",
        "agent-tasks.json",
        "README.md",
        "sources.json",
        "export-info.json",
    }
    assert set(result.written_files) >= expected
    for name in expected:
        assert (out / name).is_file(), f"missing {name}"


def test_export_project_sources_hashes_match_bytes(tmp_path: Path) -> None:
    root, _req = _make_project_with_plan(tmp_path)
    out = tmp_path / "export"
    export_project(root, "demo", out)
    sources = json.loads((out / "sources.json").read_text(encoding="utf-8"))
    by_name = {f["name"]: f for f in sources["files"]}
    for name in ("agent-pack.md", "agent-tasks.json", "README.md"):
        h = hashlib.sha256((out / name).read_bytes()).hexdigest()
        assert by_name[name]["sha256"] == h


def test_export_project_export_info_validates(tmp_path: Path) -> None:
    root, req_id = _make_project_with_plan(tmp_path)
    out = tmp_path / "export"
    export_project(
        root, "demo", out,
        planner_provider="local-file", planner_version="0.5.0", request_id=req_id,
    )
    info = json.loads((out / "export-info.json").read_text(encoding="utf-8"))
    validate(info, load_schema("agent-pack-export-info"))
    assert info["project_slug"] == "demo"
    assert info["planner_provider"] == "local-file"
    assert info["planner_version"] == "0.5.0"
    assert info["request_id"] == req_id


def test_export_project_overwrites_cleanly(tmp_path: Path) -> None:
    root, _req = _make_project_with_plan(tmp_path)
    out = tmp_path / "export"
    out.mkdir(parents=True, exist_ok=True)
    (out / "stale.txt").write_text("old")
    (out / "stale-dir").mkdir()
    (out / "stale-dir" / "nested.txt").write_text("older")
    export_project(root, "demo", out)
    assert not (out / "stale.txt").exists()
    assert not (out / "stale-dir").exists()
    assert (out / "agent-pack.md").is_file()


def test_export_project_emits_warnings_for_missing_artefacts(tmp_path: Path) -> None:
    root = tmp_path / "advdeck"
    (root / "projects" / "noplan").mkdir(parents=True)
    (root / "projects" / "noplan" / "idea.md").write_text(
        "# Noplan\n\nbody\n", encoding="utf-8"
    )
    out = tmp_path / "export"
    result = export_project(root, "noplan", out)
    assert any("brief.md" in w for w in result.warnings)
    assert any("plan.md" in w for w in result.warnings)
    assert any("tasks.json" in w for w in result.warnings)
    assert (out / "agent-pack.md").is_file()
    assert (out / "warnings.json").is_file()


def test_export_project_rejects_bad_slug(tmp_path: Path) -> None:
    with pytest.raises(ExportError, match="invalid project slug"):
        export_project(tmp_path, "Bad Slug!", tmp_path / "out")


def test_export_project_rejects_unknown_format(tmp_path: Path) -> None:
    root = tmp_path / "advdeck"
    (root / "projects" / "x").mkdir(parents=True)
    (root / "projects" / "x" / "idea.md").write_text("body", encoding="utf-8")
    with pytest.raises(ExportError, match="unknown export format"):
        export_project(root, "x", tmp_path / "out", format="definitely-not-a-thing")


def test_export_project_rejects_missing_project(tmp_path: Path) -> None:
    with pytest.raises(ExportError, match="not found"):
        export_project(tmp_path, "nope", tmp_path / "out")


def test_supported_formats_matches_documented_set() -> None:
    assert SUPPORTED_FORMATS == ("agent-pack", "github-issues")


# ---------------------------------------------------------------------------
# export_project: github-issues format
# ---------------------------------------------------------------------------


def test_export_github_issues_writes_one_per_task(tmp_path: Path) -> None:
    root, _req = _make_project_with_plan(tmp_path)
    out = tmp_path / "issues"
    result = export_project(root, "demo", out, format="github-issues")
    issues_dir = out / "issues"
    assert any(name.startswith("issues/") and name.endswith(".json")
               for name in result.written_files)
    json_files = list(issues_dir.glob("*.json"))
    assert len(json_files) >= 1
    for jf in json_files:
        payload = json.loads(jf.read_text(encoding="utf-8"))
        assert {"title", "body", "labels"} <= payload.keys()


def test_export_github_issues_index_lists_all(tmp_path: Path) -> None:
    root, _req = _make_project_with_plan(tmp_path)
    out = tmp_path / "issues"
    export_project(root, "demo", out, format="github-issues")
    index = (out / "issues" / "INDEX.md").read_text(encoding="utf-8")
    assert "GitHub issues" in index
    assert "gh issue create --input" in index
    for jf in (out / "issues").glob("*.json"):
        assert jf.name in index


def test_export_github_issues_orders_by_dependency(tmp_path: Path) -> None:
    root = tmp_path / "advdeck"
    _write(root / "projects" / "ord" / "idea.md", "# Ord\n\nbody\n")
    tasks = {
        "version": 1,
        "tasks": [
            {"id": "z", "title": "Z", "status": "todo",
             "dependencies": ["m"], "suggested_agent_role": "executor"},
            {"id": "a", "title": "A", "status": "todo",
             "dependencies": [], "suggested_agent_role": "executor"},
            {"id": "m", "title": "M", "status": "todo",
             "dependencies": ["a"], "suggested_agent_role": "executor"},
        ],
    }
    _write(root / "projects" / "ord" / "tasks.json", json.dumps(tasks))

    out = tmp_path / "issues"
    export_project(root, "ord", out, format="github-issues")
    index = (out / "issues" / "INDEX.md").read_text(encoding="utf-8")
    lines = index.splitlines()
    fence_open = lines.index("```bash")
    fence_close = lines.index("```", fence_open + 1)
    apply_block = "\n".join(lines[fence_open + 1: fence_close])
    # The INDEX entries are written as "issues/<id>.json" relative to
    # the export root, not relative to the issues/ directory.
    assert re.search(r"gh issue create --input issues/a\.json", apply_block)
    assert re.search(r"gh issue create --input issues/m\.json", apply_block)
    assert re.search(r"gh issue create --input issues/z\.json", apply_block)
    assert (
        apply_block.index("issues/a.json")
        < apply_block.index("issues/m.json")
        < apply_block.index("issues/z.json")
    )


def test_export_github_issues_handles_no_tasks(tmp_path: Path) -> None:
    root = tmp_path / "advdeck"
    _write(root / "projects" / "empty" / "idea.md", "# Empty\n\nbody\n")
    out = tmp_path / "issues"
    result = export_project(root, "empty", out, format="github-issues")
    assert any("no tasks.json" in w for w in result.warnings)
    assert (out / "issues" / "INDEX.md").is_file()
    assert list((out / "issues").glob("*.json")) == []


# ---------------------------------------------------------------------------
# CLI smoke
# ---------------------------------------------------------------------------


def test_cli_export_agent_pack_writes_files(tmp_path: Path) -> None:
    root, _req = _make_project_with_plan(tmp_path)
    out = tmp_path / "out"
    runner = CliRunner()
    result = runner.invoke(
        main,
        [
            "export",
            "--project", "demo",
            "--out", str(out),
            "--storage-root", str(root),
        ],
    )
    assert result.exit_code == 0, result.output
    assert (out / "agent-pack.md").is_file()
    assert (out / "export-info.json").is_file()
    assert "agent-pack.md" in result.output


def test_cli_export_github_issues_writes_index(tmp_path: Path) -> None:
    root, _req = _make_project_with_plan(tmp_path)
    out = tmp_path / "out"
    runner = CliRunner()
    result = runner.invoke(
        main,
        [
            "export",
            "--project", "demo",
            "--out", str(out),
            "--format", "github-issues",
            "--storage-root", str(root),
        ],
    )
    assert result.exit_code == 0, result.output
    assert (out / "issues" / "INDEX.md").is_file()
    assert "github-issues" in result.output


def test_cli_export_missing_project_exits_2(tmp_path: Path) -> None:
    runner = CliRunner()
    result = runner.invoke(
        main,
        [
            "export",
            "--project", "does-not-exist",
            "--out", str(tmp_path / "out"),
            "--storage-root", str(tmp_path),
        ],
    )
    assert result.exit_code == 2
    assert "not found" in result.output


def test_cli_export_bad_slug_exits_2(tmp_path: Path) -> None:
    runner = CliRunner()
    result = runner.invoke(
        main,
        [
            "export",
            "--project", "BAD SLUG",
            "--out", str(tmp_path / "out"),
            "--storage-root", str(tmp_path),
        ],
    )
    assert result.exit_code == 2
    assert "invalid project slug" in result.output
