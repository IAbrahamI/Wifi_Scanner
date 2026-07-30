#include "eviltwin.h"

#include <WiFi.h>
#include <algorithm>

namespace toolkit { namespace eviltwin {
namespace {

constexpr int kMaxNets = 16;

struct Net {
    char    ssid[33];
    uint8_t bssidCount;   // distinct radios advertising this SSID
    bool    sawOpen;
    bool    sawSecured;
    int8_t  bestRssi;
    uint8_t firstBssid[6];
};

Net g_nets[kMaxNets];
int g_netCount = 0;

bool     g_scanning  = false;
uint32_t g_nextScan  = 0;
uint32_t g_sweeps    = 0;

// 0 = ok, 1 = watch (duplicated SSID, could be legit mesh), 2 = risk (mismatched
// security on one SSID -- a classic evil-twin tell).
int risk(const Net& n) {
    if (n.sawOpen && n.sawSecured) return 2;
    if (n.bssidCount >= 2)         return 1;
    return 0;
}

void collect(int found) {
    Net nets[kMaxNets];
    int count = 0;

    for (int i = 0; i < found; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;  // hidden APs cannot be name-matched

        const bool secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        const int  rssi    = WiFi.RSSI(i);
        const uint8_t* bssid = WiFi.BSSID(i);

        int slot = -1;
        for (int j = 0; j < count; ++j) {
            if (strncmp(nets[j].ssid, ssid.c_str(), sizeof(nets[j].ssid)) == 0) {
                slot = j;
                break;
            }
        }
        if (slot < 0) {
            if (count >= kMaxNets) continue;
            slot = count++;
            Net& n = nets[slot];
            memset(&n, 0, sizeof(Net));
            strncpy(n.ssid, ssid.c_str(), sizeof(n.ssid) - 1);
            n.bestRssi = -127;
            if (bssid) memcpy(n.firstBssid, bssid, 6);
        }

        Net& n = nets[slot];
        // Count a radio as distinct only if its BSSID differs from the first
        // one seen -- avoids counting the same AP heard twice in a sweep.
        if (bssid && memcmp(bssid, n.firstBssid, 6) != 0 && n.bssidCount == 0) {
            n.bssidCount = 2;
        } else if (n.bssidCount == 0) {
            n.bssidCount = 1;
        } else if (bssid && memcmp(bssid, n.firstBssid, 6) != 0) {
            n.bssidCount++;
        }
        n.sawOpen    |= !secured;
        n.sawSecured |= secured;
        if (rssi > n.bestRssi) n.bestRssi = rssi;
    }

    // Riskiest first, so the interesting rows are always on screen.
    std::sort(nets, nets + count,
              [](const Net& a, const Net& b) { return risk(a) > risk(b); });

    memcpy(g_nets, nets, sizeof(Net) * count);
    g_netCount = count;
    g_sweeps++;
    WiFi.scanDelete();
}

void begin() {
    g_netCount = 0;
    g_sweeps   = 0;
    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(false, true);
    g_nextScan = millis();
    g_scanning = false;
}

void stop() { WiFi.scanDelete(); }

void poll() {
    if (!g_scanning) {
        if (millis() >= g_nextScan) {
            WiFi.scanNetworks(true, true, false, 150);
            g_scanning = true;
        }
        return;
    }
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    if (n != WIFI_SCAN_FAILED) collect(n);
    else WiFi.scanDelete();
    g_scanning = false;
    g_nextScan = millis() + 500;
}

void draw(LGFX_Sprite& c) {
    drawHeader(c, "EVIL-TWIN DETECTOR");

    c.setTextSize(1);
    c.setTextDatum(top_left);
    c.setTextColor(COL_DIM);
    c.drawString("same SSID from 2+ radios = suspicious", 6, 22);

    if (g_netCount == 0) {
        c.setTextColor(COL_GRID);
        c.drawString(g_sweeps ? "no networks found" : "scanning...", 6, 40);
        return;
    }

    const int rows = min(g_netCount, 9);
    for (int i = 0; i < rows; ++i) {
        const Net& n   = g_nets[i];
        const int  r   = risk(n);
        const int  y   = 36 + i * 14;

        const char* badge = r == 2 ? "RISK" : (r == 1 ? "WATCH" : "ok");
        const uint16_t col = r == 2 ? COL_ALARM : (r == 1 ? COL_WARN : COL_GRID);

        c.setTextDatum(top_left);
        c.setTextColor(col);
        c.drawString(badge, 6, y);

        c.setTextColor(r ? COL_TEXT : COL_DIM);
        char ssid[24];
        strncpy(ssid, n.ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        c.drawString(ssid, 52, y);

        char buf[28];
        if (r == 2)      snprintf(buf, sizeof(buf), "open+secured!");
        else             snprintf(buf, sizeof(buf), "%u radios %ddBm",
                                  n.bssidCount, n.bestRssi);
        c.setTextDatum(top_right);
        c.setTextColor(col);
        c.drawString(buf, SCREEN_W - 6, y);
    }
}

bool handleTap(int x, int y) { return kBackBtn.contains(x, y); }

}  // namespace

const Tool& tool() {
    static const Tool t{"Evil-Twin Detector",
                        "Flag SSIDs cloned across radios",
                        begin, stop, poll, draw, handleTap};
    return t;
}

}}  // namespace toolkit::eviltwin
