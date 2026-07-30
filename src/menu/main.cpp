// ===========================================================================
//  App 0 -- Launcher  (factory partition @ 0x010000)
//
//  Two screens:
//    FACE  a cyberpunk animated-eyes idle screen (the default, boots here).
//    MENU  the app list -- tap an entry to write its address into the boot
//          register and reset. Un-flashed slots are greyed out and inert.
//
//  The two physical buttons switch between them: the button by GPIO 0 shows the
//  MENU, the other (GPIO 14) shows the FACE. Touch still drives selection in the
//  menu, and poking the face makes it react.
// ===========================================================================

#include <Arduino.h>

#include "../board/board.h"
#include "../board/app_switch.h"
#include "../board/pins.h"
#include "face.h"

using app_switch::Slot;

namespace {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 170;

constexpr int HEADER_H = 26;
constexpr int ROW_H    = 38;
constexpr int ROW_GAP  = 5;
constexpr int ROW_TOP  = HEADER_H + 6;

constexpr uint16_t COL_BG       = 0x0000;
constexpr uint16_t COL_HEADER   = 0x1082;  // near-black grey
constexpr uint16_t COL_ACCENT   = 0xC81F;  // purple (for red use 0xF904)
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
    {"RF TOOLKIT",   "WiFi/BLE security tools",     Slot::Toolkit},
};
constexpr int kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

// Drawn off-screen and pushed in one DMA burst, so the menu never tears.
LGFX_Sprite canvas(&lcd);

int  pressedRow = -1;   // row currently under a finger, -1 for none
bool installed[kEntryCount];

enum class View { Face, Menu };
View g_view = View::Face;  // boot into the attract screen

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

    // No initial render() here -- we boot into the face, and loop() paints it on
    // the first pass. Drawing the menu first would flash it for a frame.
    face::begin();
}

// Edge-detected button read. Returns true once on the press.
bool pressed(uint8_t pin, bool& prev) {
    const bool down = digitalRead(pin) == LOW;  // active low
    const bool edge = down && !prev;
    prev = down;
    return edge;
}

void loopMenu() {
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
}

// The face is a static image, so it is painted once on entry (g_faceDirty) and
// then left alone -- except while a touch-triggered blink is playing, when it
// redraws every frame until the animation finishes.
bool g_faceDirty = true;

void loopFace() {
    static bool wasTouched = false;
    int32_t tx, ty;
    const bool touched = lcd.getTouch(&tx, &ty);
    if (touched && !wasTouched) face::poke();  // tap = blink
    wasTouched = touched;

    if (g_faceDirty || face::isAnimating()) {
        face::update();
        face::draw(canvas);
        canvas.pushSprite(0, 0);
        g_faceDirty = false;
    }
}

void loop() {
    static bool prevBoot = false, prevAux = false;

    // The two buttons flip between the face and the menu. GPIO 0 is also the
    // escape hatch, but that is a no-op in the launcher (already home), so it is
    // free to double as the MENU button here.
    if (pressed(PIN_BUTTON_BOOT, prevBoot) && g_view != View::Menu) {
        g_view = View::Menu;
        pressedRow = -1;
        render();
    }
    if (pressed(PIN_BUTTON_1, prevAux) && g_view != View::Face) {
        g_view = View::Face;
        g_faceDirty = true;
        face::begin();
    }

    if (g_view == View::Face) {
        loopFace();
    } else {
        loopMenu();
    }
    delay(20);
}
