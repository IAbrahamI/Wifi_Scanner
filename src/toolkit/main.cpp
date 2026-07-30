// ===========================================================================
//  App 3 -- RF Toolkit  (ota_2 @ 0xA10000)
//
//  A sub-menu of lightweight WiFi/BLE security tools. Only one runs at a time;
//  the menu calls a tool's begin() on entry and stop() on exit, so each tool
//  owns the radio while it is active and hands it back cleanly.
//
//  Escape hatch: press either side button, or hold the screen for 3s, to leave
//  the toolkit entirely and return to the main launcher.
// ===========================================================================

#include <Arduino.h>

#include "../board/board.h"
#include "../board/app_switch.h"

#include "tool.h"
#include "analyzer.h"
#include "blehid.h"
#include "deauth.h"
#include "eviltwin.h"
#include "spotlight.h"

using namespace toolkit;

namespace {

LGFX_Sprite canvas(&lcd);

const Tool* g_tools[] = {
    &deauth::tool(),
    &analyzer::tool(),
    &eviltwin::tool(),
    &spotlight::tool(),
    &blehid::tool(),
};
constexpr int kToolCount = sizeof(g_tools) / sizeof(g_tools[0]);

int g_active = -1;  // -1 == showing the menu

constexpr uint16_t COL_MENU_HDR = 0x1082;
constexpr uint16_t COL_CARD     = 0x18E3;
constexpr uint16_t COL_CARD_SEL = 0x39E7;

constexpr int MENU_TOP = 22;
constexpr int MENU_ROW = 28;

int pressedRow = -1;

int menuRowAt(int y) {
    if (y < MENU_TOP) return -1;
    const int r = (y - MENU_TOP) / MENU_ROW;
    return r < kToolCount ? r : -1;
}

void drawMenu() {
    canvas.fillSprite(COL_BG);
    canvas.fillRect(0, 0, SCREEN_W, HEADER_H, COL_MENU_HDR);
    canvas.setTextSize(1);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COL_ACCENT);
    canvas.drawString("RF TOOLKIT", 6, HEADER_H / 2 - 1);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(COL_DIM);
    canvas.drawString("hold BOOT to exit", SCREEN_W - 6, HEADER_H / 2 - 1);

    for (int i = 0; i < kToolCount; ++i) {
        const int y = MENU_TOP + i * MENU_ROW;
        const uint16_t bg = (i == pressedRow) ? COL_CARD_SEL : COL_CARD;
        canvas.fillRoundRect(6, y, SCREEN_W - 12, MENU_ROW - 4, 4, bg);

        canvas.setTextDatum(top_left);
        canvas.setTextColor(COL_TEXT);
        canvas.drawString(g_tools[i]->name, 12, y + 3);
        canvas.setTextColor(COL_DIM);
        canvas.drawString(g_tools[i]->blurb, 12, y + 14);

        int cx = SCREEN_W - 20, cy = y + (MENU_ROW - 4) / 2;
        canvas.fillTriangle(cx, cy - 5, cx, cy + 5, cx + 6, cy, COL_ACCENT);
    }
}

void render() {
    if (g_active < 0) {
        drawMenu();
    } else {
        canvas.fillSprite(COL_BG);
        g_tools[g_active]->draw(canvas);
    }
    canvas.pushSprite(0, 0);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    board::begin(1);  // landscape 320x170, USB-C on the right

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    render();
}

void loop() {
    if (g_active >= 0) g_tools[g_active]->poll();

    int32_t tx, ty;
    const bool touching = lcd.getTouch(&tx, &ty);
    app_switch::feedTouchHold(touching);

    static bool     wasTouching = false;
    static int32_t  pressX = 0, pressY = 0;
    static uint32_t pressAt = 0;

    if (touching && !wasTouching) {
        pressX  = tx;
        pressY  = ty;
        pressAt = millis();
        if (g_active < 0) pressedRow = menuRowAt(ty);
    } else if (!touching && wasTouching) {
        pressedRow = -1;
        if (millis() - pressAt < 600) {
            if (g_active < 0) {
                const int row = menuRowAt(pressY);
                if (row >= 0) {
                    g_active = row;
                    g_tools[g_active]->begin();
                }
            } else if (g_tools[g_active]->handleTap(pressX, pressY)) {
                g_tools[g_active]->stop();
                g_active = -1;
            }
        }
    }
    wasTouching = touching;

    // Tools repaint on their own cadence; the menu only when a press changes it.
    static uint32_t lastDraw = 0;
    if (g_active >= 0) {
        if (millis() - lastDraw >= 100) { lastDraw = millis(); render(); }
    } else {
        static int lastPressed = -2;
        if (pressedRow != lastPressed) { lastPressed = pressedRow; render(); }
    }

    delay(5);
}
