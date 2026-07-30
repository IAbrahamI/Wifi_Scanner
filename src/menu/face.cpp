#include "face.h"

#include <Arduino.h>

#include "geass_image.h"

// ---------------------------------------------------------------------------
//  Code Geass eye -- the launcher's idle screen.
//
//  The embedded artwork (tools/img2rgb565.py -> geass_image.h) blitted centred
//  on black. It sits still until touched, then blinks: black eyelids sweep shut
//  over the image and back open, with a glowing crimson seam where they meet.
// ---------------------------------------------------------------------------
namespace face {
namespace {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 170;

constexpr uint16_t COL_BG   = 0x0000;
constexpr uint16_t COL_DIM  = 0x6180;
constexpr uint16_t COL_LID  = 0xF9E7;  // glowing crimson lid seam
constexpr uint16_t COL_LIDG = 0x6000;  // dim halo under the seam

constexpr int IMG_X = (SCREEN_W - geass_img::WIDTH) / 2;
constexpr int IMG_Y = (SCREEN_H - geass_img::HEIGHT) / 2;
constexpr int IMG_W = geass_img::WIDTH;
constexpr int IMG_H = geass_img::HEIGHT;

// Blink timing. Closes over the first half, opens over the second.
constexpr uint32_t BLINK_MS = 260;

bool     g_blink      = false;
uint32_t g_blinkStart = 0;

void drawImage(LGFX_Sprite& c) {
    // Cast to rgb565_t so LovyanGFX applies the panel's byte order. If colours
    // come out swapped on hardware, change this to lgfx::swap565_t.
    c.pushImage(IMG_X, IMG_Y, IMG_W, IMG_H,
                reinterpret_cast<const lgfx::rgb565_t*>(geass_img::DATA));
}

void drawHints(LGFX_Sprite& c) {
    c.setTextSize(1);
    c.setTextColor(COL_DIM);
    c.setTextDatum(bottom_left);
    c.drawString("< MENU", 4, SCREEN_H - 1);
    c.setTextDatum(bottom_right);
    c.drawString("FACE >", SCREEN_W - 4, SCREEN_H - 1);
}

// Black lids closing to `p` (0 open, 1 shut) across the whole screen, with a
// crimson seam at each lid edge running the full width.
void drawLids(LGFX_Sprite& c, float p) {
    if (p <= 0.0f) return;
    const int topH = static_cast<int>(p * SCREEN_H * 0.52f);
    const int botH = static_cast<int>(p * SCREEN_H * 0.48f);

    c.fillRect(0, 0, SCREEN_W, topH, COL_BG);
    c.fillRect(0, SCREEN_H - botH, SCREEN_W, botH, COL_BG);

    const int topY = topH;
    const int botY = SCREEN_H - botH;
    c.drawFastHLine(0, topY, SCREEN_W, COL_LIDG);
    c.drawFastHLine(0, topY - 1, SCREEN_W, COL_LID);
    c.drawFastHLine(0, botY, SCREEN_W, COL_LIDG);
    c.drawFastHLine(0, botY + 1, SCREEN_W, COL_LID);
}

}  // namespace

void begin() { g_blink = false; }

void poke() {
    g_blink = true;
    g_blinkStart = millis();
}

bool isAnimating() { return g_blink; }

void update() {
    if (g_blink && millis() - g_blinkStart >= BLINK_MS) g_blink = false;
}

void draw(LGFX_Sprite& c) {
    c.fillSprite(COL_BG);
    drawImage(c);

    if (g_blink) {
        // Triangle wave: 0 -> 1 (shut) over the first half, 1 -> 0 after.
        const float t = (millis() - g_blinkStart) / static_cast<float>(BLINK_MS);
        const float p = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
        drawLids(c, p);
    }

    drawHints(c);
}

}  // namespace face
