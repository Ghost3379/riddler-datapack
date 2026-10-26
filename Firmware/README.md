# Enigma Datapack - Firmware

Firmware for the **Mini Enigma Datapack (Hero Movie Prop v1.2)** running on the **ESP32-S3-MINI-1**.

---

## Architecture Overview

* **USB Mass Storage (MSC)**: Built on the ESP32-S3 native USB OTG controller. When plugged into a PC via USB-C, it mounts as a high-speed removable drive named `ENIGMA_DRIVE`.
* **Storage Subsystem**: 4-bit native SDMMC bus connected to the onboard Würth MicroSD slot (pull-ups $R13\text{--}R17$).
* **Display Engine**: [LovyanGFX](https://github.com/lovyan03/LovyanGFX) driving the 1.14" IPS TFT LCD (ST7789, $240 \times 135$) over 40MHz SPI with double-buffered DMA rendering.
* **Lighting Engine**: [FastLED](https://github.com/FastLED/FastLED) controlling 12× WS2812B-2020 LEDs across 4 discrete physical zones (front grille, side louvers, rear turbine hubs).
* **Live Configuration Hot-Reload**: Detects writes to `CONFIG/config.ini` and `CONFIG/quotes.txt` when saved on the PC and immediately updates the display without rebooting.

---

## Directory Structure

```text
Firmware/
├── platformio.ini              # PlatformIO build configuration
├── include/
│   ├── Config.hpp              # Pinout definitions (matching PCB v1.2 layout)
│   ├── LGFX_Config.hpp         # LovyanGFX display driver setup
│   ├── DisplayEngine.hpp       # HUD, oscilloscope, and ticker engine
│   ├── LedEngine.hpp           # 4-channel organic breathing & ripple engine
│   ├── StorageManager.hpp      # SDMMC and TinyUSB MSC callbacks
│   └── QuotesManager.hpp       # INI parser and quotes cycler
├── src/
│   ├── DisplayEngine.cpp
│   ├── LedEngine.cpp
│   ├── StorageManager.cpp
│   ├── QuotesManager.cpp
│   └── main.cpp                # Core event loop and gesture controls
└── sdcard_template/
    └── CONFIG/
        ├── config.ini          # Tunable display and lighting options
        └── quotes.txt          # Plain text list of riddles and quotes
```

---

## Hardware Pinout Reference (PCB v1.2)

| Subsystem | Function | Pin / Net |
| :--- | :--- | :--- |
| **Display (ST7789)** | SCK / MOSI / MISO | `GPIO 33` / `GPIO 34` / `GPIO 26` |
| | CS / DC / RST | `GPIO 48` / `GPIO 36` / `GPIO 35` |
| | Backlight PWM | `GPIO 37` |
| **MicroSD (SDMMC)** | CLK / CMD | `GPIO 8` / `GPIO 9` |
| | DAT0 / DAT1 / DAT2 / DAT3 | `GPIO 5` / `GPIO 4` / `GPIO 11` / `GPIO 10` |
| **LEDs (WS2812B)** | Front Grille (2 LEDs) | `GPIO 7` (`NPD_F`) |
| | Right Louvers (2 LEDs) | `GPIO 2` (`NPD_RS`) |
| | Left Louvers (5 LEDs) | `GPIO 46` (`NPD_LS`) |
| | Rear Faux Fans (3 LEDs) | `GPIO 12` (`NPD_PWRS`) |
| **Actor Control** | Boot Button | `GPIO 0` (`SW2`) |

---

## Building and Flashing

### Using VS Code / PlatformIO:
1. Open the `Firmware` folder in VS Code with PlatformIO extension installed.
2. Connect the Datapack via USB-C.
3. Click **PlatformIO: Build** (`Ctrl+Alt+B`) or **Upload** (`Ctrl+Alt+U`).

### Using PlatformIO Core CLI:
```bash
pio run --target upload
pio device monitor
```
