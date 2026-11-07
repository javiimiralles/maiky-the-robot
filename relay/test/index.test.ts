import { afterEach, describe, expect, it, vi } from "vitest";
import worker from "../src/index";

const TOKEN = "test-token";
const PIPER_URL = "http://127.0.0.1:5000";
const WAV = new Uint8Array([0x52, 0x49, 0x46, 0x46, 1, 2, 3, 4]);
const REPLY = "¡Hola! Soy Maiky.";

const fromBase64 = (s: string) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));

// Workers AI does speech-to-text and the reply; Piper (a plain HTTP fetch) does the speech.
// Piper's server labels its WAV as text/html, so the mock does too.
function makeEnv(
  overrides: Partial<Record<string, (input: any) => unknown>> = {},
  piper: () => Response | Promise<Response> = () => new Response(WAV, { headers: { "Content-Type": "text/html; charset=utf-8" } }),
) {
  const outputs: Record<string, (input: any) => unknown> = {
    "@cf/openai/whisper-large-v3-turbo": () => ({ text: " Hola, ¿qué tal? " }),
    "@cf/google/gemma-4-26b-a4b-it": () => ({ choices: [{ message: { role: "assistant", content: REPLY } }] }),
    ...overrides,
  };
  const run = vi.fn(async (model: string, input: unknown) => outputs[model](input));
  const speech = vi.fn(async (_url: string, _init: RequestInit) => piper());
  vi.stubGlobal("fetch", speech);
  const env = { RELAY_TOKEN: TOKEN, AI: { run }, PIPER_URL } as any;
  return { env, run, speech };
}

afterEach(() => {
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});

function talk(body?: BodyInit | null, headers: Record<string, string> = { Authorization: `Bearer ${TOKEN}` }) {
  return new Request("http://relay.local/talk", { method: "POST", headers, body, duplex: "half" } as RequestInit);
}

// A stream that never ends: the relay must stop reading once the limit is passed.
function endlessStream() {
  return new ReadableStream<Uint8Array>({
    pull(controller) {
      controller.enqueue(new Uint8Array(64 * 1024));
    },
  });
}

describe("POST /talk", () => {
  it("returns 401 when the token is missing", async () => {
    const { env, run } = makeEnv();
    const res = await worker.fetch(talk(new Uint8Array(4), {}), env);
    expect(res.status).toBe(401);
    expect(res.headers.get("Content-Type")).toMatch(/^text\/plain/);
    expect(run).not.toHaveBeenCalled();
  });

  it("returns 401 when the token is wrong or the relay has no token configured", async () => {
    const { env, run } = makeEnv();
    expect((await worker.fetch(talk(new Uint8Array(4), { Authorization: "Bearer nope" }), env)).status).toBe(401);
    const noToken = { ...env, RELAY_TOKEN: "" };
    expect((await worker.fetch(talk(new Uint8Array(4), { Authorization: "Bearer " }), noToken)).status).toBe(401);
    expect((await worker.fetch(talk(new Uint8Array(4), { Authorization: "Bearer undefined" }), { ...env, RELAY_TOKEN: undefined })).status).toBe(401);
    expect(run).not.toHaveBeenCalled();
  });

  it("returns 400 for an empty body", async () => {
    const { env, run } = makeEnv();
    const res = await worker.fetch(talk(null), env);
    expect(res.status).toBe(400);
    expect(res.headers.get("Content-Type")).toMatch(/^text\/plain/);
    expect(run).not.toHaveBeenCalled();
  });

  it("returns 400 for an odd-length body", async () => {
    const { env, run } = makeEnv();
    const res = await worker.fetch(talk(new Uint8Array(3)), env);
    expect(res.status).toBe(400);
    expect(run).not.toHaveBeenCalled();
  });

  it("returns 413 for a body over 480,000 bytes without reading it all", async () => {
    const { env, run } = makeEnv();
    const res = await worker.fetch(talk(endlessStream()), env);
    expect(res.status).toBe(413);
    expect(res.headers.get("Content-Type")).toMatch(/^text\/plain/);
    expect(run).not.toHaveBeenCalled();

    expect((await worker.fetch(talk(new Uint8Array(480_002)), env)).status).toBe(413);
  });

  it("accepts a body of exactly 480,000 bytes", async () => {
    const { env } = makeEnv();
    expect((await worker.fetch(talk(new Uint8Array(480_000)), env)).status).toBe(200);
  });

  it("returns 204 with no body when nothing was transcribed", async () => {
    const { env, run } = makeEnv({ "@cf/openai/whisper-large-v3-turbo": () => ({ text: "  \n " }) });
    const res = await worker.fetch(talk(new Uint8Array(4)), env);
    expect(res.status).toBe(204);
    expect(await res.text()).toBe("");
    expect(run).toHaveBeenCalledTimes(1);
  });

  it("returns 502 when an AI call fails", async () => {
    const { env } = makeEnv({
      "@cf/google/gemma-4-26b-a4b-it": () => {
        throw new Error("model unavailable");
      },
    });
    vi.spyOn(console, "error").mockImplementation(() => {});
    const res = await worker.fetch(talk(new Uint8Array(4)), env);
    expect(res.status).toBe(502);
    expect(res.headers.get("Content-Type")).toMatch(/^text\/plain/);
  });

  it("returns the WAV answer for a chunked PCM upload", async () => {
    const { env, run, speech } = makeEnv();
    const chunks = [new Uint8Array([1, 0, 2]), new Uint8Array([0, 3, 0])];
    const body = new ReadableStream<Uint8Array>({
      start(controller) {
        chunks.forEach((c) => controller.enqueue(c));
        controller.close();
      },
    });

    const res = await worker.fetch(talk(body), env);

    expect(res.status).toBe(200);
    expect(res.headers.get("Content-Type")).toBe("audio/wav");
    expect(new Uint8Array(await res.arrayBuffer())).toEqual(WAV);

    expect(run.mock.calls.map((c) => c[0])).toEqual([
      "@cf/openai/whisper-large-v3-turbo",
      "@cf/google/gemma-4-26b-a4b-it",
    ]);
    const [, sttInput] = run.mock.calls[0] as [string, any];
    expect(sttInput.language).toBe("es");
    expect(fromBase64(sttInput.audio).subarray(44)).toEqual(new Uint8Array([1, 0, 2, 0, 3, 0]));

    const [, chatInput] = run.mock.calls[1] as [string, any];
    expect(chatInput.messages[0].role).toBe("system");
    expect(chatInput.messages[0].content).toMatch(/Spanish/);
    expect(chatInput.messages.slice(1)).toEqual([{ role: "user", content: "Hola, ¿qué tal?" }]);

    expect(speech).toHaveBeenCalledTimes(1);
    const [url, init] = speech.mock.calls[0];
    expect(url).toBe(`${PIPER_URL}/synthesize`);
    expect(init.method).toBe("POST");
    expect(new Headers(init.headers).get("Content-Type")).toBe("application/json");
    expect(JSON.parse(init.body as string)).toEqual({ text: REPLY });
    expect(init.signal).toBeInstanceOf(AbortSignal);
  });

  it("returns 502 when Piper fails, is not running, times out, or returns no WAV", async () => {
    vi.spyOn(console, "error").mockImplementation(() => {});
    const failures = [
      () => new Response("Internal Server Error", { status: 500 }),
      () => Promise.reject(new TypeError("fetch failed")),
      () => Promise.reject(new DOMException("The operation timed out.", "TimeoutError")),
      () => new Response(new Uint8Array(0)),
      () => new Response("<html>not audio</html>"),
    ];
    for (const failure of failures) {
      const { env } = makeEnv({}, failure);
      const res = await worker.fetch(talk(new Uint8Array(4)), env);
      expect(res.status).toBe(502);
      expect(res.headers.get("Content-Type")).toMatch(/^text\/plain/);
    }
  });

  it("wraps the PCM in a 16 kHz mono 16-bit WAV header", async () => {
    const { env, run } = makeEnv();
    const pcm = new Uint8Array(1000).map((_, i) => i % 256);
    await worker.fetch(talk(pcm), env);

    const wav = fromBase64((run.mock.calls[0] as [string, any])[1].audio);
    const view = new DataView(wav.buffer);
    const ascii = (offset: number) => String.fromCharCode(...wav.subarray(offset, offset + 4));
    expect(wav.length).toBe(44 + pcm.length);
    expect(ascii(0)).toBe("RIFF");
    expect(view.getUint32(4, true)).toBe(36 + pcm.length);
    expect(ascii(8)).toBe("WAVE");
    expect(ascii(12)).toBe("fmt ");
    expect(view.getUint32(16, true)).toBe(16);
    expect(view.getUint16(20, true)).toBe(1); // PCM
    expect(view.getUint16(22, true)).toBe(1); // mono
    expect(view.getUint32(24, true)).toBe(16000);
    expect(view.getUint32(28, true)).toBe(32000); // byte rate
    expect(view.getUint16(32, true)).toBe(2); // block align
    expect(view.getUint16(34, true)).toBe(16);
    expect(ascii(36)).toBe("data");
    expect(view.getUint32(40, true)).toBe(pcm.length);
    expect(wav.subarray(44)).toEqual(pcm);
  });
});

describe("routing", () => {
  it("returns 404 for unknown paths and 405 for other methods on /talk", async () => {
    const { env } = makeEnv();
    const notFound = await worker.fetch(new Request("http://relay.local/other", { method: "POST" }), env);
    expect(notFound.status).toBe(404);
    expect(notFound.headers.get("Content-Type")).toMatch(/^text\/plain/);
    const wrongMethod = await worker.fetch(new Request("http://relay.local/talk"), env);
    expect(wrongMethod.status).toBe(405);
    expect(wrongMethod.headers.get("Content-Type")).toMatch(/^text\/plain/);
  });
});
