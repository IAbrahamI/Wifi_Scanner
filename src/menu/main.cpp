// ===========================================================================
//  App 0 -- Launcher  (factory partition @ 0x010000)
//
//  Touch-driven main menu. Tapping an entry writes that app's address into the
//  boot register and resets. Entries whose partition has never been flashed are
//  drawn greyed out and do nothing.
// ===========================================================================

#include <Arduino.h>

#include "../board/board.h"
#include "../board/app_switch.h"

using app_switch::Slot;

namespace {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 170;

constexpr int HEADER_H = 28;
constexpr int ROW_H    = 40;
constexpr int ROW_GAP  = 6;
constexpr int ROW_TOP  = HEADER_H + 8;

constexpr uint16_t COL_BG       = 0x0000;
constexpr uint16_t COL_HEADER   = 0x1082;  // near-black grey
constexpr uint16_t COL_ACCENT   = 0xFD20;  // amber
constexpr uint16_t COL_CARD     = 0x18E3;
constexpr uint16_t COL_CARD_SEL = 0x39E7;
constexpr uint16_t COL_TEXT     = 0xFFFF;
constexpr uint16_t COL_DIM      = 0x7BEF;
constexpr uint16_t COL_DISABLED = 0x4208;

struct Entry {
    const char* name;
    const char* subtitle;
    Slot        slot;
};

const Entry kEntries[] = {
    {"CSI RADAR",    "Through-wall motion sensing", Slot::Csi},
    {"WIFI + BLE",   "Ambient proximity sniffer",   Slot::Scanner},
};
constexpr int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

// Drawn off-screen and pushed in one DMA burst, so the menu never tears.
LGFX_Sprite canvas(&lcd);

int  pressedRow = -1;   // row currently under a finger, -1 for none
bool installed[kEntryCount];

int rowY(int i) { return ROW_TOP + i * (ROW_H + ROW_GAP); }

int rowAt(int y) {
    for (int i = 0; i < kEntryCount; ++i) {
        if (y >= rowY(i) && y < rowY(i) + ROW_H) return i;
    }
    return -1;
}

void drawHeader() {
    canvas.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER);
    canvas.fillRect(0, HEADER_H - 2, SCREEN_W, 2, COL_ACCENT);

    canvas.setTextColor(COL_ACCENT);
    canvas.setTextDatum(middle_left);
    canvas.setTextSize(1);
    canvas.drawString("RF GADGET", 10, HEADER_H / 2 - 1);

    uint32_t mv = board::batteryMilliVolts();
    char buf[16];
    if (mv < 3000) {
        snprintf(buf, sizeof(buf), "USB");
    } else {
        snprintf(buf, sizeof(buf), "%lu.%02luV", mv / 1000, (mv % 1000) / 10);
    }
    canvas.setTextColor(COL_DIM);
    canvas.setTextDatum(middle_right);
    canvas.drawString(buf, SCREEN_W - 10, HEADER_H / 2 - 1);
}

void drawRow(int i) {
    const Entry& e = kEntries[i];
    const int    y = rowY(i);
    const bool   ok = installed[i];

    uint16_t bg = ok ? (i == pressedRow ? COL_CARD_SEL : COL_CARD) : COL_BG;
    canvas.fillRoundRect(8, y, SCREEN_W - 16, ROW_H, 6, bg);
    canvas.drawRoundRect(8, y, SCREEN_W - 16, ROW_H, 6,
                         ok ? COL_ACCENT : COL_DISABLED);

    canvas.setTextDatum(top_left);
    canvas.setTextSize(1);

    canvas.setTextColor(ok ? COL_TEXT : COL_DISABLED);
    canvas.drawString(e.name, 20, y + 8);

    canvas.setTextColor(ok ? COL_DIM : COL_DISABLED);
    canvas.drawString(ok ? e.subtitle : "not flashed", 20, y + 23);

    if (ok) {
        // Chevron
        int cx = SCREEN_W - 28, cy = y + ROW_H / 2;
        canvas.fillTriangle(cx, cy - 6, cx, cy + 6, cx + 7, cy, COL_ACCENT);
    }
}

void drawFooter() {
    canvas.setTextColor(COL_DISABLED);
    canvas.setTextDatum(bottom_center);
    canvas.setTextSize(1);
    canvas.drawString("hold BOOT in any app to come back", SCREEN_W / 2, SCREEN_H - 1);
}

void render() {
    canvas.fillSprite(COL_BG);
    drawHeader();
    for (int i = 0; i < kEntryCount; ++i) drawRow(i);
    drawFooter();
    canvas.pushSprite(0, 0);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    board::begin(1);  // landscape 320x170, USB-C on the right

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    for (int i = 0; i < kEntryCount; ++i) {
        installed[i] = app_switch::isInstalled(kEntries[i].slot);
    }

    render();
}

void loop() {
    static bool wasTouched = false;
    static int  lastPressed = -1;

    int32_t tx, ty;
    bool touched = lcd.getTouch(&tx, &ty);

    if (touched) {
        pressedRow = rowAt(ty);
    } else if (wasTouched) {
        // Release: launch only if the finger lifted on the row it landed on.
        int row = pressedRow;
        pressedRow = -1;
        if (row >= 0 && installed[row]) {
            app_switch::bootInto(kEntries[row].slot);  // reboots, no return
        }
    }
    wasTouched = touched;

    if (pressedRow != lastPressed) {
        render();
        lastPressed = pressedRow;
    }

    // Refresh the battery reading periodically without spinning the display.
    static uint32_t lastRefresh = 0;
    if (millis() - lastRefresh > 5000) {
        lastRefresh = millis();
        render();
    }

    delay(20);
}
