#include "QuotesManager.hpp"
#include "StorageManager.hpp"
#include <FS.h>
#include <SD_MMC.h>
#include "esp_system.h"

static const char* DDM_DIR     = "/.DDM";
static const char* CONFIG_PATH = "/.DDM/config.ini";
static const char* QUOTES_PATH = "/.DDM/quotes.txt";

static const char* DEFAULT_CONFIG_TEXT =
"# ============================================================================\n"
"# ENIGMA DATAPACK - SYSTEM CONFIGURATION (.DDM = Don't Delete Me)\n"
"# Edit values below and save. Changes apply automatically within 2 seconds.\n"
"# ============================================================================\n"
"\n"
"[DISPLAY]\n"
"display_brightness = 85  ; Backlight brightness: 10 to 100 (%)\n"
"ticker_speed = 2         ; Quote scroll speed: 1 (slow) to 8 (fast)\n"
"quote_delay = 1.5s       ; Pause between quotes: e.g. 0.5s, 3, or random(5,10)\n"
"show_waveform = true     ; Show dynamic Arkham oscilloscope (true/false)\n"
"\n"
"[LEDS]\n"
"led_brightness = 38      ; LED master brightness: 5 to 255 (38 = ~15%)\n"
"ambient_hue = 96         ; Color hue: 0=Red, 85=Green, 96=Arkham Emerald, 160=Blue\n"
"pulse_speed = 14         ; Idle breathing rate in BPM: 5 to 60\n"
"flicker_on_io = true     ; Fluid organic ripple on disk read/write (true/false)\n";

static const char* DEFAULT_QUOTES_TEXT =
"# ============================================================================\n"
"# ENIGMA DATAPACK - RIDDLER QUOTES DATABASE (.DDM = Don't Delete Me)\n"
"# Add one quote, riddle, or terminal readout per line.\n"
"# Lines starting with # or ; are ignored.\n"
"# ============================================================================\n"
"Riddle me this: What is black and blue, and dead all over?\n"
"I am a question with no answer, until you look behind you.\n"
"If you are justice, please do not lie. What is the price for your blind eye?\n"
"GCPD ARCHIVE ENCRYPTION: LEVEL 5 // OVERRIDE ACCEPTED\n"
"Hope is a bird that sings in the dark. Silence it.\n"
"Wayne Enterprises Patent Registry: COVERT ACCESS GRANTED\n"
"What does a liar do when he's dead? He lies still.\n"
"You think you've solved it, detective? The game has only just begun.\n"
"Arkham Asylum inmate #4492-B: CONFIDENTIAL TRANSFER FILE\n"
"Fear is a tool. When that light hits the sky, it's not just a call... it's a warning.\n";

QuotesManager::QuotesManager() {
    // Populate fallback quotes before SD init so ticker is never empty
    _quotes.push_back("Riddle me this: What is black and blue, and dead all over?");
    _quotes.push_back("ENIGMA PROTOCOL ACTIVE // WAITING FOR HOST QUERY");
}

bool QuotesManager::begin() {
    StorageManager* sm = StorageManager::instance();
    bool vfs = sm && sm->isVfsMounted();
    StorageManager::lockSD();
    if (vfs) {
        ensureDefaultFiles();
    }
    bool cfgOk = loadConfig();
    bool qOk = loadQuotes();
    StorageManager::unlockSD();
    _sdLoaded = (cfgOk || qOk || (sm && sm->isMounted()));
    return _sdLoaded;
}

bool QuotesManager::reload() {
    Serial.println("[QUOTES] Hot-reloading /.DDM/config.ini and /.DDM/quotes.txt from SD...");
    StorageManager* sm = StorageManager::instance();
    bool vfs = sm && sm->isVfsMounted();
    StorageManager::lockSD();
    if (vfs) {
        ensureDefaultFiles();
    }
    bool cfgOk = loadConfig();
    bool qOk = loadQuotes();
    StorageManager::unlockSD();
    _sdLoaded = (cfgOk || qOk || (sm && sm->isMounted()));
    return _sdLoaded;
}

void QuotesManager::setBrightness(uint8_t brightness) {
    _config.display_brightness = constrain(brightness, 10, 100);
}

static float parseSecondsToken(String tok) {
    tok.trim();
    while (tok.endsWith("s") || tok.endsWith("S") || tok.endsWith(" ")) {
        tok.remove(tok.length() - 1);
    }
    float v = tok.toFloat();
    return constrain(v, 0.0f, 3600.0f);
}

static void parseQuoteDelayValue(String val, float& outMin, float& outMax) {
    val.trim();
    String lower = val;
    lower.toLowerCase();

    int openParen = lower.indexOf('(');
    int closeParen = lower.lastIndexOf(')');
    if (lower.startsWith("random") && openParen >= 0 && closeParen > openParen) {
        String inner = val.substring(openParen + 1, closeParen);
        int commaIdx = inner.indexOf(',');
        if (commaIdx < 0) commaIdx = inner.indexOf('-');
        if (commaIdx >= 0) {
            float a = parseSecondsToken(inner.substring(0, commaIdx));
            float b = parseSecondsToken(inner.substring(commaIdx + 1));
            outMin = (a < b) ? a : b;
            outMax = (a > b) ? a : b;
            return;
        }
    }

    float single = parseSecondsToken(val);
    outMin = single;
    outMax = single;
}

uint32_t QuotesManager::getNextQuoteDelayMs() const {
    uint32_t minMs = (uint32_t)roundf(_config.quote_delay_min * 1000.0f);
    uint32_t maxMs = (uint32_t)roundf(_config.quote_delay_max * 1000.0f);
    if (maxMs <= minMs) return minMs;
    uint32_t span = maxMs - minMs + 1;
    uint32_t chosen = minMs + (esp_random() % span);
    Serial.printf("[QUOTES] Next quote delay: %.2fs (%u ms)\n", chosen / 1000.0f, chosen);
    return chosen;
}

void QuotesManager::ensureDefaultFiles() {
    if (!SD_MMC.exists(DDM_DIR)) {
        if (SD_MMC.mkdir(DDM_DIR)) {
            Serial.println("[QUOTES] Created /.DDM (Don't Delete Me) folder on SD card.");
        }
    }

    if (!SD_MMC.exists(CONFIG_PATH)) {
        File f = SD_MMC.open(CONFIG_PATH, FILE_WRITE);
        if (f) {
            f.print(DEFAULT_CONFIG_TEXT);
            f.close();
            Serial.println("[QUOTES] Created default /.DDM/config.ini on SD card.");
        }
    }

    if (!SD_MMC.exists(QUOTES_PATH)) {
        File f = SD_MMC.open(QUOTES_PATH, FILE_WRITE);
        if (f) {
            f.print(DEFAULT_QUOTES_TEXT);
            f.close();
            Serial.println("[QUOTES] Created default /.DDM/quotes.txt on SD card.");
        }
    }
}

void QuotesManager::parseConfigText(const String& content) {
    String currentSection = "";
    int start = 0;
    int len = content.length();

    while (start < len) {
        int end = content.indexOf('\n', start);
        if (end < 0) end = len;
        String line = content.substring(start, end);
        start = end + 1;
        if (line.startsWith("\xEF\xBB\xBF")) {
            line = line.substring(3);
        }
        line.trim();

        if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) continue;

        if (line.startsWith("[") && line.endsWith("]")) {
            currentSection = line.substring(1, line.length() - 1);
            currentSection.trim();
            continue;
        }

        int eqIdx = line.indexOf('=');
        if (eqIdx < 0) continue;

        String key = line.substring(0, eqIdx);
        String val = line.substring(eqIdx + 1);
        key.trim();
        val.trim();

        int commentIdx = val.indexOf(';');
        if (commentIdx >= 0) val = val.substring(0, commentIdx);
        commentIdx = val.indexOf('#');
        if (commentIdx >= 0) val = val.substring(0, commentIdx);
        val.trim();

        if (key.equalsIgnoreCase("display_brightness") ||
            (key.equalsIgnoreCase("brightness") && !currentSection.equalsIgnoreCase("LEDS"))) {
            _config.display_brightness = constrain(val.toInt(), 10, 100);
        } else if (key.equalsIgnoreCase("led_brightness") ||
                   (key.equalsIgnoreCase("brightness") && currentSection.equalsIgnoreCase("LEDS"))) {
            _config.led_brightness = constrain(val.toInt(), 0, 255);
        } else if (key.equalsIgnoreCase("ticker_speed")) {
            _config.ticker_speed = constrain(val.toInt(), 1, 8);
        } else if (key.equalsIgnoreCase("quote_delay") ||
                   key.equalsIgnoreCase("quote_pause") ||
                   key.equalsIgnoreCase("quote_interval")) {
            parseQuoteDelayValue(val, _config.quote_delay_min, _config.quote_delay_max);
        } else if (key.equalsIgnoreCase("show_waveform")) {
            _config.show_waveform = (val.equalsIgnoreCase("true") || val.toInt() == 1);
        } else if (key.equalsIgnoreCase("ambient_hue")) {
            _config.ambient_hue = constrain(val.toInt(), 0, 255);
        } else if (key.equalsIgnoreCase("pulse_speed")) {
            _config.pulse_speed = constrain(val.toInt(), 5, 60);
        } else if (key.equalsIgnoreCase("flicker_on_io")) {
            _config.flicker_on_io = (val.equalsIgnoreCase("true") || val.toInt() == 1);
        }
    }
    Serial.printf("[CONFIG] Loaded /.DDM/config.ini: Disp=%d%%, LEDs=%d, Hue=%d, Pulse=%dBPM, Ticker=%d\n",
                  _config.display_brightness, _config.led_brightness,
                  _config.ambient_hue, _config.pulse_speed, _config.ticker_speed);
}

bool QuotesManager::loadConfig() {
    StorageManager* sm = StorageManager::instance();
    if (sm && sm->isVfsMounted()) {
        if (!SD_MMC.exists(CONFIG_PATH)) return false;
        File f = SD_MMC.open(CONFIG_PATH, FILE_READ);
        if (!f) return false;
        String content = f.readString();
        f.close();
        parseConfigText(content);
        return true;
    } else if (sm && sm->isExFat()) {
        String content;
        if (sm->readExFatFile(".DDM", "config.ini", content)) {
            parseConfigText(content);
            return true;
        }
    }
    return false;
}

bool QuotesManager::parseQuotesText(const String& content) {
    std::vector<String> loaded;
    int start = 0;
    int len = content.length();

    while (start < len) {
        int end = content.indexOf('\n', start);
        if (end < 0) end = len;
        String line = content.substring(start, end);
        start = end + 1;
        if (line.startsWith("\xEF\xBB\xBF")) {
            line = line.substring(3);
        }
        line.trim();

        if (line.endsWith("\r")) line.remove(line.length() - 1);
        if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) continue;

        loaded.push_back(line);
    }

    if (!loaded.empty()) {
        _quotes = loaded;
        _currentIndex = 0;
        Serial.printf("[QUOTES] Loaded %d quotes from /.DDM/quotes.txt\n", (int)_quotes.size());
        return true;
    }
    return false;
}

bool QuotesManager::loadQuotes() {
    StorageManager* sm = StorageManager::instance();
    if (sm && sm->isVfsMounted()) {
        if (!SD_MMC.exists(QUOTES_PATH)) return false;
        File f = SD_MMC.open(QUOTES_PATH, FILE_READ);
        if (!f) return false;
        String content = f.readString();
        f.close();
        return parseQuotesText(content);
    } else if (sm && sm->isExFat()) {
        String content;
        if (sm->readExFatFile(".DDM", "quotes.txt", content)) {
            return parseQuotesText(content);
        }
    }
    return false;
}

String QuotesManager::getQuote(size_t index) const {
    if (_quotes.empty()) return "NO QUOTES LOADED";
    return _quotes[index % _quotes.size()];
}

String QuotesManager::getNextQuote() {
    if (_quotes.empty()) return "NO QUOTES LOADED";
    String q = _quotes[_currentIndex];
    _currentIndex = (_currentIndex + 1) % _quotes.size();
    return q;
}

String QuotesManager::getRandomQuote() {
    if (_quotes.empty()) return "NO QUOTES LOADED";
    size_t idx = random((long)_quotes.size());
    return _quotes[idx];
}
