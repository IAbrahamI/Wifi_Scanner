#include "wifi_scan.h"

#include <WiFi.h>
#include <algorithm>

namespace wifi_scan {
namespace {

Snapshot g_snap{};
bool     g_scanning  = false;
uint32_t g_nextStart = 0;

// Beacons go out roughly every 100 ms, so the radio has to sit on a channel for
// noticeably longer than that to be sure of hearing one -- especially while the
// BLE scanner is taking its share of the front-end. 120 ms was too tight and
// produced empty sweeps; 300 ms reliably catches at least three beacon slots.
constexpr uint32_t kDwellMsPerChannel = 300;
constexpr uint32_t kRestDelayMs       = 250;  // breathing room for the BLE stack

void startSweep() {
    // async = true, show_hidden = true, passive = true
    WiFi.scanNetworks(true, true, true, kDwellMsPerChannel);
    g_scanning = true;
}

void collectSweep(int n) {
    Snapshot s{};
    s.sweeps   = g_snap.sweeps + 1;
    s.failures = g_snap.failures;

    for (int i = 0; i < n; ++i) {
        int ch = WiFi.channel(i);
        if (ch > 0 && ch < kMaxChannel) s.perChannel[ch]++;

        if (s.apCount < kMaxAps) {
            Ap& ap = s.aps[s.apCount++];

            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) ssid = "<hidden>";
            strncpy(ap.ssid, ssid.c_str(), sizeof(ap.ssid) - 1);
            ap.ssid[sizeof(ap.ssid) - 1] = '\0';

            strncpy(ap.bssid, WiFi.BSSIDstr(i).c_str(), sizeof(ap.bssid) - 1);
            ap.bssid[sizeof(ap.bssid) - 1] = '\0';

            ap.channel = static_cast<uint8_t>(ch);
            ap.rssi    = static_cast<int8_t>(WiFi.RSSI(i));
            ap.auth    = static_cast<uint8_t>(WiFi.encryptionType(i));
        }
    }

    std::sort(s.aps, s.aps + s.apCount,
              [](const Ap& a, const Ap& b) { return a.rssi > b.rssi; });

    s.total = n < 0 ? 0 : n;
    g_snap  = s;

    log_i("wifi: sweep %u found %d APs", s.sweeps, n);
    WiFi.scanDelete();
}

}  // namespace

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);  // no AP association -- we only listen
    delay(100);

    g_nextStart = millis();
}

void poll() {
    if (!g_scanning) {
        if (millis() >= g_nextStart) startSweep();
        return;
    }

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;

    if (n == WIFI_SCAN_FAILED) {
        g_snap.failures++;
        log_w("wifi: sweep failed (%u total)", g_snap.failures);
        WiFi.scanDelete();
    } else {
        collectSweep(n);
    }
    g_scanning  = false;
    g_nextStart = millis() + kRestDelayMs;
}

const Snapshot& snapshot() { return g_snap; }

const char* authName(uint8_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN:            return "open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI-PSK";
        default:                        return "unknown";
    }
}

uint16_t channelFreqMhz(uint8_t channel) {
    if (channel == 14) return 2484;
    if (channel >= 1 && channel <= 13) return 2407 + channel * 5;
    return 0;
}

}  // namespace wifi_scan
