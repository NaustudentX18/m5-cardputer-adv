"""Click-based CLI for the AdvDeck dry-run bridge.

Five subcommands (PHASE-2-INTERFACES.md §8):

* ``plan``         read idea.md, run the dry-run provider, write result dir.
* ``list``         pretty-print the pending.jsonl queue.
* ``show <id>``    pretty-print the result manifest + artefact list.
* ``run-once``     pick the first pending request and process it.
* ``validate``     exit 0 if a JSON file validates against a schema, else 1.

The CLI is single-writer. Do not run multiple bridge processes against the
same ``--storage-root`` at the same time (PHASE-2-INTERFACES.md §4.2).
"""
from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path

import click

from . import validation
from .providers import (
    ProviderError,
    ProviderUnavailable,
    get_provider,
    get_transcription_provider,
)
from .paths import (
    DEFAULT_STORAGE_ROOT,
    pending_path,
    project_dir,
    result_dir,
)
from .queue import (
    PendingRequest,
    STATUS_DONE,
    STATUS_ERROR,
    STATUS_IN_FLIGHT,
    STATUS_PENDING,
    find_first_pending,
    load_all,
)
from .runner import (
    ARTIFACT_NAMES,
    BridgeError,
    plan_project,
    run_once,
    write_error_manifest,
)


def _coerce_root(path: str | None) -> Path:
    """Return an absolute Path for the storage root, defaulting to /advdeck."""
    return Path(path or DEFAULT_STORAGE_ROOT).expanduser().resolve()


def _format_age(created_at: str) -> str:
    """Render ``2026-06-14T01:02:03Z`` as a friendly relative age."""
    try:
        ts = datetime.fromisoformat(created_at.replace("Z", "+00:00"))
    except ValueError:
        return created_at
    delta = datetime.now(timezone.utc) - ts
    seconds = int(delta.total_seconds())
    if seconds < 60:
        return f"{seconds}s"
    if seconds < 3600:
        return f"{seconds // 60}m"
    if seconds < 86400:
        return f"{seconds // 3600}h"
    return f"{seconds // 86400}d"


def _print_queue_row(row: PendingRequest) -> None:
    age = _format_age(row.created_at)
    click.echo(
        f"  {row.id}  {row.status:<10}  {row.project:<24}  "
        f"age={age:<5}  attempts={row.attempts}"
    )


# ---------------------------------------------------------------------------
# CLI group
# ---------------------------------------------------------------------------


@click.group(help="AdvDeck Agent dry-run bridge CLI (Phase 2).")
@click.version_option(package_name="advdeck-bridge")
def main() -> None:
    """Entry point for ``advdeck-bridge`` (and ``python -m advdeck_bridge``)."""


# ---------------------------------------------------------------------------
# plan
# ---------------------------------------------------------------------------


@main.command(help="Generate a planning result for a project from its idea.md.")
@click.option("--project", "project_slug", required=True, metavar="<slug>",
              help="Project slug (must match ^[a-z0-9][a-z0-9-]{0,63}$).")
@click.option("--storage-root", default=None, metavar="<path>",
              help="Storage root (default: /advdeck).")
def plan(project_slug: str, storage_root: str | None) -> None:
    root = _coerce_root(storage_root)
    try:
        result = plan_project(root, project_slug)
    except BridgeError as exc:
        click.echo(f"plan: error: {exc}", err=True)
        sys.exit(2)
    except Exception as exc:  # noqa: BLE001
        click.echo(f"plan: unexpected error: {exc}", err=True)
        sys.exit(2)

    click.echo(f"plan: wrote {len(result.manifest['artifacts'])} artefacts "
               f"to {result.artifacts_dir}")
    for name in result.manifest["artifacts"]:
        click.echo(f"  - {name}")
    if result.manifest.get("warnings"):
        for warn in result.manifest["warnings"]:
            click.echo(f"warning: {warn}")

# ---------------------------------------------------------------------------
# plan-local
# ---------------------------------------------------------------------------
#
# Phase 3 (C1.1) per PHASE-3-INTERFACES.md §6: read pre-rendered
# artefacts from a directory instead of running an LLM. Used by
# A11 (agent-pack export tests) and Z03 (end-to-end smoke). The
# flow is identical to ``plan``: enqueue, mark in_flight, render,
# write the result dir + manifest. The provider swap is the only
# difference.


@main.command(
    help="Render a planning result from pre-built artefacts on disk. "
         "Identical output shape to ``plan``; the provider is the local-file "
         "reader (PHASE-3-INTERFACES.md §6).",
)
@click.option("--project", "project_slug", required=True, metavar="<slug>",
              help="Project slug (must match ^[a-z0-9][a-z0-9-]{0,63}$).")
@click.option("--artifacts", "artifacts_dir", required=True, metavar="<dir>",
              type=click.Path(exists=True, file_okay=False, dir_okay=True,
                               path_type=Path),
              help="Directory containing pre-rendered brief.md, plan.md, "
                   "tasks.json, tasks.md, calendar-suggestions.json, "
                   "agent-prompt.md.")
@click.option("--storage-root", default=None, metavar="<path>",
              help="Storage root (default: /advdeck).")
def plan_local(project_slug: str, artifacts_dir: Path,
               storage_root: str | None) -> None:
    root = _coerce_root(storage_root)
    try:
        provider = get_provider("local-file", artifacts_dir=str(artifacts_dir))
    except ProviderUnavailable as exc:
        click.echo(f"plan-local: error: {exc}", err=True)
        sys.exit(2)
    try:
        result = plan_project(root, project_slug, provider=provider)
    except BridgeError as exc:
        click.echo(f"plan-local: error: {exc}", err=True)
        sys.exit(2)
    except Exception as exc:  # noqa: BLE001
        click.echo(f"plan-local: unexpected error: {exc}", err=True)
        sys.exit(2)

    click.echo(f"plan-local: wrote {len(result.manifest['artifacts'])} "
               f"artefacts to {result.artifacts_dir}")
    for name in result.manifest["artifacts"]:
        click.echo(f"  - {name}")
    if result.manifest.get("warnings"):
        for warn in result.manifest["warnings"]:
            click.echo(f"warning: {warn}")



# ---------------------------------------------------------------------------
# list
# ---------------------------------------------------------------------------


@main.command(help="Pretty-print the contents of outbox/pending.jsonl.")
@click.option("--storage-root", default=None, metavar="<path>",
              help="Storage root (default: /advdeck).")
def list(storage_root: str | None) -> None:
    root = _coerce_root(storage_root)
    rows = load_all(root)
    if not rows:
        click.echo(f"(no pending.jsonl rows under {pending_path(root)})")
        return
    pending_count = sum(1 for r in rows if r.status == STATUS_PENDING)
    in_flight_count = sum(1 for r in rows if r.status == STATUS_IN_FLIGHT)
    done_count = sum(1 for r in rows if r.status == STATUS_DONE)
    error_count = sum(1 for r in rows if r.status == STATUS_ERROR)
    click.echo(f"queue: {len(rows)} row(s) "
               f"(pending={pending_count} in_flight={in_flight_count} "
               f"done={done_count} error={error_count})")
    click.echo("  id                       status      project                   age    attempts")
    for row in rows:
        _print_queue_row(row)


# ---------------------------------------------------------------------------
# show
# ---------------------------------------------------------------------------


@main.command(help="Show the result manifest and artefact list for a request id.")
@click.argument("request_id", metavar="<request_id>")
@click.option("--storage-root", default=None, metavar="<path>",
              help="Storage root (default: /advdeck).")
def show(request_id: str, storage_root: str | None) -> None:
    root = _coerce_root(storage_root)
    target = result_dir(root, request_id)
    manifest_path = target / "result.json"
    if not manifest_path.exists():
        click.echo(f"show: no result at {manifest_path}", err=True)
        sys.exit(1)
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        click.echo(f"show: malformed result.json: {exc}", err=True)
        sys.exit(1)
    click.echo(f"request_id: {manifest.get('request_id', request_id)}")
    click.echo(f"status:     {manifest.get('status', '?')}")
    if manifest.get("status") == "ok":
        click.echo("artifacts:")
        for name in manifest.get("artifacts", []):
            click.echo(f"  - {name}")
        warnings = manifest.get("warnings") or []
        if warnings:
            click.echo("warnings:")
            for warn in warnings:
                click.echo(f"  - {warn}")
    else:
        click.echo(f"error_code: {manifest.get('error_code', '?')}")
        click.echo(f"message:    {manifest.get('message', '?')}")
        click.echo(f"retryable:  {manifest.get('retryable', False)}")
    click.echo(f"directory:  {target}")


# ---------------------------------------------------------------------------
# run-once
# ---------------------------------------------------------------------------


@main.command(help="Process the first pending request from outbox/pending.jsonl.")
@click.option("--storage-root", default=None, metavar="<path>",
              help="Storage root (default: /advdeck).")
@click.option("--dry-run", "dry_run", is_flag=True, default=False,
              help="Print what would happen without mutating state on disk.")
def run_once_cmd(storage_root: str | None, dry_run: bool) -> None:
    root = _coerce_root(storage_root)
    target = find_first_pending(root)
    if target is None:
        click.echo("run-once: no pending request found")
        return

    click.echo(f"run-once: picked {target.id} (project={target.project})")
    if dry_run:
        click.echo(f"  would mark {target.id} as in_flight")
        click.echo(f"  would render {len(ARTIFACT_NAMES)} artefacts to "
                   f"{result_dir(root, target.id)}")
        click.echo("  would write result.json")
        return

    try:
        request, result = run_once(root)
    except BridgeError as exc:
        # Bridge raises; persist an error manifest and exit non-zero.
        err_path = write_error_manifest(
            root,
            target.id,
            error_code="invalid_ai_output",
            message=str(exc),
            retryable=False,
        )
        click.echo(f"run-once: bridge error: {exc}", err=True)
        click.echo(f"run-once: error manifest written to {err_path}", err=True)
        sys.exit(1)

    if request is None or result is None:
        click.echo("run-once: no pending request found")
        return

    click.echo(f"run-once: wrote {len(result.manifest['artifacts'])} artefacts "
               f"to {result.artifacts_dir}")


# ---------------------------------------------------------------------------
# validate
# ---------------------------------------------------------------------------


@main.command(help="Validate a JSON fixture against a JSON Schema. "
                    "Exits 0 on valid, 1 on invalid.")
@click.argument("fixture", metavar="<fixture.json>", type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.argument("schema", metavar="<schema.json>", type=click.Path(exists=True, dir_okay=False, path_type=Path))
def validate_cmd(fixture: Path, schema: Path) -> None:
    try:
        instance = json.loads(fixture.read_text(encoding="utf-8"))
        schema_dict = validation.load_schema_from_path(schema)
        validation.validate(instance, schema_dict)
    except Exception as exc:  # noqa: BLE001
        click.echo(f"invalid: {exc}", err=True)
        sys.exit(1)
    click.echo("ok")


if __name__ == "__main__":
    main()

# ---------------------------------------------------------------------------
# transcribe
# ---------------------------------------------------------------------------
#
# Phase 4 (D1.3) per PHASE-4-INTERFACES.md §5.3: turn a recording's WAV
# into ``transcript.md`` using a named provider. Does not touch the
# project folder or the outbox; ``transcribe-and-plan`` chains the
# transcript into the planning flow.


@main.command(
    help="Transcribe a single WAV file to ``<out>/transcript.md`` using the "
         "named provider. Does not touch the project folder or the outbox "
         "(PHASE-4-INTERFACES.md §5.3).",
)
@click.option("--wav", "wav_path", required=True, metavar="<path>",
              type=click.Path(exists=True, dir_okay=False, path_type=Path),
              help="Path to the recording's WAV file.")
@click.option("--out", "out_dir", required=True, metavar="<dir>",
              type=click.Path(file_okay=False, dir_okay=True, path_type=Path),
              help="Directory to write ``transcript.md`` into. Created if missing.")
@click.option("--language", default="en", metavar="<bcp47>",
              help="BCP-47 language tag forwarded to the provider. Default: en.")
@click.option("--provider", "provider_name", default="mock",
              type=click.Choice(["mock", "local-whisper", "openai"], case_sensitive=False),
              help="Transcription provider to use. Default: mock.")
@click.option("--manifest", "manifest_path", default=None, metavar="<path>",
              type=click.Path(exists=True, dir_okay=False, path_type=Path),
              help="Optional recording manifest. If given, it is validated against "
                   "the recording-manifest schema and the validation is echoed "
                   "to stdout. The CLI still writes transcript.md regardless.")
@click.option("--storage-root", default=None, metavar="<path>",
              help="Storage root (default: /advdeck). Reserved for future use.")
def transcribe(
    wav_path: Path,
    out_dir: Path,
    language: str,
    provider_name: str,
    manifest_path: Path | None,
    storage_root: str | None,
) -> None:
    """``advdeck-bridge transcribe`` body."""
    # The Click ``exists=True`` flag covers existence; we also check the
    # size so a zero-byte file (a known race during recording-stop) fails
    # loudly instead of silently producing an empty transcript.
    wav = Path(wav_path)
    if wav.stat().st_size == 0:
        click.echo(f"transcribe: error: WAV file is empty: {wav}", err=True)
        sys.exit(2)

    # Optional manifest validation: a debugging aid for the operator.
    # The CLI does not fail if the manifest is absent.
    if manifest_path is not None:
        try:
            instance = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
            schema = validation.load_schema("recording-manifest")
            validation.validate(instance, schema)
            click.echo(f"manifest: ok ({manifest_path})")
        except Exception as exc:  # noqa: BLE001
            click.echo(f"transcribe: manifest validation failed: {exc}", err=True)
            sys.exit(1)

    try:
        provider = get_transcription_provider(provider_name)
    except ProviderUnavailable as exc:
        click.echo(f"transcribe: error: {exc}", err=True)
        sys.exit(2)

    try:
        transcript = provider.transcribe(wav, language=language)
    except ProviderError as exc:  # type: ignore[misc]
        # ProviderError is a RuntimeError; CLI maps to a non-zero exit
        # and prints the provider's message verbatim.
        click.echo(f"transcribe: provider error: {exc}", err=True)
        sys.exit(1)

    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    target = out / "transcript.md"
    target.write_text(transcript, encoding="utf-8")
    click.echo(f"transcribe: wrote {target} (provider={provider.name()})")


# ---------------------------------------------------------------------------
# transcribe-and-plan
# ---------------------------------------------------------------------------
#
# Phase 4 (D1.3) per PHASE-4-INTERFACES.md §5.3: voice-to-plan loop.
# Transcribe the WAV, write the transcript, then run the local-file
# planner against the transcript as the project's idea text. The result
# manifest is the standard six-artefact shape (brief/plan/tasks/...).


@main.command(
    help="Transcribe a WAV and run the planning flow against the "
         "transcript. Writes ``transcript.md`` to the project dir and the "
         "standard six-artefact result manifest to ``outbox/results/<id>/``. "
         "The planner is driven by the local-file provider "
         "(PHASE-4-INTERFACES.md §5.3).",
)
@click.option("--wav", "wav_path", required=True, metavar="<path>",
              type=click.Path(exists=True, dir_okay=False, path_type=Path),
              help="Path to the recording's WAV file.")
@click.option("--project", "project_slug", required=True, metavar="<slug>",
              help="Project slug (must match ^[a-z0-9][a-z0-9-]{0,63}$).")
@click.option("--artifacts", "artifacts_dir", required=True, metavar="<dir>",
              type=click.Path(exists=True, file_okay=False, dir_okay=True, path_type=Path),
              help="Directory containing pre-rendered brief.md, plan.md, "
                   "tasks.json, tasks.md, calendar-suggestions.json, "
                   "agent-prompt.md.")
@click.option("--out", "out_dir", required=True, metavar="<dir>",
              type=click.Path(file_okay=False, dir_okay=True, path_type=Path),
              help="Directory to write ``transcript.md`` into (typically the "
                   "project dir). Created if missing.")
@click.option("--storage-root", default=None, metavar="<path>",
              help="Storage root (default: /advdeck).")
def transcribe_and_plan(
    wav_path: Path,
    project_slug: str,
    artifacts_dir: Path,
    out_dir: Path,
    storage_root: str | None,
) -> None:
    """``advdeck-bridge transcribe-and-plan`` body."""
    root = _coerce_root(storage_root)
    wav = Path(wav_path)
    if wav.stat().st_size == 0:
        click.echo(f"transcribe-and-plan: error: WAV file is empty: {wav}", err=True)
        sys.exit(2)

    # Step 1: transcribe the WAV. The Phase 4 voice-to-plan loop always
    # uses the mock provider; the live providers will be swapped in by
    # the operator's bridge invocation. The factory seam is real, the
    # choice is just hard-coded for now.
    try:
        provider = get_transcription_provider("mock")
    except ProviderUnavailable as exc:
        click.echo(f"transcribe-and-plan: error: {exc}", err=True)
        sys.exit(2)
    try:
        transcript = provider.transcribe(wav)
    except ProviderError as exc:  # type: ignore[misc]
        click.echo(f"transcribe-and-plan: provider error: {exc}", err=True)
        sys.exit(1)

    # Step 2: write transcript.md to the requested --out dir (typically
    # the project dir). Per §2, this is the project-level
    # ``transcript.md`` the firmware will read in the review flow.
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    transcript_path = out / "transcript.md"
    transcript_path.write_text(transcript, encoding="utf-8")
    click.echo(f"transcribe-and-plan: wrote {transcript_path}")

    # Step 3: run the planner with the local-file provider. The planner
    # reads ``idea.md`` from disk; we temporarily write the transcript
    # to ``idea.md`` for the duration of this command so the planner
    # has something to read. We back up + restore the original so the
    # "idea.md is write-once" rule is honoured after the command exits.
    proj = project_dir(root, project_slug)
    idea = proj / "idea.md"
    backup: str | None = None
    if idea.is_file():
        backup = idea.read_text(encoding="utf-8")
    try:
        proj.mkdir(parents=True, exist_ok=True)
        idea.write_text(transcript, encoding="utf-8")
        try:
            plan_provider = get_provider("local-file", artifacts_dir=str(artifacts_dir))
        except ProviderUnavailable as exc:
            click.echo(f"transcribe-and-plan: error: {exc}", err=True)
            sys.exit(2)
        try:
            result = plan_project(root, project_slug, provider=plan_provider)
        except BridgeError as exc:
            click.echo(f"transcribe-and-plan: bridge error: {exc}", err=True)
            sys.exit(2)
        click.echo(
            f"transcribe-and-plan: wrote {len(result.manifest['artifacts'])} "
            f"artefacts to {result.artifacts_dir}"
        )
        for name in result.manifest["artifacts"]:
            click.echo(f"  - {name}")
    finally:
        # Restore (or remove) the original idea.md so the file system
        # state matches what it was before this command ran.
        if backup is not None:
            idea.write_text(backup, encoding="utf-8")
        elif idea.is_file():
            idea.unlink()
