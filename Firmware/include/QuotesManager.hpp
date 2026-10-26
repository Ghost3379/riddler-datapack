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

    // Quotes access
    size_t getQuoteCount() const { return _quotes.size(); }
    String getQuote(size_t index) const;
    String getNextQuote();
    String getRandomQuote();

private:
    void ensureDefaultFiles();
    bool loadConfig();
    bool loadQuotes();

    PropConfig _config;
    std::vector<String> _quotes;
    size_t _currentIndex = 0;
};
