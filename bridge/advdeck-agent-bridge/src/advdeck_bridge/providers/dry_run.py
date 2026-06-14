"""Deterministic dry-run provider.

This is the only provider Phase 2 ships. It does **not** call an LLM, does
**not** read the network, and does **not** even try to understand the user's
idea. The output is a small canned dataset that:

* validates against every Phase 2 schema (``tasks``, ``calendar-suggestions``,
  ``result-manifest``);
* looks plausible enough that downstream integration work (firmware import,
  UI render) can be exercised end-to-end;
* is reproducible: the same project slug + a stable wall clock produce the
  same bytes (modulo the per-request timestamps we add to tasks/calendar).

The human-readable brief / plan / tasks.md / agent-prompt.md are derived from
the frozen ``agent-pack.md`` template (which is the canonical canned copy the
firmware reads in Phase 2). When A08 (Phase 3) adds a real LLM provider, that
frozen file becomes the "no-LLM fixture" path; the .j2 sibling becomes the
"rendered-from-prompt" path.
"""
from __future__ import annotations

import json
from datetime import datetime, timedelta, timezone
from pathlib import Path

from ..paths import slug_to_title
from ..queue import PendingRequest
from . import ProviderArtifacts

# Package root: this file is at <bridge>/src/advdeck_bridge/providers/dry_run.py
# so parents[3] is <bridge>. parents[2] is <bridge>/src, which is not what we want.
PACKAGE_ROOT = Path(__file__).resolve().parents[3]
TEMPLATES_DIR = PACKAGE_ROOT / "templates"
FROZEN_AGENT_PACK = TEMPLATES_DIR / "agent-pack.md"

# Agent role enum is closed (PHASE-2-INTERFACES.md §3.4). We rotate through
# the obvious handful so the canned output exercises several values.
_ROLE_ROTATION = (
    "executor",
    "tester",
    "writer",
    "reviewer",
    "designer",
    "debugger",
)


def _stable_seed(request: PendingRequest) -> int:
    """Return a small int derived from the request id, used to rotate roles."""
    # request id: req-YYYYMMDD-NNN; sum the digits of NNN for a 0..27-ish int
    tail = request.id.rsplit("-", 1)[-1]
    return sum(int(c) for c in tail if c.isdigit())


class DryRunProvider:
    """Render the canned Phase 2 fixture set."""

    def render(self, request: PendingRequest, idea_text: str) -> ProviderArtifacts:
        del idea_text  # the dry-run provider ignores the idea on purpose

        title = slug_to_title(request.project) or request.project
        seed = _stable_seed(request)

        # Anchored "now" so the canned output is stable within one call.
        # We use UTC + the request's own id date if present so successive
        # runs of the same request id produce identical bytes.
        try:
            yyyymmdd = request.id.split("-")[1]
            dt = datetime(int(yyyymmdd[0:4]), int(yyyymmdd[4:6]), int(yyyymmdd[6:8]),
                          tzinfo=timezone.utc)
        except (IndexError, ValueError):
            dt = datetime.now(timezone.utc)
        created = dt.replace(microsecond=0)
        updated = created
        cal_base = created + timedelta(days=1)

        # --- tasks.json ---------------------------------------------------
        task_specs = [
            (
                f"wire-input-{request.project}",
                f"Wire inputs for {title}",
                f"Connect the hardware inputs (per the user's idea) and confirm a "
                f"host-side stub returns plausible readings.",
                _ROLE_ROTATION[(seed + 0) % len(_ROLE_ROTATION)],
            ),
            (
                f"ui-state-{request.project}",
                f"Render state on the home screen for {title}",
                "Surface the new state on the home screen and the project list; "
                "follow the existing card layout.",
                _ROLE_ROTATION[(seed + 1) % len(_ROLE_ROTATION)],
            ),
            (
                f"log-{request.project}",
                f"Append a one-line daily summary to logs/{request.project}-<date>.jsonl",
                "Write the summary at local midnight. Append-only; never rewrite history.",
                _ROLE_ROTATION[(seed + 2) % len(_ROLE_ROTATION)],
            ),
            (
                f"host-test-{request.project}",
                f"Add a host test that drives {request.project} with a stub",
                "Drive the new code path with a stub; assert the on-disk artefact "
                "matches the schema.",
                _ROLE_ROTATION[(seed + 3) % len(_ROLE_ROTATION)],
            ),
        ]
        tasks_payload = {
            "version": 1,
            "tasks": [
                {
                    "id": spec[0],
                    "title": spec[1],
                    "objective": spec[2],
                    "files_or_modules": [f"projects/advdeck-agent/src/{request.project}.cpp"],
                    "acceptance_criteria": [
                        f"`{spec[0]}` produces the expected artefact",
                        "Existing tests still pass",
                    ],
                    "validation": [
                        "make -C projects/advdeck-agent/test/host test",
                    ],
                    "dependencies": [],
                    "risk": "low",
                    "suggested_agent_role": spec[3],
                    "status": "todo",
                    "created_at": created.isoformat().replace("+00:00", "Z"),
                    "updated_at": updated.isoformat().replace("+00:00", "Z"),
                }
                for spec in task_specs
            ],
        }
        tasks_json_str = json.dumps(tasks_payload, indent=2, ensure_ascii=False)

        # --- calendar-suggestions.json ------------------------------------
        cal_payload = {
            "version": 1,
            "suggestions": [
                {
                    "title": f"Calibrate inputs for {title}",
                    "starts_at": (cal_base + timedelta(hours=9)).isoformat().replace("+00:00", "Z"),
                    "ends_at":   (cal_base + timedelta(hours=9, minutes=15)).isoformat().replace("+00:00", "Z"),
                    "remind_at": (cal_base + timedelta(hours=8, minutes=45)).isoformat().replace("+00:00", "Z"),
                    "project": request.project,
                },
                {
                    "title": f"First field test of {title}",
                    "starts_at": (cal_base + timedelta(days=1, hours=9)).isoformat().replace("+00:00", "Z"),
                    "ends_at":   (cal_base + timedelta(days=1, hours=9, minutes=30)).isoformat().replace("+00:00", "Z"),
                    "project": request.project,
                },
            ],
        }
        cal_json_str = json.dumps(cal_payload, indent=2, ensure_ascii=False)

        # --- brief.md / plan.md -------------------------------------------
        brief_md = (
            f"# Brief: {title}\n\n"
            f"> Project slug: `{request.project}`\n"
            f"> Generated by the Phase 2 dry-run provider. Not derived from the idea.\n\n"
            f"Build the smallest thing that satisfies the user's idea: a "
            f"`{request.project}` feature on the Cardputer-Adv. The scope is "
            f"tight — host-tested, on-card visible, and logs to the SD card. "
            f"The four tasks in `tasks.json` cover the work end-to-end.\n"
        )
        plan_md = (
            f"# Plan: {title}\n\n"
            f"1. Wire and validate inputs (see task `wire-input-{request.project}`).\n"
            f"2. Render the new state on the home screen (task `ui-state-{request.project}`).\n"
            f"3. Append a daily summary to `logs/{request.project}-<date>.jsonl` "
            f"(task `log-{request.project}`).\n"
            f"4. Add a host test that drives the path with a stub "
            f"(task `host-test-{request.project}`).\n"
        )

        # --- tasks.md (human-readable mirror) -----------------------------
        tasks_md_lines = [f"# Tasks: {title}", ""]
        for spec in task_specs:
            tasks_md_lines.append(f"- [ ] {spec[0]}: {spec[1]}")
        tasks_md_lines.append("")
        tasks_md_lines.append("Trust `tasks.json` as the source of truth.")
        tasks_md = "\n".join(tasks_md_lines)

        # --- agent-prompt.md ----------------------------------------------
        # Read the frozen template. We keep it verbatim for Phase 2 so the
        # firmware's "frozen copy" path stays deterministic.
        agent_prompt_md = FROZEN_AGENT_PACK.read_text(encoding="utf-8")

        return ProviderArtifacts(
            brief_md=brief_md,
            plan_md=plan_md,
            tasks_json=tasks_json_str,
            tasks_md=tasks_md,
            calendar_suggestions_json=cal_json_str,
            agent_prompt_md=agent_prompt_md,
            warnings=[
                "Dry-run provider: output is canned and does not reflect the user's idea.",
            ],
        )
