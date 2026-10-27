#include "LedEngine.hpp"

LedEngine::LedEngine() {
}

void LedEngine::begin(const PropConfig& config) {
    _brightness = config.led_brightness;
    _ambientHue = config.ambient_hue;
    _pulseBpm   = config.pulse_speed;

    // Initialize 4 separate WS2812B physical channels (BRG byte order for 2020 LEDs)
    FastLED.addLeds<WS2812B, PIN_LED_FRONT, BRG>(_leds_front, NUM_LEDS_FRONT);
    FastLED.addLeds<WS2812B, PIN_LED_RIGHT, BRG>(_leds_right, NUM_LEDS_RIGHT);
    FastLED.addLeds<WS2812B, PIN_LED_LEFT,  BRG>(_leds_left,  NUM_LEDS_LEFT);
    FastLED.addLeds<WS2812B, PIN_LED_REAR,  BRG>(_leds_rear,  NUM_LEDS_REAR);

    FastLED.setBrightness(_brightness);
    FastLED.clear(true);

    Serial.println("[LED] 12x WS2812B initialized across 4 channels.");
}

void LedEngine::triggerDiskActivity(bool is_write) {
    // Smoothly kick activity level up (1.0 for write, 0.7 for read)
    float boost = is_write ? 1.0f : 0.7f;
    if (_activityLevel < boost) {
        _activityLevel = boost;
    }
    _lastActivityMillis = millis();
}

void LedEngine::setBrightness(uint8_t brightness) {
    _brightness = brightness;
    FastLED.setBrightness(_brightness);
}

void LedEngine::setAmbientHue(uint8_t hue) {
    _ambientHue = hue;
}

void LedEngine::setPulseSpeed(uint8_t bpm) {
    _pulseBpm = bpm;
}

void LedEngine::update() {
    uint32_t now = millis();
    if (now - _lastUpdateMillis < 16) return; // ~60 FPS update rate
    _lastUpdateMillis = now;

    // Decay activity level smoothly back to 0.0 (calm)
    if (_activityLevel > 0.001f) {
        _activityLevel *= 0.94f; // Exponential decay (~1.2 sec tail)
    } else {
        _activityLevel = 0.0f;
    }

    // 1. Ambient breathing base (sine wave between ~40 and 255)
    uint8_t breathVal = beatsin8(_pulseBpm, 40, 255);

    // 2. Slow fluid wave phase offset across channels
    // Front -> Right/Left -> Rear
    uint8_t frontWave = beatsin8(_pulseBpm, 50, 255, 0, 0);
    uint8_t sideWave  = beatsin8(_pulseBpm, 40, 240, 0, 64);
    uint8_t rearWave  = beatsin8(_pulseBpm, 30, 220, 0, 128);

    // 3. Compute organic transfer drift if disk is active
    // "flicker not fast, just slowly and stuff"
    float slowUndulation = (sin(now * 0.004f) + 1.0f) * 0.5f; // Gentle 0.6Hz wave
    float driftPhase     = (cos(now * 0.003f) + 1.0f) * 0.5f;

    // Blend between pure ambient emerald and active lime/chartreuse glow
    uint8_t activeHue = _ambientHue - (uint8_t)(16.0f * _activityLevel); // Shifts slightly yellow/lime

    // --- Channel 1: Front Grille (2 LEDs) ---
    for (int i = 0; i < NUM_LEDS_FRONT; i++) {
        float b = frontWave + (_activityLevel * (slowUndulation * 100.0f));
        uint8_t v = constrain((int)b, 20, 255);
        _leds_front[i] = CHSV(activeHue, 240, v);
    }

    // --- Channel 2: Right Side Louvers (2 LEDs) ---
    for (int i = 0; i < NUM_LEDS_RIGHT; i++) {
        float b = sideWave + (_activityLevel * (driftPhase * 90.0f));
        uint8_t v = constrain((int)b, 20, 255);
        _leds_right[i] = CHSV(activeHue, 245, v);
    }

    // --- Channel 3: Left Side Louvers (5 LEDs) ---
    for (int i = 0; i < NUM_LEDS_LEFT; i++) {
        // Subtle progressive ripple down the 5 side louvers
        uint8_t louverOffset = beatsin8(18, 40, 240, 0, i * 40);
        float b = louverOffset + (_activityLevel * (slowUndulation * 95.0f));
        uint8_t v = constrain((int)b, 20, 255);
        _leds_left[i] = CHSV(activeHue, 245, v);
    }

    // --- Channel 4: Rear / Turbine Hubs (3 LEDs) ---
    for (int i = 0; i < NUM_LEDS_REAR; i++) {
        float b = rearWave + (_activityLevel * (driftPhase * 80.0f));
        uint8_t v = constrain((int)b, 20, 255);
        _leds_rear[i] = CHSV(activeHue + 2, 230, v);
    }

    FastLED.show();
}
