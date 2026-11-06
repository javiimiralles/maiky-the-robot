# v1 — AI conversation

Objective: hold Button 1, speak, release, and Maiky answers through the speaker using free AI services. Branch: `v1`.
Delivery: `ask-on-risk` (forecast ~550 authored lines, chain strategy pending before any PR). Runners: `npm test` (vitest) in `relay/`; `pio run` build for the firmware (no on-device test runner).

## Specs

- **S1 — Talk.** "La idea es conectarlo a una IA y poder conversar con él." Hold Button 1 and speak; on release Maiky shows "Thinking...", then plays the AI answer through the speaker. The conversation language is Spanish (assumption: the user speaks Spanish).
- **S2 — Free AI.** "Que opciones tengo de IAs gratis a las que conectar al bicho?" Use the Cloudflare Workers AI free plan (10,000 neurons/day): Whisper for speech-to-text and `@cf/google/gemma-4-26b-a4b-it` for the reply. Text-to-speech: "usa voiceStudio, me da igual depender del pc" — the relay calls the local VoiceStudio (https://github.com/debpalash/VoiceStudio) HTTP API on the PC, at a configurable URL, with a Spanish voice.
- **S3 — PC now, own server later.** "de momento maiky funcionará con el pc encendido" and "en un futuro el servidor que use será uno propio (home server) y ya lo pondré ahi todo". The relay is a Cloudflare Worker in `relay/`, run on the PC with `wrangler dev` and reachable from the LAN. Later the relay and VoiceStudio move to the user's home server; the firmware then only changes the relay address, and the relay only changes the VoiceStudio URL.
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
| T2 | Firmware: Wi-Fi + streamed talk flow + MP3 playback | S1, S4, S5, S6, S7 | delegated (writer trigger: parallel unit, disjoint surface `src/`, `platformio.ini`, `.gitignore`) | done (hardware test pending, by the user) | Test-first exception: no on-device runner, network/hardware I/O. `pio run` SUCCESS, RAM 15.2%, Flash 85.7%; `src/secrets.h` git-ignored. Commit: firmware commit on `v1` |
| T3 | Docs: README v1 section, repo layout rule in CLAUDE.md/AGENTS.md | S3, S6 | inline (parent, shared files) | done | README relay section + layout rule in relay commit; README v1 usage, status table and secrets step in firmware commit |
| T5 | Relay TTS step through the local VoiceStudio API (replaces MeloTTS) | S2, S3, S5, S6 | inline (small relay change) | done | RED 3 failing → GREEN 13/13; typecheck OK; `wrangler deploy --dry-run` lists the VOICESTUDIO vars; `pio run` SUCCESS. Commit: VoiceStudio commit on `v1` |
| T4 | Independent verification of the feature | all | delegated (verification trigger: Risk items 2 and 5, token guard and new dependencies) | done | All S# PASS, no blockers; advisory A-1 fixed in the relay fix commit (RED 1 failing → GREEN 13/13) |

## Log

- **L1 (2026-11-06, user):** "vamos a comenzar con la versión 1 de maiky. En principio no se necesita ninguna actualización de hardware. La idea es conectarlo a una IA y poder conversar con él. Que opciones tengo de IAs gratis a las que conectar al bicho?"
- **L2 (2026-11-06, user):** "de momento maiky funcionará con el pc encendido, pero en un futuro la idea es que esto no sea necesario. Igualmente más adelante se cambirá el esp32 por un esp32-s3"
- **L3 (2026-11-06, user):** "sí planifica la v1"
- **L4 (2026-11-06, user):** "empieza a implementar el plan. Lanza diferentes agentes para cada tarea si lo ves necesario"
- **L5 (evidence):** Workers AI bindings always run remotely, even under `wrangler dev`, and use the free pool. MeloTTS outputs MP3 only (~18.6 neurons per audio minute). `aura-2-es` can output MP3 or linear16 but costs 2,727 neurons per 1k characters (~20 replies/day). The 2026-07-28 changelog made only kimi-k2.6, kimi-k2.7-code and glm-5.2 paid-only. v0 keeps ~3 s of audio in heap (no PSRAM), so v1 streams the upload.
- **L6 (decision):** The plan's firmware tasks (Wi-Fi, talk flow) merged into T2 because they share `src/main.cpp` and form one behavior. MP3 is the response format so the TTS can change inside the relay only.
- **L7 (evidence, T1):** The MeloTTS model page lists "Planned Deprecation | 11/15/2026" with no replacement named. The other Workers AI TTS models are Deepgram partner models: `aura-2-es` (Spanish, $0.03 per 1k characters), `aura-2-en`, `aura-1`. The relay already accepts binary MP3 from Aura, so the swap is `TTS_MODEL` + `ttsInput`. Not yet verified live: Whisper `language: "es"`, MeloTTS `lang: "es"`, `Content-Length` on the 200 MP3 response.
- **L8 (T3 partial):** README "Run the relay" section and the repository-layout rule in CLAUDE.md/AGENTS.md go with the relay commit; the README usage and secrets steps go with the firmware commit.
- **L9 (evidence, T2):** MP3 decoding uses pschatzmann/arduino-libhelix pinned to git tag `v0.9.4` (not in the PlatformIO registry; decoder only, no I2S driver; GPLv3). Unverified until the hardware test: free heap during a turn, loop-task stack depth during decoding, Wi-Fi upload stalls longer than the 128 ms mic buffer. Flash is at 85.7%, so later versions may need a larger app partition.
- **L10 (evidence, T4):** Independent verifier on `5d45313` + `45e9b17`: S1–S7 and A1 PASS, no blockers. 44 extra relay probes, live local workerd checks (`Content-Length` set on the 200 MP3 response), a byte-level emulation of the firmware request, and a static firmware review against arduino-esp32 3.3.12. Advisories: A-1 TTS object without audio returned 200 `[object Object]` (fixed); A-2 under `wrangler dev` a 401 only arrives after the upload ends, because wrangler drains the request body first (harmless); A-3 a deployed Worker over HTTPS needs TLS on the ESP32 (already out of v1); A-5 a very short tap shows "Error 400"; A-6 extra compiler errors follow the missing-secrets `#error`; A-7 flash at 85.7%.
- **L11 (2026-11-06, user):** "investiga un tts con más cuota gratis" — researched Azure Speech F0 (0.5M chars/month, throttles instead of billing) and Google Cloud TTS (4M/1M chars/month, needs billing).
- **L12 (2026-11-06, user):** "no se puede usar https://github.com/debpalash/VoiceStudio"
- **L13 (2026-11-06, user):** "usa voiceStudio, me da igual depender del pc, en un futuro el servidor que use será uno propio (home server) y ya lo pondré ahi todo" — S2 and S3 rewritten; T5 added for the relay TTS step.
- **L14 (evidence, T5):** VoiceStudio (commit 12e6578) serves `POST http://127.0.0.1:3900/v1/audio/speech` (OpenAI-compatible, `backend/api/routers/openai_compat.py:150,649`); `response_format` includes mp3; raw audio bytes come back; `tts-1` is an alias for the active engine; loopback callers need no key ("loopback-only and unauthenticated by default", `docs/api-auth.md:6`); the desktop app binds 127.0.0.1 only; OmniVoice outputs 24 kHz mono and supports `es`; the active engine's licence must be accepted in the app. A `wrangler dev` Worker reached a 127.0.0.1 service in a scratch test ("upstream 200"). The firmware answer timeout went from 30 s to 60 s because VoiceStudio renders the whole clip first and is slow on CPU. Not verified live: a full turn, VoiceStudio latency on this PC, voice consistency with `"default"`.
- **Next:** native review, then the user's hardware test (VoiceStudio open, `wrangler login`, `npm run dev`, flash).
