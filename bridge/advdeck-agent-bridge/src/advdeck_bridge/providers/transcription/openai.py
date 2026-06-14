"""OpenAI Audio transcription provider (stub for Phase 5+).

The Phase 4 spec (PHASE-4-INTERFACES.md §5.2) is explicit: this
provider is **disabled unless** ``OPENAI_API_KEY`` is set. The CLI
exits with a friendly error and the tests confirm the gate.

The provider is intentionally a stub: the SDK import is lazy, the
constructor raises ``ProviderUnavailable`` when the key is missing,
and ``transcribe()`` raises ``ProviderUnavailable`` with a message
that the operator can act on. Phase 5 will fill in the HTTP call.

The contract is real (the protocol method exists, the error type is
correct, the seam is wired) so the rest of the bridge — the
``transcribe`` and ``transcribe-and-plan`` CLI subcommands, the
``get_transcription_provider`` factory — works end-to-end with the
mock and the local-whisper providers today.
"""
from __future__ import annotations

import os
from pathlib import Path

from .. import ProviderUnavailable

DEFAULT_MODEL = "whisper-1"
DEFAULT_TIMEOUT_SECONDS = 60.0


class OpenAITranscriptionProvider:
    """Transcribe a WAV via the OpenAI Audio API (Phase 5+ stub).

    Args:
        model: OpenAI transcription model name. Defaults to ``whisper-1``.
        api_key: Optional explicit key; otherwise read from
            ``OPENAI_API_KEY``. If neither is set, the provider is
            unavailable.
        timeout_seconds: Per-call HTTP timeout. Defaults to 60.
        language: Default BCP-47 language tag forwarded to the API.
    """

    def __init__(
        self,
        model: str = DEFAULT_MODEL,
        api_key: str | None = None,
        timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
        language: str = "en",
    ) -> None:
        self._model = model
        self._api_key = api_key or os.environ.get("OPENAI_API_KEY")
        self._timeout = float(timeout_seconds)
        self._default_language = language
        if not self._api_key:
            raise ProviderUnavailable(
                "openai transcription: OPENAI_API_KEY is not set"
            )

    def name(self) -> str:
        return "openai"

    def transcribe(self, wav_path: Path, *, language: str = "en") -> str:
        """Return the transcript text from the OpenAI Audio API.

        Phase 5 will implement the actual HTTP call. For Phase 4 this
        provider is the seam only; calling ``transcribe()`` raises
        ``ProviderUnavailable`` so the CLI surfaces a clear error.
        """
        # Guard: the constructor only sets the key; we still check here
        # so a future bug that drops the key is caught loudly.
        if not self._api_key:
            raise ProviderUnavailable(
                "openai transcription: OPENAI_API_KEY is not set"
            )
        # The actual implementation is deferred to Phase 5. We do not
        # silently no-op: that would let a misconfigured "live" run
        # ship a mock transcript under the openai name.
        raise ProviderUnavailable(
            "openai transcription: live audio API not implemented in Phase 4; "
            "use --provider mock or --provider local-whisper"
        )
