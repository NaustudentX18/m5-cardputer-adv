# Templates

This directory holds the project-handoff templates for the AdvDeck bridge.

## Files

| File | Format | Used by | When |
| --- | --- | --- | --- |
| `agent-pack.md.j2` | Jinja2 source | A08 (Phase 3) | When a real LLM provider is wired in |
| `agent-pack.md` | frozen literal | dry-run provider (Phase 2) | Always in Phase 2 |
| `planning-prompt.md.j2` | Jinja2 source | A08 (Phase 3) | When a real LLM provider is wired in |

## `agent-pack.md` vs `agent-pack.md.j2`

The Phase 2 dry-run provider reads `agent-pack.md` directly. It performs an
in-process literal substitution of the `{{ var }}` placeholders — it does
**not** invoke Jinja2. This keeps the dry-run path free of any templating
dependency and lets us keep the rendered file checked in for review.

Phase 3 (A08) will render `agent-pack.md.j2` with real Jinja2 against live
LLM output. The `.j2` and `.md` files should describe the **same** document
shape — the `.md` is just a sample render of the `.j2`.

If you change the variable list, update **both** files in the same commit.
The `agent-pack.md` checked in here is the canonical sample render.

## `planning-prompt.md.j2`

This template is the prompt that A08 (Text-To-Plan Bridge) will send to the
LLM in Phase 3. The dry-run provider in Phase 2 ignores it. The variables
are:

- `project_slug` — the `^[a-z0-9][a-z0-9-]{0,63}$` slug from the request
- `idea_text` — body of `idea.md` for the project
- `style` — agent persona; one of `executor`, `planner`, `architect`,
  `writer`, `reviewer`, `debugger`, `tester`, `oracle`, `designer`. Controls
  tone and task granularity, not the output schema.

## Variables reference

### `agent-pack.md.j2`

| Variable | Source |
| --- | --- |
| `project_slug` | `PendingRequest.project` |
| `project_title` | derived from idea (human-readable title) |
| `idea` | `idea.md` body, verbatim |
| `brief` | LLM- or fixture-generated `brief.md` |
| `plan` | LLM- or fixture-generated `plan.md` |
| `tasks` | human-readable render of `tasks.json` |
| `calendar_suggestions` | human-readable render of `calendar-suggestions.json` |

### `planning-prompt.md.j2`

| Variable | Source |
| --- | --- |
| `project_slug` | `PendingRequest.project` |
| `idea_text` | `idea.md` body |
| `style` | persona selector (see above) |
