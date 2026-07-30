#include "wifi_setup.h"

#include <WiFi.h>

#include "csi_capture.h"

namespace wifi_setup {
namespace {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 170;
constexpr int HEADER_H = 18;

constexpr uint16_t COL_BG    = 0x0000;
constexpr uint16_t COL_HDR   = 0x1082;
constexpr uint16_t COL_TEXT  = 0xFFFF;
constexpr uint16_t COL_DIM   = 0x7BEF;
constexpr uint16_t COL_GRID  = 0x2124;
constexpr uint16_t COL_KEY   = 0x18E3;
constexpr uint16_t COL_ACCENT = 0x05FF;
constexpr uint16_t COL_OK    = 0x07E0;

// ---- AP list ---------------------------------------------------------------
constexpr int kMaxAps   = 24;
constexpr int LIST_TOP  = 22;
constexpr int LIST_ROW_H = 13;
constexpr int LIST_ROWS = 10;

struct Ap {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    bool    secured;
};

Ap  g_aps[kMaxAps];
int g_apCount = 0;
int g_scroll  = 0;

// ---- keyboard --------------------------------------------------------------
constexpr int KEY_TOP = 46;
constexpr int KEY_W   = 32;
constexpr int KEY_H   = 28;
constexpr int KEY_GAP = 2;
constexpr int ROW3_Y  = KEY_TOP + 3 * (KEY_H + KEY_GAP);

// Three layouts, cycled by the mode key. Between them they cover what turns up
// in a Wi-Fi password without needing a full keyboard.
const char* const kLayouts[3][3] = {
    {"qwertyuiop", "asdfghjkl-", "zxcvbnm_.@"},
    {"QWERTYUIOP", "ASDFGHJKL+", "ZXCVBNM=!?"},
    {"1234567890", "!@#$%^&*()", "-_=+[]{};:"},
};
const char* const kModeLabel[3] = {"ABC", "123", "abc"};

Stage g_stage   = Stage::Scanning;
int   g_layout  = 0;
bool  g_reveal  = false;
char  g_ssid[33] = {0};
char  g_pass[65] = {0};
int   g_passLen  = 0;

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

constexpr Rect kBackBtn   = {0, 0, 52, HEADER_H};
constexpr Rect kUpBtn     = {SCREEN_W - 58, 0, 29, HEADER_H};
constexpr Rect kDownBtn   = {SCREEN_W - 29, 0, 29, HEADER_H};
constexpr Rect kRevealBtn = {SCREEN_W - 56, 20, 56, 22};

constexpr Rect kModeBtn  = {0, ROW3_Y, 64, KEY_H};
constexpr Rect kSpaceBtn = {66, ROW3_Y, 124, KEY_H};
constexpr Rect kDelBtn   = {192, ROW3_Y, 60, KEY_H};
constexpr Rect kOkBtn    = {254, ROW3_Y, 66, KEY_H};

void collectScan(int found) {
    g_apCount = 0;
    for (int i = 0; i < found && g_apCount < kMaxAps; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;  // hidden networks cannot be picked here

        Ap& ap = g_aps[g_apCount++];
        strncpy(ap.ssid, ssid.c_str(), sizeof(ap.ssid) - 1);
        ap.ssid[sizeof(ap.ssid) - 1] = '\0';
        ap.rssi    = static_cast<int8_t>(WiFi.RSSI(i));
        ap.channel = static_cast<uint8_t>(WiFi.channel(i));
        ap.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
}

void drawKey(LGFX_Sprite& c, const Rect& r, const char* label, uint16_t col) {
    c.fillRoundRect(r.x, r.y, r.w, r.h, 3, COL_KEY);
    c.drawRoundRect(r.x, r.y, r.w, r.h, 3, COL_GRID);
    c.setTextDatum(middle_center);
    c.setTextColor(col);
    c.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
}

void drawApList(LGFX_Sprite& c) {
    c.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HDR);
    c.setTextSize(1);
    c.setTextDatum(middle_left);
    c.setTextColor(COL_ACCENT);
    c.drawString("< BACK", 6, HEADER_H / 2 - 1);
    c.setTextDatum(middle_center);
    c.setTextColor(COL_TEXT);
    c.drawString(g_stage == Stage::Scanning ? "SCANNING..." : "SELECT NETWORK",
                 SCREEN_W / 2, HEADER_H / 2 - 1);

    if (g_stage != Stage::Scanning) {
        c.setTextDatum(middle_center);
        c.setTextColor(g_scroll > 0 ? COL_ACCENT : COL_GRID);
        c.drawString("^", kUpBtn.x + kUpBtn.w / 2, HEADER_H / 2 - 1);
        c.setTextColor(g_scroll + LIST_ROWS < g_apCount ? COL_ACCENT : COL_GRID);
        c.drawString("v", kDownBtn.x + kDownBtn.w / 2, HEADER_H / 2 - 1);
    }

    const int shown = min(g_apCount - g_scroll, LIST_ROWS);
    for (int i = 0; i < shown; ++i) {
        const Ap& ap = g_aps[g_scroll + i];
        const int y  = LIST_TOP + i * LIST_ROW_H;

        // Signal bar behind the row, same idiom as the scanner app.
        float f = (ap.rssi + 95.0f) / 65.0f;
        f = f < 0 ? 0 : (f > 1 ? 1 : f);
        c.fillRect(6, y, static_cast<int>(f * (SCREEN_W - 12)), LIST_ROW_H - 2,
                   COL_KEY);

        c.setTextDatum(top_left);
        c.setTextColor(COL_TEXT);
        c.drawString(ap.ssid, 9, y + 2);

        char buf[24];
        snprintf(buf, sizeof(buf), "%s ch%u %d", ap.secured ? "*" : "open",
                 ap.channel, ap.rssi);
        c.setTextDatum(top_right);
        c.setTextColor(COL_DIM);
        c.drawString(buf, SCREEN_W - 8, y + 2);
    }

    if (g_stage == Stage::ApList && g_apCount == 0) {
        c.setTextDatum(top_left);
        c.setTextColor(COL_DIM);
        c.drawString("no networks found", 9, LIST_TOP + 4);
    }
}

void drawKeyboard(LGFX_Sprite& c) {
    c.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HDR);
    c.setTextSize(1);
    c.setTextDatum(middle_left);
    c.setTextColor(COL_ACCENT);
    c.drawString("< BACK", 6, HEADER_H / 2 - 1);
    c.setTextDatum(middle_center);
    c.setTextColor(COL_TEXT);
    c.drawString(g_ssid, SCREEN_W / 2, HEADER_H / 2 - 1);

    // Entry field. Masked by default -- someone is usually looking over your
    // shoulder when you type a password into a gadget.
    c.drawRect(4, 20, SCREEN_W - 62, 22, COL_GRID);
    c.setTextDatum(middle_left);
    c.setTextColor(COL_TEXT);
    if (g_passLen == 0) {
        c.setTextColor(COL_GRID);
        c.drawString("password", 10, 31);
    } else if (g_reveal) {
        c.drawString(g_pass, 10, 31);
    } else {
        char masked[65];
        const int n = min(g_passLen, 40);
        memset(masked, '*', n);
        masked[n] = '\0';
        c.drawString(masked, 10, 31);
    }

    drawKey(c, kRevealBtn, g_reveal ? "hide" : "show", COL_DIM);

    for (int row = 0; row < 3; ++row) {
        const char* keys = kLayouts[g_layout][row];
        for (int i = 0; keys[i]; ++i) {
            const Rect r = {i * (KEY_W + KEY_GAP) + 1,
                            KEY_TOP + row * (KEY_H + KEY_GAP), KEY_W, KEY_H};
            const char label[2] = {keys[i], '\0'};
            drawKey(c, r, label, COL_TEXT);
        }
    }

    drawKey(c, kModeBtn, kModeLabel[g_layout], COL_ACCENT);
    drawKey(c, kSpaceBtn, "space", COL_DIM);
    drawKey(c, kDelBtn, "DEL", COL_ACCENT);
    drawKey(c, kOkBtn, "CONNECT", COL_OK);
}

void appendChar(char ch) {
    if (g_passLen >= static_cast<int>(sizeof(g_pass)) - 1) return;
    g_pass[g_passLen++] = ch;
    g_pass[g_passLen]   = '\0';
}

void handleApListTap(int x, int y) {
    if (kBackBtn.contains(x, y)) {
        g_stage = Stage::Cancelled;
        return;
    }
    if (kUpBtn.contains(x, y)) {
        g_scroll = max(0, g_scroll - LIST_ROWS);
        return;
    }
    if (kDownBtn.contains(x, y)) {
        if (g_scroll + LIST_ROWS < g_apCount) g_scroll += LIST_ROWS;
        return;
    }
    if (y < LIST_TOP) return;

    const int idx = g_scroll + (y - LIST_TOP) / LIST_ROW_H;
    if (idx < 0 || idx >= g_apCount) return;

    strncpy(g_ssid, g_aps[idx].ssid, sizeof(g_ssid) - 1);
    g_ssid[sizeof(g_ssid) - 1] = '\0';
    g_passLen = 0;
    g_pass[0] = '\0';
    g_reveal  = false;

    // An open network needs no password; skip straight to the result.
    g_stage = g_aps[idx].secured ? Stage::Password : Stage::Done;
}

void handleKeyboardTap(int x, int y) {
    if (kBackBtn.contains(x, y)) {
        g_stage = Stage::ApList;
        return;
    }
    if (kRevealBtn.contains(x, y)) {
        g_reveal = !g_reveal;
        return;
    }
    if (kModeBtn.contains(x, y)) {
        g_layout = (g_layout + 1) % 3;
        return;
    }
    if (kSpaceBtn.contains(x, y)) {
        appendChar(' ');
        return;
    }
    if (kDelBtn.contains(x, y)) {
        if (g_passLen > 0) g_pass[--g_passLen] = '\0';
        return;
    }
    if (kOkBtn.contains(x, y)) {
        g_stage = Stage::Done;
        return;
    }

    if (y < KEY_TOP || y >= ROW3_Y) return;
    const int row = (y - KEY_TOP) / (KEY_H + KEY_GAP);
    const int col = x / (KEY_W + KEY_GAP);
    if (row < 0 || row > 2 || col < 0 || col > 9) return;

    const char* keys = kLayouts[g_layout][row];
    if (col < static_cast<int>(strlen(keys))) appendChar(keys[col]);
}

}  // namespace

void start() {
    // Scanning hops channels; the CSI capture has to let go of the radio.
    csi_capture::suspend();

    g_stage   = Stage::Scanning;
    g_apCount = 0;
    g_scroll  = 0;
    g_layout  = 0;
    g_reveal  = false;
    g_ssid[0] = '\0';
    clear();

    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true, false, false, 200);
}

void poll() {
    if (g_stage != Stage::Scanning) return;

    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;

    collectScan(n < 0 ? 0 : n);
    g_stage = Stage::ApList;
}

Stage stage() { return g_stage; }

void draw(LGFX_Sprite& canvas) {
    canvas.fillSprite(COL_BG);
    if (g_stage == Stage::Password) {
        drawKeyboard(canvas);
    } else {
        drawApList(canvas);
    }
}

void handleTap(int x, int y) {
    switch (g_stage) {
        case Stage::ApList:   handleApListTap(x, y);   break;
        case Stage::Password: handleKeyboardTap(x, y); break;
        case Stage::Scanning:
            if (kBackBtn.contains(x, y)) g_stage = Stage::Cancelled;
            break;
        default: break;
    }
}

const char* chosenSsid() { return g_ssid; }

const char* enteredPassword() { return g_pass; }

void clear() {
    // Overwrite rather than just resetting the length -- no reason to leave a
    // plaintext password sitting in RAM once NVS has it.
    memset(g_pass, 0, sizeof(g_pass));
    g_passLen = 0;
}

}  // namespace wifi_setup
