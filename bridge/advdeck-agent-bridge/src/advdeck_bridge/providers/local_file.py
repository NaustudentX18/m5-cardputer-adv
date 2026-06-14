"""Pre-rendered local-file provider.

This provider reads the six Phase 2/3 artefacts from a directory on disk
instead of running an LLM. It is the *test / CI / desktop* path: the A11
agent-pack export and the Z03 end-to-end test both use it to feed the
bridge deterministic bytes that an LLM would otherwise produce.

Wire format
-----------
The provider's constructor takes a directory path. The directory is
expected to contain one or more of these files (any may be missing):

* ``brief.md``
* ``plan.md``
* ``tasks.json``
* ``tasks.md``
* ``calendar-suggestions.json``
* ``agent-prompt.md``

A missing file is **not** an error: the corresponding artefact is set
to an empty string and a warning is appended to
``ProviderArtifacts.warnings``. Empty ``tasks.json`` /
``calendar-suggestions.json`` is **also** not an error from the
provider's perspective; the bridge's runner will reject the result via
its JSON Schema validator.

The "raw output" written to ``outbox/results/<id>/raw-provider-output/``
is a listing of the directory and the body of every file that was
loaded. We do not keep secrets in the raw output; it is the bridge's
audit trail, not the LLM's request payload.
"""
from __future__ import annotations

import json
from pathlib import Path

from ..queue import PendingRequest
from . import ProviderArtifacts

# Names of the six artefacts the bridge consumes, in stable order.
ARTIFACT_NAMES: tuple[str, ...] = (
    "brief.md",
    "plan.md",
    "tasks.json",
    "tasks.md",
    "calendar-suggestions.json",
    "agent-prompt.md",
)


class LocalFileProvider:
    """Read pre-rendered artefacts from a directory.

    This is the deterministic, no-network, no-LLM provider used by the
    A11 agent-pack export tests and the Z03 end-to-end smoke. End-users
    on the Cardputer never instantiate it; the CLI's ``plan-local``
    subcommand is the only public surface.

    The provider never raises on a missing file. An empty string in any
    artefact field is the consumer's signal to validate-and-reject; the
    bridge's runner will turn an empty ``tasks_json`` /
    ``calendar-suggestions.json`` into an error manifest.
    """

    def __init__(self, artifacts_dir: str | Path) -> None:
        self._dir = Path(artifacts_dir)
        if not self._dir.is_dir():
            # We could raise here, but a missing dir is a useful "this
            # bridge is misconfigured" diagnostic. Make it loud.
            raise FileNotFoundError(
                f"LocalFileProvider: artifacts dir does not exist: {self._dir}"
            )

    @property
    def artifacts_dir(self) -> Path:
        """Absolute path of the directory this provider reads from."""
        return self._dir

    def _read_optional(self, name: str) -> tuple[str, list[str]]:
        """Read a single artefact. Returns ``(body, warnings)``.

        Missing files return ``("", warning)``; this is the contract
        the runner relies on for the warnings list in the result
        manifest. Empty-but-present files also return ``("", ...)``
        but without a warning (the file was there; the consumer
        validates the content).
        """
        path = self._dir / name
        if not path.exists():
            return "", [f"LocalFileProvider: missing artefact: {name}"]
        return path.read_text(encoding="utf-8"), []

    def render(self, request: PendingRequest, idea_text: str) -> ProviderArtifacts:
        del request  # the local-file provider is project-agnostic
        del idea_text  # the local-file provider ignores the idea on purpose

        warnings: list[str] = []
        bodies: dict[str, str] = {}
        for name in ARTIFACT_NAMES:
            body, warns = self._read_optional(name)
            bodies[name] = body
            warnings.extend(warns)

        return ProviderArtifacts(
            brief_md=bodies["brief.md"],
            plan_md=bodies["plan.md"],
            tasks_json=bodies["tasks.json"],
            tasks_md=bodies["tasks.md"],
            calendar_suggestions_json=bodies["calendar-suggestions.json"],
            agent_prompt_md=bodies["agent-prompt.md"],
            warnings=warnings,
        )

    def raw_response_text(self) -> str:
        """Return a human-readable summary of the directory contents.

        The bridge writes this to ``raw-provider-output/raw-response.txt``
        so a human (or a CI log) can see what the "provider" returned
        without re-running the test fixture.
        """
        lines = [f"# LocalFileProvider dump: {self._dir}", ""]
        # Stable order so two runs against the same dir produce the same
        # bytes (the bridge snapshot tests check this).
        for name in ARTIFACT_NAMES:
            path = self._dir / name
            lines.append(f"## {name}")
            if not path.exists():
                lines.append("(missing)")
            else:
                body = path.read_text(encoding="utf-8")
                # Trim absurdly large dumps; the schema validator will
                # have already failed on the body if it was malformed.
                if len(body) > 16 * 1024:
                    body = body[:16 * 1024] + "\n... [truncated]\n"
                lines.append(body)
            lines.append("")
        return "\n".join(lines)

    def raw_metadata(self) -> dict[str, str]:
        """Return the small metadata block the runner writes alongside the dump.

        Kept tiny so the runner does not have to plumb a separate
        schema through. Provider name + dir is enough to identify the
        test fixture in the audit log.
        """
        return {
            "provider": "local-file",
            "artifacts_dir": str(self._dir),
        }


def is_valid_artifacts_dir(path: Path) -> bool:
    """True if ``path`` is a directory that contains at least one artefact.

    Convenience helper for the CLI's ``--artifacts`` argument: a bare
    directory with no artefacts is almost certainly a mistake and we
    want a friendly error before the runner reports a confusing
    "missing all six" warning.
    """
    if not path.is_dir():
        return False
    return any((path / name).exists() for name in ARTIFACT_NAMES)
