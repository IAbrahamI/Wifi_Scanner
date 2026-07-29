#include "spotlight.h"

#include <NimBLEDevice.h>
#include <algorithm>

namespace toolkit { namespace spotlight {
namespace {

constexpr int      kMaxDev  = 24;
constexpr uint32_t kStaleMs = 15000;

struct Dev {
    char     addr[18];
    char     name[20];
    int8_t   rssi;
    uint32_t last;
};

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
Dev          g_dev[kMaxDev];
int          g_count = 0;

enum class Mode { List, Meter };
Mode g_mode = Mode::List;

char  g_targetAddr[18] = {0};
char  g_targetName[20] = {0};
float g_smooth = -100.0f;   // EWMA of the target RSSI
int8_t g_peak  = -127;      // strongest reading so far, for hot/cold

int g_scroll = 0;

void record(NimBLEAdvertisedDevice* d) {
    std::string addr = d->getAddress().toString();
    portENTER_CRITICAL(&g_mux);

    int slot = -1;
    for (int i = 0; i < g_count; ++i) {
        if (strncmp(g_dev[i].addr, addr.c_str(), sizeof(g_dev[i].addr)) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (g_count < kMaxDev) {
            slot = g_count++;
        } else {
            slot = 0;
            for (int i = 1; i < g_count; ++i) {
                if (g_dev[i].last < g_dev[slot].last) slot = i;
            }
        }
        memset(&g_dev[slot], 0, sizeof(Dev));
        strncpy(g_dev[slot].addr, addr.c_str(), sizeof(g_dev[slot].addr) - 1);
        strcpy(g_dev[slot].name, "?");
    }
    g_dev[slot].rssi = d->getRSSI();
    g_dev[slot].last = millis();
    if (d->haveName()) {
        std::string n = d->getName();
        if (!n.empty()) {
            strncpy(g_dev[slot].name, n.c_str(), sizeof(g_dev[slot].name) - 1);
            g_dev[slot].name[sizeof(g_dev[slot].name) - 1] = '\0';
        }
    }
    portEXIT_CRITICAL(&g_mux);
}

class Cb : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* d) override { record(d); }
};

void begin() {
    g_mode  = Mode::List;
    g_count = 0;
    g_scroll = 0;
    g_targetAddr[0] = '\0';

    NimBLEDevice::init("");
    NimBLEScan* s = NimBLEDevice::getScan();
    s->setAdvertisedDeviceCallbacks(new Cb(), /*wantDuplicates=*/true);
    s->setActiveScan(false);
    s->setInterval(80);
    s->setWindow(60);   // high duty cycle -- refresh rate matters for a finder
    s->setMaxResults(0);
    s->start(0, nullptr, false);
}

void stop() {
    NimBLEDevice::getScan()->stop();
    NimBLEDevice::deinit(true);
}

// Latest RSSI for the locked target, or -127 if it has gone silent.
int8_t targetRssi() {
    int8_t r = -127;
    portENTER_CRITICAL(&g_mux);
    for (int i = 0; i < g_count; ++i) {
        if (strncmp(g_dev[i].addr, g_targetAddr, sizeof(g_targetAddr)) == 0) {
            if (millis() - g_dev[i].last < 3000) r = g_dev[i].rssi;
            break;
        }
    }
    portEXIT_CRITICAL(&g_mux);
    return r;
}

void poll() {
    if (g_mode != Mode::Meter) return;
    const int8_t r = targetRssi();
    if (r > -127) {
        g_smooth += 0.25f * (r - g_smooth);
        if (r > g_peak) g_peak = r;
    }
}

void drawList(LGFX_Sprite& c) {
    drawHeader(c, "PICK A DEVICE");

    Dev dev[kMaxDev];
    int n;
    portENTER_CRITICAL(&g_mux);
    // Drop stale, then sort strongest-first.
    const uint32_t now = millis();
    int w = 0;
    for (int i = 0; i < g_count; ++i) {
        if (now - g_dev[i].last < kStaleMs) g_dev[w++] = g_dev[i];
    }
    g_count = w;
    std::sort(g_dev, g_dev + g_count,
              [](const Dev& a, const Dev& b) { return a.rssi > b.rssi; });
    n = g_count;
    memcpy(dev, g_dev, sizeof(Dev) * n);
    portEXIT_CRITICAL(&g_mux);

    if (n == 0) {
        c.setTextDatum(top_left);
        c.setTextColor(COL_GRID);
        c.drawString("scanning for advertisers...", 8, 26);
        return;
    }

    const int rows = min(n - g_scroll, 11);
    for (int i = 0; i < rows; ++i) {
        const Dev& d = dev[g_scroll + i];
        const int  y = 22 + i * 13;

        const int bw = static_cast<int>(rssiFraction(d.rssi) * (SCREEN_W - 12));
        c.fillRect(6, y, bw, 11, COL_BAR);

        c.setTextDatum(top_left);
        c.setTextColor(COL_TEXT);
        c.drawString(d.name[0] && d.name[0] != '?' ? d.name : d.addr, 9, y + 1);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d", d.rssi);
        c.setTextDatum(top_right);
        c.setTextColor(COL_ACCENT);
        c.drawString(buf, SCREEN_W - 8, y + 1);
    }
}

void drawMeter(LGFX_Sprite& c) {
    drawHeader(c, "SIGNAL FINDER");

    c.setTextSize(1);
    c.setTextDatum(top_center);
    c.setTextColor(COL_DIM);
    c.drawString(g_targetName[0] && g_targetName[0] != '?' ? g_targetName
                                                           : g_targetAddr,
                 SCREEN_W / 2, 22);

    const bool live = targetRssi() > -127;
    const float f   = rssiFraction(static_cast<int>(g_smooth));

    // Hot/cold guidance from how close this reading is to the strongest seen.
    const float gap = g_peak - g_smooth;   // 0 = at the hottest point so far
    const char* word;
    uint16_t    col;
    if (!live)         { word = "SIGNAL LOST"; col = COL_GRID; }
    else if (gap < 3)  { word = "RIGHT HERE";  col = COL_ALARM; }
    else if (gap < 8)  { word = "HOT";         col = COL_WARN; }
    else if (gap < 16) { word = "WARMER";      col = COL_ACCENT; }
    else               { word = "COLD";        col = COL_DIM; }

    // Big proximity bar.
    const int bx = 20, by = 44, bw = SCREEN_W - 40, bh = 40;
    c.drawRect(bx, by, bw, bh, COL_GRID);
    c.fillRect(bx + 2, by + 2, static_cast<int>(f * (bw - 4)), bh - 4, col);

    c.setTextSize(3);
    c.setTextDatum(middle_center);
    c.setTextColor(col);
    c.drawString(word, SCREEN_W / 2, 108);

    c.setTextSize(1);
    c.setTextDatum(bottom_center);
    c.setTextColor(COL_DIM);
    char buf[40];
    snprintf(buf, sizeof(buf), "%.0f dBm   peak %d   [ retarget ]", g_smooth,
             g_peak);
    c.drawString(buf, SCREEN_W / 2, SCREEN_H - 2);
}

void draw(LGFX_Sprite& c) {
    if (g_mode == Mode::List) drawList(c); else drawMeter(c);
}

bool handleTap(int x, int y) {
    if (g_mode == Mode::Meter) {
        // Anywhere along the bottom "retarget" strip goes back to the list;
        // BACK leaves the tool.
        if (kBackBtn.contains(x, y) || y > SCREEN_H - 16) {
            g_mode = Mode::List;
            return false;
        }
        return false;
    }

    // List mode.
    if (kBackBtn.contains(x, y)) return true;   // exit tool
    if (y < 22) return false;

    const int idx = g_scroll + (y - 22) / 13;
    Dev picked{};
    bool found = false;
    portENTER_CRITICAL(&g_mux);
    if (idx >= 0 && idx < g_count) { picked = g_dev[idx]; found = true; }
    portEXIT_CRITICAL(&g_mux);

    if (found) {
        strncpy(g_targetAddr, picked.addr, sizeof(g_targetAddr));
        strncpy(g_targetName, picked.name, sizeof(g_targetName));
        g_smooth = picked.rssi;
        g_peak   = picked.rssi;
        g_mode   = Mode::Meter;
    }
    return false;
}

}  // namespace

const Tool& tool() {
    static const Tool t{"BLE Spotlight",
                        "Walk down a device by signal",
                        begin, stop, poll, draw, handleTap};
    return t;
}

}}  // namespace toolkit::spotlight
