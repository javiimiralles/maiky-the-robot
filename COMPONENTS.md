# Components

Hardware used in the current version.

| Component | Model / Spec | Purpose |
|-----------|--------------|---------|
| Microcontroller | ESP32 | Main board: logic, Wi-Fi, AI connection |
| Display | 0.96" OLED | Show the robot's face and status |
| Buttons | Push buttons | User input |
| Speaker | Mini speaker, 3 W, 4 Ω | Audio output |
| Amplifier | MAX98357A (I2S) | Drive the speaker from the ESP32 |
| Microphone | INMP441 (I2S) | Voice input |
| Breadboard | Solderless breadboard + jumper wires | Prototyping (no soldering yet) |

## Wiring

The ESP32 sits off the breadboard and connects with jumper wires, so all its pins are free. The breadboard holds the modules and three rails: 3V3, 5V and GND. Always wire modules by the labels printed on them: pin order differs between manufacturers (some OLEDs swap VCC and GND).

| Module | Module pin | ESP32 |
|--------|------------|-------|
| OLED (SSD1306, I2C 0x3C) | VCC / GND | 3V3 / GND |
| | SDA / SCL | GPIO21 / GPIO22 |
| INMP441 | VDD / GND / L/R | 3V3 (never 5V) / GND / GND |
| | SCK / WS / SD | GPIO32 / GPIO33 / GPIO34 |
| MAX98357A | VIN / GND | VIN (5V from USB) / GND |
| | BCLK / LRC / DIN | GPIO26 / GPIO25 / GPIO27 |
| | SD / GAIN | Not connected |
| Speaker | + / − | Amplifier + / − (never to GND) |
| Button 1 (talk) | one leg / other leg | GPIO18 / GND |
| Button 2 (tone) | one leg / other leg | GPIO19 / GND |
| Button 3 (volume) | one leg / other leg | GPIO23 / GND |
