# Product

## Name

AdvDeck Agent

## One-Line Pitch

A pocket Notion-AI-style project capture and planning deck for the M5Stack Cardputer-Adv.

## Problem

Builders often get useful ideas away from their main workstation. A phone can record notes, but it does not naturally produce agent-ready build plans. Existing Cardputer firmware is strong for demos, launchers, and specialist tools, but there is room for a polished daily-driver productivity app built around the keyboard, SD card, audio hardware, and tiny screen.

## Solution

AdvDeck Agent turns the Cardputer-Adv into a capture device and review console. It stores everything offline as plain files, then uses a trusted bridge service to transform rough input into structured project artifacts.

## Core User Stories

- As a builder, I can type a rough idea and save it as a project.
- As a builder, I can later speak a rough idea and have it transcribed.
- As a builder, I can turn an idea into a brief, plan, task list, and agent prompt.
- As a builder, I can add tasks and calendar items manually.
- As a builder, I can accept or reject AI suggestions before they modify my project.
- As an agent operator, I can export a project pack that another agent can execute without hidden context.

## MVP

AdvDeck Agent Alpha 0.1:

- text idea capture
- SD project folders
- local tasks
- local calendar events
- bridge fixture import
- generated artifact review
- agent pack export

## Non-Goals For MVP

- fully local on-device LLM
- fully local high-quality on-device speech-to-text
- cloud calendar sync
- automatic GitHub issue creation
- powered-off reminder guarantee
- companion C5/radio dependency

## UX Direction

Home menu:

- `C` Capture
- `I` Inbox
- `P` Projects
- `K` Calendar
- `A` Agent Packs
- `S` Settings
- `D` Device

Design rules:

- first screen is the working app
- keyboard mnemonics over touch-style navigation
- status bar always shows battery, SD, bridge, and time when available
- review generated output task-by-task
- preserve raw input forever
