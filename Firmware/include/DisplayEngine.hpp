#pragma once

#include <Arduino.h>
#include "Config.hpp"
#include "LGFX_Config.hpp"
#include "QuotesManager.hpp"

class DisplayEngine {
public:
    DisplayEngine();

    void begin(QuotesManager* quotesManager);
    void playBootAnimation();
    void update();

    // Trigger visual flash / HUD banner on file transfer
    void notifyDiskActivity(bool is_write);

    // Set brightness (0-100%)
    void setBrightness(uint8_t pct);

private:
    void renderTopBar();
    void renderCenterVisuals();
    void renderBottomTicker();

    LGFX_Mini _lcd;
    LGFX_Sprite _canvas;
    QuotesManager* _quotesManager = nullptr;

    // Ticker state
    String _currentQuote = "";
    int32_t _tickerX = 0;
    int32_t _textWidth = 0;
    uint32_t _lastTickerMillis = 0;

    // Animation & Waveform state
    float _wavePhase = 0.0f;
    uint32_t _frameCount = 0;
    uint32_t _lastRenderMillis = 0;

    // Disk activity visual flag
    uint32_t _lastIoActivityMillis = 0;
};
