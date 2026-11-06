# Maiky the Robot

Desktop companion robot built on an ESP32. Hardware and wiring: [COMPONENTS.md](COMPONENTS.md).

## v0 — hardware test

![Maiky v0: ESP32 wired to a breadboard with the amplifier, speaker, three buttons and the OLED showing its face](images/maiky.jpeg)

- **Button 1** (hold): record from the microphone; release to play it back.
- **Button 2**: play a 440 Hz test tone.
- **Button 3**: cycle the volume (5%, 10%, 20%).

## Build and flash

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -t upload      # build and flash
pio device monitor     # serial log (115200 baud)
```
