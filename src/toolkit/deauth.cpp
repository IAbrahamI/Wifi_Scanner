#include "deauth.h"

#include <WiFi.h>
#include <esp_wifi.h>

namespace toolkit { namespace deauth {
namespace {

// 802.11 management subtypes we care about.
constexpr uint8_t kSubtypeDeauth   = 0x0C;
constexpr uint8_t kSubtypeDisassoc = 0x0A;

constexpr int      kMaxOffenders = 6;
constexpr uint32_t kHopMs        = 300;   // scan every channel for attacks
constexpr float    kAlarmRate    = 2.0f;  // frames/s that count as an attack

struct Offender {
    uint8_t  bssid[6];
    uint16_t count;
    int8_t   rssi;
    uint32_t last;
};

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
Offender     g_off[kMaxOffenders];
int          g_offCount = 0;
volatile uint32_t g_total = 0;

uint8_t  g_channel   = 1;
uint32_t g_lastHop   = 0;
uint32_t g_rateAt    = 0;
uint32_t g_lastTotal = 0;
float    g_rate      = 0.0f;

// Runs in the Wi-Fi task. ISR-flavoured critical section, single short pass.
void record(const uint8_t* bssid, int8_t rssi) {
    portENTER_CRITICAL_ISR(&g_mux);
    g_total++;

    int slot = -1;
    for (int i = 0; i < g_offCount; ++i) {
        if (memcmp(g_off[i].bssid, bssid, 6) == 0) { slot = i; break; }
    }
    if (slot < 0) {
        if (g_offCount < kMaxOffenders) {
            slot = g_offCount++;
        } else {  // evict the least-recently-heard
            slot = 0;
            for (int i = 1; i < g_offCount; ++i) {
                if (g_off[i].last < g_off[slot].last) slot = i;
            }
        }
        memset(&g_off[slot], 0, sizeof(Offender));
        memcpy(g_off[slot].bssid, bssid, 6);
    }
    g_off[slot].count++;
    g_off[slot].rssi = rssi;
    g_off[slot].last = millis();
    portEXIT_CRITICAL_ISR(&g_mux);
}

void rxcb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    auto* p = static_cast<wifi_promiscuous_pkt_t*>(buf);
    const uint8_t subtype = (p->payload[0] >> 4) & 0x0F;
    if (subtype != kSubtypeDeauth && subtype != kSubtypeDisassoc) return;
    // addr3 (BSSID) at offset 16 -- the network being attacked.
    record(p->payload + 16, p->rx_ctrl.rssi);
}

void begin() {
    g_offCount  = 0;
    g_total     = 0;
    g_lastTotal = 0;
    g_rate      = 0.0f;
    g_channel   = 1;
    g_lastHop   = g_rateAt = millis();

    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(false, true);

    wifi_promiscuous_filter_t f = {};
    f.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(&rxcb);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(g_channel, WIFI_SECOND_CHAN_NONE);
}

void stop() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}

void poll() {
    const uint32_t now = millis();

    if (now - g_lastHop >= kHopMs) {
        g_lastHop = now;
        g_channel = g_channel >= 13 ? 1 : g_channel + 1;
        esp_wifi_set_channel(g_channel, WIFI_SECOND_CHAN_NONE);
    }

    if (now - g_rateAt >= 1000) {
        portENTER_CRITICAL(&g_mux);
        const uint32_t total = g_total;
        portEXIT_CRITICAL(&g_mux);
        g_rate      = (total - g_lastTotal) * 1000.0f / (now - g_rateAt);
        g_lastTotal = total;
        g_rateAt    = now;
    }
}

void draw(LGFX_Sprite& c) {
    drawHeader(c, "DEAUTH DETECTOR");

    // Snapshot under lock so the render is consistent.
    Offender off[kMaxOffenders];
    int      n;
    uint32_t total;
    portENTER_CRITICAL(&g_mux);
    n     = g_offCount;
    total = g_total;
    memcpy(off, g_off, sizeof(off));
    portEXIT_CRITICAL(&g_mux);

    const bool alarm = g_rate >= kAlarmRate;
    const uint16_t col = alarm ? COL_ALARM : (total > 0 ? COL_WARN : COL_OK);

    c.drawRect(6, 22, SCREEN_W - 12, 30, col);
    c.setTextSize(2);
    c.setTextDatum(middle_left);
    c.setTextColor(col);
    c.drawString(alarm ? "ATTACK NEARBY" : (total ? "SEEN DEAUTHS" : "ALL CLEAR"),
                 14, 37);

    c.setTextSize(1);
    c.setTextDatum(middle_right);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f/s", g_rate);
    c.setTextColor(COL_TEXT);
    c.drawString(buf, SCREEN_W - 14, 37);

    c.setTextDatum(top_left);
    c.setTextColor(COL_DIM);
    snprintf(buf, sizeof(buf), "total %lu   scanning ch %u",
             static_cast<unsigned long>(total), g_channel);
    c.drawString(buf, 8, 58);

    c.setTextColor(COL_DIM);
    c.drawString("TARGETED NETWORKS", 8, 74);

    if (n == 0) {
        c.setTextColor(COL_GRID);
        c.drawString("none - deauths are rare on a healthy network", 8, 88);
    }
    for (int i = 0; i < n; ++i) {
        const int y = 88 + i * 13;
        c.setTextDatum(top_left);
        c.setTextColor(COL_TEXT);
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", off[i].bssid[0],
                 off[i].bssid[1], off[i].bssid[2], off[i].bssid[3], off[i].bssid[4],
                 off[i].bssid[5]);
        c.drawString(buf, 10, y);

        c.setTextDatum(top_right);
        c.setTextColor(COL_WARN);
        snprintf(buf, sizeof(buf), "x%u  %ddBm", off[i].count, off[i].rssi);
        c.drawString(buf, SCREEN_W - 10, y);
    }
}

bool handleTap(int x, int y) { return kBackBtn.contains(x, y); }

}  // namespace

const Tool& tool() {
    static const Tool t{"Deauth Detector",
                        "Spot Wi-Fi disconnect / jam attacks",
                        begin, stop, poll, draw, handleTap};
    return t;
}

}}  // namespace toolkit::deauth
