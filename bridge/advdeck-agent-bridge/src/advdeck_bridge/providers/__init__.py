"""Provider package.

A *provider* turns a project idea into the six Phase 2 artefacts (brief, plan,
tasks.json, tasks.md, calendar-suggestions.json, agent-prompt.md) plus a result
manifest. Phase 2 ships exactly one provider: the deterministic dry-run
substitutor that ignores the idea and returns a canned set of bytes derived
from the frozen ``agent-pack.md`` template.

Phase 3 (A08) will add a real LLM-backed provider that satisfies the same
``Provider`` protocol and returns the same artefact shape. Swapping the
provider must be a one-line CLI flag (or registry entry) — not a refactor.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Protocol

from ..queue import PendingRequest


@dataclass
class ProviderArtifacts:
    """The bytes the provider wants written next to ``result.json``.

    ``brief_md`` / ``plan_md`` / ``tasks_md`` / ``agent_prompt_md`` are
    raw strings (utf-8). ``tasks_json`` and ``calendar_suggestions_json`` are
    pre-serialised JSON strings so the bridge can round-trip them through
    ``jsonschema`` before writing.
    """

    brief_md: str
    plan_md: str
    tasks_json: str
    tasks_md: str
    calendar_suggestions_json: str
    agent_prompt_md: str
    warnings: list[str] = field(default_factory=list)


class Provider(Protocol):
    """Render artefacts for one ``PendingRequest``."""

    def render(self, request: PendingRequest, idea_text: str) -> ProviderArtifacts: ...


def get_default_provider() -> "Provider":
    """Return the Phase 2 default provider (the dry-run fixture)."""
    from .dry_run import DryRunProvider

    return DryRunProvider()
