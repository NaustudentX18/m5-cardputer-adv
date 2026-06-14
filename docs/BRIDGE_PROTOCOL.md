# Bridge Protocol

Status: draft.

## Goal

Move files between the Cardputer firmware and an off-device bridge service without coupling the firmware to any AI provider.

## First Transport

The first implementation may use either:

- USB serial command/file protocol
- local file sync during development

The data contract should remain the same regardless of transport.

## Pending Request

`/advdeck/outbox/pending.jsonl`

Example:

```json
{"id":"req-20260614-001","project":"pocket-agent","type":"plan_project","inputs":["idea.md"],"created_at":"2026-06-14T13:00:00+10:00","status":"pending"}
```

## Result Manifest

Example:

```json
{
  "request_id": "req-20260614-001",
  "status": "ok",
  "artifacts": [
    "brief.md",
    "plan.md",
    "tasks.json",
    "tasks.md",
    "agent-prompt.md"
  ],
  "warnings": []
}
```

## Error Manifest

Example:

```json
{
  "request_id": "req-20260614-001",
  "status": "error",
  "error_code": "invalid_ai_output",
  "message": "Planner output did not match tasks schema.",
  "retryable": true
}
```

## Import Rule

Firmware imports only validated artifacts. Raw provider output belongs in logs and must not replace accepted project files.
