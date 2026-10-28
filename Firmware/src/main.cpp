#include <Arduino.h>
#include "Config.hpp"
#include "QuotesManager.hpp"
#include "StorageManager.hpp"
#include "LedEngine.hpp"
#include "DisplayEngine.hpp"
#include "USBCDC.h"
#include "esp32-hal-tinyusb.h"

extern USBCDC USBSerial;

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
            displayEngine.refreshQuote();
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

void processStreamCommand(Stream& stream) {
    if (!stream.available()) return;
    String cmd = stream.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() == 0) return;

    if (cmd.equalsIgnoreCase("bootloader")) {
        stream.println("[SYSTEM] Entering ROM Download Mode...");
        delay(50);
        usb_persist_restart(RESTART_BOOTLOADER);
    } else if (cmd.equalsIgnoreCase("reboot")) {
        stream.println("[SYSTEM] Rebooting ESP32-S3...");
        delay(100);
        esp_restart();
    } else if (cmd.equalsIgnoreCase("reload")) {
        quotesManager.reload();
        displayEngine.setBrightness(quotesManager.getConfig().display_brightness);
        ledEngine.applyConfig(quotesManager.getConfig());
        stream.println("[SYSTEM] Reloaded /.DDM/config.ini and /.DDM/quotes.txt");
    } else if (cmd.equalsIgnoreCase("status") || cmd.equalsIgnoreCase("sd")) {
        stream.println("\n--- ENIGMA DATAPACK SYSTEM STATUS ---");
        stream.printf("SD Mounted: %s (%s)\n", storageManager.isMounted() ? "YES" : "NO",
                      storageManager.isVfsMounted() ? "FAT32+.DDM" : (storageManager.isExFat() ? "exFAT+.DDM" : "RAW"));
        stream.printf("SD Bus: %s\n", storageManager.is4Bit() ? "4-bit" : "1-bit");
        stream.printf("Capacity: %llu MB\n", storageManager.getTotalCapacityMB());
        stream.printf("Quotes Loaded: %u\n", (unsigned)quotesManager.getQuoteCount());
        stream.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
        stream.printf("Uptime: %lu ms\n", millis());
        stream.println("-------------------------------------\n");
    } else if (cmd.equalsIgnoreCase("help")) {
        stream.println("Available commands: status, sd, reload, bootloader, reboot");
    }
}

void handleSerialCommands() {
    processStreamCommand(Serial);
    processStreamCommand(USBSerial);
}

void setup() {
    // Immediately turn all WS2812B LEDs OFF at power-up so they never flash random colors
    ledEngine.initOff();

    Serial.begin(115200);

    // 1. Configure Actor Button (SW2 / BOOT)
    pinMode(PIN_BTN_BOOT, INPUT_PULLUP);

    // 2. Initialize Storage FIRST (mounts SDMMC, loads/creates Quotes & Config, sets ENIGMA-PACK label, starts USB MSC)
    storageManager.setActivityCallback(onDiskActivity);
    bool sdMounted = storageManager.begin(&quotesManager);

    Serial.println("\n==========================================");
    Serial.println("  ENIGMA DATAPACK - HERO MOVIE PROP v1.2  ");
    Serial.println("  Arkham Tactical Storage Engine          ");
    Serial.println("==========================================");

    // 3. Configure LEDs with loaded configuration (staying off until main loop)
    ledEngine.begin(quotesManager.getConfig());

    // 4. Initialize Display & Play Dynamic Boot Animation
    displayEngine.begin(&quotesManager);
    displayEngine.playBootAnimation(sdMounted, storageManager.getTotalCapacityMB(), storageManager.is4Bit());

    Serial.println("[SYSTEM] Boot sequence complete. Entering main loop.");
}

void loop() {
    // 1. Check for host PC writes to hot-reload quotes/config
    if (storageManager.checkWriteDirty()) {
        quotesManager.reload();
        displayEngine.setBrightness(quotesManager.getConfig().display_brightness);
        displayEngine.refreshQuote();
        ledEngine.applyConfig(quotesManager.getConfig());
    }

    // 2. Update LED Organic Flow & Breathing
    ledEngine.update();

    // 3. Update 240x135 Display HUD, Oscilloscope & Quote Ticker
    displayEngine.update();

    // 4. Handle Actor Control Gestures
    handleActorButton();

    // 5. Handle Serial CLI Commands
    handleSerialCommands();
}
