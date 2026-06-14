# AdvDeck Agent PRD

Generated: 2026-06-14

## Summary

AdvDeck Agent is a keyboard-first, voice-assisted project capture and planning tool for the M5Stack Cardputer-Adv.

The product goal is a pocket "Notion AI for builders": capture rough ideas anywhere, turn them into structured project notes, tasks, calendar items, and agent-ready handoff prompts, then export everything as plain files that another coding agent can execute.

## Product Decision

Build AdvDeck Agent as the next tier above AdvDeck Notes, not as a separate disconnected firmware.

AdvDeck Notes remains the offline base:

- notes
- snippets
- SD-backed files
- keyboard-first navigation
- safe USB/HID export later

AdvDeck Agent adds:

- project idea inbox
- long-form idea dump capture
- voice recording and transcription through a bridge service
- AI plan generation through a bridge service
- task and calendar extraction
- agent handoff export
- review/approve flows on the Cardputer

## Target User

- Builder with ideas away from a laptop.
- Agent-heavy developer who wants a pocket capture device that can create clean task prompts.
- M5 Cardputer Adv owner who wants a useful daily-driver app instead of another demo or attack-focused firmware.
- Field user who wants offline capture first, then optional sync/AI processing when a trusted bridge is available.

## Core Promise

The user can rattle off or type a messy idea, then AdvDeck Agent turns it into:

- a clean project brief
- an actionable plan
- a task list with acceptance criteria
- calendar/reminder suggestions
- a ready-to-feed prompt for a coding agent swarm

The Cardputer never needs AI to boot or capture. AI is an enhancement, not a hard dependency.

## Main Flows

### Text Idea Dump

1. User opens `Capture`.
2. User types a long rough idea.
3. App saves it to a project inbox file.
4. User chooses `Plan`.
5. Bridge service receives the idea file.
6. Bridge returns a project brief, task breakdown, calendar suggestions, and an agent prompt.
7. User reviews summary on the Cardputer and accepts, edits, or rejects.

### Voice Idea Dump

1. User holds a push-to-talk key.
2. Cardputer records short audio to SD.
3. App queues the recording for transcription.
4. Bridge service transcribes the audio.
5. Bridge runs the same planning pipeline as text capture.
6. App stores transcript, plan, tasks, and handoff prompt under the project folder.

### Calendar And Reminders

1. User captures "Remind me tomorrow to test the build" or adds a calendar item manually.
2. App stores a local event in `events.json`.
3. Bridge can optionally export `.ics` or sync to a larger calendar system.
4. Cardputer shows upcoming items and can alert while the app is running.

Reliable background alerts during deep sleep or power-off are a later hardware validation item, not an MVP promise.

### Agent Handoff

1. User opens a planned project.
2. User selects `Export Agent Pack`.
3. App writes Markdown and JSON files to SD.
4. Bridge can copy the pack to a workspace, create local files, or later open issues.

The first handoff format should be simple enough for Codex, Claude Code, or another agent runner to consume without custom tooling.

## On-Device App Shape

Home menu:

- `C` Capture
- `I` Inbox
- `P` Projects
- `K` Calendar
- `A` Agent Packs
- `S` Settings
- `D` Device

Capture modes:

- Text idea
- Voice idea
- Quick task
- Calendar event
- Snippet/note

Project views:

- Raw idea
- Transcript
- Brief
- Plan
- Tasks
- Calendar suggestions
- Agent prompt
- Export status

## Storage Model

Use plain files on SD so data remains inspectable, recoverable, and syncable.

```text
/advdeck/
  config.json
  inbox/
    idea-YYYYMMDD-HHMMSS.md
    voice-YYYYMMDD-HHMMSS.wav
  projects/
    project-slug/
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
  calendar/
    events.json
    exports/
      project-slug.ics
  outbox/
    pending.jsonl
  logs/
```

## Bridge Service

The bridge service is the AI/runtime companion running on a laptop, phone, Pi, home server, or cloud VM.

Bridge responsibilities:

- receive files from Cardputer over USB serial, Wi-Fi SoftAP, or local network
- transcribe audio
- call an LLM or local model
- structure idea dumps into project artifacts
- generate calendar suggestions
- generate agent handoff packs
- sync files back to SD/app

Bridge should be provider-adapter based:

- local transcription adapter
- cloud transcription adapter
- local LLM adapter
- cloud LLM adapter
- calendar export adapter
- future issue-tracker adapter

Do not hardwire one AI provider into the firmware.

## Firmware Responsibilities

The Cardputer firmware owns:

- keyboard-first UI
- SD file storage
- project browser
- text editor
- audio recording shell
- event/reminder list
- bridge queue
- review/approve actions
- safe export

The firmware should not own:

- heavyweight speech-to-text
- long-running AI inference
- API key storage for cloud services
- complex calendar cloud auth
- internet-only workflows

## AI Output Contract

Every generated project should include:

- `brief.md`: concise project description
- `plan.md`: ordered implementation roadmap
- `tasks.json`: machine-readable task list
- `tasks.md`: human-readable task list
- `calendar-suggestions.json`: optional dates/reminders
- `agent-prompt.md`: one-shot prompt for a coding agent or swarm leader

Each task in `tasks.json` should include:

- `id`
- `title`
- `objective`
- `context`
- `files_or_modules`
- `acceptance_criteria`
- `validation`
- `dependencies`
- `risk`
- `suggested_agent_role`

## MVP

The MVP is successful when:

- device boots without Wi-Fi, cloud, SD errors, or AI dependency
- user can type a long idea and save it as a project
- user can create/edit local tasks and calendar items
- bridge can read a text idea and return a plan, tasks, and agent prompt
- generated artifacts are saved under the project folder
- user can review and accept/reject generated artifacts
- user can export an agent pack to SD
- all output is useful as plain Markdown/JSON even if the app is never opened again

## Non-Goals For MVP

- No fully local on-device LLM.
- No fully local on-device high-quality speech-to-text.
- No cloud calendar sync.
- No automatic issue creation.
- No background reminder guarantee while powered off.
- No RF companion dependency.
- No API keys stored in firmware.

## Safety And Trust

- AI suggestions are never auto-applied without review.
- Calendar events are suggestions until accepted.
- Agent handoff packs are written locally first; external submission is later.
- Secrets stay on the bridge, not the Cardputer.
- The device must remain useful offline.
- All generated files should include source references back to the raw idea or transcript.

## Open Questions For Prototype

- Audio API path for ES8311 recording quality and buffering.
- Maximum reliable WAV recording length before SD/heap issues.
- Best first bridge transport: USB serial file protocol or Wi-Fi SoftAP WebUI.
- Whether reminders can use reliable low-power wake on this unit.
- Best compact UI pattern for reviewing generated tasks on 240 x 135 px.
