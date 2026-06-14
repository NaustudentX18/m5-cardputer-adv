# 0001: Use An Off-Device AI Bridge

Date: 2026-06-14

## Status

Accepted.

## Context

The Cardputer-Adv is an ESP32-S3 device with a small display, keyboard, SD storage, and audio hardware. It is strong as a capture and control surface, but heavyweight speech-to-text and LLM planning are better handled off-device.

## Decision

Use an off-device bridge service for:

- speech-to-text
- LLM planning
- calendar export
- agent pack generation
- provider credentials

The firmware owns:

- capture
- storage
- review
- local tasks/calendar
- bridge queue
- export

## Consequences

Positive:

- device remains useful offline
- secrets stay off-device
- AI provider can change without reflashing firmware
- tests can use dry-run providers

Tradeoffs:

- voice-to-plan requires a bridge
- sync state must be handled carefully
- first implementation needs both firmware and bridge surfaces
