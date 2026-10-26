#include "StorageManager.hpp"
#include "USB.h"
#include "USBMSC.h"
#include "SD_MMC.h"

StorageManager* StorageManager::_instance = nullptr;
static USBMSC msc;

StorageManager::StorageManager() {
    _instance = this;
}

int32_t StorageManager::onMscRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (!_instance || !_instance->_sdMounted) return -1;

    if (_instance->_activityCallback) {
        _instance->_activityCallback(false); // Read activity
    }

    uint32_t sectors = bufsize / 512;
    uint8_t* ptr = (uint8_t*)buffer;
    for (uint32_t i = 0; i < sectors; i++) {
        if (!SD_MMC.readRAW(ptr + (i * 512), lba + i)) {
            return -1;
        }
    }
    return bufsize;
}

int32_t StorageManager::onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (!_instance || !_instance->_sdMounted) return -1;

    _instance->_writeDirty = true;
    _instance->_lastWriteMillis = millis();

    if (_instance->_activityCallback) {
        _instance->_activityCallback(true); // Write activity
    }

    uint32_t sectors = bufsize / 512;
    for (uint32_t i = 0; i < sectors; i++) {
        if (!SD_MMC.writeRAW(buffer + (i * 512), lba + i)) {
            return -1;
        }
    }
    return bufsize;
}

bool StorageManager::begin() {
    Serial.println("[STORAGE] Configuring 4-bit SDMMC pins...");

    // Set 4-bit SDMMC pins (Mini PCB v1.2)
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);

    // Mount SDMMC: 4-bit mode = false (1bit mode is true), format_if_empty = false, freq = 20000 kHz
    if (!SD_MMC.begin("/sdcard", false, false, 20000)) {
        Serial.println("[STORAGE] SDMMC Mount Failed! Checking card presence...");
        _sdMounted = false;
        return false;
    }

    _sdMounted = true;
    uint64_t totalBytes = SD_MMC.cardSize();
    uint32_t blockCount = totalBytes / 512;
    Serial.printf("[STORAGE] SDMMC Mounted! Capacity: %llu MB (%u sectors)\n", totalBytes / (1024 * 1024), blockCount);

    // Configure USB Mass Storage Class
    msc.vendorID(USB_VENDOR_ID);
    msc.productID(USB_PRODUCT_ID);
    msc.productRevision(USB_REVISION);
    msc.onRead(onMscRead);
    msc.onWrite(onMscWrite);
    msc.mediaPresent(true);

    if (msc.begin(blockCount, 512)) {
        Serial.println("[STORAGE] TinyUSB MSC Initialized. Removable drive exposed to host.");
    } else {
        Serial.println("[STORAGE] Warning: Failed to initialize USB MSC!");
    }

    // Start USB stack
    USB.begin();

    return true;
}

bool StorageManager::checkWriteDirty() {
    // If the host wrote data and the bus has settled for at least 1500ms, trigger reload
    if (_writeDirty && (millis() - _lastWriteMillis > 1500)) {
        _writeDirty = false;
        return true;
    }
    return false;
}

uint64_t StorageManager::getTotalCapacityMB() const {
    if (!_sdMounted) return 0;
    return SD_MMC.cardSize() / (1024 * 1024);
}

uint64_t StorageManager::getFreeCapacityMB() const {
    if (!_sdMounted) return 0;
    return (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024);
}
