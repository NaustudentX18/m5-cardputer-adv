# AdvDeck Agent Bridge

Off-device helper service for AdvDeck Agent.

## Status

Placeholder. Implementation should start after the firmware storage and bridge fixture contracts are defined.

## Responsibilities

- receive Cardputer project files
- transcribe audio files
- generate project plans from ideas/transcripts
- validate generated JSON
- write Markdown/JSON artifacts
- export calendar files
- prepare agent handoff packs

## First Milestone

Build a dry-run CLI that reads:

```text
idea.md
```

and writes:

```text
brief.md
plan.md
tasks.json
tasks.md
agent-prompt.md
```

No credentials should be required for the dry-run provider.
