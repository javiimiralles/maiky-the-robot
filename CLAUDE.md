# Maiky the Robot

Desktop companion robot built on an ESP32. It listens to the user, sends the conversation to an AI service, and speaks the answer back. Features are added incrementally across versions.

## Versioning

- Each version lives in its own git branch named `vX.X` (e.g. `v0.1`, `v0.2`).
- Work for a version stays in its branch. Do not mix features from different versions.
- Use Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`...).

## Hardware

- The components used in the current version are listed in `COMPONENTS.md`. Treat it as the source of truth.
- Do not assume a component or pin exists unless it is in `COMPONENTS.md`. When the hardware changes, update that file in the same commit.

## Code and docs

- All code, comments, and documentation are written in English.
- Never commit secrets (Wi-Fi credentials, API keys). Keep them in a git-ignored config file.

## Maintaining this file

- Update this file whenever a new convention is agreed or a recurring way of working emerges, so it learns from how the project is actually built.
- Keep it short: only conventions and rules, no tutorials or version-specific details. Remove rules that no longer apply.
- `CLAUDE.md` and `AGENTS.md` must always be identical. Any change to one is applied to the other in the same commit.
