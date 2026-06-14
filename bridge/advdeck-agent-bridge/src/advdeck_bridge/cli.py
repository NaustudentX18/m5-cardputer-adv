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
from .paths import (
    DEFAULT_STORAGE_ROOT,
    pending_path,
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
