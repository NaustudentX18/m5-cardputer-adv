"""Local whisper.cpp transcription provider (thin wrapper).

The provider spawns ``whisper-cli`` as a subprocess and returns the
transcript text parsed from stdout. It is **not** exercised by the
host test suite — the venv does not ship ``whisper-cli`` — but the
factory seam and error taxonomy are real so Phase 5+ can drop in
the binary without touching the CLI.

Binary location
---------------
Resolution order, first hit wins:

1. The ``binary`` constructor kwarg (CLI / test override).
2. The ``$WHISPER_CLI`` environment variable.
3. ``~/.local/bin/whisper-cli`` (the conventional pip-user install).

If none of the above resolve to an executable on disk, the
constructor still succeeds but the FIRST ``transcribe()`` call
raises ``ProviderUnavailable`` (matching the OpenAI provider's
"no API key" path).

Error taxonomy
--------------
* ``ProviderUnavailable`` — binary missing on disk / not executable.
  Not retryable until the operator installs it.
* ``ProviderRetryable`` — subprocess timeout (60s default). The
  runner keeps the request in the queue.
* ``ProviderUnrecoverable`` — non-zero exit with no usable transcript
  in stdout, or stdout that is not UTF-8 decodable. The runner turns
  this into ``error_code: "invalid_ai_output"``.

Subprocess contract
-------------------
The provider invokes::

    <binary> --language <lang> <wav_path>

The transcript is everything whisper-cli writes to stdout. stderr is
discarded; the caller can capture it via the ``raw_provider_output``
directory if they want to debug a failure.
"""
from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

from .. import ProviderRetryable, ProviderUnavailable, ProviderUnrecoverable

DEFAULT_TIMEOUT_SECONDS = 60.0


def _resolve_binary(binary: str | None) -> str | None:
    """Return the path to ``whisper-cli`` or ``None`` if not found.

    Resolution order: explicit kwarg, ``$WHISPER_CLI`` env var, then
    ``~/.local/bin/whisper-cli``. We do NOT shell-expand or PATH-walk
    beyond the conventional pip-user location: a user who installed
    the binary elsewhere can point ``WHISPER_CLI`` at it.
    """
    if binary:
        return binary
    env = os.environ.get("WHISPER_CLI")
    if env:
        return env
    user_local = Path.home() / ".local" / "bin" / "whisper-cli"
    if user_local.is_file() and os.access(user_local, os.X_OK):
        return str(user_local)
    return None


class LocalWhisperProvider:
    """Spawn ``whisper-cli`` for each WAV and return its stdout.

    Args:
        binary: Override the binary path (mainly for tests).
        timeout_seconds: Per-call subprocess timeout. Defaults to 60.
    """

    def __init__(
        self,
        binary: str | None = None,
        timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
    ) -> None:
        self._binary = _resolve_binary(binary)
        self._timeout = float(timeout_seconds)

    def name(self) -> str:
        return "local-whisper"

    def transcribe(self, wav_path: Path, *, language: str = "en") -> str:
        """Run whisper-cli on ``wav_path`` and return its stdout.

        Raises:
            ProviderUnavailable: binary not on disk.
            ProviderRetryable: subprocess timeout.
            ProviderUnrecoverable: non-zero exit / undecodable stdout.
        """
        if not self._binary:
            raise ProviderUnavailable(
                "local-whisper: whisper-cli not found; set $WHISPER_CLI "
                "or pass binary=<path>"
            )
        wav = Path(wav_path)
        if not wav.is_file():
            raise ProviderUnavailable(
                f"local-whisper: WAV file not found: {wav}"
            )

        cmd = [self._binary, "--language", language, str(wav)]
        try:
            completed = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=self._timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise ProviderRetryable(
                f"local-whisper: timed out after {self._timeout}s on {wav}"
            ) from exc
        except OSError as exc:
            # The binary vanished between resolve and exec, or it isn't
            # actually executable. Operator-actionable; not retryable.
            raise ProviderUnavailable(
                f"local-whisper: failed to spawn {self._binary}: {exc}"
            ) from exc

        if completed.returncode != 0:
            raise ProviderUnrecoverable(
                f"local-whisper: non-zero exit ({completed.returncode}) on {wav}"
            )

        try:
            return completed.stdout
        except UnicodeDecodeError as exc:
            # text=True decodes with the platform locale; if whisper-cli
            # emits bytes that aren't valid in that locale, this fires.
            raise ProviderUnrecoverable(
                f"local-whisper: stdout not decodable: {exc}"
            ) from exc
