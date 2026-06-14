"""End-to-end test: voice-to-plan pipeline round-trip.

Z04 (Phase 4): proves the full voice-to-plan loop works without a real
transcription provider, an LLM, an SD card, or the Cardputer in the
room.

Pipeline exercised (Z04 of PHASE-4-INTERFACES.md §5.3):

  1. Pre-render six Phase 2/3 artefacts in a temp dir.
  2. Write a known 1-second mono 16 kHz 16-bit PCM WAV file with the
     ``wave`` stdlib module.
  3. ``advdeck-bridge transcribe --wav <wav> --out <dir> --provider mock``
     writes ``transcript.md`` to ``<dir>`` with the canned
     ``MockTranscriptionProvider`` text for the WAV's basename.
  4. ``advdeck-bridge transcribe-and-plan --wav <wav> --project <slug>``
     combines the two: transcribes the WAV, writes ``transcript.md`` to
     the project folder, then runs the existing ``plan`` flow with the
     ``LocalFileProvider`` to produce the standard six-artefact result
     manifest. ``idea.md`` is backed up and restored so the
     "write-once" rule is honoured.

The test is self-contained: no network, no LLM, no real transcription.
"""
from __future__ import annotations

import json
import struct
import subprocess
import sys
import wave
from pathlib import Path

import jsonschema
import pytest

from advdeck_bridge.paths import project_dir
from advdeck_bridge.runner import ARTIFACT_NAMES
from advdeck_bridge.validation import load_schema


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _write_known_wav(path: Path) -> Path:
    """Write a 1-second mono 16 kHz 16-bit PCM WAV with deterministic samples.

    The sample values are a 440 Hz sine wave at full-scale amplitude,
    which is a real audio signal — the MockTranscriptionProvider does
    not read the bytes, but the test exercises the file as if it came
    from the firmware's WavWriter.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    sample_rate = 16000
    duration_s = 1
    n_samples = sample_rate * duration_s
    frequency_hz = 440
    amplitude = 16000  # ~half of int16 max — plenty of headroom, no clipping

    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)  # 16-bit
        w.setframerate(sample_rate)
        frames = bytearray()
        for i in range(n_samples):
            sample = int(amplitude * _sin_2pi(frequency_hz * i / sample_rate))
            frames += struct.pack("<h", sample)
        w.writeframes(bytes(frames))
    return path


def _sin_2pi(x: float) -> float:
    """Return sin(2*pi*x) via a tiny integer-arithmetic table-free formula.

    Kept inline (no ``math`` import needed for the body) to avoid
    pulling dependencies into a helper that's only used by the
    fixture.
    """
    # math.sin / math.tau is fine, but we only need a few calls; the
    # closure lives above the helper for clarity.
    import math
    return math.sin(math.tau * x)


def _run_bridge(*args: str) -> subprocess.CompletedProcess[str]:
    """Invoke the bridge CLI as a subprocess using the test venv's Python.

    ``sys.executable -m advdeck_bridge`` runs the freshly installed
    package in editable mode, so a new process always sees the latest
    source. ``check=False`` so the test can inspect non-zero exits.
    """
    return subprocess.run(
        [sys.executable, "-m", "advdeck_bridge", *args],
        capture_output=True,
        text=True,
        check=False,
    )


def _result_manifest_path(storage_root: Path) -> Path:
    """Return the (single) result.json under ``outbox/results/``."""
    results = storage_root / "outbox" / "results"
    assert results.is_dir(), f"expected results dir at {results}"
    subdirs = [p for p in results.iterdir() if p.is_dir()]
    assert len(subdirs) == 1, f"expected one result dir, got {subdirs}"
    return subdirs[0] / "result.json"


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture()
def artifacts_dir(tmp_path: Path) -> Path:
    """Six Phase 2/3 artefacts, each schema-valid where applicable.

    Mirrors the Z03 ``artifacts_dir`` fixture: brief.md, plan.md,
    tasks.json, tasks.md, calendar-suggestions.json, agent-prompt.md.
    Contents are deterministic and not the point of the test.
    """
    d = tmp_path / "artifacts"
    d.mkdir()

    d.joinpath("brief.md").write_text(
        "# Voice Brief\n\nBuild the thing the user just said.\n", encoding="utf-8"
    )
    d.joinpath("plan.md").write_text(
        "# Voice Plan\n\n1. transcribe the WAV\n"
        "2. write transcript.md\n3. plan from the transcript\n",
        encoding="utf-8",
    )
    d.joinpath("tasks.json").write_text(
        json.dumps(
            {
                "version": 1,
                "tasks": [
                    {
                        "id": "voice-task-1",
                        "title": "Voice task one",
                        "objective": "do the first voice-driven thing",
                        "status": "todo",
                        "files_or_modules": ["voice/"],
                        "acceptance_criteria": ["first thing is done"],
                        "validation": ["first test passes"],
                        "suggested_agent_role": "executor",
                        "created_at": "2026-06-14T09:00:00Z",
                        "updated_at": "2026-06-14T09:00:00Z",
                    },
                    {
                        "id": "voice-task-2",
                        "title": "Voice task two",
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
        "- [ ] voice-task-1: Voice task one\n"
        "- [ ] voice-task-2: Voice task two\n",
        encoding="utf-8",
    )
    d.joinpath("calendar-suggestions.json").write_text(
        json.dumps(
            {
                "version": 1,
                "suggestions": [
                    {
                        "title": "Start voice work",
                        "starts_at": "2026-06-15T09:00:00Z",
                        "ends_at": "2026-06-15T10:00:00Z",
                        "project": "voice",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    d.joinpath("agent-prompt.md").write_text(
        "# Voice agent prompt\n\nYou are an external agent. Read the brief.\n",
        encoding="utf-8",
    )
    return d


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_e2e_transcribe_subcommand_writes_transcript_md(tmp_path: Path) -> None:
    """``transcribe`` with ``--provider mock`` writes ``transcript.md``.

    Uses the canned ``pocket-agent-recording-1.wav`` basename so the
    mock provider returns a known, non-empty transcript (the mock
    reads the basename, not the WAV bytes).
    """
    wav = _write_known_wav(tmp_path / "voice" / "pocket-agent-recording-1.wav")
    out_dir = tmp_path / "out"

    proc = _run_bridge(
        "transcribe",
        "--wav", str(wav),
        "--out", str(out_dir),
        "--provider", "mock",
    )
    assert proc.returncode == 0, (
        f"transcribe failed: stdout={proc.stdout!r} stderr={proc.stderr!r}"
    )

    transcript = out_dir / "transcript.md"
    assert transcript.is_file(), f"missing transcript at {transcript}"
    body = transcript.read_text(encoding="utf-8")
    # The canned pocket-agent fixture is the canonical seed.
    assert "Pocket Agent" in body, f"unexpected transcript: {body!r}"


def test_e2e_transcribe_and_plan_subcommand_writes_full_result(
    tmp_path: Path, artifacts_dir: Path
) -> None:
    """``transcribe-and-plan`` writes the transcript + 6-artefact result.

    Pipeline under test:

      1. The mock provider turns the WAV into a transcript string.
      2. The transcript is written to ``<out>/transcript.md``
         (which the test points at the project folder).
      3. The local-file provider renders the six artefacts and the
         runner writes ``result.json`` to
         ``<storage>/outbox/results/<id>/``.
    """
    wav = _write_known_wav(tmp_path / "voice" / "pocket-agent-recording-1.wav")
    storage_root = tmp_path / "advdeck"
    project = project_dir(storage_root, "voice")
    project.mkdir(parents=True)

    proc = _run_bridge(
        "transcribe-and-plan",
        "--wav", str(wav),
        "--project", "voice",
        "--artifacts", str(artifacts_dir),
        "--out", str(project),
        "--storage-root", str(storage_root),
    )
    assert proc.returncode == 0, (
        f"transcribe-and-plan failed: stdout={proc.stdout!r} "
        f"stderr={proc.stderr!r}"
    )

    # transcript.md landed in the project folder (= --out).
    transcript = project / "transcript.md"
    assert transcript.is_file(), f"missing transcript at {transcript}"
    assert "Pocket Agent" in transcript.read_text(encoding="utf-8")

    # The planner wrote the standard six-artefact result manifest.
    result_manifest_path = _result_manifest_path(storage_root)
    assert result_manifest_path.is_file(), (
        f"missing result manifest at {result_manifest_path}"
    )
    files = sorted(
        p.name
        for p in result_manifest_path.parent.iterdir()
        if p.is_file()
    )
    assert "result.json" in files
    for name in ARTIFACT_NAMES:
        assert name in files, f"missing artefact {name} in {files}"

    # result.json validates against the schema and lists the six names.
    manifest = json.loads(result_manifest_path.read_text(encoding="utf-8"))
    schema = load_schema("result-manifest")
    jsonschema.validate(manifest, schema)
    assert manifest["status"] == "ok"
    assert sorted(manifest["artifacts"]) == sorted(ARTIFACT_NAMES)


def test_e2e_transcribe_and_plan_preserves_idea_md(
    tmp_path: Path, artifacts_dir: Path
) -> None:
    """``transcribe-and-plan`` must back up and restore any existing ``idea.md``.

    PHASE-4-INTERFACES.md §2: ``idea.md`` is write-once and is never
    modified by the bridge. ``transcribe-and-plan`` temporarily writes
    the transcript over ``idea.md`` to drive the planner, then
    restores the original bytes on the way out.
    """
    wav = _write_known_wav(tmp_path / "voice" / "pocket-agent-recording-1.wav")
    storage_root = tmp_path / "advdeck"
    project = project_dir(storage_root, "voice")
    project.mkdir(parents=True)

    original = (
        "# Original voice idea\n"
        "\n"
        "The user typed this before they ever pressed record.\n"
    )
    idea = project / "idea.md"
    idea.write_text(original, encoding="utf-8")

    proc = _run_bridge(
        "transcribe-and-plan",
        "--wav", str(wav),
        "--project", "voice",
        "--artifacts", str(artifacts_dir),
        "--out", str(project),
        "--storage-root", str(storage_root),
    )
    assert proc.returncode == 0, (
        f"transcribe-and-plan failed: stdout={proc.stdout!r} "
        f"stderr={proc.stderr!r}"
    )

    # idea.md is byte-for-byte identical to the original.
    assert idea.is_file(), f"idea.md disappeared at {idea}"
    assert idea.read_text(encoding="utf-8") == original
