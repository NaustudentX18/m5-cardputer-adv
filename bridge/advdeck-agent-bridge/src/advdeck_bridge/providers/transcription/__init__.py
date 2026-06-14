"""Transcription provider implementations (Phase 4, D1.3).

The three implementations live in sibling modules:

* :mod:`.mock` — deterministic canned transcripts (Z04, E2E).
* :mod:`.local_whisper` — thin wrapper around ``whisper-cli`` (Phase 5+).
* :mod:`.openai` — OpenAI Audio API stub (Phase 5+).

Selection is via ``get_transcription_provider(name, **kwargs)`` in the
parent ``providers`` package.
"""
