# Agent Instructions

This repository is the build surface for AdvDeck Agent, a Cardputer-Adv app plus off-device AI bridge.

## Mission

Build a pocket project-capture and agent-handoff system:

- Cardputer firmware captures ideas, notes, tasks, calendar items, and audio.
- Bridge service performs speech-to-text, LLM planning, calendar export, and agent pack generation.
- All user data remains durable as plain Markdown/JSON files.

## First Principles

- The Cardputer must boot and capture without cloud, Wi-Fi, bridge, companion hardware, or API keys.
- Keep secrets off-device. Bridge owns provider credentials.
- Preserve raw ideas and transcripts.
- AI output is generated, validated, then reviewed before acceptance.
- Prefer small, testable modules over one large firmware file.
- Keep RF/companion hardware outside the MVP critical path.

## Required Reading Before Implementation

1. `docs/PRODUCT.md`
2. `docs/ARCHITECTURE.md`
3. `ROADMAP.md`
4. `roadmap/advdeck-agent-swarm-tasks.md`
5. `research/INDEX.md`

## Task Workflow

When taking a task:

1. Pick one task from `roadmap/advdeck-agent-swarm-tasks.md`.
2. State the task ID in commits/PRs.
3. Keep changes scoped to that task.
4. Add or update tests/fixtures for data transforms.
5. Update docs if behavior or file formats change.
6. Record open hardware gaps rather than guessing.

## Firmware Guidance

- Use PlatformIO + Arduino first.
- Use `M5Cardputer`, `M5Unified`, and `M5GFX`.
- Call `M5Cardputer.update()` in the main loop or shared input loop.
- Keep UI keyboard-first and readable on 240 x 135 px.
- Avoid blocking scans or long operations in the UI loop.
- Write SD files atomically where practical.
- Keep parsers and schemas host-testable where possible.

## Bridge Guidance

- Use adapter interfaces for transcription, LLM planning, and calendar export.
- Include a dry-run provider so tests do not require credentials.
- Validate generated JSON before returning it to firmware.
- Store raw provider output separately for debugging.
- Do not assume one AI vendor.

## Definition Of Done

A task is done when:

- the requested behavior or document exists
- tests or manual verification evidence are recorded
- file formats are documented
- failure modes are explicit
- no unrelated repo churn is included
