#pragma once

#include <Arduino.h>
#include <vector>
#include "Config.hpp"

class QuotesManager {
public:
    QuotesManager();

    // Initialize & load files from SD card
    bool begin();

    // Hot-reload configuration and quotes from SD
    bool reload();

    // Configuration access
    const PropConfig& getConfig() const { return _config; }
    void setBrightness(uint8_t brightness);
    uint32_t getNextQuoteDelayMs() const;

    // Quotes access
    size_t getQuoteCount() const { return _quotes.size(); }
    String getQuote(size_t index) const;
    String getNextQuote();
    String getRandomQuote();

    // Status
    bool isSdMounted() const { return _sdLoaded; }

private:
    void ensureDefaultFiles();
    bool loadConfig();
    bool loadQuotes();
    void parseConfigText(const String& content);
    bool parseQuotesText(const String& content);

    PropConfig _config;
    std::vector<String> _quotes;
    size_t _currentIndex = 0;
    bool _sdLoaded = false;
};
