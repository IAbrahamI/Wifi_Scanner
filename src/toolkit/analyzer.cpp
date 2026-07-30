#include "analyzer.h"

#include <WiFi.h>

namespace toolkit { namespace analyzer {
namespace {

struct Chan {
    uint8_t aps;
    int8_t  best;
};

Chan     g_ch[14] = {};
uint16_t g_total   = 0;
uint32_t g_sweeps  = 0;
bool     g_scanning = false;
uint32_t g_nextScan = 0;

void collect(int n) {
    Chan ch[14] = {};
    for (auto& c : ch) c.best = -127;

    for (int i = 0; i < n; ++i) {
        const int channel = WiFi.channel(i);
        if (channel < 1 || channel > 13) continue;
        ch[channel].aps++;
        const int rssi = WiFi.RSSI(i);
        if (rssi > ch[channel].best) ch[channel].best = rssi;
    }
    memcpy(g_ch, ch, sizeof(g_ch));
    g_total = n < 0 ? 0 : n;
    g_sweeps++;
    WiFi.scanDelete();
}

// The three non-overlapping channels each collect interference from their two
// neighbours as well as themselves, so score a 1/6/11 choice by the weighted
// congestion in its 5-channel window and recommend the quietest.
int recommend() {
    const int primary[3] = {1, 6, 11};
    int    best      = 6;
    float  bestScore = 1e9f;
    for (int p : primary) {
        float score = 0.0f;
        for (int d = -2; d <= 2; ++d) {
            const int ch = p + d;
            if (ch < 1 || ch > 13) continue;
            // Own channel counts full; adjacent channels bleed in partially.
            const float weight = (d == 0) ? 1.0f : (abs(d) == 1 ? 0.5f : 0.25f);
            score += g_ch[ch].aps * weight;
        }
        if (score < bestScore) { bestScore = score; best = p; }
    }
    return best;
}

void begin() {
    memset(g_ch, 0, sizeof(g_ch));
    g_total = 0;
    g_sweeps = 0;
    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(false, true);
    g_nextScan = millis();
    g_scanning = false;
}

void stop() {
    WiFi.scanDelete();
}

void poll() {
    if (!g_scanning) {
        if (millis() >= g_nextScan) {
            WiFi.scanNetworks(true, true, false, 120);
            g_scanning = true;
        }
        return;
    }
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n != WIFI_SCAN_FAILED) collect(n);
    else WiFi.scanDelete();
    g_scanning = false;
    g_nextScan = millis() + 250;
}

void draw(LGFX_Sprite& c) {
    drawHeader(c, "WIFI ANALYZER");

    char buf[40];
    snprintf(buf, sizeof(buf), "%u APs   sweep %lu", g_total,
             static_cast<unsigned long>(g_sweeps));
    c.setTextSize(1);
    c.setTextDatum(top_right);
    c.setTextColor(COL_DIM);
    c.drawString(buf, SCREEN_W - 6, 22);

    c.setTextDatum(top_left);
    c.setTextColor(COL_ACCENT);
    c.drawString("2.4GHz CONGESTION", 6, 22);

    const int base = 118;
    const int h    = 78;
    uint16_t peak  = 1;
    for (int ch = 1; ch <= 13; ++ch) peak = max<uint16_t>(peak, g_ch[ch].aps);

    c.drawFastHLine(6, base, SCREEN_W - 12, COL_GRID);

    const int slotW = (SCREEN_W - 12) / 13;
    const int barW  = slotW - 3;
    const int rec   = g_sweeps ? recommend() : 0;

    for (int ch = 1; ch <= 13; ++ch) {
        const int x  = 6 + (ch - 1) * slotW + 1;
        const int bh = g_ch[ch].aps * (h - 12) / peak;
        const bool primary = (ch == 1 || ch == 6 || ch == 11);

        uint16_t col = primary ? COL_ACCENT : COL_BAR;
        if (ch == rec) col = COL_OK;
        if (bh > 0) c.fillRect(x, base - bh, barW, bh, col);

        c.setTextDatum(top_center);
        c.setTextColor(ch == rec ? COL_OK : (primary ? COL_ACCENT : COL_GRID));
        c.drawNumber(ch, x + barW / 2, base + 2);

        if (g_ch[ch].aps > 0) {
            c.setTextDatum(bottom_center);
            c.setTextColor(COL_TEXT);
            c.drawNumber(g_ch[ch].aps, x + barW / 2, base - bh - 1);
        }
    }

    if (g_sweeps) {
        c.setTextDatum(bottom_left);
        c.setTextColor(COL_OK);
        snprintf(buf, sizeof(buf), "best for your router: channel %d", rec);
        c.drawString(buf, 8, SCREEN_H - 2);
    }
}

bool handleTap(int x, int y) { return kBackBtn.contains(x, y); }

}  // namespace

const Tool& tool() {
    static const Tool t{"WiFi Analyzer",
                        "Channel congestion + best channel",
                        begin, stop, poll, draw, handleTap};
    return t;
}

}}  // namespace toolkit::analyzer
