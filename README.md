# Maiky the Robot

Desktop companion robot built on an ESP32. Hardware and wiring: [COMPONENTS.md](COMPONENTS.md).

## v1 — talk to Maiky

![Maiky: ESP32 wired to a breadboard with the amplifier, speaker, three buttons and the OLED showing its face](images/maiky.jpeg)

- **Button 1** (hold): ask something; release to hear the answer. The screen shows "Listening...", "Thinking..." and "Speaking...".
- **Button 2**: play a 440 Hz test tone.
- **Button 3**: cycle the volume (5%, 10%, 20%).

Each question is independent: Maiky does not remember earlier turns yet. Needs the [relay](#run-the-relay) running.

| Screen | Meaning |
|--------|---------|
| No Wi-Fi | Could not join the network in `src/secrets.h`. Button 1 retries. |
| No relay | The relay is not running or not reachable at `RELAY_HOST:RELAY_PORT`. |
| Bad token | `RELAY_TOKEN` differs between `src/secrets.h` and `relay/.dev.vars`. |
| Didn't hear | The relay found no speech in the recording. |
| Error &lt;code&gt; | The relay answered with that HTTP status (502 = an AI service failed). |
| No memory | Not enough RAM to decode the answer. |

## Run the relay

Maiky talks to the AI through a small Cloudflare Worker in [relay/](relay/). It turns your voice into text (Whisper), asks a chat model (Gemma) for an answer, and sends the answer back as speech (MeloTTS). For now it runs on your PC and uses the Workers AI free plan (10,000 neurons per day).

Requires [Node.js](https://nodejs.org/) and a free Cloudflare account.

```sh
cd relay
npm install
cp .dev.vars.example .dev.vars   # then set RELAY_TOKEN to a long random string
npx wrangler login               # once
npm run dev                      # serves POST /talk on port 8787 on every network interface
npm test                         # unit tests, no Cloudflare account needed
```

The AI models always run on Cloudflare, even under `npm run dev`, so every turn uses the free daily quota. If the ESP32 cannot reach the relay, allow port 8787 in the PC firewall.

## Build and flash

Requires [PlatformIO](https://platformio.org/).

```sh
cp src/secrets.example.h src/secrets.h   # then fill in Wi-Fi, the PC's LAN IP and the relay token
pio run -t upload      # build and flash
pio device monitor     # serial log (115200 baud)
```
