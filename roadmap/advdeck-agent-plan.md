# AdvDeck Agent Plan Of Attack

Generated: 2026-06-14

## Objective

Turn the Cardputer-Adv into a pocket agent front-end: capture rough ideas by keyboard or voice, structure them into actionable project plans, and export task packs that coding agents can execute.

This should start from AdvDeck Notes instead of replacing it. Notes, snippets, SD storage, and keyboard editing become the foundation for the agent layer.

## Architecture

### 1. Cardputer Firmware

Runs on the M5Stack Cardputer-Adv.

Responsibilities:

- boot into a fast offline UI
- capture text ideas
- record voice clips to SD
- manage projects, notes, tasks, and reminders
- queue files for bridge processing
- display generated summaries and tasks
- let the user accept, edit, reject, or export generated artifacts

Recommended stack:

- PlatformIO
- Arduino framework
- `M5Cardputer`
- `M5Unified`
- `M5GFX`
- SD/FS APIs
- direct `M5GFX` UI first, LVGL only if widgets become worth the overhead

### 2. Bridge Service

Runs off-device on a laptop, Pi, phone, server, or cloud VM.

Responsibilities:

- receive project files from Cardputer
- transcribe audio
- call LLM planner
- generate structured files
- send results back
- optionally export `.ics`, local workspace files, or issue payloads

First implementation should be a local CLI/service named `advdeck-agent-bridge`.

Recommended bridge shape:

```text
bridge/
  advdeck_bridge/
    server.py
    file_protocol.py
    transcription/
      base.py
      local_whisper.py
      cloud_stt.py
    planner/
      base.py
      llm_planner.py
      templates/
        project_plan.md
        agent_pack.md
    calendar/
      parser.py
      ics_export.py
    schemas/
      tasks.schema.json
      calendar.schema.json
```

### 3. Data Contract

The firmware and bridge communicate through plain files first. This keeps debugging simple and avoids coupling the app to any one AI vendor.

Minimum contract:

- input: `idea.md`
- optional input: `transcript.md`
- optional input: `recording.wav`
- output: `brief.md`
- output: `plan.md`
- output: `tasks.json`
- output: `tasks.md`
- output: `calendar-suggestions.json`
- output: `agent-prompt.md`

### 4. Transport

Start with one transport and keep adapters small.

Best first candidates:

- USB serial command/file protocol
- Wi-Fi SoftAP WebUI file transfer

USB serial is likely simpler for agent-workspace integration. SoftAP WebUI is better for phone/laptop use. Build the queue and file contract so either transport can be added without changing the planning logic.

## Product Phases

### Phase 0: Reframe AdvDeck Notes Into AgentDeck Base

Outcome: one shared app foundation for notes, projects, snippets, and future agent workflows.

Work:

- define `projects/advdeck-agent/` or evolve `projects/advdeck-notes/`
- keep the existing notes/snippets MVP scope intact
- add project folder conventions
- add typed route names: Notes, Projects, Capture, Calendar, Agent Packs
- define build flags for experimental agent features

Validation:

- firmware skeleton builds
- no AI or network required to boot
- SD absent path still works

### Phase 1: Offline Project Capture

Outcome: useful without AI.

Work:

- project list
- new project from text idea
- edit raw idea
- local task list
- local calendar/reminder list
- export project folder to SD

Validation:

- create project
- power-cycle
- reopen project
- edit idea and tasks
- export files remain readable on a computer

### Phase 2: Bridge Protocol

Outcome: Cardputer can hand files to an off-device helper and receive results.

Work:

- define command protocol
- implement pending outbox queue
- implement bridge-side file receiver
- implement bridge-side file sender
- add firmware result import
- add sync status UI

Validation:

- send `idea.md` to bridge
- bridge returns static fixture files
- firmware imports fixture result
- failures leave queued files intact

### Phase 3: Text-To-Plan AI

Outcome: typed idea dumps become structured plans.

Work:

- bridge planner prompt templates
- schema validation for `tasks.json`
- deterministic artifact writer
- firmware review screen for generated outputs
- accept/reject generated artifacts

Validation:

- messy idea produces `brief.md`, `plan.md`, `tasks.json`, `tasks.md`, and `agent-prompt.md`
- invalid AI JSON is caught and returned as a recoverable error
- user can reject generated output without losing original idea

### Phase 4: Voice Capture And Transcription

Outcome: user can rattle off an idea and get the same plan pipeline.

Work:

- ES8311 recording proof
- push-to-talk capture flow
- WAV/PCM save to SD
- bridge transcription adapter
- transcript review screen
- transcript-to-plan pipeline

Validation:

- record 15, 60, and 180 second clips
- bridge transcribes a clip
- transcript is saved beside the recording
- user can edit transcript before planning

### Phase 5: Calendar And Reminder Intelligence

Outcome: plans can produce local reminders and calendar exports.

Work:

- manual event editor
- event list
- accepted calendar suggestions
- simple reminder alerts while app is running
- `.ics` export through bridge
- optional natural-language date extraction in bridge

Validation:

- manual event persists
- generated suggestion requires acceptance
- accepted event appears in local calendar
- `.ics` opens in a normal calendar app

### Phase 6: Agent Swarm Handoff

Outcome: a generated project can be executed by another agent with minimal rewrite.

Work:

- `agent-pack.md` template
- `agent-tasks.json` schema
- task dependency ordering
- role suggestion per task
- validation instructions per task
- optional workspace export folder on bridge

Validation:

- export contains enough context for a fresh agent
- each task has objective, scope, acceptance criteria, and validation
- no task depends on hidden chat context

### Phase 7: Companion Hardware Integration

Outcome: C5/radio/GPS hardware becomes a module, not a prerequisite.

Work:

- show C5 companion status in Device/Extensions
- attach LoRa/GPS metadata to notes/projects when available
- keep CC1101/nRF24 experiments isolated from agent MVP
- add region/safety gates before any transmit behavior

Validation:

- no companion hardware needed for AdvDeck Agent
- with companion connected, status handshake works
- missing companion fails quietly

## First Build Target

Build `AdvDeck Agent Alpha 0.1` with:

- text idea capture
- project folders
- local tasks
- local calendar events
- static bridge fixture import
- generated artifact review UI
- agent pack export

Do not start with voice. Get the text-to-plan and file contract working first, then add recording.

## Recommended Repo Layout

```text
projects/
  advdeck-agent/
    platformio.ini
    src/
      main.cpp
      app/
        routes.cpp
        capture.cpp
        projects.cpp
        calendar.cpp
        agent_packs.cpp
      platform/
        board.cpp
        keyboard.cpp
        display.cpp
        storage.cpp
        audio.cpp
      services/
        project_store.cpp
        task_store.cpp
        calendar_store.cpp
        bridge_queue.cpp
        export_pack.cpp
      ui/
        status_bar.cpp
        menu.cpp
        text_editor.cpp
        task_list.cpp
        modal.cpp
    test/
      host/
        task_schema_tests.cpp
        calendar_tests.cpp
        slug_tests.cpp
bridge/
  advdeck-agent-bridge/
    README.md
    pyproject.toml
    advdeck_bridge/
      cli.py
      file_protocol.py
      planner.py
      schemas/
      templates/
```

## Tiny-Screen UX Rules

- First screen is the working app, not a marketing splash.
- Use keyboard mnemonics instead of nested touch-style menus.
- Keep status bar constant: battery, SD, bridge, time.
- Use review screens with one action per key: accept, edit, reject, next.
- Do not show huge AI walls by default; show summary first, then drill down.
- Always preserve raw idea/transcript.

## Risk Register

| Risk | Mitigation |
| --- | --- |
| On-device STT is too heavy | Use bridge transcription first. |
| Audio capture API is rough | Prototype ES8311 recording before committing to voice UX. |
| AI output is invalid | Validate schemas, keep raw output logs, show recoverable errors. |
| Tiny screen makes plan review painful | Summary-first UI, task-by-task review, export full files to SD. |
| Cloud keys leak | Store secrets only on bridge; firmware stores no API keys. |
| Calendar reminders are unreliable in sleep | MVP only guarantees alerts while app is running. |
| SD corruption or missing SD | No-SD degraded mode and atomic write pattern. |
| Scope creep into radio firmware | Keep C5/radio work as Phase 7 extension. |

## Stop Condition For Alpha

Alpha is ready when a user can type a project idea on the Cardputer, process it through the bridge, review the generated plan/tasks on-device, and export an agent-ready pack from SD without any hidden chat context.
