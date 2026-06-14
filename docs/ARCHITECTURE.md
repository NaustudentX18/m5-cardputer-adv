# Architecture

## Overview

AdvDeck Agent has two major runtime surfaces:

- Cardputer firmware
- off-device bridge service

The firmware owns capture, review, local storage, and on-device UX. The bridge owns heavyweight AI, speech-to-text, calendar export, and agent handoff generation.

## Firmware Boundary

Firmware responsibilities:

- initialize Cardputer-Adv hardware
- poll keyboard and render UI
- mount SD and manage project files
- capture text ideas
- manage local tasks and calendar events
- queue bridge requests
- import bridge results
- export agent packs
- later, record audio to SD

Firmware should not:

- store cloud provider API keys
- require internet to boot
- depend on one AI provider
- perform heavyweight LLM or high-quality STT locally
- make companion radio hardware part of the core app

## Bridge Boundary

Bridge responsibilities:

- receive files from firmware
- transcribe audio
- plan projects from rough ideas/transcripts
- validate generated JSON
- write deterministic Markdown/JSON artifacts
- export `.ics`
- optionally copy agent packs into a local workspace

Bridge adapters:

- transcription provider
- planner provider
- calendar exporter
- transport provider

## Data Model

Primary SD folder:

```text
/advdeck/
  config.json
  inbox/
  projects/
  calendar/
  outbox/
  logs/
```

Project folder:

```text
/advdeck/projects/<project-slug>/
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

## Bridge Request Lifecycle

1. User captures a project idea.
2. Firmware writes raw input to the project folder.
3. Firmware appends a pending request to `/advdeck/outbox/pending.jsonl`.
4. Bridge receives or reads the request.
5. Bridge writes generated artifacts to a staging result.
6. Firmware imports validated artifacts.
7. User reviews and accepts/rejects outputs.
8. Export writes the final agent pack.

## Failure Strategy

- Raw idea/transcript is never deleted by bridge processing.
- Failed bridge jobs remain retryable.
- Invalid AI output is stored as debug output but not accepted as project state.
- Missing SD shows degraded mode.
- Missing bridge shows queued status, not data loss.

## Testing Strategy

Firmware:

- host tests for slugs, paths, schemas, parsers
- hardware smoke for keyboard, display, SD, audio
- manual no-SD boot test

Bridge:

- dry-run provider tests
- schema validation tests
- fixture idea-to-artifact tests
- optional live provider smoke tests only when credentials are available
