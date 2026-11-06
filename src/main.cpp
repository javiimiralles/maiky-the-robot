// Maiky v0.1 - hardware test.
// Button 1 (hold): record from the mic, release to play it back.
// Button 2: play a test tone. Button 3: cycle the volume.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

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
constexpr size_t MAX_RECORD_SAMPLES = SAMPLE_RATE * 3;  // 3 s of 16-bit mono
// 24-bit mic data in a 32-bit slot -> 16-bit. Speech sits ~60 dB below full scale, so keep extra gain.
constexpr int MIC_SHIFT = 11;

// A 3 W speaker at 5V is already very loud at 25%, so stay well below it.
constexpr float VOLUMES[] = {0.05f, 0.1f, 0.2f};
constexpr int VOLUME_COUNT = sizeof(VOLUMES) / sizeof(VOLUMES[0]);
int volumeIndex = 0;

Adafruit_SSD1306 display(128, 64, &Wire, -1);
int16_t *recording = nullptr;
size_t recordCapacity = 0;

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

size_t record() {
  int32_t raw[256];
  size_t count = 0;
  int32_t peak[2] = {0, 0};
  i2s_zero_dma_buffer(MIC_PORT);  // drop audio captured before the button press
  while (digitalRead(PIN_BTN_TALK) == LOW && count < recordCapacity) {
    size_t bytesRead = 0;
    i2s_read(MIC_PORT, raw, sizeof(raw), &bytesRead, portMAX_DELAY);
    for (size_t i = 0; i + 1 < bytesRead / sizeof(int32_t) && count < recordCapacity; i += 2) {
      int32_t a = raw[i] >> MIC_SHIFT;
      int32_t b = raw[i + 1] >> MIC_SHIFT;
      peak[0] = max(peak[0], abs(a));
      peak[1] = max(peak[1], abs(b));
      recording[count++] = constrain(a + b, INT16_MIN, INT16_MAX);
    }
  }
  // Per slot: 0 = no data, a few thousand while talking = OK, ~32767 even in silence = garbage.
  Serial.printf("Recorded %u samples, peak slot0 %ld, slot1 %ld\n", count, peak[0], peak[1]);
  return count;
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

  // No PSRAM: take as much of the 3 s buffer as the largest free heap block allows.
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) - 4096;
  recordCapacity = min(MAX_RECORD_SAMPLES, largest / sizeof(int16_t));
  recording = (int16_t *)malloc(recordCapacity * sizeof(int16_t));
  Serial.printf("Recording buffer: %.1f s\n", (float)recordCapacity / SAMPLE_RATE);

  drawFace("Ready", false);
}

void loop() {
  if (digitalRead(PIN_BTN_TALK) == LOW) {
    drawFace("Listening...", false);
    size_t count = record();
    drawFace("Speaking...", true);
    play(recording, count);
    drawFace("Ready", false);
  } else if (pressed(PIN_BTN_TONE)) {
    drawFace("Tone", true);
    playTone();
    drawFace("Ready", false);
  } else if (pressed(PIN_BTN_VOLUME)) {
    volumeIndex = (volumeIndex + 1) % VOLUME_COUNT;
    drawFace("Volume", false);
  }
}
