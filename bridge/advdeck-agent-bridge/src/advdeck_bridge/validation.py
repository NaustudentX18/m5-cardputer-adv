"""Schema loading and validation.

The bridge uses the same Draft 2020-12 schemas the firmware embeds. We load
them once on demand and cache the parsed dicts in module state; this avoids
re-parsing on every CLI invocation while still letting tests point at
alternative schema files (the ``validate`` subcommand does this).

We use ``jsonschema.Draft202012Validator`` (not the top-level
``jsonschema.validate``) so format errors (e.g. ``date-time`` on
``calendar-suggestions.json``) actually fail — the top-level shortcut
silently ignores ``format`` in older versions.
"""
from __future__ import annotations

import json
from functools import lru_cache
from pathlib import Path
from typing import Any

import jsonschema
from jsonschema import Draft202012Validator

# Package root: <bridge>/src/advdeck_bridge/validation.py -> <bridge>
PACKAGE_ROOT = Path(__file__).resolve().parents[2]
SCHEMAS_DIR = PACKAGE_ROOT / "schemas"


@lru_cache(maxsize=None)
def _load_schema_by_name(name: str) -> dict:
    """Load a schema by basename (no extension) from the bundled schemas dir."""
    path = SCHEMAS_DIR / f"{name}.schema.json"
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def load_schema(name: str) -> dict:
    """Return the parsed schema for the given basename (e.g. ``tasks``)."""
    return _load_schema_by_name(name)


def load_schema_from_path(path: Path) -> dict:
    """Load a schema directly from an arbitrary path (used by the validate CLI)."""
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def validate(instance: Any, schema: dict) -> None:
    """Raise ``jsonschema.ValidationError`` on the first problem found.

    Format checkers (``date-time``) are explicitly registered so the calendar
    fixture with ``starts_at: "tomorrow at 9am"`` actually fails.
    """
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema, format_checker=Draft202012Validator.FORMAT_CHECKER)
    validator.validate(instance)


def is_valid(instance: Any, schema: dict) -> bool:
    """Return True if ``instance`` validates against ``schema``."""
    try:
        validate(instance, schema)
    except jsonschema.ValidationError:
        return False
    return True
