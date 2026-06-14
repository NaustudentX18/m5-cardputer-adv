"""Mock transcription provider.

Returns canned transcripts keyed on the WAV file's basename. Used by
Z04 (end-to-end) and the in-process unit tests. The provider does NOT
read the WAV bytes, does NOT call the network, and does NOT consume
wall-clock time.

Canned fixture mapping
----------------------
The default mapping is::

    pocket-agent-recording-1.wav -> "# Pocket Agent\\n\\nBuild a tiny
        thing that captures a voice memo on the Cardputer-Adv.\\n"

Callers may override the mapping by passing ``fixtures={name: text}``
to the constructor; this lets tests cover the generic-fallback path
without mutating module state.

Unknown files return a generic two-line idea so the planner still has
something to work with::

    "# Voice Note\\n\\nA short idea captured on the Cardputer.\\n"

The mock provider never raises. The Phase 4 contract is "transcribe
a WAV -> produce a transcript string", and the mock honours that
without exercising the failure paths the live providers cover.
"""
from __future__ import annotations

from pathlib import Path
from typing import Mapping


# Canned transcripts keyed by WAV basename. Names follow the firmware's
# convention of ``<project-slug>-recording-<n>.wav`` (per D1.1 manifest
# layout). Z04 reads these to assert the end-to-end plan flow.
DEFAULT_FIXTURES: dict[str, str] = {
    "pocket-agent-recording-1.wav": (
        "# Pocket Agent\n\n"
        "Build a tiny thing that captures a voice memo on the Cardputer-Adv.\n"
    ),
    "garden-watering-recording-1.wav": (
        "# Garden Watering\n\n"
        "Water the back garden on a schedule and remind me on the device.\n"
    ),
    "inbox-zero-recording-1.wav": (
        "# Inbox Zero\n\n"
        "Triage the inbox every morning and surface the three things to do.\n"
    ),
}

GENERIC_TRANSCRIPT = (
    "# Voice Note\n\n"
    "A short idea captured on the Cardputer-Adv that needs turning into a plan.\n"
)


class MockTranscriptionProvider:
    """Return a canned transcript for a WAV file's basename.

    Args:
        fixtures: Optional override for the default canned mapping.
            The keys are exact basenames; any basename not in
            ``fixtures`` falls back to ``GENERIC_TRANSCRIPT``.
    """

    def __init__(self, fixtures: Mapping[str, str] | None = None) -> None:
        self._fixtures: dict[str, str] = dict(fixtures) if fixtures is not None else dict(DEFAULT_FIXTURES)

    def name(self) -> str:
        return "mock"

    def transcribe(self, wav_path: Path, *, language: str = "en") -> str:
        """Return the canned transcript for ``wav_path``.

        ``language`` is accepted for protocol parity with the live
        providers; the mock does not use it.
        """
        # Use the basename so callers can pass either an absolute path
        # (as the CLI does) or just a name (as some tests do).
        key = Path(wav_path).name
        if key in self._fixtures:
            return self._fixtures[key]
        return GENERIC_TRANSCRIPT
