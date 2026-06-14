# Invalid-output fixtures

These files are intentionally malformed against the Phase 2 schemas. They are
used by the bridge E2E test (Z02) and by `tests/test_schemas.py` to confirm the
validator actually rejects them.

| Fixture | Schema it should fail | Why it fails |
| --- | --- | --- |
| `tasks-missing-status.json` | `tasks.schema.json` | Second task omits the required `status` field. |
| `tasks-unknown-status.json` | `tasks.schema.json` | First task uses `status: "in-progress"`, which is not in the `todo`/`doing`/`done` enum. |
| `calendar-bad-datetime.json` | `calendar-suggestions.schema.json` | `starts_at: "tomorrow at 9am"` is not an ISO 8601 `date-time`. The schema declares the `format`; the host validator (jsonschema or our C++ port) is expected to enforce it. |
| `result-manifest-missing-artifacts.json` | `result-manifest.schema.json` | The required `artifacts` array is missing entirely. |

The fixtures are committed to git on purpose. Do not fix them. If the validator
ever *accepts* one of these, that's a regression — fix the validator, not the
fixture.
