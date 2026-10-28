#include "LedEngine.hpp"
#include <math.h>

LedEngine::LedEngine()
    : _strip_front(NUM_LEDS_FRONT, PIN_LED_FRONT, NEO_GRB + NEO_KHZ800),
      _strip_right(NUM_LEDS_RIGHT, PIN_LED_RIGHT, NEO_GRB + NEO_KHZ800),
      _strip_left(NUM_LEDS_LEFT,   PIN_LED_LEFT,  NEO_GRB + NEO_KHZ800),
      _strip_rear(NUM_LEDS_REAR,   PIN_LED_REAR,  NEO_GRB + NEO_KHZ800) {
}

void LedEngine::initOff() {
    _strip_front.begin();
    _strip_right.begin();
    _strip_left.begin();
    _strip_rear.begin();

    _strip_front.clear();
    _strip_right.clear();
    _strip_left.clear();
    _strip_rear.clear();

    _strip_front.show();
    _strip_right.show();
    _strip_left.show();
    _strip_rear.show();
}

void LedEngine::begin(const PropConfig& config) {
    _brightness  = config.led_brightness;
    _ambientHue  = config.ambient_hue;
    _pulseBpm    = config.pulse_speed;
    _flickerOnIo = config.flicker_on_io;

    initOff();
    applyConfig(config);

    Serial.println("[LED] 12x WS2812B initialized (all strips cleared OFF at boot).");
}

void LedEngine::applyConfig(const PropConfig& config) {
    setBrightness(config.led_brightness);
    setAmbientHue(config.ambient_hue);
    setPulseSpeed(config.pulse_speed);
    setFlickerOnIo(config.flicker_on_io);
}

void LedEngine::triggerDiskActivity(bool is_write) {
    if (!_flickerOnIo) return;
    float boost = is_write ? 1.0f : 0.7f;
    if (_activityLevel < boost) {
        _activityLevel = boost;
    }
    _lastActivityMillis = millis();
}

void LedEngine::setBrightness(uint8_t brightness) {
    _brightness = constrain(brightness, 5, 255);
    _strip_front.setBrightness(_brightness);
    _strip_right.setBrightness(_brightness);
    _strip_left.setBrightness(_brightness);
    _strip_rear.setBrightness(_brightness);
}

void LedEngine::setAmbientHue(uint8_t hue) {
    _ambientHue = hue;
}

void LedEngine::setPulseSpeed(uint8_t bpm) {
    _pulseBpm = constrain(bpm, 5, 60);
}

void LedEngine::update() {
    uint32_t now = millis();
    if (now - _lastUpdateMillis < 20) return; // 50 FPS
    _lastUpdateMillis = now;

    // Decay activity level smoothly
    if (_activityLevel > 0.001f) {
        _activityLevel *= 0.94f;
    } else {
        _activityLevel = 0.0f;
    }

    // Smooth organic sine wave breathing
    float periodMs = (60000.0f / (float)_pulseBpm);
    float phase = fmodf((float)now, periodMs) / periodMs * 6.28318f;
    float sineBreath = (sinf(phase) + 1.0f) * 0.5f; // 0.0 to 1.0

    // Green base intensity: 40 to 255
    uint8_t greenBase = 40 + (uint8_t)(sineBreath * 215.0f);

    // Activity flicker/boost
    uint8_t boost = (uint8_t)(_activityLevel * 80.0f);
    uint8_t r = (uint8_t)(_activityLevel * 30.0f); // subtle amber shift on write
    uint8_t g = constrain(greenBase + boost, 0, 255);
    uint8_t b = (uint8_t)(_activityLevel * 10.0f);

    uint32_t greenColor = _strip_front.Color(r, g, b);

    // 1. Front Grille
    for (int i = 0; i < NUM_LEDS_FRONT; i++) {
        _strip_front.setPixelColor(i, greenColor);
    }

    // 2. Right Louvers
    for (int i = 0; i < NUM_LEDS_RIGHT; i++) {
        _strip_right.setPixelColor(i, greenColor);
    }

    // 3. Left Louvers (subtle staggered wave)
    for (int i = 0; i < NUM_LEDS_LEFT; i++) {
        float louverPhase = phase + (i * 0.4f);
        float louverSine = (sinf(louverPhase) + 1.0f) * 0.5f;
        uint8_t louverG = 40 + (uint8_t)(louverSine * 215.0f);
        _strip_left.setPixelColor(i, _strip_left.Color(r, constrain(louverG + boost, 0, 255), b));
    }

    // 4. Rear / Turbine Fans (counter-phase)
    float rearPhase = phase + 3.14159f;
    float rearSine = (sinf(rearPhase) + 1.0f) * 0.5f;
    uint8_t rearG = 30 + (uint8_t)(rearSine * 225.0f);
    uint32_t rearColor = _strip_rear.Color(r, constrain(rearG + boost, 0, 255), b);
    for (int i = 0; i < NUM_LEDS_REAR; i++) {
        _strip_rear.setPixelColor(i, rearColor);
    }

    // Push out to all 4 channels
    _strip_front.show();
    _strip_right.show();
    _strip_left.show();
    _strip_rear.show();
}
