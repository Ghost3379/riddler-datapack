#pragma once

#include <Arduino.h>
#include "Config.hpp"

// Callback type to notify system of disk read/write
typedef void (*DiskActivityCallback)(bool is_write);

class StorageManager {
public:
    StorageManager();

    // Initialize 4-bit SDMMC bus and USB Mass Storage class
    bool begin();

    // Check if the host PC has finished writing and changes need reloading
    bool checkWriteDirty();

    // Activity callback registration
    void setActivityCallback(DiskActivityCallback cb) { _activityCallback = cb; }

    // Status queries
    bool isMounted() const { return _sdMounted; }
    uint64_t getTotalCapacityMB() const;
    uint64_t getFreeCapacityMB() const;

    // Internal MSC callbacks
    static int32_t onMscRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    static int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);

private:
    bool _sdMounted = false;
    static StorageManager* _instance;
    DiskActivityCallback _activityCallback = nullptr;

    volatile bool _writeDirty = false;
    volatile uint32_t _lastWriteMillis = 0;
};
