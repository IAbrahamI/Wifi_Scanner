#pragma once

#include <Arduino.h>

// Passive ambient WiFi survey: a rolling asynchronous scan across all 2.4 GHz
// channels, kept as both a per-channel density histogram and a list of the
// individual access points behind it.
//
// The scan is asynchronous on purpose. A blocking WiFi.scanNetworks() parks the
// radio for seconds at a time, which starves the BLE stack sharing the same
// front-end and shows up as dropped advertisements.
namespace wifi_scan {

constexpr int kMaxChannel = 14;  // index 1..13 used
constexpr int kMaxAps     = 40;

struct Ap {
    char    ssid[33];
    char    bssid[18];
    uint8_t channel;
    int8_t  rssi;
    uint8_t auth;  // wifi_auth_mode_t
};

struct Snapshot {
    uint16_t perChannel[kMaxChannel];  // AP count per channel
    uint16_t total;                    // APs seen in the last completed sweep
    Ap       aps[kMaxAps];             // strongest first
    uint16_t apCount;                  // entries populated in `aps`
    uint32_t sweeps;                   // completed sweeps since boot
    uint32_t failures;                 // sweeps that came back FAILED
};

void begin();

// Drives the scan state machine. Call from loop(); never blocks.
void poll();

const Snapshot& snapshot();

// Human-readable security mode, e.g. "WPA2-PSK", "open".
const char* authName(uint8_t auth);

// Centre frequency in MHz for a 2.4 GHz channel number.
uint16_t channelFreqMhz(uint8_t channel);

}  // namespace wifi_scan
