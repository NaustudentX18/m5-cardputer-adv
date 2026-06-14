# AdvDeck Agent Swarm Task Pack

Generated: 2026-06-14

This file is written so a coordinator can hand independent slices to an agent swarm. Each task should produce durable files, tests, or docs. Avoid relying on chat-only decisions.

## Global Constraints

- Target hardware: M5Stack Cardputer-Adv.
- Firmware stack: PlatformIO + Arduino + `M5Cardputer` + `M5Unified` + `M5GFX`.
- Device must boot without AI, Wi-Fi, bridge, companion board, or cloud.
- Use SD plain files as the durable source of truth.
- Keep secrets off the Cardputer.
- Generated AI output must be reviewable before acceptance.
- Do not make RF or C5 companion work part of the MVP critical path.

## Shared Acceptance Criteria

- Every file format is documented.
- Every generated artifact can be read without the app.
- Failure modes preserve raw user input.
- The app works in no-SD degraded mode where feasible.
- Agent handoff tasks include objective, context, acceptance criteria, and validation.

## Task A01: Firmware Skeleton

Suggested role: `executor`

Objective:

Create the initial `projects/advdeck-agent/` firmware skeleton.

Deliverables:

- `platformio.ini`
- `src/main.cpp`
- app route registry
- display/status bar shell
- keyboard polling wrapper
- SD mount helper

Acceptance:

- project builds with PlatformIO
- boot screen reaches home menu
- `M5Cardputer.update()` is called in the loop
- no radio/network behavior starts at boot

Validation:

- `pio run`
- hardware smoke notes if device is connected

## Task A02: Storage Contract

Suggested role: `architect`

Objective:

Define and implement the SD folder/data model.

Deliverables:

- storage service
- atomic write helper
- project slug generator
- documented `/advdeck/` folder contract
- host tests for slugs and path handling

Acceptance:

- creates `/advdeck/inbox`, `/advdeck/projects`, `/advdeck/calendar`, `/advdeck/outbox`
- can create, read, update, and list a project
- handles missing SD with a clear degraded state

Validation:

- host tests for path generation
- manual SD-present and SD-absent smoke test

## Task A03: Text Capture And Project Browser

Suggested role: `executor`

Objective:

Build the first useful offline workflow: type an idea and save it as a project.

Deliverables:

- capture route
- multiline text editor
- project list
- project detail route
- raw idea viewer/editor

Acceptance:

- user can create a project from a long typed idea
- project persists after reboot
- raw idea is never overwritten by AI output

Validation:

- create/edit/reopen manual test
- long text edge case test

## Task A04: Local Tasks

Suggested role: `executor`

Objective:

Add local task lists before AI generation.

Deliverables:

- `tasks.json` format
- task list UI
- add/edit/toggle/delete task
- Markdown export for tasks

Acceptance:

- tasks survive reboot
- malformed `tasks.json` fails gracefully
- task UI remains usable on 240 x 135 px

Validation:

- host schema test
- manual create/toggle/reopen test

## Task A05: Calendar And Reminders Base

Suggested role: `executor`

Objective:

Implement local calendar events and simple reminders.

Deliverables:

- `events.json` format
- event editor
- upcoming event list
- app-running reminder alert
- bridge-export-ready calendar schema

Acceptance:

- manual event persists
- upcoming events are sorted
- app-running reminder alert triggers
- no claim of powered-off reminder reliability

Validation:

- host date/event sorting tests
- manual reminder test

## Task A06: Bridge Protocol Fixture

Suggested role: `executor`

Objective:

Create the smallest end-to-end bridge flow without real AI.

Deliverables:

- bridge queue file format
- firmware outbox queue
- bridge fixture CLI
- firmware import of result files
- sync status UI

Acceptance:

- Cardputer queues a project for planning
- fixture bridge returns canned `brief.md`, `plan.md`, `tasks.json`, and `agent-prompt.md`
- firmware imports and displays generated artifacts
- failed sync remains retryable

Validation:

- fixture end-to-end test using local files
- manual import test on SD

## Task A07: Planner Schemas And Templates

Suggested role: `architect`

Objective:

Define strict AI output schemas and prompt templates.

Deliverables:

- `tasks.schema.json`
- `calendar-suggestions.schema.json`
- `agent-pack.md` template
- planning prompt template
- invalid-output examples

Acceptance:

- tasks include objective, context, acceptance criteria, validation, dependencies, risk, suggested role
- schema validator rejects malformed output
- templates do not depend on hidden chat state

Validation:

- schema validation tests
- sample messy idea fixture produces complete artifacts

## Task A08: Text-To-Plan Bridge

Suggested role: `executor`

Objective:

Replace fixture output with a real planner adapter while keeping provider choice isolated.

Deliverables:

- planner adapter interface
- first LLM provider adapter
- local dry-run provider
- artifact writer
- error handling and retry metadata

Acceptance:

- bridge can plan from `idea.md`
- invalid JSON is caught
- raw AI output is logged separately
- firmware sees a clean success/error status

Validation:

- dry-run tests
- one live provider smoke test when credentials are available

## Task A09: Voice Recording Spike

Suggested role: `debugger`

Objective:

Prove or disprove reliable ES8311 audio capture for voice idea dumps.

Deliverables:

- minimal recorder route or standalone spike
- WAV/PCM writer
- memory/SD buffering notes
- tested recording length table

Acceptance:

- records at least 15 seconds to SD
- documents max stable tested length
- failure modes are understood

Validation:

- play back recorded WAV on host
- test 15, 60, 180 second attempts

## Task A10: Speech-To-Text Bridge

Suggested role: `dependency-expert`

Objective:

Add transcription as a bridge adapter, not firmware logic.

Deliverables:

- transcription adapter interface
- local transcription option
- cloud transcription option
- transcript artifact writer
- transcript review flow contract

Acceptance:

- bridge can produce `transcript.md` from a WAV file
- provider failures are recoverable
- firmware can show transcript before planning

Validation:

- transcribe known sample
- compare expected words roughly

## Task A11: Agent Pack Export

Suggested role: `writer`

Objective:

Create the final export format that another agent can execute.

Deliverables:

- `agent-pack.md`
- `agent-tasks.json`
- `README.md` inside export folder
- optional bridge workspace copier

Acceptance:

- fresh agent can understand project without chat history
- task dependencies are explicit
- validation instructions are concrete

Validation:

- dry-run handoff review by a verifier/critic
- parse JSON successfully

## Task A12: UX Review And Tiny Screen Polish

Suggested role: `designer`

Objective:

Make the app usable on a 240 x 135 px display.

Deliverables:

- route map
- keybinding map
- status/footer layout
- generated-output review screen design
- error state copy

Acceptance:

- no screen depends on dense paragraph reading
- common actions fit visible footer labels
- long content can be paged/scrolled

Validation:

- screenshot/mock review
- hardware visual pass when available

## Task A13: Verification Harness

Suggested role: `test-engineer`

Objective:

Make repeated agent work safe.

Deliverables:

- host-testable parsers and schemas
- build command docs
- smoke checklist
- regression fixture set

Acceptance:

- every non-hardware data transform has a host test
- every release has build plus SD/no-SD smoke checklist
- bridge tests can run without live AI provider

Validation:

- `pio run`
- bridge unit tests
- documented manual hardware smoke

## Task A14: Documentation And Release Notes

Suggested role: `writer`

Objective:

Keep future users and agents aligned.

Deliverables:

- firmware README
- bridge README
- install/run guide
- known limitations
- release checklist

Acceptance:

- install path is exact
- bridge setup is exact
- limitations are honest about voice, AI, reminders, and cloud credentials

Validation:

- fresh-read review by a separate agent

## Recommended Swarm Order

1. A01 Firmware Skeleton
2. A02 Storage Contract
3. A03 Text Capture And Project Browser
4. A04 Local Tasks
5. A05 Calendar And Reminders Base
6. A06 Bridge Protocol Fixture
7. A07 Planner Schemas And Templates
8. A08 Text-To-Plan Bridge
9. A11 Agent Pack Export
10. A12 UX Review And Tiny Screen Polish
11. A13 Verification Harness
12. A09 Voice Recording Spike
13. A10 Speech-To-Text Bridge
14. A14 Documentation And Release Notes

Voice starts after the text-to-plan loop because the hardest product value is the planning/export contract. Audio is important, but it should not block the first useful build.
