"""Path helpers for the AdvDeck SD layout.

The Cardputer storage layout is rooted at a single directory (Phase 1 default
``/advdeck`` on the device, an arbitrary temp dir in tests). All on-disk
artefacts that the bridge reads or writes live under that root.
"""
from __future__ import annotations

from pathlib import Path


DEFAULT_STORAGE_ROOT = "/advdeck"


def project_dir(storage_root: Path, slug: str) -> Path:
    """Return ``<root>/projects/<slug>``."""
    return storage_root / "projects" / slug


def idea_path(storage_root: Path, slug: str) -> Path:
    """Return ``<root>/projects/<slug>/idea.md``."""
    return project_dir(storage_root, slug) / "idea.md"


def outbox_dir(storage_root: Path) -> Path:
    """Return ``<root>/outbox``."""
    return storage_root / "outbox"


def pending_path(storage_root: Path) -> Path:
    """Return ``<root>/outbox/pending.jsonl``."""
    return outbox_dir(storage_root) / "pending.jsonl"


def results_dir(storage_root: Path) -> Path:
    """Return ``<root>/outbox/results``."""
    return outbox_dir(storage_root) / "results"


def result_dir(storage_root: Path, request_id: str) -> Path:
    """Return ``<root>/outbox/results/<request_id>``."""
    return results_dir(storage_root) / request_id


def log_dir(storage_root: Path) -> Path:
    """Return ``<root>/logs`` (the firmware writes ``bridge-import.log`` here)."""
    return storage_root / "logs"


def slug_to_title(slug: str) -> str:
    """Best-effort human title for a project slug.

    ``garden-watering`` -> ``Garden Watering``. Used only for templated output;
    the firmware never reads the result of this.
    """
    if not slug:
        return ""
    return " ".join(part.capitalize() for part in slug.split("-"))
