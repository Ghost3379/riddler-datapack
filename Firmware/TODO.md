# Mini Enigma Datapack - Firmware Architecture & Software TODO

This document outlines the software design, system architecture, and implementation tasks for the **Mini Enigma Datapack (Hero Movie Prop)** running on the ESP32-S3-MINI-1.

---

## 1. System Overview & Tech Stack

* **Platform / Build System**: [PlatformIO](https://platformio.org/) (`framework = arduino`, ESP32-S3 target).
* **Display Engine**: [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
  * Target: 1.14" IPS TFT (ST7789, 240 × 135 RGB565).
  * SPI Clock: **40 MHz** with hardware DMA (double-buffered sprites for 60+ FPS zero-tear rendering).
* **Storage & USB**:
  * **4-bit SDMMC**: Native hardware SDMMC driver connected to Würth MicroSD slot (`J3`).
  * **TinyUSB MSC (Mass Storage Class)**: Mounts the SD card as a plug-and-play removable USB drive (`ENIGMA_PAYLOAD`) on Windows/macOS/Linux.
  * **TinyUSB CDC**: Optional virtual COM port for Gotham cyber-terminal debug shell.
* **Lighting Engine**:
  * FastLED / NeoPixel driver managing 12× WS2812B-2020 LEDs (10 perimeter/grille louvers + 2 rear faux fan hubs).

---

## 2. Core Features & On-Set Flow

### A. The "Enigma Drive" (USB Mass Storage + Real-Time I/O Activity)
* When plugged into an on-set computer, the prop immediately mounts as a real USB drive.
* **Prop Files on Drive**: Contains confidential Gotham City Police Department (GCPD) files, Edward Nygma audio files, Wayne Enterprises blueprints, and video clips.
* **Hardware Read/Write Hook**:
  * Intercept TinyUSB sector callbacks (`tud_msc_read10_cb` & `tud_msc_write10_cb`).
  * When a file is opened or read on the PC, trigger rapid, frantic white/emerald micro-strobes on the LEDs and flash an `[I/O TRANSFER >> 48 MB/s]` HUD banner on screen.

### B. On-Set `config.ini` (Zero-Flashing Director Customization)
* A plain text configuration file lives on the root of the MicroSD card:
  ```ini
  [DISPLAY]
  brightness = 80          ; 0 - 100%
  scene = RIDDLE_1         ; IDLE, RIDDLE_1, RIDDLE_2, GLITCH, COUNTDOWN
  glitch_interval_sec = 12 ; Random cinematic CRT glitch flicker

  [LEDS]
  brightness = 140         ; 0 - 255
  pulse_speed = 3          ; Ambient breathing rate (1 - 10)
  fan_spin_effect = true   ; Strobe the 2 rear fan LEDs to simulate rotating turbines

  [TEXT]
  line1 = "RIDDLE ME THIS..."
  line2 = "What is black and blue,"
  line3 = "and dead all over?"
  ```
* **Hot-Reload**: The director or prop master can tweak text, change scenes, or dim LEDs in Notepad on set without recompiling or flashing.

### C. Display Animation States (LovyanGFX @ 40 MHz)
* **State 1: Idle / Arkham Oscilloscope**: Animated pulsating question mark with real-time green audio frequency waveform and subtle CRT scanlines.
* **State 2: Script Riddle Reader**: Types out movie script riddles in retro monospaced Gotham font.
* **State 3: Host Transfer Stream**: Matrix-style hex dump with a progress bar and high-speed data stream animation.
* **State 4: Tamper / Countdown**: Flashing amber/red warning HUD with a ticking countdown clock for dramatic scenes.

### D. LED Choreography
* **Perimeter Louvers (10 LEDs)**: Deep emerald green breathing pulse with subtle random glitch drops.
* **Turbine Fans (2 LEDs)**: Phase-shifted rotary strobe backlighting the dual rear faceplate vents to simulate spinning fan blades.
* **Disk Thrash**: Microsecond overrides during file read/writes.

### E. Actor Controls (`BOOT` / GPIO0)
* **Single Click**: Advance to next movie scene/riddle.
* **Double Click**: Trigger "Decryption Success" green cascade wipe.
* **Long Press (3s)**: Trigger "Tamper / Self-Destruct" alarm mode.

---

## 3. Implementation Checklist & Phase Roadmap

### Phase 1: Environment & Display Pipeline
- [ ] Initialize PlatformIO project with `esp32-s3-devkitc-1` definition.
- [ ] Configure `platformio.ini` with build flags for native USB and PSRAM/Flash settings.
- [ ] Create `LGFX_Config.hpp` mapping SPI pins (MOSI, SCLK, CS, DC, RST) and set SPI frequency to 40 MHz with DMA enabled.
- [ ] Implement double-buffered 240×135 framebuffer sprite and benchmark rendering loop.

### Phase 2: SDMMC & TinyUSB Mass Storage
- [ ] Configure 4-bit SDMMC bus (`CLK`, `CMD`, `D0`, `D1`, `D2`, `D3`).
- [ ] Implement TinyUSB MSC sector read/write callbacks mapped directly to SDMMC blocks.
- [ ] Add disk activity flag and callback to notify the LED/display engine during host reads/writes.
- [ ] Populate default prop directory tree on SD card with movie assets.

### Phase 3: Configuration Engine
- [ ] Implement simple, lightweight `config.ini` parser for ESP32.
- [ ] Load settings on boot and monitor file modification timestamp for live hot-reloading.

### Phase 4: LED Engine & Visual Effects
- [ ] Initialize WS2812B driver for 12 LEDs.
- [ ] Implement non-blocking state machine for:
  - Emerald ambient breathe.
  - Rear turbine rotation simulator.
  - I/O read/write burst flicker.
  - Tamper alarm strobe.

### Phase 5: UI Scenes & Actor Button Handler
- [ ] Build scene manager (Idle Waveform, Riddle Text, Data Stream, Countdown).
- [ ] Implement debounced `BOOT` button handler with single/double/hold gestures.
- [ ] Final on-set rehearsal testing.
