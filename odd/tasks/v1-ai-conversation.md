# v1 — AI conversation

Objective: hold Button 1, speak, release, and Maiky answers through the speaker using free AI services. Branch: `v1`.
Delivery: `ask-on-risk` (forecast ~550 authored lines, chain strategy pending before any PR). Runners: `npm test` (vitest) in `relay/`; `pio run` build for the firmware (no on-device test runner).

## Specs

- **S1 — Talk.** "La idea es conectarlo a una IA y poder conversar con él." Hold Button 1 and speak; on release Maiky shows "Thinking...", then plays the AI answer through the speaker. The conversation language is Spanish (assumption: the user speaks Spanish).
- **S2 — Free AI.** "Que opciones tengo de IAs gratis a las que conectar al bicho?" Use the Cloudflare Workers AI free plan (10,000 neurons/day): Whisper for speech-to-text, `@cf/google/gemma-4-26b-a4b-it` for the reply, `@cf/myshell-ai/melotts` for text-to-speech. The TTS model is one constant so it can be swapped (e.g. `@cf/deepgram/aura-2-es` with MP3 output) without touching the firmware.
- **S3 — PC now, standalone later.** "de momento maiky funcionará con el pc encendido, pero en un futuro la idea es que esto no sea necesario." The relay is a Cloudflare Worker in `relay/`, run on the PC with `wrangler dev` and reachable from the LAN. The same code must deploy later with `wrangler deploy`; the firmware then only changes the relay address.
- **S4 — No new hardware.** "En principio no se necesita ninguna actualización de hardware." Reuse the pins in `COMPONENTS.md`; that file does not change.
- **S5 — Relay contract.** `POST /talk` with `Authorization: Bearer <RELAY_TOKEN>`; body is raw PCM, signed 16-bit little-endian, mono, 16 kHz, possibly sent with `Transfer-Encoding: chunked`; at most 15 s (480,000 bytes).
  - Wrong or missing token: `401`. Empty or odd-length body: `400`. Body over the limit: `413`. Nothing transcribed: `204`. AI failure: `502`. Errors are `text/plain`.
  - Success: `200`, `Content-Type: audio/mpeg`, MP3 body with `Content-Length`.
- **S6 — Secrets.** Wi-Fi credentials, relay address and token live in git-ignored files (`src/secrets.h`, `relay/.dev.vars`), with committed examples.
- **S7 — Future ESP32-S3.** "más adelante se cambirá el esp32 por un esp32-s3" — out of v1. Keep pin numbers in one place in the firmware (already true).
- **Assumption A1.** No conversation memory in v1: every question is independent (the user was asked and moved on to implementation; adding memory later is a relay-only change).
- **Out of v1.** Deploying the relay / HTTPS on the ESP32, ESP32-S3 port, wake word.

## Tasks

| ID | Task | Specs | Route | Status | Evidence |
|----|------|-------|-------|--------|----------|
| T1 | Relay Worker in `relay/` with tests | S2, S3, S5, S6 | delegated (writer trigger: parallel unit, disjoint surface `relay/**`) | done | RED 12/12 failing on a 501 stub → GREEN 12/12; typecheck OK; `wrangler deploy --dry-run` OK; local workerd smoke test (401/400/413/404/405). Commit: relay commit on `v1` |
| T2 | Firmware: Wi-Fi + streamed talk flow + MP3 playback | S1, S4, S5, S6, S7 | delegated (writer trigger: parallel unit, disjoint surface `src/`, `platformio.ini`, `.gitignore`) | pending | |
| T3 | Docs: README v1 section, repo layout rule in CLAUDE.md/AGENTS.md | S3, S6 | inline (parent, shared files) | pending | |
| T4 | Independent verification of the feature | all | delegated (verification trigger: Risk item 5, new dependencies) | pending | |

## Log

- **L1 (2026-11-06, user):** "vamos a comenzar con la versión 1 de maiky. En principio no se necesita ninguna actualización de hardware. La idea es conectarlo a una IA y poder conversar con él. Que opciones tengo de IAs gratis a las que conectar al bicho?"
- **L2 (2026-11-06, user):** "de momento maiky funcionará con el pc encendido, pero en un futuro la idea es que esto no sea necesario. Igualmente más adelante se cambirá el esp32 por un esp32-s3"
- **L3 (2026-11-06, user):** "sí planifica la v1"
- **L4 (2026-11-06, user):** "empieza a implementar el plan. Lanza diferentes agentes para cada tarea si lo ves necesario"
- **L5 (evidence):** Workers AI bindings always run remotely, even under `wrangler dev`, and use the free pool. MeloTTS outputs MP3 only (~18.6 neurons per audio minute). `aura-2-es` can output MP3 or linear16 but costs 2,727 neurons per 1k characters (~20 replies/day). The 2026-07-28 changelog made only kimi-k2.6, kimi-k2.7-code and glm-5.2 paid-only. v0 keeps ~3 s of audio in heap (no PSRAM), so v1 streams the upload.
- **L6 (decision):** The plan's firmware tasks (Wi-Fi, talk flow) merged into T2 because they share `src/main.cpp` and form one behavior. MP3 is the response format so the TTS can change inside the relay only.
- **L7 (evidence, T1):** The MeloTTS model page lists "Planned Deprecation | 11/15/2026" with no replacement named. The other Workers AI TTS models are Deepgram partner models: `aura-2-es` (Spanish, $0.03 per 1k characters), `aura-2-en`, `aura-1`. The relay already accepts binary MP3 from Aura, so the swap is `TTS_MODEL` + `ttsInput`. Not yet verified live: Whisper `language: "es"`, MeloTTS `lang: "es"`, `Content-Length` on the 200 MP3 response.
- **L8 (T3 partial):** README "Run the relay" section and the repository-layout rule in CLAUDE.md/AGENTS.md go with the relay commit; the README usage and secrets steps go with the firmware commit.
- **Next:** finish T2, then T3, then T4. Open decision for the user: TTS after MeloTTS is retired on 2026-11-15.
