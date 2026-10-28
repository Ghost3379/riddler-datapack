#pragma once

#include <Arduino.h>
#include "Config.hpp"

class QuotesManager;

// Callback type to notify system of disk read/write
typedef void (*DiskActivityCallback)(bool is_write);

class StorageManager {
public:
    StorageManager();

    // Initialize 4-bit SDMMC bus, load/prepare config via QuotesManager, repair FAT32 metadata, and expose USB MSC
    bool begin(QuotesManager* quotesManager = nullptr);

    // Check if the host PC has finished writing and changes need reloading
    bool checkWriteDirty();

    // Thread-safe SD bus lock/unlock for local VFS file operations
    static void lockSD();
    static void unlockSD();
    static StorageManager* instance() { return _instance; }

    // Activity callback registration
    void setActivityCallback(DiskActivityCallback cb) { _activityCallback = cb; }

    // Status queries
    bool isMounted() const { return _sdMounted; }
    bool isVfsMounted() const { return _vfsMounted; }
    bool isExFat() const { return _exfatMounted; }
    bool is4Bit() const { return _is4Bit; }
    uint64_t getTotalCapacityMB() const;
    uint64_t getFreeCapacityMB() const;

    // Read a file directly from exFAT directory (e.g., dirName=".DDM", fileName="quotes.txt")
    bool readExFatFile(const char* dirName, const char* fileName, String& outContent);

    // Internal MSC callbacks
    static int32_t onMscRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
    static int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
    static bool onMscStartStop(uint8_t power_condition, bool start, bool load_eject);

private:
    bool initRawSD(bool try4Bit, int freqKhz);
    void repairVolumeMetadata();
    bool findExFatEntry(uint32_t dirCluster, bool dirNoChain, const char* targetName,
                        uint32_t& outFirstCluster, uint64_t& outSize, bool& outNoChain, bool& outIsDir);
    uint32_t getNextExFatCluster(uint32_t cluster, bool noFatChain);

    bool _sdMounted = false;
    bool _vfsMounted = false;
    bool _exfatMounted = false;
    bool _is4Bit = false;
    uint64_t _totalBytes = 0;
    uint32_t _blockCount = 0;

    uint32_t _exfatStartLba = 0;
    uint32_t _exfatFatOffset = 0;
    uint32_t _exfatClusterHeapOffset = 0;
    uint32_t _exfatRootCluster = 0;
    uint8_t  _exfatSpcShift = 0;

    static StorageManager* _instance;
    DiskActivityCallback _activityCallback = nullptr;

    volatile bool _writeDirty = false;
    volatile uint32_t _lastWriteMillis = 0;
    volatile uint32_t _lastReadMillis = 0;
};
