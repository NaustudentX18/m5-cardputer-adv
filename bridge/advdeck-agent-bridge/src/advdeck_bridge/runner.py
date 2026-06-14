"""High-level bridge operations: render artefacts, write the result dir, build manifests.

This module is the *only* place that knows the on-disk layout of
``outbox/results/<id>/``. The CLI subcommands delegate to it.
"""
from __future__ import annotations

import json
import shutil
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from . import validation
from .paths import idea_path, result_dir
from .providers import Provider, ProviderArtifacts, get_default_provider
from .queue import (
    PendingRequest,
    QueueError,
    STATUS_IN_FLIGHT,
    STATUS_PENDING,
    load_all,
    update_status,
)

# Names of the six artefacts we always write. Order matters only for stable
# diffs in the E2E test logs; the manifest stores them in the same order.
ARTIFACT_NAMES: tuple[str, ...] = (
    "brief.md",
    "plan.md",
    "tasks.json",
    "tasks.md",
    "calendar-suggestions.json",
    "agent-prompt.md",
)


@dataclass
class RunResult:
    """What a successful ``run`` produced."""

    request: PendingRequest
    artifacts_dir: Path
    manifest: dict[str, Any]


class BridgeError(RuntimeError):
    """Raised for non-recoverable bridge errors (caller maps to a manifest)."""


def _read_idea(storage_root: Path, slug: str) -> str:
    path = idea_path(storage_root, slug)
    if not path.exists():
        raise BridgeError(
            f"project {slug!r} has no idea.md at {path}; cannot plan"
        )
    return path.read_text(encoding="utf-8")


def _write_artifacts(target_dir: Path, artefacts: ProviderArtifacts) -> list[str]:
    """Write the six artefacts into ``target_dir`` and return their basenames."""
    target_dir.mkdir(parents=True, exist_ok=True)
    bodies: dict[str, str] = {
        "brief.md": artefacts.brief_md,
        "plan.md": artefacts.plan_md,
        "tasks.json": artefacts.tasks_json,
        "tasks.md": artefacts.tasks_md,
        "calendar-suggestions.json": artefacts.calendar_suggestions_json,
        "agent-prompt.md": artefacts.agent_prompt_md,
    }
    for name in ARTIFACT_NAMES:
        (target_dir / name).write_text(bodies[name], encoding="utf-8")
    return list(ARTIFACT_NAMES)


def _build_manifest(
    request: PendingRequest,
    artifacts_dir: Path,
    artefacts: ProviderArtifacts,
) -> dict[str, Any]:
    manifest = {
        "request_id": request.id,
        "status": "ok",
        "artifacts": sorted(p.name for p in artifacts_dir.iterdir() if p.is_file()),
        "warnings": list(artefacts.warnings),
    }
    schema = validation.load_schema("result-manifest")
    validation.validate(manifest, schema)
    # The schema requires artifact names in stable order; rewrite sorted.
    manifest["artifacts"] = sorted(manifest["artifacts"])
    return manifest


def write_error_manifest(
    storage_root: Path,
    request_id: str,
    error_code: str,
    message: str,
    *,
    retryable: bool,
) -> Path:
    """Write an error manifest at ``outbox/results/<id>/result.json``.

    Used by ``run-once`` when the provider raises or the project is missing.
    """
    payload = {
        "request_id": request_id,
        "status": "error",
        "error_code": error_code,
        "message": message,
        "retryable": retryable,
    }
    schema = validation.load_schema("error-manifest")
    validation.validate(payload, schema)

    out_dir = result_dir(storage_root, request_id)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "result.json"
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    return out_path


def run_for_request(
    storage_root: Path,
    request: PendingRequest,
    *,
    provider: Provider | None = None,
) -> RunResult:
    """Run the provider for one already-pending request.

    Steps:
      1. Read idea.md.
      2. Render artefacts via the provider.
      3. Validate tasks.json and calendar-suggestions.json against schemas.
      4. Write artefacts to ``outbox/results/<id>/``.
      5. Write the result manifest.

    Raises:
        BridgeError: any failure that the caller should map to an error manifest.
    """
    provider = provider or get_default_provider()
    idea_text = _read_idea(storage_root, request.project)
    artefacts = provider.render(request, idea_text)

    # Sanity-check the JSON artefacts against their schemas before writing.
    # The provider is trusted, but a real LLM in Phase 3 will not be.
    try:
        validation.validate(
            json.loads(artefacts.tasks_json),
            validation.load_schema("tasks"),
        )
        validation.validate(
            json.loads(artefacts.calendar_suggestions_json),
            validation.load_schema("calendar-suggestions"),
        )
    except Exception as exc:  # noqa: BLE001
        raise BridgeError(f"provider produced invalid JSON artefacts: {exc}") from exc

    target = result_dir(storage_root, request.id)
    if target.exists():
        # Defensive: a previous run wrote a partial dir. Wipe and retry.
        shutil.rmtree(target)
    _write_artifacts(target, artefacts)
    manifest = _build_manifest(request, target, artefacts)

    manifest_path = target / "result.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")

    return RunResult(request=request, artifacts_dir=target, manifest=manifest)


def plan_project(
    storage_root: Path,
    project_slug: str,
    *,
    provider: Provider | None = None,
) -> RunResult:
    """``plan`` subcommand entry: enqueue, mark in_flight, run, return result.

    The caller is responsible for surfacing a friendly error if the slug is
    invalid. We always go through the pending.jsonl queue so the firmware
    can later import the result the same way it imports ``run-once`` results.
    """
    from .queue import enqueue

    request = enqueue(
        storage_root,
        project=project_slug,
        request_type="plan_project",
        inputs=["idea.md"],
    )
    # We immediately mark in_flight + run synchronously; the firmware's
    # outbox is single-writer, so this is safe.
    update_status(storage_root, request.id, STATUS_IN_FLIGHT)
    # Reload to pick up the bumped status before passing downstream.
    rows = [r for r in load_all(storage_root) if r.id == request.id]
    in_flight = rows[0] if rows else request
    return run_for_request(storage_root, in_flight, provider=provider)


def run_once(
    storage_root: Path,
    *,
    provider: Provider | None = None,
) -> tuple[PendingRequest, RunResult] | tuple[None, None]:
    """``run-once`` entry: pick first pending, mark in_flight, run.

    Returns ``(None, None)`` if there's no pending request.
    """
    rows = load_all(storage_root)
    target = next((r for r in rows if r.status == STATUS_PENDING), None)
    if target is None:
        return None, None
    update_status(storage_root, target.id, STATUS_IN_FLIGHT, bump_attempts=True)
    rows = load_all(storage_root)
    in_flight = next(r for r in rows if r.id == target.id)
    result = run_for_request(storage_root, in_flight, provider=provider)
    return in_flight, result


def now_iso() -> str:
    """Return the current UTC time as ISO 8601 with a ``Z`` suffix."""
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


__all__ = [
    "ARTIFACT_NAMES",
    "BridgeError",
    "RunResult",
    "QueueError",
    "plan_project",
    "run_for_request",
    "run_once",
    "write_error_manifest",
    "now_iso",
]
