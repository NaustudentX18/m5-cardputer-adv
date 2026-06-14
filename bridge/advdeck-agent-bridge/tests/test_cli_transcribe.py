"""Tests for ``advdeck-bridge transcribe`` and ``transcribe-and-plan``.

The transcribe subcommand:
* writes ``transcript.md`` to the requested output dir
* validates the WAV file exists and is non-empty
* accepts a known mock fixture and produces canned output

The transcribe-and-plan subcommand:
* writes ``transcript.md`` to the project dir
* drives the local-file planner to produce the six-artefact result
* leaves the project's ``idea.md`` unchanged (backed up + restored)

We exercise the real Click runner against temp storage roots and a
real ``LocalFileProvider``-compatible artefacts directory.
"""
from __future__ import annotations

import json
import os
import shutil
from pathlib import Path

import pytest
from click.testing import CliRunner

from advdeck_bridge.cli import main
from advdeck_bridge.providers.dry_run import DryRunProvider, PACKAGE_ROOT
from advdeck_bridge.queue import PendingRequest


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def artifacts_dir(tmp_path: Path) -> Path:
    """A directory of valid planning artefacts suitable for the local-file provider.

    We render the dry-run provider's output once (the only way to get a
    schema-valid tasks.json without an LLM) and write the six
    artefacts. The contents are not the point of these tests; they
    just need to validate against the result-manifest schema.
    """
    out = tmp_path / "artifacts"
    out.mkdir()
    fake_req = PendingRequest(
        id="req-20260614-001",
        project="demo",
        type="plan_project",
        inputs=["idea.md"],
        created_at="2026-06-14T12:00:00Z",
        status="in_flight",
        attempts=0,
    )
    arts = DryRunProvider().render(fake_req, "ignored")
    bodies = {
        "brief.md": arts.brief_md,
        "plan.md": arts.plan_md,
        "tasks.json": arts.tasks_json,
        "tasks.md": arts.tasks_md,
        "calendar-suggestions.json": arts.calendar_suggestions_json,
        "agent-prompt.md": arts.agent_prompt_md,
    }
    for name, body in bodies.items():
        (out / name).write_text(body, encoding="utf-8")
    return out


def _write_wav(path: Path, *, basename: str | None = None, body: bytes = b"RIFF") -> Path:
    """Write a tiny non-empty WAV to ``path`` and return it.

    Creates the parent directory if needed (pytest's ``tmp_path``
    only creates the leaf, not nested subdirs).
    """
    if basename is not None:
        path = path.parent / basename
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(body)
    return path


def _run_transcribe(*args: str):
    """Invoke the transcribe subcommand via Click's CliRunner."""
    return CliRunner().invoke(main, ["transcribe", *args], catch_exceptions=False)


def _run_transcribe_and_plan(*args: str):
    """Invoke the transcribe-and-plan subcommand via Click's CliRunner."""
    return CliRunner().invoke(
        main, ["transcribe-and-plan", *args], catch_exceptions=False
    )


# ---------------------------------------------------------------------------
# transcribe
# ---------------------------------------------------------------------------


def test_transcribe_writes_transcript_md_to_output_dir(tmp_path: Path) -> None:
    """The CLI writes ``transcript.md`` to the requested --out dir."""
    wav = _write_wav(tmp_path / "voice" / "pocket-agent-recording-1.wav")
    out = tmp_path / "out"
    result = _run_transcribe(
        "--wav", str(wav), "--out", str(out), "--provider", "mock"
    )
    assert result.exit_code == 0, result.stdout + result.stderr
    transcript = out / "transcript.md"
    assert transcript.is_file()
    body = transcript.read_text(encoding="utf-8")
    # The canned fixture for pocket-agent is the seed.
    assert "Pocket Agent" in body


def test_transcribe_validates_wav_exists_and_is_non_empty(tmp_path: Path) -> None:
    """A missing --wav fails with a clear error; an empty --wav fails with another."""
    out = tmp_path / "out"
    # Missing file: Click's exists=True catches this and prints a usage error.
    missing = tmp_path / "nope.wav"
    r = _run_transcribe("--wav", str(missing), "--out", str(out))
    assert r.exit_code != 0
    assert "does not exist" in r.stderr or "Error" in r.stderr

    # Empty file: the CLI catches this with its own message + exit 2.
    empty = _write_wav(tmp_path / "empty.wav", body=b"")
    r = _run_transcribe("--wav", str(empty), "--out", str(out))
    assert r.exit_code == 2
    assert "empty" in r.stderr


def test_transcribe_rejects_missing_wav_with_clear_error(tmp_path: Path) -> None:
    """A non-existent --wav path produces a clear, actionable error message."""
    out = tmp_path / "out"
    r = _run_transcribe("--wav", "/no/such/file.wav", "--out", str(out))
    assert r.exit_code != 0
    # Click's usage error mentions the option by name.
    assert "--wav" in r.stderr or "no/such/file.wav" in r.stderr


def test_transcribe_validates_optional_manifest(tmp_path: Path) -> None:
    """If --manifest is given, the CLI validates it against the recording-manifest schema."""
    wav = _write_wav(tmp_path / "voice" / "demo-recording-1.wav")
    out = tmp_path / "out"

    # A manifest missing required keys should fail validation.
    bad_manifest = tmp_path / "bad.json"
    bad_manifest.write_text(json.dumps({"version": 1}), encoding="utf-8")
    r = _run_transcribe(
        "--wav", str(wav), "--out", str(out),
        "--provider", "mock", "--manifest", str(bad_manifest),
    )
    assert r.exit_code == 1
    assert "manifest" in r.stderr.lower() or "invalid" in r.stderr.lower()

    # A valid manifest should pass and the transcript should still be written.
    good_manifest = tmp_path / "good.json"
    good_manifest.write_text(json.dumps({
        "version": 1,
        "file": "recording-1.wav",
        "duration_ms": 1000,
        "sample_rate": 16000,
        "channels": 1,
        "bits_per_sample": 16,
        "captured_at": "2026-06-14T15:32:11Z",
        "sha256": "0" * 64,
    }), encoding="utf-8")
    r = _run_transcribe(
        "--wav", str(wav), "--out", str(out),
        "--provider", "mock", "--manifest", str(good_manifest),
    )
    assert r.exit_code == 0, r.stdout + r.stderr
    assert (out / "transcript.md").is_file()
    assert "manifest: ok" in r.stdout


# ---------------------------------------------------------------------------
# transcribe-and-plan
# ---------------------------------------------------------------------------


def test_transcribe_and_plan_writes_transcript_and_result_manifest(
    tmp_path: Path, artifacts_dir: Path
) -> None:
    """End-to-end: transcribe the WAV and produce the standard result manifest."""
    wav = _write_wav(tmp_path / "voice" / "demo-recording-1.wav")
    out = tmp_path / "out"
    storage = tmp_path / "advdeck"

    result = _run_transcribe_and_plan(
        "--wav", str(wav),
        "--project", "demo",
        "--artifacts", str(artifacts_dir),
        "--out", str(out),
        "--storage-root", str(storage),
    )
    assert result.exit_code == 0, result.stdout + result.stderr

    # transcript.md lives in --out
    assert (out / "transcript.md").is_file()

    # the planner wrote the standard six-artefact result manifest
    result_dirs = list((storage / "outbox" / "results").iterdir())
    assert len(result_dirs) == 1
    result_dir = result_dirs[0]
    manifest = json.loads((result_dir / "result.json").read_text(encoding="utf-8"))
    assert manifest["status"] == "ok"
    assert "brief.md" in manifest["artifacts"]
    assert "tasks.json" in manifest["artifacts"]
    assert "plan.md" in manifest["artifacts"]

    # idea.md is restored (i.e. NOT left as the transcript content)
    idea_path = storage / "projects" / "demo" / "idea.md"
    assert not idea_path.exists(), (
        "transcribe-and-plan must restore/remove idea.md; "
        f"found leftover: {idea_path.read_text(encoding='utf-8')[:60]!r}"
    )


def test_transcribe_and_plan_restores_existing_idea_md(
    tmp_path: Path, artifacts_dir: Path
) -> None:
    """If the project already has an idea.md, it is restored verbatim after the run."""
    wav = _write_wav(tmp_path / "voice" / "demo-recording-1.wav")
    out = tmp_path / "out"
    storage = tmp_path / "advdeck"
    project_dir = storage / "projects" / "demo"
    project_dir.mkdir(parents=True)
    idea = project_dir / "idea.md"
    original = "# Original idea\ndo not lose me\n"
    idea.write_text(original, encoding="utf-8")

    result = _run_transcribe_and_plan(
        "--wav", str(wav),
        "--project", "demo",
        "--artifacts", str(artifacts_dir),
        "--out", str(out),
        "--storage-root", str(storage),
    )
    assert result.exit_code == 0, result.stdout + result.stderr
    assert idea.read_text(encoding="utf-8") == original
