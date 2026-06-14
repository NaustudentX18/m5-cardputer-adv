"""End-to-end test: bridge CLI + firmware data path round-trip.

Z03 (Phase 3): proves the full pipeline works without an LLM, an
SD card, or the Cardputer in the room.

Pipeline exercised (Z03 of PHASE-3-INTERFACES.md):
  1. Pre-render six Phase 2/3 artefacts in a temp dir.
  2. ``advdeck-bridge plan-local`` reads them via LocalFileProvider,
     enqueues the request, marks it in_flight, and writes the result
     dir + manifest to ``outbox/results/<id>/``.
  3. The firmware-side importer logic is replicated in Python: copy
     artefacts into the project folder, validate tasks.json +
     calendar-suggestions.json against their schemas, append a
     ``bridge-import.log`` line. (The C++ BridgeImport has its own
     host tests; we replicate here so the Python test exercises the
     same data path the firmware does.)
  4. The firmware-side agent-pack exporter logic is replicated in
     Python: read the project folder, write the export/ folder
     (agent-pack.md, agent-tasks.json, README.md, sources.json,
     export-info.json) with SHA-256 hashes. (The C++
     AgentPackExporter has its own host tests.)

The test is self-contained: no network, no LLM, no real SdStorage.
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from advdeck_bridge.paths import idea_path, log_dir, project_dir, result_dir
from advdeck_bridge.queue import load_all
from advdeck_bridge.runner import ARTIFACT_NAMES
from advdeck_bridge.validation import load_schema, validate


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def bridge_binary() -> str:
    """Return the path to the ``advdeck-bridge`` console script.

    The script's shebang is ``#!/usr/bin/python3``, which has the
    bridge's ``src/`` on ``sys.path`` via the editable install.
    ``shutil.which`` finds it in the user's ``.local/bin`` even when
    the test is run from the project venv.
    """
    import shutil

    path = shutil.which("advdeck-bridge")
    if not path:
        pytest.skip("advdeck-bridge binary not on PATH")
    return path


@pytest.fixture()
def storage_root(tmp_path: Path) -> Path:
    """Per-test storage root with a populated project folder.

    The project has a hand-written ``idea.md`` and ``transcript.md``
    that must NOT be touched by the import path.
    """
    root = tmp_path / "advdeck"
    project = project_dir(root, "e2e")
    project.mkdir(parents=True)

    # idea.md is sacred per PHASE-3-INTERFACES.md §2. The test asserts
    # it is preserved unchanged across the import step.
    idea_body = (
        "# E2E test idea\n"
        "\n"
        "Build a small thing that does a small thing, end-to-end.\n"
    )
    idea_path(root, "e2e").write_text(idea_body, encoding="utf-8")

    # transcript.md is also untouched (pre-Phase-3 file).
    (project / "transcript.md").write_text(
        "# Transcript\n\nUser: hello\nBot: world\n",
        encoding="utf-8",
    )

    return root


@pytest.fixture()
def artifacts_dir(tmp_path: Path) -> Path:
    """Six Phase 2/3 artefacts, each schema-valid where applicable."""
    d = tmp_path / "artifacts"
    d.mkdir()

    d.joinpath("brief.md").write_text(
        "# E2E Brief\n\nBuild the thing.\n", encoding="utf-8"
    )
    d.joinpath("plan.md").write_text(
        "# E2E Plan\n\n1. step one\n2. step two\n", encoding="utf-8"
    )
    d.joinpath("tasks.json").write_text(
        json.dumps(
            {
                "version": 1,
                "tasks": [
                    {
                        "id": "e2e-task-1",
                        "title": "E2E task one",
                        "objective": "do the first thing",
                        "status": "todo",
                        "files_or_modules": ["e2e/"],
                        "acceptance_criteria": ["first thing is done"],
                        "validation": ["first test passes"],
                        "suggested_agent_role": "executor",
                        "created_at": "2026-06-14T09:00:00Z",
                        "updated_at": "2026-06-14T09:00:00Z",
                    },
                    {
                        "id": "e2e-task-2",
                        "title": "E2E task two",
                        "status": "doing",
                        "suggested_agent_role": "tester",
                        "created_at": "2026-06-14T09:01:00Z",
                        "updated_at": "2026-06-14T09:01:00Z",
                    },
                ],
            }
        ),
        encoding="utf-8",
    )
    d.joinpath("tasks.md").write_text(
        "- [ ] e2e-task-1: E2E task one\n"
        "- [ ] e2e-task-2: E2E task two\n",
        encoding="utf-8",
    )
    d.joinpath("calendar-suggestions.json").write_text(
        json.dumps(
            {
                "version": 1,
                "suggestions": [
                    {
                        "title": "Start e2e work",
                        "starts_at": "2026-06-15T09:00:00Z",
                        "ends_at": "2026-06-15T10:00:00Z",
                        "project": "e2e",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    d.joinpath("agent-prompt.md").write_text(
        "# E2E agent prompt\n\nYou are an external agent. Read the brief.\n",
        encoding="utf-8",
    )
    return d


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _run_bridge(
    binary: str,
    *args: str,
) -> subprocess.CompletedProcess[str]:
    """Invoke the bridge CLI as a subprocess.

    The script is installed in editable mode, so a fresh process
    picks up the latest source. ``check=False`` so the test can
    inspect non-zero exits (we mostly expect 0, but we want the
    failure message if it isn't).
    """
    return subprocess.run(
        [binary, *args],
        capture_output=True,
        text=True,
        check=False,
    )


def _sha256_hex(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# ---------------------------------------------------------------------------
# Firmware-side data path (Python re-implementation)
# ---------------------------------------------------------------------------
#
# The C++ side has BridgeImport (stage_only / import) and
# AgentPackExporter. Both have host tests in projects/advdeck-agent
# that exercise the full pipeline against HostStorage. We do NOT
# link the C++ shared library into Python (it would require a
# pybind11 build, which the project explicitly avoids in Phase 3);
# the e2e test instead re-implements the importer's and exporter's
# logic in Python so we can assert the data path end-to-end without
# an LLM or an SD card.
#
# The re-implementations are kept deliberately small and well-
# documented; if the C++ side ever drifts, the host tests will catch
# it and this test will need a corresponding update.


def _firmware_import(
    storage_root: Path,
    request_id: str,
    project_slug: str,
    *,
    now_iso: str,
) -> Path:
    """Replicate BridgeImport::import in Python.

    Reads the result manifest, copies the six artefacts from
    outbox/results/<id>/ into the project folder, validates
    tasks.json + calendar-suggestions.json against the schemas, and
    appends a log line. Returns the project folder.
    """
    src = result_dir(storage_root, request_id)
    manifest_path = src / "result.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("status") != "ok":
        raise RuntimeError(f"result manifest is not ok: {manifest}")

    project = project_dir(storage_root, project_slug)
    project.mkdir(parents=True, exist_ok=True)

    # Copy each artefact that the manifest lists, then validate the
    # two JSON artefacts against their schemas. Validation matches
    # what BridgeImport does in C++ (per the host test for it).
    for name in manifest["artifacts"]:
        shutil.copy2(src / name, project / name)

    tasks = json.loads((project / "tasks.json").read_text(encoding="utf-8"))
    cal = json.loads(
        (project / "calendar-suggestions.json").read_text(encoding="utf-8")
    )
    validate(tasks, load_schema("tasks"))
    validate(cal, load_schema("calendar-suggestions"))

    # The C++ BridgeImport appends one line per import to
    # <root>/logs/bridge-import.log. We do the same so the on-disk
    # contract matches.
    log_path = log_dir(storage_root) / "bridge-import.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as f:
        f.write(
            f"{now_iso}\timport\trequest_id={request_id}\t"
            f"project={project_slug}\tstatus=ok\n"
        )
    return project


def _firmware_export(
    storage_root: Path,
    project_slug: str,
    *,
    request_id: str,
    planner_provider: str,
    planner_version: str,
    now_iso: str,
) -> Path:
    """Replicate AgentPackExporter::export_project in Python.

    Reads the five source artefacts from the project folder, picks
    the most recent calendar-suggestions.json from outbox/results/,
    stitches them into agent-pack.md, builds agent-tasks.json, and
    writes the export folder with sources.json + export-info.json
    (validated against the new Phase 3 schema).
    """
    project = project_dir(storage_root, project_slug)

    # 1. Read the five source artefacts from the project folder.
    source_names = (
        "brief.md",
        "plan.md",
        "tasks.json",
        "tasks.md",
        "agent-prompt.md",
    )
    sources: dict[str, str] = {}
    for name in source_names:
        path = project / name
        sources[name] = path.read_text(encoding="utf-8") if path.exists() else ""

    # 2. Pick the most recent calendar-suggestions.json from results.
    results_root = storage_root / "outbox" / "results"
    calendar_json = ""
    if results_root.is_dir():
        for d in sorted(results_root.iterdir(), key=lambda p: p.name):
            cal = d / "calendar-suggestions.json"
            if cal.is_file():
                calendar_json = cal.read_text(encoding="utf-8")
    has_calendar = bool(calendar_json)

    # 3. Build agent-pack.md by stitching the source artefacts.
    project_title = " ".join(w.capitalize() for w in project_slug.split("-"))
    pack_lines: list[str] = [
        f"# Agent pack: {project_title}",
        "",
        f"> Project slug: `{project_slug}`",
        f"> Planner provider: `{planner_provider}`",
        f"> Planner version: `{planner_version}`",
        "",
        "---",
        "",
    ]
    for name, body in sources.items():
        pack_lines.append(f"## {name}")
        pack_lines.append("")
        pack_lines.append(body)
        if not body.endswith("\n"):
            pack_lines.append("")
        pack_lines.append("")
    if has_calendar:
        pack_lines.append("## calendar-suggestions.json")
        pack_lines.append("")
        pack_lines.append("```json")
        pack_lines.append(calendar_json)
        pack_lines.append("```")
        pack_lines.append("")
    agent_pack_md = "\n".join(pack_lines)

    # 4. Build agent-tasks.json with the task list and optional calendar.
    agent_tasks: dict = {
        "version": 1,
        "project_slug": project_slug,
        "tasks": [],
    }
    if sources.get("tasks.json"):
        try:
            parsed = json.loads(sources["tasks.json"])
            if isinstance(parsed, dict) and isinstance(parsed.get("tasks"), list):
                agent_tasks["tasks"] = parsed["tasks"]
        except json.JSONDecodeError:
            pass
    if has_calendar:
        try:
            parsed_cal = json.loads(calendar_json)
            if isinstance(parsed_cal, dict) and isinstance(
                parsed_cal.get("suggestions"), list
            ):
                agent_tasks["calendar_suggestions"] = parsed_cal["suggestions"]
        except json.JSONDecodeError:
            pass
    agent_tasks_json = json.dumps(agent_tasks, indent=2, ensure_ascii=False)

    # 5. SHA-256 of every artefact that goes into the export.
    hash_block: dict[str, str] = {
        name: _sha256_hex_of_str(body) for name, body in sources.items()
    }
    if has_calendar:
        hash_block["calendar-suggestions.json"] = _sha256_hex_of_str(calendar_json)

    # 6. Render the README from the on-disk template.
    template = Path(__file__).resolve().parents[1] / "templates" / "export-README.md"
    readme_body = template.read_text(encoding="utf-8")
    readme_body = readme_body.replace("{{ project_title }}", project_title)
    readme_body = readme_body.replace("{{ project_slug }}", project_slug)
    readme_body = readme_body.replace("{{ exported_at }}", now_iso)
    readme_body = readme_body.replace("{{ planner_provider }}", planner_provider)
    readme_body = readme_body.replace("{{ request_id }}", request_id)

    # 7. Write the export folder.
    export_root = project / "export"
    export_root.mkdir(parents=True, exist_ok=True)
    files_to_write: list[tuple[str, str]] = [
        ("agent-pack.md", agent_pack_md),
        ("agent-tasks.json", agent_tasks_json),
        ("README.md", readme_body),
    ]
    for name, body in files_to_write:
        (export_root / name).write_text(body, encoding="utf-8")
    for name, body in sources.items():
        (export_root / name).write_text(body, encoding="utf-8")
    if has_calendar:
        (export_root / "calendar-suggestions.json").write_text(
            calendar_json, encoding="utf-8"
        )

    # 8. sources.json index (mandatory).
    sources_index: dict = {
        "version": 1,
        "project_slug": project_slug,
        "files": [
            {
                "path": name,
                "bytes": len(body.encode("utf-8")),
                "sha256": _sha256_hex_of_str(body),
            }
            for name, body in files_to_write
        ],
    }
    for name, body in sources.items():
        sources_index["files"].append(
            {
                "path": name,
                "bytes": len(body.encode("utf-8")),
                "sha256": _sha256_hex_of_str(body),
            }
        )
    if has_calendar:
        sources_index["files"].append(
            {
                "path": "calendar-suggestions.json",
                "bytes": len(calendar_json.encode("utf-8")),
                "sha256": _sha256_hex_of_str(calendar_json),
            }
        )
    (export_root / "sources.json").write_text(
        json.dumps(sources_index, indent=2, ensure_ascii=False), encoding="utf-8"
    )

    # 9. export-info.json (validated against the new Phase 3 schema).
    info: dict = {
        "version": 1,
        "exported_at": now_iso,
        "project_slug": project_slug,
        "planner_provider": planner_provider,
        "planner_version": planner_version,
        "request_id": request_id,
        "artifact_hashes": hash_block,
    }
    info_path = export_root / "export-info.json"
    info_path.write_text(
        json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    validate(info, load_schema("agent-pack-export-info"))
    return export_root


def _sha256_hex_of_str(s: str) -> str:
    return hashlib.sha256(s.encode("utf-8")).hexdigest()


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_e2e_bridge_to_firmware_round_trip(
    bridge_binary: str,
    storage_root: Path,
    artifacts_dir: Path,
) -> None:
    """The full pipeline: bridge CLI -> importer -> exporter, with assertions.

    Steps (PHASE-3-INTERFACES.md §6 + §8):
      1. Run ``advdeck-bridge plan-local`` to write result + manifest.
      2. Assert the result dir has the six artefacts + result.json.
      3. Assert result.json validates against the result-manifest schema.
      4. Replicate the firmware importer: copy artefacts to the
         project folder, validate tasks.json + calendar against
         schemas, append a log line. Assert idea.md is preserved.
      5. Replicate the firmware agent-pack exporter: write export/
         with agent-pack.md, agent-tasks.json, README.md,
         sources.json, export-info.json. Assert SHA-256s match.
    """
    # -- 1. Run the bridge CLI via subprocess. ---------------------------
    proc = _run_bridge(
        bridge_binary,
        "plan-local",
        "--project",
        "e2e",
        "--artifacts",
        str(artifacts_dir),
        "--storage-root",
        str(storage_root),
    )
    assert proc.returncode == 0, (
        f"plan-local failed: stdout={proc.stdout!r} stderr={proc.stderr!r}"
    )

    # -- 2. The result dir has the six artefacts + result.json. ----------
    rows = load_all(storage_root)
    assert rows, "expected one row in pending.jsonl"
    request_id = rows[0].id
    target = result_dir(storage_root, request_id)
    files = sorted(p.name for p in target.iterdir() if p.is_file())
    assert "result.json" in files
    for name in ARTIFACT_NAMES:
        assert name in files, f"missing artefact {name} in {files}"

    # -- 3. result.json validates against the schema. -------------------
    manifest = json.loads((target / "result.json").read_text(encoding="utf-8"))
    validate(manifest, load_schema("result-manifest"))
    assert manifest["status"] == "ok"
    assert sorted(manifest["artifacts"]) == sorted(ARTIFACT_NAMES)

    # -- 4. Replicate the firmware importer. -----------------------------
    idea_before = idea_path(storage_root, "e2e").read_text(encoding="utf-8")
    transcript_before = (project_dir(storage_root, "e2e") / "transcript.md").read_text(
        encoding="utf-8"
    )
    project = _firmware_import(
        storage_root,
        request_id,
        "e2e",
        now_iso="2026-06-14T10:00:00Z",
    )
    assert project == project_dir(storage_root, "e2e")

    # All six artefacts landed in the project folder.
    project_files = sorted(p.name for p in project.iterdir() if p.is_file())
    for name in ARTIFACT_NAMES:
        assert name in project_files, f"missing imported artefact {name}"

    # idea.md + transcript.md are untouched.
    assert idea_path(storage_root, "e2e").read_text(encoding="utf-8") == idea_before
    assert (project / "transcript.md").read_text(encoding="utf-8") == transcript_before

    # The log line was appended.
    log_text = (log_dir(storage_root) / "bridge-import.log").read_text(
        encoding="utf-8"
    )
    assert request_id in log_text
    assert "project=e2e" in log_text
    assert "status=ok" in log_text

    # -- 5. Replicate the firmware agent-pack exporter. -----------------
    now = "2026-06-14T10:05:00Z"
    export_root = _firmware_export(
        storage_root,
        "e2e",
        request_id=request_id,
        planner_provider="local-file",
        planner_version="1.0.0",
        now_iso=now,
    )
    assert export_root == project / "export"

    # The five mandatory files are present.
    expected = {
        "agent-pack.md",
        "agent-tasks.json",
        "README.md",
        "sources.json",
        "export-info.json",
    }
    actual = {p.name for p in export_root.iterdir() if p.is_file()}
    assert expected.issubset(actual), (
        f"missing export files: expected={expected} actual={actual}"
    )

    # sources.json lists every file that was written, with a SHA-256
    # of the actual bytes on disk.
    sources_index = json.loads(
        (export_root / "sources.json").read_text(encoding="utf-8")
    )
    for entry in sources_index["files"]:
        path = export_root / entry["path"]
        assert path.is_file(), f"sources.json lists missing file {entry['path']}"
        assert _sha256_hex(path) == entry["sha256"], (
            f"sha256 mismatch for {entry['path']}"
        )
        assert entry["bytes"] == path.stat().st_size

    # export-info.json validates against the new Phase 3 schema and
    # carries a request_id.
    info = json.loads((export_root / "export-info.json").read_text(encoding="utf-8"))
    validate(info, load_schema("agent-pack-export-info"))
    assert info["project_slug"] == "e2e"
    assert info["planner_provider"] == "local-file"
    assert info["planner_version"] == "1.0.0"
    assert info["request_id"] == request_id
    # The hash block covers every source artefact the firmware
    # contract promises (PHASE-3-INTERFACES.md §3.1).
    for name in (
        "brief.md",
        "plan.md",
        "tasks.json",
        "tasks.md",
        "agent-prompt.md",
    ):
        assert name in info["artifact_hashes"]
        assert len(info["artifact_hashes"][name]) == 64
