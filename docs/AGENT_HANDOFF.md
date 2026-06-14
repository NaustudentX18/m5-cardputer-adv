# Agent Handoff

## Current State

The repository is scaffolded for GitHub and ready for implementation tasks. The product and architecture are documented, but firmware and bridge code are still placeholders.

## First Tasks

Start in this order:

1. `A01 Firmware Skeleton`
2. `A02 Storage Contract`
3. `A03 Text Capture And Project Browser`
4. `A04 Local Tasks`
5. `A05 Calendar And Reminders Base`
6. `A06 Bridge Protocol Fixture`

Task definitions live in `roadmap/advdeck-agent-swarm-tasks.md`.

## Do Not Start With

- real cloud provider integration
- voice recording
- companion C5/radio features
- GitHub issue creation from the bridge
- rich UI frameworks before direct `M5GFX` is proven insufficient

## Best First PR

Implement `A01 Firmware Skeleton`.

Expected files:

- `projects/advdeck-agent/platformio.ini`
- `projects/advdeck-agent/src/main.cpp`
- `projects/advdeck-agent/src/platform/keyboard.*`
- `projects/advdeck-agent/src/platform/storage.*`
- `projects/advdeck-agent/src/ui/status_bar.*`
- `projects/advdeck-agent/src/app/routes.*`

Acceptance:

- PlatformIO build passes
- app reaches a home menu
- keyboard polling has a single wrapper
- SD mount failure is visible and non-fatal

## Coordination Rules

- One task per branch or PR where practical.
- Mention the task ID in PR titles.
- Keep generated fixtures small.
- Update docs when the file contract changes.
- Leave hardware uncertainty as an explicit TODO with evidence.
