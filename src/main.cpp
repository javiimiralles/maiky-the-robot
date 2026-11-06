// Maiky v1 - AI conversation.
// Button 1 (hold): stream your question to the relay, release to hear the AI answer.
// Button 2: play a test tone. Button 3: cycle the volume.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MP3DecoderHelix.h>
#include <driver/i2s.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing src/secrets.h: copy src/secrets.example.h to src/secrets.h and fill in your values"
#endif

// Pins (see COMPONENTS.md)
constexpr int PIN_OLED_SDA = 21;
constexpr int PIN_OLED_SCL = 22;
constexpr int PIN_MIC_SCK = 32;
constexpr int PIN_MIC_WS = 33;
constexpr int PIN_MIC_SD = 34;
constexpr int PIN_AMP_BCLK = 26;
constexpr int PIN_AMP_LRC = 25;
constexpr int PIN_AMP_DIN = 27;
constexpr int PIN_BTN_TALK = 18;
constexpr int PIN_BTN_TONE = 19;
constexpr int PIN_BTN_VOLUME = 23;

constexpr i2s_port_t MIC_PORT = I2S_NUM_0;
constexpr i2s_port_t AMP_PORT = I2S_NUM_1;
constexpr int SAMPLE_RATE = 16000;
// 24-bit mic data in a 32-bit slot -> 16-bit. Speech sits ~60 dB below full scale, so keep extra gain.
constexpr int MIC_SHIFT = 11;

// The relay accepts at most 15 s of 16-bit mono audio.
constexpr size_t MAX_UPLOAD_BYTES = 15 * SAMPLE_RATE * sizeof(int16_t);
constexpr int32_t CONNECT_TIMEOUT_MS = 3000;
constexpr unsigned long ANSWER_TIMEOUT_MS = 30000;  // speech-to-text + LLM + text-to-speech
constexpr unsigned long READ_TIMEOUT_MS = 5000;

// A 3 W speaker at 5V is already very loud at 25%, so stay well below it.
constexpr float VOLUMES[] = {0.05f, 0.1f, 0.2f};
constexpr int VOLUME_COUNT = sizeof(VOLUMES) / sizeof(VOLUMES[0]);
int volumeIndex = 0;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
uint32_t ampRate = SAMPLE_RATE;

void drawFace(const char *status, bool mouthOpen) {
  display.clearDisplay();
  display.fillRoundRect(28, 12, 24, 24, 6, SSD1306_WHITE);
  display.fillRoundRect(76, 12, 24, 24, 6, SSD1306_WHITE);
  if (mouthOpen) {
    display.fillRoundRect(54, 40, 20, 10, 4, SSD1306_WHITE);
  } else {
    display.drawFastHLine(52, 45, 24, SSD1306_WHITE);
  }
  display.setCursor(0, 56);
  display.print(status);
  display.setCursor(98, 56);
  display.print((int)(VOLUMES[volumeIndex] * 100));
  display.print('%');
  display.display();
}

void setupMic() {
  i2s_config_t config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      // Read both slots: the mic leaves the unused one at 0, so summing them works
      // no matter which slot the driver maps it to (it differs between IDF versions).
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
  };
  i2s_pin_config_t pins = {
      .mck_io_num = I2S_PIN_NO_CHANGE,  // neither module needs MCLK; unset it defaults to GPIO0
      .bck_io_num = PIN_MIC_SCK,
      .ws_io_num = PIN_MIC_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = PIN_MIC_SD,
  };
  if (i2s_driver_install(MIC_PORT, &config, 0, nullptr) != ESP_OK || i2s_set_pin(MIC_PORT, &pins) != ESP_OK) {
    Serial.println("Mic I2S setup failed");
  }
}

void setupAmp() {
  i2s_config_t config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      // Stereo frames with the same sample on both sides: the MAX98357A plays (L+R)/2.
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true,  // output silence when nothing is queued
  };
  i2s_pin_config_t pins = {
      .mck_io_num = I2S_PIN_NO_CHANGE,  // neither module needs MCLK; unset it defaults to GPIO0
      .bck_io_num = PIN_AMP_BCLK,
      .ws_io_num = PIN_AMP_LRC,
      .data_out_num = PIN_AMP_DIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };
  if (i2s_driver_install(AMP_PORT, &config, 0, nullptr) != ESP_OK || i2s_set_pin(AMP_PORT, &pins) != ESP_OK) {
    Serial.println("Amp I2S setup failed");
  }
}

// Tries for 10 s so a missing network never blocks the robot.
bool connectWifi() {
  drawFace("Wi-Fi...", false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);  // modem sleep delays TCP ACKs and stalls the audio upload
  for (int i = 0; i < 100 && WiFi.status() != WL_CONNECTED; i++) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection failed");
    return false;
  }
  Serial.printf("Wi-Fi IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// Streams mic audio as HTTP chunks while Button 1 is held, up to the relay's limit.
// Stops early if the relay already answered (e.g. a bad token) or the connection drops.
void streamMic(WiFiClient &client) {
  int32_t raw[256];
  int16_t pcm[128];
  uint8_t chunk[8 + sizeof(pcm) + 2];
  size_t sent = 0;
  int32_t peak[2] = {0, 0};
  while (digitalRead(PIN_BTN_TALK) == LOW && sent < MAX_UPLOAD_BYTES && !client.available()) {
    size_t bytesRead = 0;
    i2s_read(MIC_PORT, raw, sizeof(raw), &bytesRead, portMAX_DELAY);
    size_t count = 0;
    for (size_t i = 0; i + 1 < bytesRead / sizeof(int32_t) && sent + count * sizeof(int16_t) < MAX_UPLOAD_BYTES; i += 2) {
      int32_t a = raw[i] >> MIC_SHIFT;
      int32_t b = raw[i + 1] >> MIC_SHIFT;
      peak[0] = max(peak[0], abs(a));
      peak[1] = max(peak[1], abs(b));
      pcm[count++] = constrain(a + b, INT16_MIN, INT16_MAX);
    }
    if (count == 0) continue;  // an empty chunk would end the request body
    // One HTTP chunk: hex size, CRLF, data, CRLF. Copied so the int16 samples stay aligned.
    size_t bytes = count * sizeof(int16_t);
    size_t head = sprintf((char *)chunk, "%X\r\n", (unsigned)bytes);
    memcpy(chunk + head, pcm, bytes);
    memcpy(chunk + head + bytes, "\r\n", 2);
    if (client.write(chunk, head + bytes + 2) != head + bytes + 2) break;  // relay gone
    sent += bytes;
  }
  // Per slot: 0 = no data, a few thousand while talking = OK, ~32767 even in silence = garbage.
  Serial.printf("Sent %u bytes, peak slot0 %ld, slot1 %ld\n", sent, peak[0], peak[1]);
}

void play(const int16_t *samples, size_t count) {
  int16_t frames[512];  // 256 stereo frames
  float volume = VOLUMES[volumeIndex];
  for (size_t i = 0; i < count;) {
    size_t n = 0;
    for (; n < 256 && i < count; n++, i++) {
      int16_t s = samples[i] * volume;
      frames[2 * n] = s;
      frames[2 * n + 1] = s;
    }
    size_t written = 0;
    i2s_write(AMP_PORT, frames, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
  }
}

// Decoder output: match the amp clock to the MP3 and fold stereo into mono for play().
void playDecoded(MP3FrameInfo &info, short *pcm, size_t len, void *) {
  if ((uint32_t)info.samprate != ampRate) {
    ampRate = info.samprate;
    i2s_set_sample_rates(AMP_PORT, ampRate);
    Serial.printf("MP3: %d Hz, %d channel(s)\n", info.samprate, info.nChans);
  }
  if (info.nChans == 2) {
    len /= 2;
    for (size_t i = 0; i < len; i++) pcm[i] = (pcm[2 * i] + pcm[2 * i + 1]) / 2;
  }
  play(pcm, len);
}

libhelix::MP3DecoderHelix mp3(playDecoded);

// Feeds up to len body bytes to the decoder, which plays them as they decode.
// Gives up when the relay goes quiet, so a dead relay never hangs the robot.
size_t feedDecoder(WiFiClient &client, size_t len) {
  uint8_t buf[512];
  size_t total = 0;
  for (uint32_t last = millis(); total < len && millis() - last < READ_TIMEOUT_MS;) {
    int n = client.read(buf, min(sizeof(buf), len - total));
    if (n < 0) break;  // connection error
    if (n == 0) {
      delay(2);
      continue;
    }
    mp3.write(buf, n);
    total += n;
    last = millis();
  }
  return total;
}

// One conversation turn. Returns a short error status, or nullptr when it ended normally.
const char *talk() {
  if (WiFi.status() != WL_CONNECTED) return connectWifi() ? nullptr : "No Wi-Fi";

  drawFace("Listening...", false);
  i2s_zero_dma_buffer(MIC_PORT);  // drop audio captured before the button press
  WiFiClient client;
  if (!client.connect(RELAY_HOST, RELAY_PORT, CONNECT_TIMEOUT_MS)) return "No relay";
  client.printf("POST /talk HTTP/1.1\r\nHost: %s:%d\r\nAuthorization: Bearer %s\r\n"
                "Content-Type: application/octet-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n",
                RELAY_HOST, RELAY_PORT, RELAY_TOKEN);
  streamMic(client);
  // If the relay already answered it stopped reading: writing more could reset the connection.
  if (!client.available()) client.print("0\r\n\r\n");
  if (!client.connected()) return "No relay";

  drawFace("Thinking...", false);
  client.setTimeout(ANSWER_TIMEOUT_MS);
  String statusLine = client.readStringUntil('\n');  // "HTTP/1.1 200 OK"
  if (statusLine.isEmpty()) return "No relay";
  int code = statusLine.substring(9, 12).toInt();
  Serial.printf("HTTP %d\n", code);
  if (code == 204) return "Didn't hear";
  if (code == 401) return "Bad token";
  if (code != 200) {
    static char error[16];
    snprintf(error, sizeof(error), "Error %d", code);
    return error;
  }

  client.setTimeout(READ_TIMEOUT_MS);
  size_t length = SIZE_MAX;  // no Content-Length: read until the relay goes quiet
  bool chunked = false;
  for (String line; (line = client.readStringUntil('\n')).length() > 1;) {
    line.toLowerCase();
    if (line.startsWith("content-length:")) length = line.substring(15).toInt();
    if (line.startsWith("transfer-encoding:") && line.indexOf("chunked") > 0) chunked = true;
  }

  drawFace("Speaking...", true);
  if (!mp3.begin()) return "No memory";
  Serial.printf("Heap while decoding: %lu free\n", ESP.getFreeHeap());
  size_t received = 0;
  if (chunked) {
    for (size_t size; (size = strtoul(client.readStringUntil('\n').c_str(), nullptr, 16)) > 0;) {
      received += feedDecoder(client, size);
      client.readStringUntil('\n');  // CRLF after the chunk data
    }
  } else {
    received = feedDecoder(client, length);
  }
  mp3.end();  // also plays the frames still buffered, then frees the decoder
  Serial.printf("Received %u bytes\n", received);
  if (ampRate != SAMPLE_RATE) {
    delay(150);  // let the queued audio finish before changing the clock
    ampRate = SAMPLE_RATE;
    i2s_set_sample_rates(AMP_PORT, SAMPLE_RATE);  // Button 2's tone is 16 kHz
  }
  return nullptr;
}

void playTone() {
  constexpr size_t count = SAMPLE_RATE / 2;  // 0.5 s at 440 Hz
  int16_t *tone = (int16_t *)malloc(count * sizeof(int16_t));
  if (!tone) return;
  for (size_t i = 0; i < count; i++) {
    tone[i] = 12000 * sin(2 * PI * 440 * i / SAMPLE_RATE);
  }
  play(tone, count);
  free(tone);
}

// Waits for the button to be released so one press triggers one action.
bool pressed(int pin) {
  if (digitalRead(pin) == HIGH) return false;
  delay(20);
  if (digitalRead(pin) == HIGH) return false;
  while (digitalRead(pin) == LOW) delay(5);
  return true;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_TALK, INPUT_PULLUP);
  pinMode(PIN_BTN_TONE, INPUT_PULLUP);
  pinMode(PIN_BTN_VOLUME, INPUT_PULLUP);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  // ponytail: assumes an SSD1306 at 0x3C; some modules use 0x3D.
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found: check SDA/SCL wiring and the I2C address");
  }
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  setupMic();
  setupAmp();

  // Without Wi-Fi the robot still runs; Button 1 retries the connection.
  drawFace(connectWifi() ? "Ready" : "No Wi-Fi", false);
}

void loop() {
  if (digitalRead(PIN_BTN_TALK) == LOW) {
    Serial.printf("Heap before turn: %lu free\n", ESP.getFreeHeap());
    const char *error = talk();
    Serial.printf("Heap after turn: %lu free, lowest since boot %lu\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    if (error) {
      drawFace(error, false);
      delay(2000);
    }
    drawFace("Ready", false);
    while (digitalRead(PIN_BTN_TALK) == LOW) delay(5);  // one press, one turn
  } else if (pressed(PIN_BTN_TONE)) {
    drawFace("Tone", true);
    playTone();
    drawFace("Ready", false);
  } else if (pressed(PIN_BTN_VOLUME)) {
    volumeIndex = (volumeIndex + 1) % VOLUME_COUNT;
    drawFace("Volume", false);
  }
}
