#include "illuminator.h"

#include <esp_wifi.h>
#include <string.h>

namespace illuminator {
namespace {

// 802.11 probe request: 24-byte management header, then the tagged parameters.
//
//   [0..1]   frame control -- type 0 (management), subtype 4 (probe request)
//   [2..3]   duration
//   [4..9]   addr1, destination -- the target AP, so only it replies
//   [10..15] addr2, source      -- our own MAC, filled in at begin()
//   [16..21] addr3, BSSID       -- the target AP again
//   [22..23] sequence control   -- the hardware overwrites this
//
// Then: a wildcard SSID element, and a supported-rates element listing OFDM
// rates only. See the header for why leaving 802.11b rates out matters.
uint8_t g_frame[] = {
    0x40, 0x00,
    0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00,

    0x00, 0x00,  // SSID element, length 0 -- matches any network

    // Supported rates: 6, 9, 12, 18, 24, 36, 48, 54 Mbit/s. The high bit marks
    // a rate as basic. No 1/2/5.5/11 Mbit/s entries, on purpose.
    0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c,
};

constexpr int kAddr1 = 4;
constexpr int kAddr2 = 10;
constexpr int kAddr3 = 16;

bool     g_enabled   = false;
bool     g_hasTarget = false;
uint32_t g_sent      = 0;
uint32_t g_failed    = 0;
uint32_t g_lastSend  = 0;

}  // namespace

void begin() {
    // Source address must be our real interface MAC -- the driver rejects raw
    // frames that claim to come from anywhere else, and spoofing is not the
    // point here anyway.
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    memcpy(g_frame + kAddr2, mac, 6);
}

void setTarget(const uint8_t bssid[6]) {
    memcpy(g_frame + kAddr1, bssid, 6);
    memcpy(g_frame + kAddr3, bssid, 6);
    g_hasTarget = true;
}

void setEnabled(bool on) {
    g_enabled = on;
    if (on) g_lastSend = 0;  // send immediately rather than waiting a period
}

bool enabled() { return g_enabled; }

bool hasTarget() { return g_hasTarget; }

void poll() {
    if (!g_enabled || !g_hasTarget) return;

    const uint32_t now      = millis();
    const uint32_t periodMs = 1000 / kRateHz;
    if (g_lastSend != 0 && now - g_lastSend < periodMs) return;
    g_lastSend = now;

    // en_sys_seq = true: let the hardware own the sequence number.
    const esp_err_t err =
        esp_wifi_80211_tx(WIFI_IF_STA, g_frame, sizeof(g_frame), true);
    if (err == ESP_OK) {
        g_sent++;
    } else {
        g_failed++;
        static uint32_t complained = 0;
        if (now - complained > 5000) {
            complained = now;
            log_w("illuminator: tx failed: %s", esp_err_to_name(err));
        }
    }
}

uint32_t sent() { return g_sent; }

uint32_t failed() { return g_failed; }

}  // namespace illuminator
