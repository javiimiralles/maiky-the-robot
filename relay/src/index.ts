// Relay between Maiky (ESP32) and Cloudflare Workers AI: PCM in, spoken MP3 answer out.

const STT_MODEL = "@cf/openai/whisper-large-v3-turbo";
const CHAT_MODEL = "@cf/google/gemma-4-26b-a4b-it";
// To swap the voice, change TTS_MODEL and ttsInput together,
// e.g. "@cf/deepgram/aura-2-es" with { text, encoding: "mp3" }.
const TTS_MODEL = "@cf/myshell-ai/melotts";
const ttsInput = (text: string) => ({ prompt: text, lang: "es" });

const SYSTEM_PROMPT =
  "You are Maiky, a small and friendly desktop robot. " +
  "Always answer in Spanish, in one or two short sentences. " +
  "Your answer is read aloud by a text-to-speech engine, so write plain text only: " +
  "no markdown, no lists, no emojis.";

const SAMPLE_RATE = 16000;
const MAX_PCM_BYTES = 15 * SAMPLE_RATE * 2; // 15 s of 16-bit mono = 480,000 bytes

interface Env {
  AI: Ai;
  RELAY_TOKEN?: string;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    if (new URL(request.url).pathname !== "/talk") return text(404, "Not found");
    if (request.method !== "POST") return text(405, "Method not allowed", { Allow: "POST" });
    // Check the token before touching the body: unauthenticated uploads are never read.
    if (!(await tokenMatches(request.headers.get("Authorization"), env.RELAY_TOKEN))) {
      return text(401, "Unauthorized");
    }

    const pcm = await readBody(request, MAX_PCM_BYTES);
    if (!pcm) return text(413, `Audio longer than ${MAX_PCM_BYTES} bytes`);
    if (pcm.length === 0 || pcm.length % 2) return text(400, "Body must be non-empty 16-bit PCM");

    try {
      const stt = await env.AI.run(STT_MODEL, { audio: toBase64(toWav(pcm)), language: "es" });
      const transcript = stt.text.trim();
      console.log(`Transcript: ${transcript}`);
      if (!transcript) return new Response(null, { status: 204 });

      const chat = await env.AI.run(CHAT_MODEL, {
        messages: [
          { role: "system", content: SYSTEM_PROMPT },
          { role: "user", content: transcript },
        ],
        // Thinking is on by default for this model; it only adds latency for one-line answers.
        chat_template_kwargs: { enable_thinking: false },
      });
      const reply = chat.choices[0]?.message.content?.trim();
      if (!reply) throw new Error("Chat model returned no text");
      console.log(`Reply: ${reply}`);

      const mp3 = await audioBytes(await env.AI.run(TTS_MODEL, ttsInput(reply)));
      if (!mp3.length) throw new Error("TTS model returned no audio");
      return new Response(mp3, { headers: { "Content-Type": "audio/mpeg" } });
    } catch (err) {
      console.error("AI call failed:", err);
      return text(502, "AI service failed");
    }
  },
};

function text(status: number, body: string, headers: Record<string, string> = {}): Response {
  return new Response(body, { status, headers: { "Content-Type": "text/plain; charset=utf-8", ...headers } });
}

// Compares SHA-256 digests so the response time does not reveal how much of the token matched.
async function tokenMatches(header: string | null, token: string | undefined): Promise<boolean> {
  if (!token || !header) return false;
  const digest = async (s: string) => new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(s)));
  const [a, b] = await Promise.all([digest(header), digest(`Bearer ${token}`)]);
  return a.every((byte, i) => byte === b[i]);
}

// Reads the body chunk by chunk and gives up (null) as soon as it exceeds `limit`.
async function readBody(request: Request, limit: number): Promise<Uint8Array | null> {
  if (!request.body) return new Uint8Array(0);
  const reader = request.body.getReader();
  const chunks: Uint8Array[] = [];
  let size = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.byteLength;
    if (size > limit) {
      await reader.cancel();
      return null;
    }
    chunks.push(value);
  }
  const body = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) {
    body.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return body;
}

// 44-byte RIFF/WAVE header for 16 kHz, mono, 16-bit PCM.
function toWav(pcm: Uint8Array): Uint8Array {
  const wav = new Uint8Array(44 + pcm.length);
  const view = new DataView(wav.buffer);
  const ascii = (offset: number, s: string) => wav.set(new TextEncoder().encode(s), offset);
  ascii(0, "RIFF");
  view.setUint32(4, 36 + pcm.length, true);
  ascii(8, "WAVE");
  ascii(12, "fmt ");
  view.setUint32(16, 16, true); // fmt chunk size
  view.setUint16(20, 1, true); // PCM
  view.setUint16(22, 1, true); // mono
  view.setUint32(24, SAMPLE_RATE, true);
  view.setUint32(28, SAMPLE_RATE * 2, true); // byte rate
  view.setUint16(32, 2, true); // block align
  view.setUint16(34, 16, true); // bits per sample
  ascii(36, "data");
  view.setUint32(40, pcm.length, true);
  wav.set(pcm, 44);
  return wav;
}

function toBase64(bytes: Uint8Array): string {
  let binary = "";
  // Chunked so String.fromCharCode never gets more arguments than the engine allows.
  for (let i = 0; i < bytes.length; i += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  }
  return btoa(binary);
}

// TTS models answer either { audio: base64 } (MeloTTS) or raw binary audio (e.g. Aura).
async function audioBytes(output: unknown): Promise<Uint8Array> {
  const audio = (output as { audio?: unknown } | null)?.audio;
  if (typeof audio === "string") return Uint8Array.from(atob(audio), (c) => c.charCodeAt(0));
  // Anything else must be binary: a plain object would otherwise become the text "[object Object]".
  if (output instanceof ReadableStream || output instanceof ArrayBuffer || ArrayBuffer.isView(output)) {
    return new Uint8Array(await new Response(output).arrayBuffer());
  }
  throw new Error("TTS model returned no audio");
}
