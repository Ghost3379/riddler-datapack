#pragma once

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "Config.hpp"

class LedEngine {
public:
    LedEngine();

    // Immediately drive LED data pins low and turn all LEDs OFF at power-up
    void initOff();

    void begin(const PropConfig& config);
    void update();

    // Trigger organic disk activity ripple (called on SD read/write)
    void triggerDiskActivity(bool is_write);

    // Dynamic configuration update
    void applyConfig(const PropConfig& config);
    void setBrightness(uint8_t brightness);
    void setAmbientHue(uint8_t hue);
    void setPulseSpeed(uint8_t bpm);
    void setFlickerOnIo(bool enable) { _flickerOnIo = enable; }

private:
    Adafruit_NeoPixel _strip_front;
    Adafruit_NeoPixel _strip_right;
    Adafruit_NeoPixel _strip_left;
    Adafruit_NeoPixel _strip_rear;

    uint8_t _brightness  = 38; // ~15%
    uint8_t _ambientHue  = 96;
    uint8_t _pulseBpm    = 14;
    bool    _flickerOnIo = true;

    float _activityLevel = 0.0f;    // 0.0 = calm breath, 1.0 = active transfer
    uint32_t _lastActivityMillis = 0;
    uint32_t _lastUpdateMillis   = 0;
};
