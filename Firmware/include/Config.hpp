#pragma once

#include <Arduino.h>

// ==============================================================================
// HARDWARE PIN ASSIGNMENTS (Mini Version v1.2)
// Matches KiCad layout in PCB/Mini Version/Riddler-Datapack mini/
// ==============================================================================

// --- ST7789 1.14" IPS TFT LCD (240x135 SPI) ---
#define PIN_TFT_SCLK      33
#define PIN_TFT_MOSI      34
#define PIN_TFT_MISO      26
#define PIN_TFT_CS        48
#define PIN_TFT_DC        36
#define PIN_TFT_RST       35
#define PIN_TFT_BL        37    // Backlight PWM

#define TFT_WIDTH         240
#define TFT_HEIGHT        135

// --- MicroSD Card Slot (4-bit SDMMC Interface) ---
#define PIN_SD_CLK        8
#define PIN_SD_CMD        9     // MOSI
#define PIN_SD_D0         5     // MISO
#define PIN_SD_D1         4
#define PIN_SD_D2         11
#define PIN_SD_D3         10    // CS
#define PIN_SD_CS2        47

// --- Addressable WS2812B-2020 LEDs (12 LEDs across 4 channels) ---
#define PIN_LED_FRONT     7     // NPD_F    (2 LEDs: D12, D13 - Front Grille)
#define NUM_LEDS_FRONT    2

#define PIN_LED_RIGHT     2     // NPD_RS   (2 LEDs: D1, D3 - Right Louvers)
#define NUM_LEDS_RIGHT    2

#define PIN_LED_LEFT      46    // NPD_LS   (5 LEDs: D7..D11 - Left Louvers)
#define NUM_LEDS_LEFT     5

#define PIN_LED_REAR      12    // NPD_PWRS (3 LEDs: D4..D6 - Faux Fan / Rear)
#define NUM_LEDS_REAR     3

#define NUM_LEDS_TOTAL    (NUM_LEDS_FRONT + NUM_LEDS_RIGHT + NUM_LEDS_LEFT + NUM_LEDS_REAR)

// --- Actor & Control Buttons ---
#define PIN_BTN_BOOT      0     // SW2 (Boot / Multi-gesture trigger)

// --- USB Identifiers ---
#define USB_VENDOR_ID     "GOTHAM"
#define USB_PRODUCT_ID    "ENIGMA_DRIVE"
#define USB_REVISION      "1.0"
#define USB_VOLUME_LABEL  "ENIGMA_DATA"

// ==============================================================================
// CONFIGURATION DEFAULTS
// ==============================================================================
struct PropConfig {
    uint8_t  display_brightness = 85;       // 10 - 100%
    uint8_t  led_brightness     = 38;       // 15% brightness (0 - 255)
    uint8_t  ticker_speed       = 2;        // 1 (slow) to 8 (fast)
    uint8_t  ambient_hue        = 96;       // 96 = Arkham Emerald Green (0-255 HSV)
    uint8_t  pulse_speed        = 14;       // Beats per minute for idle breath
    bool     show_waveform      = true;     // Show dynamic Arkham oscilloscope
    bool     flicker_on_io      = true;     // Smooth ripple on disk read/write
};
