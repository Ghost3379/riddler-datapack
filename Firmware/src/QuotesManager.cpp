#include "QuotesManager.hpp"
#include <FS.h>
#include <SD_MMC.h>

static const char* CONFIG_PATH = "/CONFIG/config.ini";
static const char* QUOTES_PATH = "/CONFIG/quotes.txt";

static const char* DEFAULT_CONFIG_TEXT = 
"# ENIGMA DATAPACK CONFIGURATION\n"
"[DISPLAY]\n"
"brightness = 85          ; 10 to 100%\n"
"ticker_speed = 2         ; 1 (slow) to 8 (fast)\n"
"show_waveform = true     ; dynamic Arkham oscilloscope\n"
"\n"
"[LEDS]\n"
"brightness = 38          ; 15% brightness (0 to 255)\n"
"ambient_hue = 96         ; 96 = Arkham Emerald Green (0-255)\n"
"pulse_speed = 14         ; Ambient breath rate (BPM)\n"
"flicker_on_io = true     ; Fluid organic ripple on disk read/write\n";

static const char* DEFAULT_QUOTES_TEXT = 
"# RIDDLER QUOTES DATABASE\n"
"# Add one quote or riddle per line:\n"
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
}

bool QuotesManager::begin() {
    ensureDefaultFiles();
    loadConfig();
    return loadQuotes();
}

bool QuotesManager::reload() {
    Serial.println("[QUOTES] Hot-reloading config and quotes from SD...");
    loadConfig();
    return loadQuotes();
}

void QuotesManager::ensureDefaultFiles() {
    if (!SD_MMC.exists("/CONFIG")) {
        SD_MMC.mkdir("/CONFIG");
    }

    if (!SD_MMC.exists(CONFIG_PATH)) {
        File f = SD_MMC.open(CONFIG_PATH, FILE_WRITE);
        if (f) {
            f.print(DEFAULT_CONFIG_TEXT);
            f.close();
            Serial.println("[QUOTES] Created default config.ini on SD card.");
        }
    }

    if (!SD_MMC.exists(QUOTES_PATH)) {
        File f = SD_MMC.open(QUOTES_PATH, FILE_WRITE);
        if (f) {
            f.print(DEFAULT_QUOTES_TEXT);
            f.close();
            Serial.println("[QUOTES] Created default quotes.txt on SD card.");
        }
    }
}

bool QuotesManager::loadConfig() {
    if (!SD_MMC.exists(CONFIG_PATH)) return false;

    File f = SD_MMC.open(CONFIG_PATH, FILE_READ);
    if (!f) return false;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();

        // Skip comments or empty lines
        if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) continue;

        int eqIdx = line.indexOf('=');
        if (eqIdx < 0) continue;

        String key = line.substring(0, eqIdx);
        String val = line.substring(eqIdx + 1);
        key.trim();
        val.trim();

        // Strip inline comments if any
        int commentIdx = val.indexOf(';');
        if (commentIdx >= 0) val = val.substring(0, commentIdx);
        commentIdx = val.indexOf('#');
        if (commentIdx >= 0) val = val.substring(0, commentIdx);
        val.trim();

        if (key.equalsIgnoreCase("brightness")) {
            _config.display_brightness = constrain(val.toInt(), 10, 100);
        } else if (key.equalsIgnoreCase("ticker_speed")) {
            _config.ticker_speed = constrain(val.toInt(), 1, 8);
        } else if (key.equalsIgnoreCase("show_waveform")) {
            _config.show_waveform = (val.equalsIgnoreCase("true") || val.toInt() == 1);
        } else if (key.equalsIgnoreCase("led_brightness")) {
            _config.led_brightness = constrain(val.toInt(), 0, 255);
        } else if (key.equalsIgnoreCase("ambient_hue")) {
            _config.ambient_hue = constrain(val.toInt(), 0, 255);
        } else if (key.equalsIgnoreCase("pulse_speed")) {
            _config.pulse_speed = constrain(val.toInt(), 5, 60);
        } else if (key.equalsIgnoreCase("flicker_on_io")) {
            _config.flicker_on_io = (val.equalsIgnoreCase("true") || val.toInt() == 1);
        }
    }
    f.close();
    Serial.printf("[CONFIG] Loaded: Disp=%d%%, LEDs=%d, TickerSpeed=%d\n", 
                  _config.display_brightness, _config.led_brightness, _config.ticker_speed);
    return true;
}

bool QuotesManager::loadQuotes() {
    if (!SD_MMC.exists(QUOTES_PATH)) return false;

    File f = SD_MMC.open(QUOTES_PATH, FILE_READ);
    if (!f) return false;

    _quotes.clear();
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();

        // Strip carriage returns and quotes if quoted
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) continue;

        _quotes.push_back(line);
    }
    f.close();

    Serial.printf("[QUOTES] Loaded %d quotes from quotes.txt\n", (int)_quotes.size());

    // Fallback if empty
    if (_quotes.empty()) {
        _quotes.push_back("Riddle me this: What is black and blue, and dead all over?");
        _quotes.push_back("ENIGMA PROTOCOL ACTIVE // WAITING FOR HOST QUERY");
    }

    _currentIndex = 0;
    return true;
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
