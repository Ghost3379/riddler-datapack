#pragma once

#include <Arduino.h>
#define FASTLED_INTERNAL
#include <FastLED.h>
#include "Config.hpp"

class LedEngine {
public:
    LedEngine();

    void begin(const PropConfig& config);
    void update();

    // Trigger organic disk activity ripple (called on SD read/write)
    void triggerDiskActivity(bool is_write);

    // Dynamic configuration update
    void setBrightness(uint8_t brightness);
    void setAmbientHue(uint8_t hue);
    void setPulseSpeed(uint8_t bpm);

private:
    CRGB _leds_front[NUM_LEDS_FRONT];
    CRGB _leds_right[NUM_LEDS_RIGHT];
    CRGB _leds_left[NUM_LEDS_LEFT];
    CRGB _leds_rear[NUM_LEDS_REAR];

    uint8_t _brightness = 140;
    uint8_t _ambientHue = 96;       // Emerald Green
    uint8_t _pulseBpm   = 14;

    float _activityLevel = 0.0f;    // 0.0 = calm breath, 1.0 = active transfer
    uint32_t _lastActivityMillis = 0;
    uint32_t _lastUpdateMillis   = 0;
};
