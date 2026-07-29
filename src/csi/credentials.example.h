// Copy this file to `credentials.h` and fill in your own network.
//
//     cp src/csi/credentials.example.h src/csi/credentials.h
//
// `credentials.h` is gitignored, so your password stays out of the repo. If the
// file is absent the firmware still builds -- active mode is simply offered as
// unavailable, and the radar falls back to passive listening.
//
// Use your own access point. Active mode associates to it and pings it a
// hundred times a second; that is unremarkable load for a router you own, and
// rude on one you do not.

#pragma once

#define CSI_WIFI_SSID "your-network-name"
#define CSI_WIFI_PASS "your-password"
