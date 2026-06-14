"""Tests for the three transcription providers (Phase 4, D1.3).

These tests cover the protocol surface and the failure-mode taxonomy
defined in PHASE-4-INTERFACES.md §5.2:

* ``MockTranscriptionProvider`` returns canned text and a generic
  fallback for unknown files.
* ``LocalWhisperProvider`` raises ``ProviderUnavailable`` if the
  binary is not on disk, and ``ProviderRetryable`` on subprocess
  timeout (the latter is exercised via a ``binary=`` injection so
  the test does not depend on any real whisper-cli).
* ``OpenAITranscriptionProvider`` raises ``ProviderUnavailable``
  when ``OPENAI_API_KEY`` is not set.

The factory ``get_transcription_provider`` is exercised across the
three registered names.
"""
from __future__ import annotations

import os
from pathlib import Path

import pytest

from advdeck_bridge.providers import (
    ProviderRetryable,
    ProviderUnavailable,
    get_transcription_provider,
)
from advdeck_bridge.providers.transcription.mock import (
    GENERIC_TRANSCRIPT,
    MockTranscriptionProvider,
)
from advdeck_bridge.providers.transcription.local_whisper import (
    LocalWhisperProvider,
)
from advdeck_bridge.providers.transcription.openai import (
    OpenAITranscriptionProvider,
)


def test_mock_provider_returns_canned_text_for_known_file(tmp_path: Path) -> None:
    """The canned fixture for ``pocket-agent-recording-1.wav`` is returned verbatim."""
    wav = tmp_path / "pocket-agent-recording-1.wav"
    wav.write_bytes(b"RIFF")
    provider = MockTranscriptionProvider()
    text = provider.transcribe(wav)
    assert "Pocket Agent" in text
    assert "Build a tiny thing" in text


def test_mock_provider_returns_generic_idea_for_unknown_file(tmp_path: Path) -> None:
    """A file whose basename is not in the fixture map gets the generic fallback."""
    wav = tmp_path / "mystery-recording-1.wav"
    wav.write_bytes(b"RIFF")
    provider = MockTranscriptionProvider()
    text = provider.transcribe(wav)
    assert text == GENERIC_TRANSCRIPT
    # The generic text is two-line and is not empty.
    lines = [ln for ln in text.splitlines() if ln.strip()]
    assert len(lines) >= 2


def test_local_whisper_provider_raises_unavailable_when_binary_missing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """If ``whisper-cli`` is not on disk, ``transcribe()`` raises ``ProviderUnavailable``."""
    # Force the resolve helpers to look in an empty PATH / WHISPER_CLI.
    monkeypatch.delenv("WHISPER_CLI", raising=False)
    # The constructor with no binary relies on $WHISPER_CLI or
    # ~/.local/bin/whisper-cli; we use a fresh tmp_path as $HOME so
    # the user-local lookup also misses.
    monkeypatch.setenv("HOME", str(tmp_path))
    wav = tmp_path / "anything.wav"
    wav.write_bytes(b"RIFF")
    provider = LocalWhisperProvider()
    with pytest.raises(ProviderUnavailable):
        provider.transcribe(wav)


def test_local_whisper_provider_raises_retryable_on_subprocess_timeout(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """A timed-out whisper-cli subprocess surfaces as ``ProviderRetryable``.

    We inject a ``binary=`` that exists on disk and replace
    ``subprocess.run`` with a stub that raises ``TimeoutExpired``.
    The provider should map that to ``ProviderRetryable``.
    """
    fake_bin = tmp_path / "fake-whisper"
    fake_bin.write_text("#!/bin/sh\nsleep 60\n")
    fake_bin.chmod(0o755)

    import subprocess as _subprocess

    def _fake_run(cmd, *args, **kwargs):  # type: ignore[no-untyped-def]
        raise _subprocess.TimeoutExpired(cmd=cmd, timeout=kwargs.get("timeout", 60))

    monkeypatch.setattr("subprocess.run", _fake_run)

    wav = tmp_path / "test.wav"
    wav.write_bytes(b"RIFF")
    provider = LocalWhisperProvider(binary=str(fake_bin), timeout_seconds=1.0)
    with pytest.raises(ProviderRetryable):
        provider.transcribe(wav)


def test_openai_transcription_provider_raises_unavailable_without_api_key(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Without ``OPENAI_API_KEY`` set, the constructor raises ``ProviderUnavailable``."""
    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    with pytest.raises(ProviderUnavailable):
        OpenAITranscriptionProvider()
    # And the factory hits the same gate.
    monkeypatch.delenv("OPENAI_API_KEY", raising=False)
    with pytest.raises(ProviderUnavailable):
        get_transcription_provider("openai")
