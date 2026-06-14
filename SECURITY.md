# Security

## Supported Versions

This project is pre-alpha. Security reports should target the current `main` branch unless releases are created later.

## Reporting

Open a private security advisory on GitHub if available. If not, open an issue with minimal exploit detail and mark it clearly as security-related.

## Sensitive Areas

- bridge provider credentials
- local project files containing private ideas
- generated agent prompts that may include sensitive context
- future calendar sync credentials
- future workspace export paths

## Rules

- Never store cloud API keys in firmware.
- Keep `.env` files out of git.
- Prefer bridge-local credentials.
- Redact private project content from issues unless needed for debugging.
- Treat generated plans as untrusted until validated and reviewed.
