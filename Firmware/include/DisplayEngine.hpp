#pragma once

#include <Arduino.h>
#include "Config.hpp"
#include "LGFX_Config.hpp"
#include "QuotesManager.hpp"

class DisplayEngine {
public:
    DisplayEngine();

    void begin(QuotesManager* quotesManager);
    void playBootAnimation(bool sdMounted = true, uint64_t capacityMB = 0, bool is4Bit = true);
    void update();

    // Trigger visual flash / HUD banner on file transfer
    void notifyDiskActivity(bool is_write);

    // Immediately advance/refresh the quote shown on the ticker
    void refreshQuote();

    // Set brightness (0-100%)
    void setBrightness(uint8_t pct);

private:
    void renderCenterVisuals(float dt);
    void renderBottomTicker();
    void updateQuoteMetrics();

    LGFX_Mini _lcd;
    LGFX_Sprite _canvas;
    QuotesManager* _quotesManager = nullptr;

    // Ticker state
    String _currentQuote = "";
    int32_t _tickerX = 0;
    int32_t _textWidth = 0;
    uint32_t _lastTickerMillis = 0;
    bool _waitingForNextQuote = false;
    uint32_t _quoteWaitStartMillis = 0;
    uint32_t _quoteWaitDurationMs = 0;

    // Animation & Waveform state
    float _wavePhase = 0.0f;
    float _noisePhase = 0.0f;
    float _sineBlend = 0.0f;      // 0.0 = subtle noise, 1.0 = full sine wave (0.5s transition)
    float _waveDir = -1.0f;       // -1.0 = forward (write, L->R), +1.0 = backward (read, R->L)
    float _targetDir = -1.0f;
    uint32_t _frameCount = 0;
    uint32_t _lastRenderMillis = 0;

    // Disk activity visual timestamps
    volatile uint32_t _lastIoActivityMillis = 0;
    volatile uint32_t _lastWriteActivityMillis = 0;
};
