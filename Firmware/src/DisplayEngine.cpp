#include "DisplayEngine.hpp"
#include <math.h>

// Arkham Color Palette (RGB565)
static const uint16_t COLOR_BG         = 0x0000; // Deep Black
static const uint16_t COLOR_PANEL_BG   = 0x0882; // Very Dark Slate Green
static const uint16_t COLOR_EMERALD    = 0x07E0; // Neon Green (#00FF00)
static const uint16_t COLOR_DARK_GREEN = 0x03E0; // Forest Green
static const uint16_t COLOR_DIM_GREEN  = 0x01E0; // Subtle Background Green
static const uint16_t COLOR_WHITE      = 0xFFFF; // Pure White

// Deterministic smooth 1D value noise (-1.0f to +1.0f) for subtle background static
static float hash1D(int32_t n) {
    n = (n << 13) ^ n;
    int32_t m = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
    return 1.0f - ((float)m / 1073741824.0f);
}

static float smoothNoise1D(float x) {
    int32_t i = (int32_t)floorf(x);
    float f = x - (float)i;
    float u = f * f * (3.0f - 2.0f * f);
    return hash1D(i) * (1.0f - u) + hash1D(i + 1) * u;
}

DisplayEngine::DisplayEngine() : _canvas(&_lcd) {
}

void DisplayEngine::begin(QuotesManager* quotesManager) {
    _quotesManager = quotesManager;

    _lcd.init();
    _lcd.setRotation(3); // Landscape (240 x 135)
    _lcd.setBrightness(quotesManager ? quotesManager->getConfig().display_brightness : 85);
    _lcd.fillScreen(COLOR_BG);

    // Initialize full-screen double-buffered sprite
    _canvas.setColorDepth(16);
    _canvas.createSprite(TFT_WIDTH, TFT_HEIGHT);
    _canvas.setTextWrap(false);

    Serial.println("[DISPLAY] ST7789 initialized with 240x135 16-bit double buffer.");
}

void DisplayEngine::setBrightness(uint8_t pct) {
    _lcd.setBrightness(constrain(pct, 10, 100));
}

void DisplayEngine::notifyDiskActivity(bool is_write) {
    uint32_t now = millis();
    _lastIoActivityMillis = now;
    if (is_write) {
        _lastWriteActivityMillis = now;
        _targetDir = -1.0f; // Write: wave traverses forward (left -> right)
    } else if (now - _lastWriteActivityMillis > 300) {
        _targetDir = +1.0f; // Read: wave traverses backward (right -> left)
    }
}

void DisplayEngine::updateQuoteMetrics() {
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _textWidth = _canvas.textWidth(_currentQuote.c_str());
}

void DisplayEngine::refreshQuote() {
    if (_quotesManager) {
        _currentQuote = _quotesManager->getNextQuote();
        updateQuoteMetrics();
        _tickerX = TFT_WIDTH;
        _waitingForNextQuote = false;
    }
}

void DisplayEngine::playBootAnimation(bool sdMounted, uint64_t capacityMB, bool is4Bit) {
    _lcd.fillScreen(COLOR_BG);

    // --- Stage 1: CRT Horizontal Flare ---
    for (int h = 1; h <= 6; h++) {
        _lcd.drawFastHLine(0, TFT_HEIGHT / 2 - h / 2, TFT_WIDTH, COLOR_EMERALD);
        delay(20);
    }
    _lcd.fillScreen(COLOR_BG);

    // --- Stage 2: Rapid Cyber POST Readout ---
    _lcd.setFont(&fonts::Font0);
    _lcd.setTextColor(COLOR_EMERALD, COLOR_BG);
    _lcd.setTextSize(1);

    char sdLine[36];
    if (sdMounted) {
        snprintf(sdLine, sizeof(sdLine), "> SD: %lluMB [%s]", capacityMB, is4Bit ? "4-BIT" : "1-BIT");
    } else {
        snprintf(sdLine, sizeof(sdLine), "> SD: NO CARD / CHECK LATCH");
    }

    const char* bootLines[] = {
        "> NYGMA-OS v1p0",
        "> ESP32-S3 @ 240MHz ... OK",
        sdLine,
        "> CRYPT_SUITE ... ARMED",
        sdMounted ? "> ENIGMA DRIVE ONLINE" : "> ENIGMA DRIVE OFFLINE"
    };

    for (int i = 0; i < 5; i++) {
        _lcd.setCursor(14, 20 + (i * 18));
        _lcd.print(bootLines[i]);
        delay(90);
    }
    delay(250);

    // Prime the first quote for the ticker
    refreshQuote();
    _lastRenderMillis = millis();
}

void DisplayEngine::renderCenterVisuals(float dt) {
    _frameCount++;
    uint32_t now = millis();

    // 1. Update 0.5-second smooth transition between subtle noise (0.0) and full sine (1.0)
    bool ioActive = (_lastIoActivityMillis != 0) && (now - _lastIoActivityMillis < 300);
    if (ioActive) {
        _sineBlend += dt / 0.50f; // 0.5s morph into full sine
        if (_sineBlend > 1.0f) _sineBlend = 1.0f;
    } else {
        _sineBlend -= dt / 0.50f; // 0.5s dissolve back into subtle noise
        if (_sineBlend < 0.0f) _sineBlend = 0.0f;
    }

    // Smoothly interpolate wave traversal direction (-1.0 = forward L->R on write, +1.0 = backward R->L on read)
    _waveDir += (_targetDir - _waveDir) * 0.20f;

    // Advance phases
    _wavePhase  += _waveDir * (0.05f + 0.16f * _sineBlend);
    _noisePhase += 0.35f;

    // Smoothstep blend factor (0.0 = pure subtle noise, 1.0 = pure full sine)
    float s = _sineBlend * _sineBlend * (3.0f - 2.0f * _sineBlend);

    int centerX = TFT_WIDTH / 2;   // 120
    int centerY = 53;              // Vertical center of upper area (0..106)
    int circleRadius = 38;

    // 2. Render Background Signal across full width (x = 0..239)
    bool showWave = !_quotesManager || _quotesManager->getConfig().show_waveform;
    if (showWave) {
        // Interpolate wave color from subtle dim green (idle noise) to bright emerald green (active sine)
        uint8_t gVal = (uint8_t)(65.0f + s * 190.0f); // 65 (subtle) -> 255 (bright green)
        uint16_t waveColor = _lcd.color565(0, gVal, (uint8_t)(s * 20.0f));

        int prevX = 0;
        int prevY = centerY;

        for (int x = 0; x < TFT_WIDTH; x += 2) {
            // Subtle low-amplitude background noise (~ +/- 3.5px + tiny jitter)
            float n1 = smoothNoise1D((float)x * 0.16f + _noisePhase);
            float n2 = smoothNoise1D((float)x * 0.45f - _noisePhase * 1.7f) * 0.45f;
            float noiseOffset = (n1 + n2) * 3.2f;

            // Full sine wave (~2 periods across 240px, amplitude 24px)
            float sineOffset = sinf((float)x * 0.05236f + _wavePhase) * 24.0f;

            // Morph smoothly between subtle noise and full sine
            float yFloat = (float)centerY + (1.0f - s) * noiseOffset + s * sineOffset;
            int y = constrain((int)roundf(yFloat), 4, 102);

            if (x > 0) {
                _canvas.drawLine(prevX, prevY, x, y, waveColor);
                if (s > 0.35f) {
                    // Slightly thicker trace when morphed into active sine wave
                    _canvas.drawLine(prevX, prevY + 1, x, y + 1, COLOR_DARK_GREEN);
                }
            }
            prevX = x;
            prevY = y;
        }
    }

    // 3. Render Central Riddler Question Mark Emblem on top (occluding the wave behind the circle)
    _canvas.fillCircle(centerX, centerY, circleRadius, COLOR_BG);

    // Crisp multi-ring green border around the emblem
    _canvas.drawCircle(centerX, centerY, circleRadius, COLOR_EMERALD);
    _canvas.drawCircle(centerX, centerY, circleRadius - 1, COLOR_EMERALD);
    _canvas.drawCircle(centerX, centerY, circleRadius - 2, COLOR_DARK_GREEN);

    // Huge Green Question Mark mathematically centered inside the circle
    // FreeSansBold24pt7b '?' glyph is 24px wide x 35px tall (xOffset=3, yOffset=-34, metrics.y_offset=-35)
    // With top_left datum, drawString places the 24x35 bitmap at (x, y + 1).
    int qX = centerX - 12; // Spans [centerX - 12 .. centerX + 12]
    int qY = centerY - 18; // Spans [centerY - 17 .. centerY + 17]
    _canvas.setFont(&fonts::FreeSansBold24pt7b);
    _canvas.setTextSize(1);
    _canvas.setTextDatum( textdatum_t::top_left );
    // Subtle dark green shadow/glow offset for depth
    _canvas.setTextColor(COLOR_DARK_GREEN);
    _canvas.drawString("?", qX + 1, qY + 1);
    // Bright neon green foreground '?'
    _canvas.setTextColor(COLOR_EMERALD);
    _canvas.drawString("?", qX, qY);
}

void DisplayEngine::renderBottomTicker() {
    int lineY = 106;
    int tickerY = 107;
    int tickerHeight = TFT_HEIGHT - tickerY; // 28px

    // Dark grayish-slate panel background + green top border line
    _canvas.fillRect(0, tickerY, TFT_WIDTH, tickerHeight, COLOR_PANEL_BG);
    _canvas.drawFastHLine(0, lineY, TFT_WIDTH, COLOR_DARK_GREEN);

    uint32_t now = millis();

    // Handle pause between quotes (e.g. quote_delay = 0.5s or random(5,10))
    if (_waitingForNextQuote) {
        if (now - _quoteWaitStartMillis >= _quoteWaitDurationMs) {
            _waitingForNextQuote = false;
            if (_quotesManager) {
                _currentQuote = _quotesManager->getNextQuote();
                updateQuoteMetrics();
            }
            _tickerX = TFT_WIDTH + 10;
            _lastTickerMillis = now;
        } else {
            return; // Keep bottom ticker panel visible during pause
        }
    }

    // Update ticker scroll position
    uint8_t speed = _quotesManager ? _quotesManager->getConfig().ticker_speed : 2;
    if (now - _lastTickerMillis >= (uint32_t)(24 / speed)) {
        _lastTickerMillis = now;
        _tickerX -= 2;

        // When current quote completely scrolls past the left edge, enter pause or pick next quote
        if (_tickerX < -_textWidth) {
            uint32_t delayMs = _quotesManager ? _quotesManager->getNextQuoteDelayMs() : 0;
            if (delayMs > 0) {
                _waitingForNextQuote = true;
                _quoteWaitStartMillis = now;
                _quoteWaitDurationMs = delayMs;
                return;
            } else {
                if (_quotesManager) {
                    _currentQuote = _quotesManager->getNextQuote();
                    updateQuoteMetrics();
                }
                _tickerX = TFT_WIDTH + 10;
            }
        }
    }

    // Draw rolling quote text in crisp 16px Font2 on dark grayish panel background
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_EMERALD, COLOR_PANEL_BG);
    _canvas.setCursor(_tickerX, tickerY + 6);
    _canvas.print(_currentQuote);
}

void DisplayEngine::update() {
    uint32_t now = millis();
    uint32_t elapsed = now - _lastRenderMillis;
    if (elapsed < 25) return; // ~40 FPS rendering loop
    float dt = (elapsed > 200) ? 0.025f : ((float)elapsed / 1000.0f);
    _lastRenderMillis = now;

    // Clear buffer
    _canvas.fillScreen(COLOR_BG);

    // Render center emblem + background noise/sine wave and bottom rolling ticker
    renderCenterVisuals(dt);
    renderBottomTicker();

    // Push double-buffered frame to ST7789 display with zero tear
    _canvas.pushSprite(0, 0);
}
