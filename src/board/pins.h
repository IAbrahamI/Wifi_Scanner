#pragma once

// ---------------------------------------------------------------------------
//  LILYGO T-Display-S3 Touch -- pin map
//  Taken from LilyGO's pin_config.h for the touch variant of the board.
// ---------------------------------------------------------------------------

// Peripheral power gate. Must be HIGH or the LCD, touch controller and battery
// divider are all dead -- this is the pin the project spec calls out, and it is
// necessary but NOT sufficient to get a picture (see PIN_LCD_BL below).
#define PIN_POWER_ON    15

// LCD backlight. Without this the panel is powered and drawing, but black.
#define PIN_LCD_BL      38

// ST7789V over an 8-bit i80 parallel bus (LCD_CAM peripheral + DMA).
#define PIN_LCD_D0      39
#define PIN_LCD_D1      40
#define PIN_LCD_D2      41
#define PIN_LCD_D3      42
#define PIN_LCD_D4      45
#define PIN_LCD_D5      46
#define PIN_LCD_D6      47
#define PIN_LCD_D7      48
#define PIN_LCD_WR      8
#define PIN_LCD_RD      9
#define PIN_LCD_DC      7
#define PIN_LCD_CS      6
#define PIN_LCD_RES     5

// CST816 capacitive touch controller, I2C addr 0x15.
#define PIN_TOUCH_SDA   18
#define PIN_TOUCH_SCL   17
#define PIN_TOUCH_INT   16
#define PIN_TOUCH_RES   21

// Buttons. BOOT is also the escape hatch back to the launcher.
#define PIN_BUTTON_BOOT 0   // active LOW
#define PIN_BUTTON_1    14  // active LOW

// Battery sense, behind a 1:2 divider.
#define PIN_BAT_VOLT    4

// Panel geometry. The ST7789V controller has 240x320 of RAM but this panel
// only exposes a 170px-wide window into it, hence the 35px column offset.
#define PANEL_W         170
#define PANEL_H         320
#define PANEL_OFFSET_X  35
#define PANEL_OFFSET_Y  0
