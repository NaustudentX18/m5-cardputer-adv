# Agent pack export — {{ project_title }}

> Project slug: `{{ project_slug }}`
> Exported at: `{{ exported_at }}`
> Planner provider: `{{ planner_provider }}`
> Request id: `{{ request_id }}`

---

You are a fresh coding agent. The file `agent-pack.md` in this folder
is the single source of truth for this project — read it top to
bottom before doing anything else. The structured task list lives in
`agent-tasks.json` next to it. `sources.json` is a machine-readable
index of the four files in this folder with their SHA-256 hashes.
`export-info.json` is the export-time metadata (planner version,
exported_at, project slug, request id).

Do not invent requirements that are not in the brief or plan. If a
task's `acceptance_criteria` is ambiguous, ask before implementing.
Match existing style; do not refactor adjacent code. Run the
project's existing tests before declaring a task done.
