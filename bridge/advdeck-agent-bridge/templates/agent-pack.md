{# agent-pack.md.j2
   Agent pack for project: garden-watering
   Variables: project_slug, project_title, idea, brief, plan, tasks, calendar_suggestions
   The .md (frozen) copy is what the Phase 2 dry-run provider actually substitutes.
   Phase 3 (A08) will render this template via Jinja2 against real LLM output. #}
# Garden Watering Reminder

> Project slug: `garden-watering`
> This pack is a self-contained handoff for an external coding agent.
> Read it top-to-bottom. It is the only authoritative source for this project.

---

## 1. Idea

I want a small device that tells me when the tomatoes in the back garden need water. I have a moisture sensor lying around and I keep forgetting to check on weekends.

---

## 2. Brief

Build a Cardputer-Adv app that reads a soil moisture sensor once per hour, shows a watering-needed indicator on the home screen, and logs a daily summary to the SD card. Stretch goal: push the daily summary to the outbox so the bridge can sync it.

---

## 3. Plan

1. Wire the capacitive moisture sensor to a free GPIO and read it via ADC.
2. Add a `moisture` task group with a 1-hour poll loop.
3. Render a low-priority banner on the home screen when the reading is below the dry threshold for two consecutive polls.
4. Append a one-line summary to `logs/moisture-<YYYY-MM-DD>.jsonl` at local midnight.
5. Add a host test that simulates the ADC and asserts the banner state.

---

## 4. Tasks

The full task list lives in `tasks.json` next to this file. Below is a human-readable
view of the same data; trust `tasks.json` as the source of truth.

- [ ] wire-moisture-sensor: Connect the sensor to GPIO 32 and read via ADC1_CH4.
- [ ] banner-ui: Show 'water tomatoes' banner when dry for 2 polls.
- [ ] daily-log: Append daily min/avg/max to logs/moisture-<date>.jsonl.
- [ ] host-test: Drive the loop with a mock ADC and assert banner state.

---

## 5. Calendar suggestions

The bridge also produced `calendar-suggestions.json` with time-boxed reminders that
correspond to the task list. They are *suggestions* — the user has not accepted them
in Phase 2.

- 2026-06-21 09:00 — Check moisture sensor calibration (15 min)
- 2026-06-22 09:00 — First field test of watering banner

---

## 6. Working agreements

- Do not invent requirements not present in the brief, plan, or tasks.
- If a task's `acceptance_criteria` is ambiguous, ask before implementing.
- Prefer the smallest change that satisfies the task's `validation` list.
- Match existing style in `files_or_modules`; do not refactor adjacent code.
- Run the project's existing tests before declaring a task done.
