#include <Arduino.h>
#include "Config.hpp"
#include "QuotesManager.hpp"
#include "StorageManager.hpp"
#include "LedEngine.hpp"
#include "DisplayEngine.hpp"

// Subsystem Instances
static QuotesManager  quotesManager;
static StorageManager storageManager;
static LedEngine      ledEngine;
static DisplayEngine  displayEngine;

// Disk activity hook
void onDiskActivity(bool is_write) {
    ledEngine.triggerDiskActivity(is_write);
    displayEngine.notifyDiskActivity(is_write);
}

// Actor button state
static uint32_t btnPressTime = 0;
static bool btnLastState = HIGH;

void handleActorButton() {
    bool btnState = digitalRead(PIN_BTN_BOOT);
    uint32_t now = millis();

    // Button Pressed (Active LOW)
    if (btnState == LOW && btnLastState == HIGH) {
        btnPressTime = now;
    }
    // Button Released
    else if (btnState == HIGH && btnLastState == LOW) {
        uint32_t duration = now - btnPressTime;

        if (duration >= 50 && duration < 800) {
            // Short press: Advance to next quote immediately
            Serial.println("[ACTOR] BOOT click: Advancing quote.");
            displayEngine.notifyDiskActivity(false);
            ledEngine.triggerDiskActivity(false);
        } else if (duration >= 1500) {
            // Long press: Toggle high/low LED brightness
            Serial.println("[ACTOR] BOOT hold: Cycling brightness mode.");
            static bool highBrightness = false;
            highBrightness = !highBrightness;
            ledEngine.setBrightness(highBrightness ? 255 : 120);
        }
    }
    btnLastState = btnState;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n==========================================");
    Serial.println("  ENIGMA DATAPACK - HERO MOVIE PROP v1.2  ");
    Serial.println("  Arkham Tactical Storage Engine          ");
    Serial.println("==========================================");

    // 1. Configure Actor Button (SW2 / BOOT)
    pinMode(PIN_BTN_BOOT, INPUT_PULLUP);

    // 2. Initialize LEDs
    ledEngine.begin(quotesManager.getConfig());

    // 3. Initialize Storage (4-bit SDMMC & USB MSC)
    storageManager.setActivityCallback(onDiskActivity);
    storageManager.begin();

    // 4. Load Quotes and Config from SD card
    quotesManager.begin();

    // Synchronize loaded configuration with engines
    ledEngine.setBrightness(quotesManager.getConfig().led_brightness);
    ledEngine.setAmbientHue(quotesManager.getConfig().ambient_hue);
    ledEngine.setPulseSpeed(quotesManager.getConfig().pulse_speed);

    // 5. Initialize Display & Play Crisp Boot Animation
    displayEngine.begin(&quotesManager);
    displayEngine.playBootAnimation();

    Serial.println("[SYSTEM] Boot sequence complete. Entering main loop.");
}

void loop() {
    // 1. Check for host PC writes to hot-reload quotes/config
    if (storageManager.checkWriteDirty()) {
        quotesManager.reload();
        displayEngine.setBrightness(quotesManager.getConfig().display_brightness);
        ledEngine.setBrightness(quotesManager.getConfig().led_brightness);
        ledEngine.setAmbientHue(quotesManager.getConfig().ambient_hue);
        ledEngine.setPulseSpeed(quotesManager.getConfig().pulse_speed);
    }

    // 2. Update LED Organic Flow & Breathing
    ledEngine.update();

    // 3. Update 240x135 Display HUD, Oscilloscope & Quote Ticker
    displayEngine.update();

    // 4. Handle Actor Control Gestures
    handleActorButton();
}
