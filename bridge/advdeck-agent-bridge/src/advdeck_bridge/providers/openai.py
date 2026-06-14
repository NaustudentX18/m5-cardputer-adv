"""OpenAI Chat Completions provider.

This is the *live* provider. It is **not** used by the default CLI
flow — the Phase 2 dry-run provider is the default, and the Phase 3
``LocalFileProvider`` is the test/CI path. The OpenAI provider exists
so end users who set ``OPENAI_API_KEY`` can run ``advdeck-bridge plan
--provider openai --project <slug>`` against a real model.

The provider is intentionally thin: the LLM does the heavy lifting in
the system prompt, and we just plumb the response into the same
``ProviderArtifacts`` shape the dry-run / local-file providers
produce. The OpenAI SDK is imported lazily so end users who do not
need the live path do not pay its import cost.

Error taxonomy
--------------
The provider raises one of three exception types from
``advdeck_bridge.providers``:

* ``ProviderUnavailable`` — the SDK is not installed or the API key is
  not set. NOT retryable.
* ``ProviderRetryable`` — HTTP 5xx, network timeout, rate limit. The
  runner turns this into ``retryable: true``.
* ``ProviderUnrecoverable`` — HTTP 200 with a body that is not JSON,
  JSON that does not parse, or a response missing the required keys.
  The runner turns this into ``error_code: "invalid_ai_output"``.

The raw HTTP response (and the rendered prompt) is always written to
``outbox/results/<id>/raw-provider-output/`` *before* the runner
applies its schema check, so a verifier can always see what the model
returned.
"""
from __future__ import annotations

import json
import os
import time
from pathlib import Path
from typing import Any

from ..queue import PendingRequest
from . import (
    ProviderArtifacts,
    ProviderRetryable,
    ProviderUnavailable,
    ProviderUnrecoverable,
)

# Package root: <bridge>/src/advdeck_bridge/providers/openai.py -> <bridge>
PACKAGE_ROOT = Path(__file__).resolve().parents[3]
TEMPLATES_DIR = PACKAGE_ROOT / "templates"
PLANNING_PROMPT = TEMPLATES_DIR / "planning-prompt.md.j2"

# Default model. gpt-4o-mini is the cheapest OpenAI model that still
# follows the JSON-only instruction reliably. Phase 4+ may switch to a
# different default.
DEFAULT_MODEL = "gpt-4o-mini"
DEFAULT_TIMEOUT_SECONDS = 60.0

# The keys we expect the model to return. If any are missing the
# response is unrecoverable. The model may also return extra keys;
# those are ignored.
REQUIRED_KEYS: tuple[str, ...] = (
    "brief_md",
    "plan_md",
    "tasks_json",
    "tasks_md",
    "calendar_suggestions_json",
    "agent_prompt_md",
)


def _read_prompt_template() -> str:
    """Read the planning-prompt.md.j2 template literally.

    We do not use Jinja2 here: the provider-side prompt rendering is
    the only place Jinja2 would be needed in the live path, and we
    want the openai extra to stay dependency-light. The template uses
    ``{{ var }}`` placeholders that ``render_prompt`` substitutes
    with ``str.replace``.
    """
    if not PLANNING_PROMPT.is_file():
        raise ProviderUnavailable(
            f"OpenAIProvider: planning prompt template missing: {PLANNING_PROMPT}"
        )
    return PLANNING_PROMPT.read_text(encoding="utf-8")


def render_prompt(project_slug: str, idea_text: str, style: str = "planner") -> str:
    """Substitute the four variables the planning-prompt.md.j2 expects.

    This is the same substitution the rest of the bridge does against
    the agent-pack template; a tiny replace loop is enough.
    """
    template = _read_prompt_template()
    out = template
    out = out.replace("{{ project_slug }}", project_slug)
    out = out.replace("{{ idea_text }}", idea_text)
    out = out.replace("{{ style }}", style)
    return out


class OpenAIProvider:
    """Live OpenAI Chat Completions provider.

    Constructed with a model name and an optional API key. If
    ``api_key`` is not given, ``OPENAI_API_KEY`` is read from the
    environment. If neither is set, ``ProviderUnavailable`` is raised
    at construction time so the CLI can exit with a clear message
    before doing any I/O.

    The provider is **stateless** across calls: every ``render`` opens
    a fresh chat completion. The runner wraps the call in its retry
    policy; the provider itself does not retry.
    """

    def __init__(
        self,
        model: str = DEFAULT_MODEL,
        api_key: str | None = None,
        timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
    ) -> None:
        self._model = model
        self._api_key = api_key or os.environ.get("OPENAI_API_KEY", "")
        if not self._api_key:
            raise ProviderUnavailable(
                "OpenAIProvider: OPENAI_API_KEY is not set. "
                "Set it in the environment or pass api_key=... to the constructor."
            )
        self._timeout_seconds = float(timeout_seconds)
        self._last_raw_response: str = ""
        self._last_raw_metadata: dict[str, str] = {}

    @property
    def model(self) -> str:
        return self._model

    def _client(self) -> Any:
        """Return an OpenAI client. Imported lazily."""
        try:
            from openai import OpenAI  # type: ignore
        except ImportError as exc:
            raise ProviderUnavailable(
                "OpenAIProvider: the 'openai' package is not installed. "
                "Install with: pip install 'advdeck-bridge[openai]'"
            ) from exc
        return OpenAI(api_key=self._api_key, timeout=self._timeout_seconds)

    def render(self, request: PendingRequest, idea_text: str) -> ProviderArtifacts:
        prompt = render_prompt(request.project, idea_text)
        response_text, raw_meta = self._call(prompt)
        # Remember the raw response for the runner to dump to disk.
        self._last_raw_response = response_text
        self._last_raw_metadata = raw_meta
        payload = self._parse(response_text)
        return ProviderArtifacts(
            brief_md=str(payload.get("brief_md", "")),
            plan_md=str(payload.get("plan_md", "")),
            # The model returns JSON values for tasks_json /
            # calendar_suggestions_json; we re-serialise them as strings
            # so the rest of the bridge (which already speaks JSON
            # strings) does not have to special-case the live path.
            tasks_json=json.dumps(payload.get("tasks_json", {}), ensure_ascii=False)
            if not isinstance(payload.get("tasks_json"), str)
            else str(payload["tasks_json"]),
            tasks_md=str(payload.get("tasks_md", "")),
            calendar_suggestions_json=json.dumps(
                payload.get("calendar_suggestions_json", {}), ensure_ascii=False
            )
            if not isinstance(payload.get("calendar_suggestions_json"), str)
            else str(payload["calendar_suggestions_json"]),
            agent_prompt_md=str(payload.get("agent_prompt_md", "")),
            warnings=[],
        )

    # ----- internals ----------------------------------------------------

    def _call(self, prompt: str) -> tuple[str, dict[str, str]]:
        """Invoke the OpenAI Chat Completions API.

        Returns ``(response_text, raw_metadata)``. Raises
        ``ProviderRetryable`` on transient failures and
        ``ProviderUnrecoverable`` on response shape problems. Network
        errors propagate as ``ProviderRetryable`` so the runner keeps
        the request alive.
        """
        client = self._client()
        try:
            response = client.chat.completions.create(
                model=self._model,
                messages=[
                    {
                        "role": "system",
                        "content": (
                            "You produce structured project plans for the AdvDeck "
                            "bridge. Always reply with a single JSON object that "
                            "matches the keys: brief_md, plan_md, tasks_json, "
                            "tasks_md, calendar_suggestions_json, agent_prompt_md. "
                            "Do not include commentary outside the JSON."
                        ),
                    },
                    {"role": "user", "content": prompt},
                ],
                response_format={"type": "json_object"},
                timeout=self._timeout_seconds,
            )
        except Exception as exc:  # noqa: BLE001
            # We translate every SDK exception into one of our two
            # error types. The retryable/unrecoverable boundary is
            # best-effort: HTTP 4xx (other than 429) is
            # unrecoverable; everything else is retryable. The SDK
            # does not expose a clean status code, so we look at the
            # class name.
            name = type(exc).__name__.lower()
            if "apierror" in name or "badrequest" in name or "notfound" in name:
                raise ProviderUnrecoverable(
                    f"OpenAIProvider: API error ({type(exc).__name__}): {exc}"
                ) from exc
            if "ratelimit" in name or "429" in str(exc):
                raise ProviderRetryable(
                    f"OpenAIProvider: rate limited: {exc}"
                ) from exc
            # Network, timeout, 5xx, etc.
            raise ProviderRetryable(
                f"OpenAIProvider: transient failure ({type(exc).__name__}): {exc}"
            ) from exc
        # The shape is documented and stable. We extract defensively so
        # a future SDK change does not crash the runner.
        try:
            text = response.choices[0].message.content or ""
        except (AttributeError, IndexError, KeyError) as exc:
            raise ProviderUnrecoverable(
                f"OpenAIProvider: response has no choices[0].message.content: {exc}"
            ) from exc
        meta = {
            "provider": "openai",
            "model": self._model,
            "prompt_chars": str(len(prompt)),
            "response_chars": str(len(text)),
        }
        return text, meta

    def _parse(self, text: str) -> dict[str, Any]:
        """Parse the model's JSON body. Raise ``ProviderUnrecoverable`` on shape errors."""
        try:
            payload = json.loads(text)
        except json.JSONDecodeError as exc:
            raise ProviderUnrecoverable(
                f"OpenAIProvider: model response is not valid JSON: {exc}"
            ) from exc
        if not isinstance(payload, dict):
            raise ProviderUnrecoverable(
                f"OpenAIProvider: model response is not a JSON object: {type(payload).__name__}"
            )
        missing = [k for k in REQUIRED_KEYS if k not in payload]
        if missing:
            raise ProviderUnrecoverable(
                f"OpenAIProvider: model response missing keys: {missing}"
            )
        return payload

    # ----- runner-facing audit fields -----------------------------------

    @property
    def last_raw_response(self) -> str:
        """The raw text the model returned on the most recent call."""
        return self._last_raw_response

    @property
    def last_raw_metadata(self) -> dict[str, str]:
        """Small metadata dict for ``raw-provider-output/raw-metadata.json``."""
        # Stamp the time at access time so each call gets a fresh value.
        meta = dict(self._last_raw_metadata)
        meta["timestamp"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        return meta
