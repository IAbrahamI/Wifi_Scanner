#pragma once

#include <Arduino.h>

#include "../board/display.h"

// ---------------------------------------------------------------------------
//  Shared scaffolding for the RF Toolkit's tools.
//
//  Each tool is a small self-contained module that owns one radio mode. Only
//  one runs at a time: the toolkit menu calls begin() on entry and stop() on
//  exit, so a tool may reconfigure the radio however it likes inside that
//  window and must put it back in stop().
// ---------------------------------------------------------------------------
namespace toolkit {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 170;
constexpr int HEADER_H = 18;

constexpr uint16_t COL_BG     = 0x0000;
constexpr uint16_t COL_HDR    = 0x1082;
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x7BEF;
constexpr uint16_t COL_GRID   = 0x2124;
constexpr uint16_t COL_ACCENT = 0x05FF;  // cyan
constexpr uint16_t COL_OK     = 0x07E0;  // green
constexpr uint16_t COL_WARN   = 0xFD20;  // amber
constexpr uint16_t COL_ALARM  = 0xF800;  // red
constexpr uint16_t COL_BAR    = 0x18E3;

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

constexpr Rect kBackBtn = {0, 0, 54, HEADER_H};

// Draws the standard header with a "< BACK" button on the left.
void drawHeader(LGFX_Sprite& c, const char* title);

// RSSI (dBm) to a 0..1 strength, -95 floor, -30 ceiling. Shared by every tool
// that draws a signal bar.
inline float rssiFraction(int rssi) {
    float f = (rssi + 95.0f) / 65.0f;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

// A tool's lifecycle. handleTap returns true to request exit back to the
// toolkit menu (typically when the tool's own BACK is pressed at its root).
struct Tool {
    const char* name;
    const char* blurb;
    void (*begin)();
    void (*stop)();
    void (*poll)();
    void (*draw)(LGFX_Sprite&);
    bool (*handleTap)(int, int);
};

}  // namespace toolkit
