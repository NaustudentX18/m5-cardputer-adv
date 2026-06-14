# Contributing

## Project Priorities

1. Offline-first capture on Cardputer-Adv.
2. Plain-file project storage.
3. Reliable bridge contracts.
4. Agent-ready export.
5. Voice and cloud providers after the text-to-plan loop works.

## Development Flow

1. Pick an issue or task ID from `roadmap/advdeck-agent-swarm-tasks.md`.
2. Create a focused branch.
3. Implement the smallest useful slice.
4. Add tests or fixtures for data contracts.
5. Update docs for changed behavior.
6. Open a PR using the template.

## Commit Style

Use short, scoped commits:

```text
A01: add firmware skeleton
A07: define task schema fixture
docs: clarify bridge failure handling
```

## Pull Request Expectations

PRs should include:

- task ID
- summary
- validation evidence
- hardware tested, if any
- remaining risks

## Safety Boundaries

- Do not add cloud credentials to firmware.
- Do not make RF transmit behavior part of the default app.
- Do not remove raw user input during generated-output processing.
- Do not claim reminder reliability that has not been tested on hardware.
