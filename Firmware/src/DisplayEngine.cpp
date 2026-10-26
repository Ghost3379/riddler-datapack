#include "DisplayEngine.hpp"

// Arkham Color Palette (RGB565)
static const uint16_t COLOR_BG         = 0x0000; // Deep Black
static const uint16_t COLOR_PANEL_BG   = 0x0882; // Very Dark Slate Green
static const uint16_t COLOR_EMERALD    = 0x07E0; // Neon Green (#00FF00)
static const uint16_t COLOR_DARK_GREEN = 0x03E0; // Forest Green
static const uint16_t COLOR_DIM_GREEN  = 0x0200; // Low Ambient Green
static const uint16_t COLOR_WHITE      = 0xFFFF; // Pure White
static const uint16_t COLOR_CYAN       = 0x07FF; // Cyan Accent

DisplayEngine::DisplayEngine() : _canvas(&_lcd) {
}

void DisplayEngine::begin(QuotesManager* quotesManager) {
    _quotesManager = quotesManager;

    _lcd.init();
    _lcd.setRotation(1); // Landscape (240 x 135)
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
    _lastIoActivityMillis = millis();
}

void DisplayEngine::playBootAnimation() {
    _lcd.fillScreen(COLOR_BG);

    // --- Stage 1: CRT Horizontal Flare ---
    for (int h = 1; h <= 6; h++) {
        _lcd.drawFastHLine(0, TFT_HEIGHT / 2 - h / 2, TFT_WIDTH, COLOR_WHITE);
        delay(25);
    }
    _lcd.fillScreen(COLOR_BG);

    // --- Stage 2: Rapid Cyber POST Readout ---
    _lcd.setTextColor(COLOR_EMERALD, COLOR_BG);
    _lcd.setTextSize(1);

    const char* bootLines[] = {
        "> NYGMA_OS KERNEL v1.2",
        "> ESP32-S3 @ 240MHz ... OK",
        "> 4-BIT SDMMC LINK ... 48 MB/s",
        "> CRYPT_SUITE ... ARMED",
        "> ENIGMA DRIVE MOUNTED"
    };

    for (int i = 0; i < 5; i++) {
        _lcd.setCursor(14, 20 + (i * 18));
        _lcd.print(bootLines[i]);
        delay(120);
    }
    delay(350);

    // --- Stage 3: Glowing Question Mark Flare ---
    for (int r = 5; r <= 35; r += 5) {
        _lcd.fillScreen(COLOR_BG);
        _lcd.drawCircle(TFT_WIDTH / 2, TFT_HEIGHT / 2, r, COLOR_DARK_GREEN);
        _lcd.drawCircle(TFT_WIDTH / 2, TFT_HEIGHT / 2, r + 2, COLOR_EMERALD);
        _lcd.setTextColor(COLOR_WHITE, COLOR_BG);
        _lcd.setTextSize(3);
        _lcd.setCursor(TFT_WIDTH / 2 - 9, TFT_HEIGHT / 2 - 12);
        _lcd.print("?");
        delay(40);
    }
    delay(300);

    // Prime the first quote for the ticker
    if (_quotesManager) {
        _currentQuote = _quotesManager->getNextQuote();
        _canvas.setTextSize(1);
        _textWidth = _canvas.textWidth(_currentQuote.c_str());
        _tickerX = TFT_WIDTH; // Start off right edge
    }
}

void DisplayEngine::renderTopBar() {
    // Header background bar
    _canvas.fillRect(0, 0, TFT_WIDTH, 18, COLOR_PANEL_BG);
    _canvas.drawFastHLine(0, 18, TFT_WIDTH, COLOR_DARK_GREEN);

    // Left Title
    _canvas.setTextColor(COLOR_EMERALD, COLOR_PANEL_BG);
    _canvas.setTextSize(1);
    _canvas.setCursor(6, 5);
    _canvas.print("[?] RIDDLER-PACK");

    // Right Status Indicator
    bool isTransferring = (millis() - _lastIoActivityMillis < 500);
    _canvas.setCursor(148, 5);
    if (isTransferring) {
        _canvas.setTextColor(COLOR_WHITE, COLOR_PANEL_BG);
        _canvas.print("DATA I/O >>");
    } else {
        _canvas.setTextColor(COLOR_DARK_GREEN, COLOR_PANEL_BG);
        _canvas.print("ONLINE // OK");
    }
}

void DisplayEngine::renderCenterVisuals() {
    _frameCount++;
    _wavePhase += 0.08f;

    // --- Left Section: Animated Glowing Question Mark ---
    int qCenterX = 30;
    int qCenterY = 60;

    // Concentric breathing sonar rings
    float pulse = (sin(_frameCount * 0.06f) + 1.0f) * 0.5f; // 0.0 to 1.0
    int ringRadius = 16 + (int)(pulse * 6.0f);
    _canvas.drawCircle(qCenterX, qCenterY, ringRadius, COLOR_DIM_GREEN);
    _canvas.drawCircle(qCenterX, qCenterY, 24, 0x01A0);

    // Stylized Question Mark Symbol
    _canvas.setTextColor(COLOR_EMERALD, COLOR_BG);
    _canvas.setTextSize(2);
    _canvas.setCursor(qCenterX - 6, qCenterY - 7);
    _canvas.print("?");

    // Vertical separator
    _canvas.drawFastVLine(62, 22, 80, COLOR_DIM_GREEN);

    // --- Right Section: Dynamic Arkham Oscilloscope Waveform ---
    int waveStartX = 70;
    int waveEndX   = 232;
    int waveCenterY = 58;

    // Background tactical telemetry lines
    _canvas.drawFastHLine(waveStartX, waveCenterY, waveEndX - waveStartX, 0x0160);
    _canvas.drawFastHLine(waveStartX, waveCenterY - 20, waveEndX - waveStartX, 0x00E0);
    _canvas.drawFastHLine(waveStartX, waveCenterY + 20, waveEndX - waveStartX, 0x00E0);

    // Dynamic Waveform plotting
    int prevX = waveStartX;
    int prevY = waveCenterY;

    bool isTransferring = (millis() - _lastIoActivityMillis < 600);
    float ampBoost = isTransferring ? 1.6f : 1.0f;

    for (int x = waveStartX; x <= waveEndX; x += 3) {
        float sample1 = sin((x * 0.07f) + _wavePhase) * (14.0f * ampBoost);
        float sample2 = sin((x * 0.14f) - (_wavePhase * 1.3f)) * (6.0f * ampBoost);
        int y = waveCenterY + (int)(sample1 + sample2);
        y = constrain(y, 24, 98);

        uint16_t waveColor = isTransferring ? COLOR_WHITE : COLOR_EMERALD;
        _canvas.drawLine(prevX, prevY, x, y, waveColor);
        prevX = x;
        prevY = y;
    }

    // Small cyber telemetry footer
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_DARK_GREEN, COLOR_BG);
    _canvas.setCursor(70, 92);
    _canvas.printf("FREQ: 142.8MHz  MOD: GOTHAM-NET");
}

void DisplayEngine::renderBottomTicker() {
    int tickerY = 107;
    int tickerHeight = 28;

    // Ticker background & borders
    _canvas.fillRect(0, tickerY, TFT_WIDTH, tickerHeight, COLOR_PANEL_BG);
    _canvas.drawFastHLine(0, tickerY, TFT_WIDTH, COLOR_DARK_GREEN);

    // Update ticker position
    uint32_t now = millis();
    uint8_t speed = _quotesManager ? _quotesManager->getConfig().ticker_speed : 2;
    if (now - _lastTickerMillis >= (uint32_t)(25 / speed)) {
        _lastTickerMillis = now;
        _tickerX -= 2; // Smooth scrolling towards the left

        // When current quote completely scrolls past the left edge, pick next quote!
        if (_tickerX < -_textWidth) {
            if (_quotesManager) {
                _currentQuote = _quotesManager->getNextQuote();
            }
            _canvas.setTextSize(1);
            _textWidth = _canvas.textWidth(_currentQuote.c_str());
            _tickerX = TFT_WIDTH + 10; // Reset off the right edge
        }
    }

    // Draw Quote Text
    _canvas.setTextColor(COLOR_EMERALD, COLOR_PANEL_BG);
    _canvas.setTextSize(1);
    _canvas.setCursor(_tickerX, tickerY + 9);
    _canvas.print(_currentQuote);
}

void DisplayEngine::update() {
    uint32_t now = millis();
    if (now - _lastRenderMillis < 25) return; // ~40 FPS rendering loop
    _lastRenderMillis = now;

    // Clear buffer
    _canvas.fillScreen(COLOR_BG);

    // Render layers
    renderTopBar();
    renderCenterVisuals();
    renderBottomTicker();

    // Push double-buffered frame to ST7789 display with zero tear
    _canvas.pushSprite(0, 0);
}
