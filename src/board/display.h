#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "pins.h"

// ---------------------------------------------------------------------------
//  LovyanGFX device definition for the T-Display-S3 Touch.
//
//  LovyanGFX drives the 8-bit parallel bus through the ESP32-S3's LCD_CAM
//  peripheral with DMA, which is what the spec means by "bypassing SPI latency"
//  -- there is no SPI involved at all. Touch is folded into the same object, so
//  `lcd.getTouch(&x, &y)` gives coordinates already in the current rotation.
// ---------------------------------------------------------------------------
class LGFX_TDisplayS3 : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789     _panel;
    lgfx::Bus_Parallel8    _bus;
    lgfx::Light_PWM        _light;
    lgfx::Touch_CST816S    _touch;

public:
    LGFX_TDisplayS3() {
        {
            auto cfg = _bus.config();
            cfg.freq_write = 20000000;
            cfg.pin_wr = PIN_LCD_WR;
            cfg.pin_rd = PIN_LCD_RD;
            cfg.pin_rs = PIN_LCD_DC;
            cfg.pin_d0 = PIN_LCD_D0;
            cfg.pin_d1 = PIN_LCD_D1;
            cfg.pin_d2 = PIN_LCD_D2;
            cfg.pin_d3 = PIN_LCD_D3;
            cfg.pin_d4 = PIN_LCD_D4;
            cfg.pin_d5 = PIN_LCD_D5;
            cfg.pin_d6 = PIN_LCD_D6;
            cfg.pin_d7 = PIN_LCD_D7;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs           = PIN_LCD_CS;
            cfg.pin_rst          = PIN_LCD_RES;
            cfg.pin_busy         = -1;
            cfg.memory_width     = 240;
            cfg.memory_height    = 320;
            cfg.panel_width      = PANEL_W;
            cfg.panel_height     = PANEL_H;
            cfg.offset_x         = PANEL_OFFSET_X;
            cfg.offset_y         = PANEL_OFFSET_Y;
            cfg.offset_rotation  = 0;
            cfg.readable         = false;   // no MISO on this bus
            cfg.invert           = true;    // IPS panel wants inverted colour
            cfg.rgb_order        = false;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = PIN_LCD_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.x_min           = 0;
            cfg.x_max           = PANEL_W - 1;
            cfg.y_min           = 0;
            cfg.y_max           = PANEL_H - 1;
            cfg.pin_int         = PIN_TOUCH_INT;
            cfg.pin_rst         = PIN_TOUCH_RES;
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port        = 0;
            cfg.pin_sda         = PIN_TOUCH_SDA;
            cfg.pin_scl         = PIN_TOUCH_SCL;
            cfg.freq            = 400000;
            cfg.i2c_addr        = 0x15;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

// The one display instance, defined in board.cpp.
extern LGFX_TDisplayS3 lcd;
