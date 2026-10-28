#include "StorageManager.hpp"
#include "QuotesManager.hpp"
#include "USB.h"
#include "USBMSC.h"
#include "USBCDC.h"
#include "SD_MMC.h"

USBCDC USBSerial(0);
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include "tusb.h"
#include "ff.h"

// Accessor for protected _card pointer in SDMMCFS
class SDMMC_CardAccessor : public fs::SDMMCFS {
public:
    static sdmmc_card_t* getCard(fs::SDMMCFS& sd) {
        return ((SDMMC_CardAccessor*)&sd)->_card;
    }
};

StorageManager* StorageManager::_instance = nullptr;

static USBMSC& getMsc() {
    static USBMSC mscInstance;
    return mscInstance;
}

static sdmmc_card_t s_rawCard;
static sdmmc_card_t* s_activeCard = nullptr;
static SemaphoreHandle_t s_sdMutex = nullptr;

// Dedicated 32-bit aligned internal SRAM buffers for SDMMC DMA
DRAM_ATTR __attribute__((aligned(4))) static uint8_t s_mscBounceBuf[4096];
DRAM_ATTR __attribute__((aligned(4))) static uint8_t s_secBuf[512];
DRAM_ATTR __attribute__((aligned(4))) static uint8_t s_vbrBuf[512];
DRAM_ATTR __attribute__((aligned(4))) static uint8_t s_fsiBuf[512];

StorageManager::StorageManager() {
    _instance = this;
    USBMSC& msc = getMsc();
    msc.vendorID(USB_VENDOR_ID);
    msc.productID(USB_PRODUCT_ID);
    msc.productRevision(USB_REVISION);
    msc.onStartStop(onMscStartStop);
    msc.onRead(onMscRead);
    msc.onWrite(onMscWrite);
}

bool StorageManager::onMscStartStop(uint8_t power_condition, bool start, bool load_eject) {
    if (load_eject) {
        getMsc().mediaPresent(false);
    } else if (start && _instance && _instance->_sdMounted) {
        getMsc().mediaPresent(true);
    }
    return true;
}

int32_t StorageManager::onMscRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (!_instance || !_instance->_sdMounted || !s_activeCard || bufsize == 0) return -1;

    _instance->_lastReadMillis = millis();
    if (_instance->_activityCallback) {
        _instance->_activityCallback(false); // Read activity
    }

    uint32_t sectors = bufsize / 512;
    if (s_sdMutex) xSemaphoreTake(s_sdMutex, portMAX_DELAY);
    esp_err_t err = sdmmc_read_sectors(s_activeCard, s_mscBounceBuf, lba, sectors);
    if (err == ESP_OK) {
        memcpy(buffer, s_mscBounceBuf, bufsize);
    }
    if (s_sdMutex) xSemaphoreGive(s_sdMutex);

    return (err == ESP_OK) ? (int32_t)bufsize : -1;
}

int32_t StorageManager::onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    if (!_instance || !_instance->_sdMounted || !s_activeCard || bufsize == 0) return -1;

    _instance->_writeDirty = true;
    _instance->_lastWriteMillis = millis();

    if (_instance->_activityCallback) {
        _instance->_activityCallback(true); // Write activity
    }

    uint32_t sectors = bufsize / 512;
    if (s_sdMutex) xSemaphoreTake(s_sdMutex, portMAX_DELAY);
    memcpy(s_mscBounceBuf, buffer, bufsize);
    esp_err_t err = sdmmc_write_sectors(s_activeCard, s_mscBounceBuf, lba, sectors);
    if (s_sdMutex) xSemaphoreGive(s_sdMutex);

    return (err == ESP_OK) ? (int32_t)bufsize : -1;
}

bool StorageManager::initRawSD(bool try4Bit, int freqKhz) {
    sdmmc_host_deinit();
    delay(20);

    gpio_pullup_en((gpio_num_t)PIN_SD_CMD);
    gpio_pullup_en((gpio_num_t)PIN_SD_D0);
    gpio_pullup_en((gpio_num_t)PIN_SD_D1);
    gpio_pullup_en((gpio_num_t)PIN_SD_D2);
    gpio_pullup_en((gpio_num_t)PIN_SD_D3);
    gpio_pullup_en((gpio_num_t)PIN_SD_CLK);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = freqKhz;
    host.flags = try4Bit ? SDMMC_HOST_FLAG_4BIT : SDMMC_HOST_FLAG_1BIT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = (gpio_num_t)PIN_SD_CLK;
    slot_config.cmd = (gpio_num_t)PIN_SD_CMD;
    slot_config.d0  = (gpio_num_t)PIN_SD_D0;
    slot_config.d1  = try4Bit ? (gpio_num_t)PIN_SD_D1 : GPIO_NUM_NC;
    slot_config.d2  = try4Bit ? (gpio_num_t)PIN_SD_D2 : GPIO_NUM_NC;
    slot_config.d3  = try4Bit ? (gpio_num_t)PIN_SD_D3 : GPIO_NUM_NC;
    slot_config.width = try4Bit ? 4 : 1;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    if (sdmmc_host_init() != ESP_OK) return false;
    if (sdmmc_host_init_slot(host.slot, &slot_config) != ESP_OK) return false;

    memset(&s_rawCard, 0, sizeof(sdmmc_card_t));
    if (sdmmc_card_init(&host, &s_rawCard) != ESP_OK) return false;

    // Verify 8-sector DMA read works reliably
    if (sdmmc_read_sectors(&s_rawCard, s_mscBounceBuf, 0, 8) != ESP_OK) return false;

    s_activeCard = &s_rawCard;
    _is4Bit = try4Bit;
    _totalBytes = (uint64_t)s_rawCard.csd.capacity * s_rawCard.csd.sector_size;
    _blockCount = _totalBytes / 512;
    return true;
}

void StorageManager::repairVolumeMetadata() {
    if (!s_activeCard) return;

    // Read Sector 0 (MBR or Superfloppy VBR)
    if (sdmmc_read_sectors(s_activeCard, s_secBuf, 0, 1) != ESP_OK) return;
    if (s_secBuf[510] != 0x55 || s_secBuf[511] != 0xAA) return;

    uint32_t startLba = 0;
    uint8_t partType = 0;
    bool isMbr = false;

    if (memcmp(&s_secBuf[82], "FAT32   ", 8) == 0 || memcmp(&s_secBuf[3], "EXFAT   ", 8) == 0) {
        startLba = 0;
        memcpy(s_vbrBuf, s_secBuf, 512);
    } else {
        uint8_t* p1 = &s_secBuf[446];
        partType = p1[4];
        uint32_t p1Start = p1[8] | (p1[9] << 8) | (p1[10] << 16) | (p1[11] << 24);
        uint32_t p1Sectors = p1[12] | (p1[13] << 8) | (p1[14] << 16) | (p1[15] << 24);
        if (p1Start == 0 || p1Sectors == 0) return;

        if (sdmmc_read_sectors(s_activeCard, s_vbrBuf, p1Start, 1) != ESP_OK) return;
        if (s_vbrBuf[510] != 0x55 || s_vbrBuf[511] != 0xAA) return;

        startLba = p1Start;
        isMbr = true;
    }

    // --- CASE A: exFAT Volume ---
    if (memcmp(&s_vbrBuf[3], "EXFAT   ", 8) == 0) {
        uint8_t sectorShift = s_vbrBuf[108];
        uint8_t spcShift    = s_vbrBuf[109];
        if (sectorShift != 9) return; // Expect 512-byte sectors

        _exfatStartLba          = startLba;
        _exfatFatOffset         = s_vbrBuf[80] | (s_vbrBuf[81] << 8) | (s_vbrBuf[82] << 16) | (s_vbrBuf[83] << 24);
        _exfatClusterHeapOffset = s_vbrBuf[88] | (s_vbrBuf[89] << 8) | (s_vbrBuf[90] << 16) | (s_vbrBuf[91] << 24);
        _exfatRootCluster       = s_vbrBuf[96] | (s_vbrBuf[97] << 8) | (s_vbrBuf[98] << 16) | (s_vbrBuf[99] << 24);
        _exfatSpcShift          = spcShift;
        _exfatMounted           = (_exfatRootCluster >= 2);

        // Ensure exFAT Root Directory has Volume Label "ENIGMA-PACK" (EntryType 0x83, 11 UTF-16LE chars, no checksum)
        if (_exfatMounted) {
            uint32_t rootLba = _exfatStartLba + _exfatClusterHeapOffset + ((_exfatRootCluster - 2) << _exfatSpcShift);
            if (sdmmc_read_sectors(s_activeCard, s_secBuf, rootLba, 1) == ESP_OK) {
                int labelSlot = -1;
                bool alreadyNamed = false;
                const char* targetLabel = USB_VOLUME_LABEL; // "ENIGMA-PACK"
                size_t labelLen = strlen(targetLabel);
                if (labelLen > 11) labelLen = 11;

                for (int i = 0; i < 16; i++) {
                    uint8_t* ent = &s_secBuf[i * 32];
                    if (ent[0] == 0x83) {
                        labelSlot = i;
                        if (ent[1] == labelLen) {
                            bool match = true;
                            for (size_t c = 0; c < labelLen; c++) {
                                if (ent[2 + c * 2] != (uint8_t)targetLabel[c] || ent[3 + c * 2] != 0) {
                                    match = false;
                                    break;
                                }
                            }
                            if (match) alreadyNamed = true;
                        }
                        break;
                    } else if (ent[0] == 0x03) {
                        // Empty volume label placeholder created by Windows format
                        labelSlot = i;
                        break;
                    } else if (ent[0] == 0x00) {
                        if (labelSlot < 0) labelSlot = i;
                        break;
                    }
                }

                if (!alreadyNamed && labelSlot >= 0) {
                    uint8_t* ent = &s_secBuf[labelSlot * 32];
                    memset(ent, 0, 32);
                    ent[0] = 0x83; // Volume Label Directory Entry (In-Use)
                    ent[1] = (uint8_t)labelLen;
                    for (size_t c = 0; c < labelLen; c++) {
                        ent[2 + c * 2] = (uint8_t)targetLabel[c];
                        ent[3 + c * 2] = 0x00;
                    }
                    sdmmc_write_sectors(s_activeCard, s_secBuf, rootLba, 1);
                    Serial.printf("[STORAGE] Set exFAT Volume Label to %s\n", targetLabel);
                }
            }
        }
        return;
    }

    // --- CASE B: FAT32 Volume ---
    if (memcmp(&s_vbrBuf[82], "FAT32   ", 8) != 0) return;

    if (isMbr && partType != 0x0C && partType != 0x0B) {
        Serial.printf("[STORAGE] Repairing MBR Partition 1 type: 0x%02X -> 0x0C (FAT32 LBA)\n", partType);
        s_secBuf[446 + 4] = 0x0C;
        sdmmc_write_sectors(s_activeCard, s_secBuf, 0, 1);
    }

    uint16_t bytesPerSec = s_vbrBuf[11] | (s_vbrBuf[12] << 8);
    uint8_t  secPerClus  = s_vbrBuf[13];
    uint16_t rsvdSecCnt  = s_vbrBuf[14] | (s_vbrBuf[15] << 8);
    uint8_t  numFATs     = s_vbrBuf[16];
    uint32_t totSec32    = s_vbrBuf[32] | (s_vbrBuf[33] << 8) | (s_vbrBuf[34] << 16) | (s_vbrBuf[35] << 24);
    uint32_t fatSz32     = s_vbrBuf[36] | (s_vbrBuf[37] << 8) | (s_vbrBuf[38] << 16) | (s_vbrBuf[39] << 24);
    uint16_t fsInfoSec   = s_vbrBuf[48] | (s_vbrBuf[49] << 8);
    uint16_t bkBootSec   = s_vbrBuf[50] | (s_vbrBuf[51] << 8);

    if (bytesPerSec != 512 || secPerClus == 0 || rsvdSecCnt == 0 || fatSz32 == 0) return;

    // 2. Set volume label to ENIGMA
    if (memcmp(&s_vbrBuf[71], "ENIGMA     ", 11) != 0) {
        memcpy(&s_vbrBuf[71], "ENIGMA     ", 11);
        sdmmc_write_sectors(s_activeCard, s_vbrBuf, startLba, 1);
        if (bkBootSec > 0 && bkBootSec < rsvdSecCnt) {
            sdmmc_write_sectors(s_activeCard, s_vbrBuf, startLba + bkBootSec, 1);
        }
    }

    // 2b. Ensure Root Directory (Cluster 2) has a Volume Label entry ("ENIGMA     ", ATTR_VOLUME_ID = 0x08)
    uint32_t rootDirLba = startLba + rsvdSecCnt + ((uint32_t)numFATs * fatSz32);
    if (sdmmc_read_sectors(s_activeCard, s_secBuf, rootDirLba, 1) == ESP_OK) {
        bool hasVolLabel = false;
        int labelSlot = -1;
        for (int i = 0; i < 16; i++) {
            uint8_t* ent = &s_secBuf[i * 32];
            if (ent[0] == 0x00) {
                if (labelSlot < 0) labelSlot = i;
                break;
            }
            if (ent[0] == 0xE5) {
                if (labelSlot < 0) labelSlot = i;
                continue;
            }
            if (ent[11] == 0x08) {
                labelSlot = i;
                if (memcmp(ent, "ENIGMA     ", 11) == 0) {
                    hasVolLabel = true;
                }
                break;
            }
        }
        if (!hasVolLabel && labelSlot >= 0) {
            uint8_t* ent = &s_secBuf[labelSlot * 32];
            memset(ent, 0, 32);
            memcpy(ent, "ENIGMA     ", 11);
            ent[11] = 0x08; // ATTR_VOLUME_ID
            ent[24] = 0x21; ent[25] = 0x54; // Date
            sdmmc_write_sectors(s_activeCard, s_secBuf, rootDirLba, 1);
        }
    }

    // 3. Ensure FAT32 FSInfo sector has a valid FSI_Free_Count (not 0xFFFFFFFF)
    if (fsInfoSec > 0 && fsInfoSec < rsvdSecCnt) {
        uint32_t totalDataSectors = totSec32 - rsvdSecCnt - ((uint32_t)numFATs * fatSz32);
        uint32_t totalClusters = totalDataSectors / secPerClus;

        if (sdmmc_read_sectors(s_activeCard, s_fsiBuf, startLba + fsInfoSec, 1) == ESP_OK) {
            uint32_t usedClusters = 0;
            uint32_t lastUsed = 2;
            uint32_t fat1Lba = startLba + rsvdSecCnt;
            uint32_t scanSectors = (fatSz32 < 32) ? fatSz32 : 32;

            for (uint32_t s = 0; s < scanSectors; s++) {
                if (sdmmc_read_sectors(s_activeCard, s_secBuf, fat1Lba + s, 1) == ESP_OK) {
                    uint32_t* entries = (uint32_t*)s_secBuf;
                    for (int idx = 0; idx < 128; idx++) {
                        uint32_t clusIdx = s * 128 + idx;
                        if (clusIdx < 2) continue;
                        if ((entries[idx] & 0x0FFFFFFF) != 0) {
                            usedClusters++;
                            lastUsed = clusIdx;
                        }
                    }
                }
            }

            uint32_t freeCount = (totalClusters > usedClusters) ? (totalClusters - usedClusters) : 0;
            uint32_t nextFree = lastUsed + 1;

            s_fsiBuf[0] = 0x52; s_fsiBuf[1] = 0x52; s_fsiBuf[2] = 0x61; s_fsiBuf[3] = 0x41;
            s_fsiBuf[484] = 0x72; s_fsiBuf[485] = 0x72; s_fsiBuf[486] = 0x41; s_fsiBuf[487] = 0x61;
            s_fsiBuf[488] = (uint8_t)(freeCount & 0xFF);
            s_fsiBuf[489] = (uint8_t)((freeCount >> 8) & 0xFF);
            s_fsiBuf[490] = (uint8_t)((freeCount >> 16) & 0xFF);
            s_fsiBuf[491] = (uint8_t)((freeCount >> 24) & 0xFF);
            s_fsiBuf[492] = (uint8_t)(nextFree & 0xFF);
            s_fsiBuf[493] = (uint8_t)((nextFree >> 8) & 0xFF);
            s_fsiBuf[494] = (uint8_t)((nextFree >> 16) & 0xFF);
            s_fsiBuf[495] = (uint8_t)((nextFree >> 24) & 0xFF);
            s_fsiBuf[510] = 0x55; s_fsiBuf[511] = 0xAA;

            sdmmc_write_sectors(s_activeCard, s_fsiBuf, startLba + fsInfoSec, 1);
            if (bkBootSec > 0 && (bkBootSec + fsInfoSec) < rsvdSecCnt) {
                sdmmc_write_sectors(s_activeCard, s_fsiBuf, startLba + bkBootSec + fsInfoSec, 1);
            }
            Serial.printf("[STORAGE] Synced FAT32 FSInfo: freeClusters=%u, nextFree=%u\n", freeCount, nextFree);
        }
    }

    // 4. Ensure FAT[1] Clean Shutdown (0x08000000) and No Hard Error (0x04000000) bits are set
    uint32_t fat1Lba = startLba + rsvdSecCnt;
    if (sdmmc_read_sectors(s_activeCard, s_secBuf, fat1Lba, 1) == ESP_OK) {
        uint32_t* fatEntries = (uint32_t*)s_secBuf;
        if ((fatEntries[1] & 0x0C000000) != 0x0C000000) {
            fatEntries[1] |= 0x0C000000;
            sdmmc_write_sectors(s_activeCard, s_secBuf, fat1Lba, 1);
            if (numFATs >= 2) {
                sdmmc_write_sectors(s_activeCard, s_secBuf, fat1Lba + fatSz32, 1);
            }
            Serial.println("[STORAGE] Cleared FAT32 dirty volume bit.");
        }
    }
}

uint32_t StorageManager::getNextExFatCluster(uint32_t cluster, bool noFatChain) {
    if (cluster < 2) return 0xFFFFFFFF;
    if (noFatChain) return cluster + 1;
    uint32_t fatSector = _exfatStartLba + _exfatFatOffset + (cluster / 128);
    if (sdmmc_read_sectors(s_activeCard, s_fsiBuf, fatSector, 1) != ESP_OK) return 0xFFFFFFFF;
    uint32_t* entries = (uint32_t*)s_fsiBuf;
    return entries[cluster & 127];
}

bool StorageManager::findExFatEntry(uint32_t dirCluster, bool dirNoChain, const char* targetName,
                                    uint32_t& outFirstCluster, uint64_t& outSize, bool& outNoChain, bool& outIsDir) {
    if (!_exfatMounted || !s_activeCard || dirCluster < 2) return false;

    uint32_t spc = 1U << _exfatSpcShift;
    uint32_t curCluster = dirCluster;
    uint32_t curSecInCluster = 0;
    uint32_t maxSectors = 64; // Search up to 64 sectors (1024 directory entries)
    uint32_t sectorsRead = 0;

    auto readNextSector = [&]() -> bool {
        if (sectorsRead >= maxSectors || curCluster < 2 || curCluster >= 0xFFFFFFF6) return false;
        uint32_t lba = _exfatStartLba + _exfatClusterHeapOffset + ((curCluster - 2) << _exfatSpcShift) + curSecInCluster;
        if (sdmmc_read_sectors(s_activeCard, s_secBuf, lba, 1) != ESP_OK) return false;
        sectorsRead++;
        curSecInCluster++;
        if (curSecInCluster >= spc) {
            curSecInCluster = 0;
            curCluster = getNextExFatCluster(curCluster, dirNoChain);
        }
        return true;
    };

    int slotInSec = 16;
    auto nextEntry = [&]() -> uint8_t* {
        if (slotInSec >= 16) {
            if (!readNextSector()) return nullptr;
            slotInSec = 0;
        }
        return &s_secBuf[(slotInSec++) * 32];
    };

    while (true) {
        uint8_t* ent = nextEntry();
        if (!ent || ent[0] == 0x00) break; // End of directory

        if (ent[0] == 0x85) { // File Directory Entry
            uint8_t secCount = ent[1];
            uint16_t attr = ent[4] | (ent[5] << 8);
            bool isDir = (attr & 0x0010) != 0;
            if (secCount < 2) continue;

            uint8_t* streamEnt = nextEntry();
            if (!streamEnt || streamEnt[0] != 0xC0) continue;

            bool noFatChain = (streamEnt[1] & 0x02) != 0;
            uint8_t nameLen = streamEnt[3];
            uint64_t validDataLen = 0;
            memcpy(&validDataLen, &streamEnt[8], 8);
            uint32_t firstCluster = streamEnt[20] | (streamEnt[21] << 8) | (streamEnt[22] << 16) | (streamEnt[23] << 24);

            char nameBuf[128];
            int namePos = 0;
            for (int k = 0; k < (int)secCount - 1; k++) {
                uint8_t* nameEnt = nextEntry();
                if (!nameEnt) break;
                if (nameEnt[0] == 0xC1) {
                    for (int c = 0; c < 15 && namePos < nameLen && namePos < 127; c++) {
                        uint16_t ch = nameEnt[2 + c * 2] | (nameEnt[3 + c * 2] << 8);
                        nameBuf[namePos++] = (ch < 128) ? (char)ch : '?';
                    }
                }
            }
            nameBuf[namePos] = '\0';

            if (strcasecmp(nameBuf, targetName) == 0) {
                outFirstCluster = firstCluster;
                outSize = validDataLen;
                outNoChain = noFatChain;
                outIsDir = isDir;
                return true;
            }
        }
    }
    return false;
}

bool StorageManager::readExFatFile(const char* dirName, const char* fileName, String& outContent) {
    if (!_exfatMounted || !s_activeCard) return false;

    uint32_t ddmCluster = _exfatRootCluster;
    bool ddmNoChain = false;

    if (dirName && dirName[0] != '\0') {
        uint64_t dirSize = 0;
        bool isDir = false;
        if (!findExFatEntry(_exfatRootCluster, false, dirName, ddmCluster, dirSize, ddmNoChain, isDir) || !isDir) {
            return false;
        }
    }

    uint32_t fileCluster = 0;
    uint64_t fileSize = 0;
    bool fileNoChain = false;
    bool fileIsDir = false;
    if (!findExFatEntry(ddmCluster, ddmNoChain, fileName, fileCluster, fileSize, fileNoChain, fileIsDir) || fileIsDir) {
        return false;
    }

    outContent = "";
    if (fileSize == 0 || fileCluster < 2) return true;
    if (fileSize > 16384) fileSize = 16384; // Cap to 16KB safety limit

    outContent.reserve((size_t)fileSize + 1);
    uint32_t spc = 1U << _exfatSpcShift;
    uint32_t curCluster = fileCluster;
    uint32_t curSec = 0;
    uint64_t remaining = fileSize;

    while (remaining > 0 && curCluster >= 2 && curCluster < 0xFFFFFFF6) {
        uint32_t lba = _exfatStartLba + _exfatClusterHeapOffset + ((curCluster - 2) << _exfatSpcShift) + curSec;
        if (sdmmc_read_sectors(s_activeCard, s_secBuf, lba, 1) != ESP_OK) return false;

        uint32_t chunk = (remaining > 512) ? 512 : (uint32_t)remaining;
        for (uint32_t i = 0; i < chunk; i++) {
            if (s_secBuf[i] != 0) {
                outContent += (char)s_secBuf[i];
            }
        }
        remaining -= chunk;
        curSec++;
        if (curSec >= spc) {
            curSec = 0;
            curCluster = getNextExFatCluster(curCluster, fileNoChain);
        }
    }
    return true;
}

void StorageManager::lockSD() {
    if (s_sdMutex) xSemaphoreTake(s_sdMutex, portMAX_DELAY);
}

void StorageManager::unlockSD() {
    if (s_sdMutex) xSemaphoreGive(s_sdMutex);
}

bool StorageManager::begin(QuotesManager* quotesManager) {
    if (!s_sdMutex) {
        s_sdMutex = xSemaphoreCreateMutex();
    }

    // Hold USB enumeration while SD card is probed and volume metadata is repaired
    bool wasUsbInited = tud_inited();
    if (wasUsbInited) {
        tud_disconnect();
    }

    Serial.println("\n[STORAGE] Initializing MicroSD subsystem...");

    gpio_pullup_en((gpio_num_t)PIN_SD_CMD);
    gpio_pullup_en((gpio_num_t)PIN_SD_D0);
    gpio_pullup_en((gpio_num_t)PIN_SD_D1);
    gpio_pullup_en((gpio_num_t)PIN_SD_D2);
    gpio_pullup_en((gpio_num_t)PIN_SD_D3);
    gpio_pullup_en((gpio_num_t)PIN_SD_CLK);

    // Stage 1: Try mounting FAT32 VFS WITHOUT auto-formatting (so exFAT cards are never wiped!)
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    bool vfsMounted = SD_MMC.begin("/sdcard", false, false, 20000);
    if (vfsMounted) {
        _is4Bit = true;
    } else {
        SD_MMC.end();
        SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
        vfsMounted = SD_MMC.begin("/sdcard", true, false, 20000);
        if (vfsMounted) {
            _is4Bit = false;
        }
    }

    if (vfsMounted) {
        _vfsMounted = true;
        _exfatMounted = false;
        _sdMounted = true;
        s_activeCard = SDMMC_CardAccessor::getCard(SD_MMC);
        _totalBytes = SD_MMC.cardSize();
        _blockCount = _totalBytes / 512;

        if (quotesManager) {
            quotesManager->begin();
        }
        repairVolumeMetadata();
    } else {
        // VFS mount failed (card is exFAT or raw) -> Initialize 4-bit Raw SDMMC and mount exFAT reader!
        SD_MMC.end();
        _vfsMounted = false;
        if (initRawSD(true, SDMMC_FREQ_DEFAULT) ||
            initRawSD(true, 10000) ||
            initRawSD(false, SDMMC_FREQ_DEFAULT) ||
            initRawSD(false, 10000)) {
            _sdMounted = true;
            repairVolumeMetadata();
            if (quotesManager) {
                quotesManager->begin();
            }
        } else {
            _sdMounted = false;
        }
    }

    USBMSC& msc = getMsc();
    if (!_sdMounted || _blockCount == 0) {
        Serial.println("[STORAGE] ERROR: MicroSD card failed to initialize!");
        msc.mediaPresent(false);
        USBSerial.begin(115200);
        USB.begin();
        if (wasUsbInited) {
            delay(100);
            tud_connect();
        }
        return false;
    }

    Serial.printf("[STORAGE] SDMMC Ready! Capacity: %llu MB (%u sectors, %s bus, %s mode)\n",
                  _totalBytes / (1024 * 1024), _blockCount,
                  _is4Bit ? "4-bit" : "1-bit",
                  _vfsMounted ? "FAT32" : (_exfatMounted ? "exFAT" : "RAW"));

    msc.vendorID(USB_VENDOR_ID);
    msc.productID(USB_PRODUCT_ID);
    msc.productRevision(USB_REVISION);
    msc.onStartStop(onMscStartStop);
    msc.onRead(onMscRead);
    msc.onWrite(onMscWrite);
    msc.mediaPresent(true);
    msc.begin(_blockCount, 512);

    USBSerial.begin(115200);
    USB.begin();
    if (wasUsbInited) {
        delay(200);
        tud_connect();
    }

    return true;
}

bool StorageManager::checkWriteDirty() {
    if (!_vfsMounted && !_exfatMounted) return false;
    uint32_t now = millis();
    if (_writeDirty && (now - _lastWriteMillis > 2000) && (now - _lastReadMillis > 1000)) {
        _writeDirty = false;
        if (_vfsMounted) {
            lockSD();
            FATFS* fs = nullptr;
            DWORD fre_clust = 0;
            if (f_getfree("0:", &fre_clust, &fs) == FR_OK && fs) {
                fs->winsect = (DWORD)-1;
                fs->wflag = 0;
            }
            unlockSD();
        }
        return true;
    }
    return false;
}

uint64_t StorageManager::getTotalCapacityMB() const {
    if (!_sdMounted) return 0;
    return _totalBytes / (1024 * 1024);
}

uint64_t StorageManager::getFreeCapacityMB() const {
    if (!_sdMounted || !_vfsMounted) return 0;
    return (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024);
}
