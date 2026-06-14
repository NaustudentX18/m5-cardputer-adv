"""Host-side rebuild of an AdvDeck agent pack ``export/`` folder.

The C++ firmware's ``AgentPackExporter`` writes the canonical export
folder to the SD card after a planning request is accepted
(``<project>/export/agent-pack.md`` + ``agent-tasks.json`` + ``README.md`` +
``sources.json`` + ``export-info.json``).  This module is the host-side
equivalent: a user can run ``advdeck-bridge export --project <slug>``
on their laptop to rebuild the same export folder from the artefacts
on the SD card without having the device attached.

Two output formats are supported:

* ``agent-pack`` (default) — the same five files the firmware writes.
* ``github-issues`` — one ``issues/<id>.json`` per task, shaped like
  the GitHub REST API's issue-creation payload, plus an
  ``issues/INDEX.md`` describing how to feed them to ``gh issue create
  --input``.  Useful when a user wants to fan the agent pack out into
  a tracker without copy-paste.

The host-side exporter does not run the planner.  It packages what is
already on disk. For each source artefact we first look in
``<root>/projects/<slug>/`` (where the firmware imports a result) and
fall back to the most recent ``<root>/outbox/results/<id>/`` (where
``plan`` wrote the raw provider output). The calendar suggestions
follow the same rule but the result dir is preferred when both exist,
since the project folder only carries calendar data the firmware
already accepted into ``events.json``.
"""
from __future__ import annotations

import hashlib
import json
import re
import shutil
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from . import validation
from .paths import project_dir, results_dir
from .runner import ARTIFACT_NAMES

# Package root: <bridge>/src/advdeck_bridge/export.py -> <bridge>
PACKAGE_ROOT = Path(__file__).resolve().parents[2]
TEMPLATES_DIR = PACKAGE_ROOT / "templates"
EXPORT_README_TEMPLATE = TEMPLATES_DIR / "export-README.md"
AGENT_PACK_TEMPLATE = TEMPLATES_DIR / "agent-pack.md.j2"

# Names of the source artefacts we read. Order matches
# ``runner.ARTIFACT_NAMES`` for stable diffs.
SOURCE_ARTIFACT_NAMES: tuple[str, ...] = ARTIFACT_NAMES

SUPPORTED_FORMATS: tuple[str, ...] = ("agent-pack", "github-issues")


class ExportError(RuntimeError):
    """Raised when an export cannot be produced (missing artefact, bad slug, etc.)."""


@dataclass
class ExportResult:
    """What a successful export produced.

    ``format`` is one of ``SUPPORTED_FORMATS``.  ``written_files`` is the
    list of paths created on disk, relative to ``out_dir``.  ``warnings``
    is a list of human-readable notes (e.g. "calendar suggestions
    missing — calendar section omitted").
    """

    project_slug: str
    format: str
    out_dir: Path
    written_files: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Slug + planner metadata
# ---------------------------------------------------------------------------

_SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")


def _check_slug(slug: str) -> None:
    if not _SLUG_RE.match(slug):
        raise ExportError(
            f"invalid project slug {slug!r}; must match {_SLUG_RE.pattern}"
        )


def _now_iso() -> str:
    return (
        datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z")
    )


def _read_from_latest_result(root_path: Path, name: str) -> str | None:
    """Read a file from the most recent result dir, or None.

    "Most recent" is by mtime descending. Used as a fallback when the
    firmware has not yet imported the result into the project folder.
    """
    results = results_dir(root_path)
    if not results.is_dir():
        return None
    candidates = sorted(
        (p for p in results.iterdir() if p.is_dir()),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    for d in candidates:
        cand = d / name
        if cand.is_file():
            return cand.read_text(encoding="utf-8")
    return None


def _read_first(storage_root: Path, slug: str, name: str) -> str | None:
    """Read a file from the project folder, falling back to the most recent result dir.

    When the firmware has imported a result, the artefacts live under
    ``<root>/projects/<slug>/``. When the user runs ``export`` from a
    host *before* the firmware has imported (i.e. only
    ``outbox/results/<id>/`` exists), the export still has to work.
    """
    proj = project_dir(storage_root, slug) / name
    if proj.is_file():
        return proj.read_text(encoding="utf-8")
    return _read_from_latest_result(storage_root, name)


def _read_calendar_suggestions(storage_root: Path, slug: str) -> str | None:
    """Read calendar-suggestions.json, preferring the project folder, then the latest result dir.

    The project-folder copy wins because the firmware only copies
    calendar-suggestions.json on accept, so a stale result-dir copy
    should not overwrite a project that has already been reviewed.
    """
    proj = project_dir(storage_root, slug) / "calendar-suggestions.json"
    if proj.is_file():
        return proj.read_text(encoding="utf-8")
    return _read_from_latest_result(storage_root, "calendar-suggestions.json")


# ---------------------------------------------------------------------------
# Project-title rendering
# ---------------------------------------------------------------------------

def _slug_to_title(slug: str) -> str:
    return " ".join(part.capitalize() for part in slug.split("-")) or slug


def _read_title(storage_root: Path, slug: str) -> str:
    """Best-effort project title. Falls back to slug if the project has no idea.md."""
    idea = _read_first(storage_root, slug, "idea.md")
    if idea is None:
        return _slug_to_title(slug)
    for line in idea.splitlines():
        s = line.strip()
        if s.startswith("# "):
            return s[2:].strip()
    return _slug_to_title(slug)


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

def _render_agent_pack_md(
    slug: str,
    title: str,
    artefacts: dict[str, str | None],
) -> str:
    """Render the agent-pack.md body.

    Falls back to a Jinja2-style substitution of the bundled
    ``agent-pack.md.j2`` if the project's brief/plan/tasks/calendar are
    missing, so the file is always populated and never empty.
    """
    if not AGENT_PACK_TEMPLATE.is_file():
        return _fallback_agent_pack_md(slug, title, artefacts)

    tpl = AGENT_PACK_TEMPLATE.read_text(encoding="utf-8")
    if artefacts.get("brief_md") and artefacts.get("plan_md") and artefacts.get("tasks_md"):
        return (
            tpl
            .replace("{{ project_slug }}", slug)
            .replace("{{ project_title }}", title)
            .replace("{{ idea }}", artefacts.get("idea_md") or "")
            .replace("{{ brief }}", artefacts.get("brief_md") or "")
                       .replace("{{ plan }}", artefacts.get("plan_md") or "")
            .replace("{{ tasks }}", artefacts.get("tasks_md") or "")
            .replace("{{ calendar_suggestions }}", artefacts.get("calendar_md") or "")
        )
    return _fallback_agent_pack_md(slug, title, artefacts)


def _fallback_agent_pack_md(
    slug: str,
    title: str,
    artefacts: dict[str, str | None],
) -> str:
    return (
        f"# {title}\n\n"
        f"> Project slug: `{slug}`\n"
        f"> This pack is a self-contained handoff for an external coding agent.\n\n"
        f"This export was rebuilt on the host by `advdeck-bridge export`. "
        f"The full brief/plan/tasks were not all present; see `agent-tasks.json` "
        f"for the structured view.\n"
    )


def _render_export_readme(
    slug: str,
    title: str,
    exported_at: str,
    planner_provider: str,
    request_id: str | None,
) -> str:
    if not EXPORT_README_TEMPLATE.is_file():
        return f"# {title}\n\nExported at `{exported_at}`.\n"
    tpl = EXPORT_README_TEMPLATE.read_text(encoding="utf-8")
    return (
        tpl
        .replace("{{ project_slug }}", slug)
        .replace("{{ project_title }}", title)
        .replace("{{ exported_at }}", exported_at)
        .replace("{{ planner_provider }}", planner_provider)
        .replace("{{ request_id }}", request_id or "n/a")
    )


def _render_agent_tasks_json(
    artefacts: dict[str, str | None],
) -> dict[str, Any] | None:
    """Return the tasks.json payload, or None if no tasks are present."""
    raw = artefacts.get("tasks_json")
    if not raw:
        return None
    return json.loads(raw)


def _dependency_order(tasks: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Stable topological order of tasks by their ``dependencies`` list.

    Tasks with no deps come first (preserving the input order), then
    tasks whose deps are satisfied. Cycles are tolerated: the second
    pass emits whatever remains in input order. This matches what a
    coding agent wants to see — a "good enough" ordering, not a
    cycle-detection report.
    """
    by_id = {t["id"]: t for t in tasks}
    seen: set[str] = set()
    out: list[dict[str, Any]] = []

    def visit(t: dict[str, Any], path: set[str]) -> None:
        if t["id"] in seen or t["id"] in path:
            return
        for dep in t.get("dependencies", []):
            other = by_id.get(dep)
            if other is None:
                continue
            visit(other, path | {t["id"]})
        seen.add(t["id"])
        out.append(t)

    for t in tasks:
        visit(t, set())
    return out


def _task_to_github_issue(
    task: dict[str, Any],
    project_title: str,
    project_slug: str,
    brief_md: str | None,
) -> dict[str, Any]:
    """Shape a Task into a GitHub issue-create payload."""
    lines: list[str] = []
    lines.append("### Objective")
    lines.append("")
    lines.append(task.get("objective") or "_No objective provided._")
    lines.append("")
    if task.get("context"):
        lines.append("### Context")
        lines.append("")
        lines.append(task["context"])
        lines.append("")
    if task.get("files_or_modules"):
        lines.append("### Files / modules")
        lines.append("")
        for f in task["files_or_modules"]:
            lines.append(f"- `{f}`")
        lines.append("")
    if task.get("acceptance_criteria"):
        lines.append("### Acceptance criteria")
        lines.append("")
        for c in task["acceptance_criteria"]:
            lines.append(f"- [ ] {c}")
        lines.append("")
    if task.get("validation"):
        lines.append("### Validation")
        lines.append("")
        for v in task["validation"]:
            lines.append(f"- `{v}`")
        lines.append("")
    if task.get("dependencies"):
        lines.append("### Depends on")
        lines.append("")
        for d in task["dependencies"]:
            lines.append(f"- #{d}")
        lines.append("")
    if task.get("risk"):
        lines.append("### Risk")
        lines.append("")
        lines.append(task["risk"])
        lines.append("")
    if task.get("suggested_agent_role"):
        lines.append(f"### Suggested agent role: `{task['suggested_agent_role']}`")
        lines.append("")
    lines.append("---")
    lines.append("")
    lines.append(
        f"Generated by `advdeck-bridge export --format github-issues` "
        f"for project `{project_slug}` ({project_title})."
    )

    role = task.get("suggested_agent_role") or "agent"
    labels = ["advdeck", f"role:{role}"]
    if task.get("status"):
        labels.append(f"status:{task['status']}")
    if task.get("dependencies"):
        labels.append("has-deps")
    if task.get("risk") in {"medium", "high"}:
        labels.append(f"risk:{task['risk']}")

    return {
        "title": task["title"],
        "body": "\n".join(lines),
        "labels": labels,
    }


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def export_project(
    storage_root: Path,
    project_slug: str,
    out_dir: Path,
    *,
    format: str = "agent-pack",
    planner_provider: str = "host-export",
    planner_version: str = "0.5.0",
    request_id: str | None = None,
) -> ExportResult:
    """Rebuild an agent pack export folder from a project's on-disk artefacts.

    See module docstring for the on-disk contract. ``out_dir`` is created
    if missing and is overwritten.
    """
    if format not in SUPPORTED_FORMATS:
        raise ExportError(
            f"unknown export format {format!r}; expected one of {SUPPORTED_FORMATS}"
        )
    _check_slug(project_slug)

    proj = project_dir(storage_root, project_slug)
    if not proj.is_dir():
        raise ExportError(
            f"project {project_slug!r} not found at {proj}"
        )

    # Read the source artefacts, with result-dir fallback. Missing ones
    # become None and the renderer substitutes a placeholder; we only
    # collect warnings so the user knows what was missing.
    artefacts: dict[str, str | None] = {
        "idea_md": _read_first(storage_root, project_slug, "idea.md"),
        "brief_md": _read_first(storage_root, project_slug, "brief.md"),
        "plan_md": _read_first(storage_root, project_slug, "plan.md"),
        "tasks_json": _read_first(storage_root, project_slug, "tasks.json"),
        "tasks_md": _read_first(storage_root, project_slug, "tasks.md"),
        "calendar_suggestions_json": _read_calendar_suggestions(storage_root, project_slug),
        "agent_prompt_md": _read_first(storage_root, project_slug, "agent-prompt.md"),
    }
    warnings: list[str] = []
    if artefacts["brief_md"] is None:
        warnings.append("brief.md missing in project folder or any result dir")
    if artefacts["plan_md"] is None:
        warnings.append("plan.md missing in project folder or any result dir")
    if artefacts["tasks_json"] is None:
        warnings.append(
            "tasks.json missing in project folder or any result dir; "
            "agent-tasks.json will be empty"
        )
    if artefacts["calendar_suggestions_json"] is None:
        warnings.append(
            "no calendar-suggestions.json in any result dir; calendar section omitted"
        )

    title = _read_title(storage_root, project_slug)
    exported_at = _now_iso()

    out_dir.mkdir(parents=True, exist_ok=True)
    # Wipe any prior contents so re-export is deterministic.
    for child in out_dir.iterdir():
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink()

    written: list[str] = []

    if format == "agent-pack":
        _export_agent_pack(
            out_dir=out_dir,
            slug=project_slug,
            title=title,
            artefacts=artefacts,
            planner_provider=planner_provider,
            planner_version=planner_version,
            request_id=request_id,
            exported_at=exported_at,
            warnings=warnings,
            written=written,
        )
    else:  # github-issues
        _export_github_issues(
            out_dir=out_dir,
            slug=project_slug,
            title=title,
            artefacts=artefacts,
            written=written,
            warnings=warnings,
        )

    return ExportResult(
        project_slug=project_slug,
        format=format,
        out_dir=out_dir,
        written_files=written,
        warnings=warnings,
    )


# ---------------------------------------------------------------------------
# Format: agent-pack
# ---------------------------------------------------------------------------

def _export_agent_pack(
    *,
    out_dir: Path,
    slug: str,
    title: str,
    artefacts: dict[str, str | None],
    planner_provider: str,
    planner_version: str,
    request_id: str | None,
    exported_at: str,
    warnings: list[str],
    written: list[str],
) -> None:
    # 1. agent-pack.md
    pack_md = _render_agent_pack_md(slug, title, artefacts)
    (out_dir / "agent-pack.md").write_text(pack_md, encoding="utf-8")
    written.append("agent-pack.md")

    # 2. agent-tasks.json
    tasks_obj = _render_agent_tasks_json(artefacts) or {"version": 1, "tasks": []}
    (out_dir / "agent-tasks.json").write_text(
        json.dumps(tasks_obj, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    written.append("agent-tasks.json")

    # 3. README.md (from the bundled template)
    readme = _render_export_readme(slug, title, exported_at, planner_provider, request_id)
    (out_dir / "README.md").write_text(readme, encoding="utf-8")
    written.append("README.md")

    # 4. sources.json — file index with SHA-256 of every file we wrote.
    sources = {
        "version": 1,
        "project_slug": slug,
        "exported_at": exported_at,
        "files": [
            {"name": "agent-pack.md", "sha256": _sha256(out_dir / "agent-pack.md")},
            {"name": "agent-tasks.json", "sha256": _sha256(out_dir / "agent-tasks.json")},
            {"name": "README.md", "sha256": _sha256(out_dir / "README.md")},
        ],
    }
    (out_dir / "sources.json").write_text(
        json.dumps(sources, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    written.append("sources.json")

    # 5. export-info.json — metadata that validates against the
    # kAgentPackExportInfo schema. We compute artifact_hashes from the
    # export files we just wrote so a verifier can re-check them.
    artifact_hashes = {
        "agent-pack.md": _sha256(out_dir / "agent-pack.md"),
        "agent-tasks.json": _sha256(out_dir / "agent-tasks.json"),
        "README.md": _sha256(out_dir / "README.md"),
    }
    info: dict[str, Any] = {
        "version": 1,
        "exported_at": exported_at,
        "project_slug": slug,
        "planner_provider": planner_provider,
        "planner_version": planner_version,
        "artifact_hashes": artifact_hashes,
    }
    if request_id:
        info["request_id"] = request_id
    schema = validation.load_schema("agent-pack-export-info")
    validation.validate(info, schema)
    (out_dir / "export-info.json").write_text(
        json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    written.append("export-info.json")

    if warnings:
        (out_dir / "warnings.json").write_text(
            json.dumps({"version": 1, "warnings": warnings}, indent=2, ensure_ascii=False),
            encoding="utf-8",
        )
        written.append("warnings.json")


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


# ---------------------------------------------------------------------------
# Format: github-issues
# ---------------------------------------------------------------------------

def _export_github_issues(
    *,
    out_dir: Path,
    slug: str,
    title: str,
    artefacts: dict[str, str | None],
    written: list[str],
    warnings: list[str],
) -> None:
    issues_dir = out_dir / "issues"
    issues_dir.mkdir(parents=True, exist_ok=True)

    tasks_obj = _render_agent_tasks_json(artefacts)
    if tasks_obj is None:
        warnings.append("no tasks.json in project folder or any result dir; no issues emitted")
        _write_github_index(issues_dir, slug, title, [])
        written.append("issues/INDEX.md")
        return

    tasks = tasks_obj.get("tasks", [])
    ordered = _dependency_order(tasks)
    index_entries: list[dict[str, Any]] = []
    for task in ordered:
        issue = _task_to_github_issue(
            task,
            project_title=title,
            project_slug=slug,
            brief_md=artefacts.get("brief_md"),
        )
        out_path = issues_dir / f"{task['id']}.json"
        out_path.write_text(
            json.dumps(issue, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        written.append(f"issues/{out_path.name}")
        index_entries.append(
            {
                "id": task["id"],
                "title": task["title"],
                "file": f"issues/{out_path.name}",
                "suggested_agent_role": task.get("suggested_agent_role") or "",
                "depends_on": task.get("dependencies", []),
            }
        )

    _write_github_index(issues_dir, slug, title, index_entries)
    written.append("issues/INDEX.md")


def _write_github_index(
    issues_dir: Path,
    slug: str,
    title: str,
    entries: list[dict[str, Any]],
) -> None:
    lines: list[str] = [
        f"# GitHub issues — {title}",
        "",
        f"> Project slug: `{slug}`",
        f"> Generated by `advdeck-bridge export --format github-issues`.",
        "",
    ]
    if not entries:
        lines.append("_No tasks were available to fan out into issues._")
    else:
        lines.append("Each row is one issue payload. The order is dependency-first.")
        lines.append("")
        lines.append("| # | Title | Role | Depends on | File |")
        lines.append("|---|-------|------|------------|------|")
        for i, e in enumerate(entries, 1):
            deps = ", ".join(f"`{d}`" for d in e["depends_on"]) or "—"
            lines.append(
                f"| {i} | {e['title']} | `{e['suggested_agent_role']}` | {deps} | `{e['file']}` |"
            )
        lines.append("")
        lines.append("## Apply")
        lines.append("")
        lines.append("```bash")
        lines.append("# from a checkout of the target repo")
        for e in entries:
            lines.append(f"gh issue create --input {e['file']}")
        lines.append("```")
    (issues_dir / "INDEX.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


__all__ = [
    "ExportError",
    "ExportResult",
    "SUPPORTED_FORMATS",
    "export_project",
]
