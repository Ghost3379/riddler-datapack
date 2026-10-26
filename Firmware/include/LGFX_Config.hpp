#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "Config.hpp"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI      _bus_instance;
    lgfx::Light_PWM    _light_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = SPI2_HOST;     // FSPI on ESP32-S3
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;      // 40 MHz SPI write speed
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = PIN_TFT_SCLK;
            cfg.pin_mosi   = PIN_TFT_MOSI;
            cfg.pin_miso   = PIN_TFT_MISO;
            cfg.pin_dc     = PIN_TFT_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs           = PIN_TFT_CS;
            cfg.pin_rst          = PIN_TFT_RST;
            cfg.pin_busy         = -1;
            cfg.panel_width      = 135;
            cfg.panel_height     = 240;
            cfg.offset_x         = 52;      // Standard ST7789 240x135 window offset
            cfg.offset_y         = 40;
            cfg.offset_rotation  = 0;
            cfg.readable         = false;
            cfg.invert           = true;    // IPS ST7789 panels usually require inverted colors
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = PIN_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 12000;        // 12 kHz PWM for silent backlight dimming
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        setPanel(&_panel_instance);
    }
};
