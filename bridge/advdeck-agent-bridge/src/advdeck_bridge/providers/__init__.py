"""Provider package.

A *provider* turns a project idea into the six Phase 2 artefacts (brief, plan,
tasks.json, tasks.md, calendar-suggestions.json, agent-prompt.md) plus a result
manifest. Phase 2 shipped exactly one provider: the deterministic dry-run
substitutor. Phase 3 (C1.1) adds two more:

* ``LocalFileProvider`` — reads pre-rendered artefacts from a directory.
  Used by A11 (agent-pack export tests) and Z03 (end-to-end smoke).
* ``OpenAIProvider`` — calls OpenAI's Chat Completions API. Disabled by
  default; the CLI's ``--provider openai`` path only fires if
  ``OPENAI_API_KEY`` is set, and the tests mock the SDK.

The provider is selected by name through ``get_provider(name, **kwargs)``.
Swapping providers is a one-line CLI flag (or registry entry) — not a
refactor.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Protocol

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


# ---------------------------------------------------------------------------
# Provider exception types
# ---------------------------------------------------------------------------
#
# The Phase 2 runner had a single ``BridgeError``. Phase 3 (C1.1) splits
# provider-side failures into three buckets so the runner's retry
# policy can be precise (PHASE-3-INTERFACES.md §8):
#
# * ``ProviderUnavailable`` — the provider cannot run at all (no API
#   key, missing module). NOT retryable until the operator fixes config.
# * ``ProviderRetryable`` — transient failure (HTTP 5xx, network
#   timeout). The runner marks the request ``retryable: true`` and
#   keeps it in the queue.
# * ``ProviderUnrecoverable`` — the provider ran but the response is
#   unusable (malformed JSON, schema violation that we can detect on
#   the provider side). The runner marks the request
#   ``retryable: false`` after the third consecutive occurrence.


class ProviderError(RuntimeError):
    """Base class for all provider-side errors."""


class ProviderUnavailable(ProviderError):
    """The provider is not configured or cannot run in this environment.

    Examples: missing ``OPENAI_API_KEY``, missing ``openai`` SDK. The
    CLI catches this in the entry points and exits with a friendly
    message — there is no point retrying.
    """


class ProviderRetryable(ProviderError):
    """A transient provider failure. The runner should retry.

    Examples: HTTP 5xx, network timeout, rate limit. The runner turns
    this into an error manifest with ``retryable: true``.
    """


class ProviderUnrecoverable(ProviderError):
    """The provider ran but the response is unusable.

    Examples: HTTP 200 with a body that is not JSON, JSON that does
    not parse, JSON that fails our schema check on the provider side.
    The runner turns this into ``error_code: "invalid_ai_output"``
    with ``retryable: true``; the third consecutive occurrence flips
    ``retryable`` to ``false``.
    """


# ---------------------------------------------------------------------------
# Registry
# ---------------------------------------------------------------------------
#
# The provider registry is a small dict keyed by name. We do not use
# entry points or any other auto-discovery: this is a CLI, not a plugin
# framework, and we want the error messages to be deterministic.
#
# "test" providers (``dry-run``, ``local-file``) live in the base
# install. The "live" ``openai`` provider lives in the ``[live]``
# extra (pyproject.toml) so end users who only want the dry-run flow
# do not pull in the OpenAI SDK.
#
# ``get_provider`` raises ``ProviderUnavailable`` for unknown names so
# the CLI can map that to exit code 2 with a clear message.

PROVIDER_REGISTRY: dict[str, dict[str, str]] = {
    "dry-run": {
        "module": "advdeck_bridge.providers.dry_run",
        "class": "DryRunProvider",
        "version": "1.0.0",
        "kind": "test",
    },
    "local-file": {
        "module": "advdeck_bridge.providers.local_file",
        "class": "LocalFileProvider",
        "version": "1.0.0",
        "kind": "test",
    },
    "openai": {
        "module": "advdeck_bridge.providers.openai",
        "class": "OpenAIProvider",
        "version": "1.0.0",
        "kind": "live",
    },
}
# ---------------------------------------------------------------------------
# Transcription providers (Phase 4, D1.3)
# ---------------------------------------------------------------------------
#
# The transcription sub-protocol mirrors ``Provider`` but returns a single
# transcript string instead of the six planning artefacts. The CLI uses
# this to turn a recording's WAV into ``transcript.md`` before kicking
# off the planning flow (PHASE-4-INTERFACES.md §5.2).
#
# ``TranscriptionProvider`` lives next to ``Provider`` so the seam is
# discoverable. The factory ``get_transcription_provider`` mirrors
# ``get_provider`` and raises ``ProviderUnavailable`` for unknown names.


class TranscriptionProvider(Protocol):
    """Render the transcript of one WAV file.

    Implementations return a single string. The string MAY include a
    leading ``DURATION: <s>s\nWORDS: <n>\n`` header (per the spec);
    the CLI strips / passes it through unchanged. Implementations
    raise the same ``ProviderError`` subclasses as ``Provider``.
    """

    def name(self) -> str: ...

    def transcribe(self, wav_path: Path, *, language: str = "en") -> str: ...


TRANSCRIPTION_REGISTRY: dict[str, dict[str, str]] = {
    "mock": {
        "module": "advdeck_bridge.providers.transcription.mock",
        "class": "MockTranscriptionProvider",
        "version": "1.0.0",
        "kind": "test",
    },
    "local-whisper": {
        "module": "advdeck_bridge.providers.transcription.local_whisper",
        "class": "LocalWhisperProvider",
        "version": "1.0.0",
        "kind": "test",
    },
    "openai": {
        "module": "advdeck_bridge.providers.transcription.openai",
        "class": "OpenAITranscriptionProvider",
        "version": "1.0.0",
        "kind": "live",
    },
}


def _import_transcription_provider_class(name: str) -> type:
    """Import the transcription provider class registered under ``name``.

    Mirrors ``_import_provider_class`` for the planning-side registry.
    """
    if name not in TRANSCRIPTION_REGISTRY:
        raise ProviderUnavailable(
            f"unknown transcription provider: {name!r}; "
            f"known: {sorted(TRANSCRIPTION_REGISTRY)}"
        )
    entry = TRANSCRIPTION_REGISTRY[name]
    try:
        module = __import__(entry["module"], fromlist=[entry["class"]])
    except ImportError as exc:
        raise ProviderUnavailable(
            f"transcription provider {name!r} requires the [{name}] extra: {exc}"
        ) from exc
    return getattr(module, entry["class"])


def get_transcription_provider(name: str, **kwargs: Any) -> "TranscriptionProvider":
    """Return an instantiated transcription provider by name.

    Args:
        name: One of ``mock``, ``local-whisper``, ``openai``.
        **kwargs: Forwarded to the provider's constructor. Per-provider
            kwargs are validated here so a CLI typo surfaces as a clean
            ``ProviderUnavailable`` error, not a ``TypeError`` at
            transcribe() time.

    Raises:
        ProviderUnavailable: Unknown name or missing optional dep.
    """
    if name == "mock":
        cls = _import_transcription_provider_class(name)
        # Optional ``fixtures`` kwarg overrides the canned transcripts.
        return cls(**kwargs)  # type: ignore[abstract]
    if name == "local-whisper":
        cls = _import_transcription_provider_class(name)
        allowed = {"binary", "timeout_seconds"}
        unknown = set(kwargs) - allowed
        if unknown:
            raise ProviderUnavailable(
                f"local-whisper provider does not accept: {sorted(unknown)}"
            )
        return cls(**kwargs)
    if name == "openai":
        cls = _import_transcription_provider_class(name)
        allowed = {"model", "api_key", "timeout_seconds", "language"}
        unknown = set(kwargs) - allowed
        if unknown:
            raise ProviderUnavailable(
                f"openai transcription provider does not accept: {sorted(unknown)}"
            )
        return cls(**kwargs)
    raise ProviderUnavailable(f"unknown transcription provider: {name!r}")


def _import_provider_class(name: str) -> type:
    """Import the class registered under ``name`` or raise ``ProviderUnavailable``."""
    if name not in PROVIDER_REGISTRY:
        raise ProviderUnavailable(
            f"unknown provider: {name!r}; "
            f"known: {sorted(PROVIDER_REGISTRY)}"
        )
    entry = PROVIDER_REGISTRY[name]
    try:
        module = __import__(entry["module"], fromlist=[entry["class"]])
    except ImportError as exc:
        # A missing optional dep (e.g. the openai SDK) lands here.
        raise ProviderUnavailable(
            f"provider {name!r} requires the [{name}] extra: {exc}"
        ) from exc
    return getattr(module, entry["class"])


def get_provider(name: str, **kwargs: Any) -> "Provider":
    """Return an instantiated provider by name.

    Args:
        name: One of ``dry-run``, ``local-file``, ``openai``.
        **kwargs: Forwarded to the provider's constructor. The
            factory validates the per-provider kwargs so a typo on
            the CLI shows up as a clear error, not a TypeError at
            provider.render() time.

    Raises:
        ProviderUnavailable: Unknown name or missing optional dep.
    """
    if name == "dry-run":
        # The dry-run provider takes no kwargs; the CLI passes nothing
        # and the runner instantiates the default. Keep the dispatch
        # here for symmetry with the other providers.
        cls = _import_provider_class(name)
        return cls(**kwargs)  # type: ignore[abstract]
    if name == "local-file":
        if "artifacts_dir" not in kwargs:
            raise ProviderUnavailable(
                "local-file provider requires artifacts_dir=<path>"
            )
        cls = _import_provider_class(name)
        return cls(artifacts_dir=kwargs["artifacts_dir"])
    if name == "openai":
        # ``model`` and ``api_key`` are forwarded; the OpenAIProvider
        # class itself validates the api_key (env fallback) and
        # raises ProviderUnavailable if absent.
        cls = _import_provider_class(name)
        allowed = {"model", "api_key", "timeout_seconds"}
        unknown = set(kwargs) - allowed
        if unknown:
            raise ProviderUnavailable(
                f"openai provider does not accept: {sorted(unknown)}"
            )
        return cls(**kwargs)
    # Defensive: _import_provider_class already covered this, but the
    # explicit branch above is the source of truth for kwargs.
    raise ProviderUnavailable(f"unknown provider: {name!r}")


def get_default_provider() -> "Provider":
    """Return the Phase 2 default provider (the dry-run fixture)."""
    return get_provider("dry-run")
