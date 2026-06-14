"""Tests for ``OpenAIProvider`` (C1.1).

The contract asks for 3 tests (the assignment specifies 4 bullets but
counts 3 cases in its description):
  1. ``OPENAI_API_KEY`` not set -> ``ProviderUnavailable`` is raised
  2. HTTP 500 from a mocked client -> ``ProviderRetryable``
  3. JSON parse error from the model -> ``ProviderUnrecoverable``

We use a tiny monkeypatch of the OpenAIProvider's ``_client`` method
so we do not need to install the ``openai`` package. The test
``renders_all_six_artefacts_when_response_is_well_formed`` is a
bonus that uses the same monkeypatch shape.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import pytest

from advdeck_bridge.providers import (
    ProviderError,
    ProviderRetryable,
    ProviderUnavailable,
    ProviderUnrecoverable,
)
from advdeck_bridge.queue import PendingRequest


# ---------------------------------------------------------------------------
# Stubs that mimic the parts of the openai SDK the provider touches.
# ---------------------------------------------------------------------------


class _FakeCompletions:
    def __init__(self, behaviour: Any) -> None:
        self._behaviour = behaviour

    def create(self, *args: Any, **kwargs: Any) -> Any:
        return self._behaviour(*args, **kwargs)


class _FakeChat:
    def __init__(self, behaviour: Any) -> None:
        self.completions = _FakeCompletions(behaviour)


class _FakeOpenAIClient:
    def __init__(self, behaviour: Any) -> None:
        self.chat = _FakeChat(behaviour)


class _FakeMessage:
    def __init__(self, content: str) -> None:
        self.content = content


class _FakeChoice:
    def __init__(self, content: str) -> None:
        self.message = _FakeMessage(content)


class _FakeResponse:
    def __init__(self, content: str) -> None:
        self.choices = [_FakeChoice(content)]


class _FakeHTTPError(Exception):
    """Mirrors the SDK's exception classes by name (snake-case lower)."""


class _FakeRateLimitError(Exception):
    pass


class _FakeAPIError(Exception):
    pass


def _build_provider(api_key: str, behaviour: Any, monkeypatch: pytest.MonkeyPatch):
    """Build an OpenAIProvider with a stub OpenAI client.

    We patch the provider's ``_client`` method so we never call into
    the real SDK. The ``behaviour`` callable gets the kwargs the
    provider passes and returns either a response object or raises
    one of the fake exceptions.
    """
    # Import here so the test still runs if the openai package is not
    # installed (the contract says ``live`` is opt-in).
    from advdeck_bridge.providers import openai as openai_mod

    # Build a provider that would normally require an API key, but
    # bypass the env check by setting ``_api_key`` directly via a
    # constructor that the production code would refuse. We reach into
    # the class to avoid the env dance.
    provider = openai_mod.OpenAIProvider.__new__(openai_mod.OpenAIProvider)
    provider._api_key = api_key
    provider._model = openai_mod.DEFAULT_MODEL
    provider._timeout_seconds = openai_mod.DEFAULT_TIMEOUT_SECONDS
    provider._last_raw_response = ""
    provider._last_raw_metadata = {}
    # Monkey-patch the client factory.
    monkeypatch.setattr(provider, "_client", lambda: _FakeOpenAIClient(behaviour))
    return provider


def _make_request() -> PendingRequest:
    return PendingRequest(
        id="req-20260614-001",
        project="demo",
        type="plan_project",
        inputs=["idea.md"],
        created_at="2026-06-14T12:00:00Z",
        status="pending",
        attempts=0,
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_openai_key_not_set_raises_provider_unavailable(monkeypatch: pytest.MonkeyPatch) -> None:
    """Without ``OPENAI_API_KEY`` and no api_key kwarg, ``ProviderUnavailable``."""
    from advdeck_bridge.providers import openai as openai_mod

    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    with pytest.raises(ProviderUnavailable) as exc:
        openai_mod.OpenAIProvider()  # no api_key
    assert "OPENAI_API_KEY" in str(exc.value)


def test_openai_http_500_raises_provider_retryable(monkeypatch: pytest.MonkeyPatch) -> None:
    """Transient SDK error -> ``ProviderRetryable``."""

    def _behaviour(*args: Any, **kwargs: Any) -> Any:
        raise _FakeHTTPError("HTTP 500: upstream down")

    provider = _build_provider("sk-test", _behaviour, monkeypatch)
    with pytest.raises(ProviderRetryable):
        provider.render(_make_request(), idea_text="an idea")


def test_openai_json_parse_error_raises_provider_unrecoverable(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Model returned 200 OK but the body is not valid JSON."""

    def _behaviour(*args: Any, **kwargs: Any) -> Any:
        return _FakeResponse("not-json-{{broken")

    provider = _build_provider("sk-test", _behaviour, monkeypatch)
    with pytest.raises(ProviderUnrecoverable) as exc:
        provider.render(_make_request(), idea_text="an idea")
    assert "JSON" in str(exc.value)


def test_openai_renders_six_artefacts_when_response_is_well_formed(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """A well-formed JSON body produces all six artefact fields."""

    payload = {
        "brief_md": "# Brief\n",
        "plan_md": "# Plan\n1. step\n",
        "tasks_json": {"version": 1, "tasks": []},
        "tasks_md": "- [ ] t1\n",
        "calendar_suggestions_json": {"version": 1, "suggestions": []},
        "agent_prompt_md": "# Agent\n",
    }

    def _behaviour(*args: Any, **kwargs: Any) -> Any:
        return _FakeResponse(json.dumps(payload))

    provider = _build_provider("sk-test", _behaviour, monkeypatch)
    artefacts = provider.render(_make_request(), idea_text="an idea")
    assert artefacts.brief_md == "# Brief\n"
    assert artefacts.plan_md == "# Plan\n1. step\n"
    assert artefacts.tasks_md == "- [ ] t1\n"
    assert artefacts.agent_prompt_md == "# Agent\n"
    # JSON artefacts were re-serialised as strings.
    assert json.loads(artefacts.tasks_json) == {"version": 1, "tasks": []}
    assert json.loads(artefacts.calendar_suggestions_json) == {
        "version": 1,
        "suggestions": [],
    }


def test_openai_missing_key_raises_provider_unavailable_via_factory(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """The factory surfaces a friendly error when the key is absent."""
    from advdeck_bridge.providers import get_provider

    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    with pytest.raises(ProviderError):
        get_provider("openai")


def test_openai_api_error_raises_provider_unrecoverable(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """SDK APIError (HTTP 4xx) -> ``ProviderUnrecoverable`` (not retryable)."""

    def _behaviour(*args: Any, **kwargs: Any) -> Any:
        raise _FakeAPIError("400 Bad Request: malformed")

    provider = _build_provider("sk-test", _behaviour, monkeypatch)
    with pytest.raises(ProviderUnrecoverable):
        provider.render(_make_request(), idea_text="an idea")
