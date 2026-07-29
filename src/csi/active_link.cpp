#include "active_link.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

// Optional: absent on a fresh clone, and the firmware still has to build.
#if __has_include("credentials.h")
#include "credentials.h"
#endif

#ifndef CSI_WIFI_SSID
#define CSI_WIFI_SSID ""
#define CSI_WIFI_PASS ""
#endif

namespace active_link {
namespace {

// 10 ms between echo requests. The ping task waits for each reply before the
// next interval, so the real rate lands around 70-100 Hz on a LAN -- an order
// of magnitude past what passive listening delivers.
constexpr uint32_t kPingIntervalMs = 10;
constexpr uint32_t kPingTimeoutMs  = 200;
constexpr uint32_t kConnectTimeout = 15000;

State             g_state   = State::Idle;
uint32_t          g_startAt = 0;
uint8_t           g_channel = 0;
uint8_t           g_bssid[6] = {0};
esp_ping_handle_t g_ping    = nullptr;

// Incremented from the ping task.
volatile uint32_t g_replies = 0;

void onPingSuccess(esp_ping_handle_t, void*) { g_replies++; }

void startPinging() {
    if (g_ping) return;

    const IPAddress gw = WiFi.gatewayIP();
    ip_addr_t target;
    IP_ADDR4(&target, gw[0], gw[1], gw[2], gw[3]);

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.target_addr = target;
    cfg.count       = ESP_PING_COUNT_INFINITE;
    cfg.interval_ms = kPingIntervalMs;
    cfg.timeout_ms  = kPingTimeoutMs;
    cfg.data_size   = 32;

    esp_ping_callbacks_t cbs = {};
    cbs.on_ping_success = onPingSuccess;

    if (esp_ping_new_session(&cfg, &cbs, &g_ping) != ESP_OK) {
        log_e("active_link: could not create ping session");
        g_ping = nullptr;
        return;
    }
    esp_ping_start(g_ping);
    log_i("active_link: pinging %s every %ums", gw.toString().c_str(),
          kPingIntervalMs);
}

}  // namespace

bool configured() { return strlen(CSI_WIFI_SSID) > 0; }

const char* ssid() { return CSI_WIFI_SSID; }

void connect() {
    if (!configured()) {
        g_state = State::Failed;
        return;
    }
    // Sniffing and associating are mutually exclusive uses of the radio.
    esp_wifi_set_promiscuous(false);

    WiFi.mode(WIFI_STA);
    WiFi.begin(CSI_WIFI_SSID, CSI_WIFI_PASS);
    g_state   = State::Connecting;
    g_startAt = millis();
}

State state() { return g_state; }

uint8_t channel() { return g_channel; }

const uint8_t* bssid() { return g_bssid; }

void poll() {
    switch (g_state) {
        case State::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                g_channel = WiFi.channel();
                if (const uint8_t* b = WiFi.BSSID()) memcpy(g_bssid, b, 6);
                g_state = State::Connected;
                log_i("active_link: joined %s on ch%u", CSI_WIFI_SSID, g_channel);
                startPinging();
            } else if (millis() - g_startAt > kConnectTimeout) {
                log_w("active_link: association timed out");
                g_state = State::Failed;
            }
            return;

        case State::Connected:
            // The ping session runs on its own task; nothing to drive here.
            // Losing the AP means losing the illumination, so surface it.
            if (WiFi.status() != WL_CONNECTED) {
                log_w("active_link: lost association");
                g_state = State::Failed;
            }
            return;

        default:
            return;
    }
}

uint32_t replies() { return g_replies; }

void stop() {
    if (g_ping) {
        esp_ping_stop(g_ping);
        esp_ping_delete_session(g_ping);
        g_ping = nullptr;
    }
    WiFi.disconnect(false, false);
    g_state = State::Idle;
}

}  // namespace active_link
