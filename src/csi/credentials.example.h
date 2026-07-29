// OPTIONAL convenience seed. The preferred way to provision is over serial:
//
//     pio device monitor
//     wifi MyNetwork MyPassword
//
// That writes the credentials straight into the device's NVS partition and
// connects. Nothing ends up in the firmware image, the build directory, or any
// copy of the .bin you flash onto another board. `wifi-clear` forgets them and
// `wifi-status` reports without echoing the password back.
//
// If you would rather not type it each time you set up a fresh board, copy this
// file to `credentials.h` and fill it in:
//
//     cp src/csi/credentials.example.h src/csi/credentials.h
//
// `credentials.h` is gitignored, and its contents are copied into NVS on first
// boot -- but be aware they are also compiled into the firmware binary, which
// is exactly what serial provisioning avoids. Delete the file once the board
// has booted once and NVS has the credentials.
//
// Either way: use your own access point. Active mode associates to it and pings
// it a hundred times a second. That is unremarkable load for a router you own.

#pragma once

#define CSI_WIFI_SSID "your-network-name"
#define CSI_WIFI_PASS "your-password"
