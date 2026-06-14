# Data Model

Status: draft.

## SD Root

```text
/advdeck/
  config.json
  inbox/
  projects/
  calendar/
  outbox/
  logs/
```

## Project

```text
/advdeck/projects/<slug>/
  idea.md
  transcript.md
  brief.md
  plan.md
  tasks.json
  tasks.md
  calendar-suggestions.json
  agent-prompt.md
  export/
    agent-pack.md
    agent-tasks.json
```

Only `idea.md` is required at project creation.

## Task Object

```json
{
  "id": "A01",
  "title": "Firmware Skeleton",
  "objective": "Create the initial firmware shell.",
  "context": "Cardputer-Adv firmware app.",
  "files_or_modules": ["projects/advdeck-agent/src/main.cpp"],
  "acceptance_criteria": ["Build passes", "Home menu renders"],
  "validation": ["pio run"],
  "dependencies": [],
  "risk": "Hardware not connected during CI.",
  "suggested_agent_role": "executor",
  "status": "todo"
}
```

## Calendar Event Object

```json
{
  "id": "evt-20260614-001",
  "title": "Test Alpha 0.1 firmware",
  "starts_at": "2026-06-15T09:00:00+10:00",
  "ends_at": null,
  "remind_at": "2026-06-15T08:30:00+10:00",
  "source": "manual",
  "project": "advdeck-agent",
  "status": "accepted"
}
```

## Write Rules

- Prefer write-temp-then-rename for important files.
- Preserve raw idea and transcript files.
- Do not accept generated JSON unless it validates.
- Keep user-editable files readable outside the app.
