# Roadmap

## Alpha 0.1: Text-To-Plan Loop

Goal: prove the core value without voice or cloud lock-in.

Scope:

- firmware skeleton
- SD folder contract
- text idea capture
- project browser
- local task list
- local calendar events
- bridge fixture import
- generated artifact review UI
- agent pack export

Exit criteria:

- user can type a messy idea on the Cardputer
- the idea persists under `/advdeck/projects/<slug>/`
- bridge fixture returns `brief.md`, `plan.md`, `tasks.json`, and `agent-prompt.md`
- generated files import and display on-device
- exported agent pack can be understood without chat history

## Alpha 0.2: Real Planner Bridge

Goal: replace fixture output with a real planner adapter.

Scope:

- bridge CLI/service
- local dry-run provider
- first live LLM provider adapter
- schema validation
- recoverable invalid-output errors
- artifact writer

Exit criteria:

- `idea.md` becomes a valid project plan and task pack
- invalid AI output is caught
- raw AI output is logged separately
- firmware receives clean success/error states

## Alpha 0.3: Calendar Intelligence

Goal: make plans actionable over time.

Scope:

- generated calendar suggestions
- accept/reject flow
- local reminders while app is running
- `.ics` export
- manual event editor polish

Exit criteria:

- accepted suggestions become local events
- exported `.ics` opens in normal calendar apps
- docs are explicit about reminder limits

## Alpha 0.4: Voice Capture

Goal: support rough spoken idea dumps.

Scope:

- ES8311 recording spike
- WAV/PCM save to SD
- transcription bridge adapter
- transcript review flow
- transcript-to-plan pipeline

Exit criteria:

- 15, 60, and 180 second recording attempts are documented
- at least one stable voice path works end-to-end
- transcript can be edited before planning

## Beta 0.5: Agent Workflow Polish

Goal: make the output genuinely useful to other agents.

Scope:

- stronger task schema
- role suggestions
- dependency ordering
- validation checklist per task
- workspace export from bridge
- optional GitHub issue payload export

Exit criteria:

- a fresh coding agent can start from `agent-pack.md`
- task JSON validates
- handoff contains no hidden context dependency

## Later

- BLE HID snippet export
- WebUI over SoftAP
- local workspace watcher
- companion C5 status panel
- LoRa/GPS project metadata
- optional issue tracker integrations
- optional secure sync
