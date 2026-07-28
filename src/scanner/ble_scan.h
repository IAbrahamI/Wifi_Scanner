#pragma once

#include <Arduino.h>

// Passive BLE observer. Never transmits a scan request -- it only listens to
// advertising frames, so nothing on the air knows the gadget is there.
//
// Modern phones and wearables rotate their advertised MAC every ~15 minutes, so
// the address is not a stable identity. Two things do survive randomisation and
// are worth surfacing: the manufacturer ID inside the advertisement (which
// still says "Apple" or "Samsung"), and raw signal volume.
namespace ble_scan {

constexpr int kMaxDevices = 40;
constexpr int kNameLen    = 24;
constexpr int kMfgLen     = 24;

struct Device {
    char     addr[18];
    char     name[kNameLen];
    int8_t   rssi;
    int8_t   txPower;      // dBm at 1m as claimed by the device, 127 if absent
    uint16_t companyId;    // Bluetooth SIG member ID, 0xFFFF if not advertised
    uint16_t serviceUuid;  // first advertised 16-bit service UUID, 0 if none
    uint8_t  addrType;     // 0 public, 1 random, 2 public-ID, 3 random-ID
    uint8_t  mfg[kMfgLen]; // raw manufacturer-specific payload, company ID first
    uint8_t  mfgLen;
    uint16_t hits;
    uint32_t lastSeenMs;
};

void begin();

// Copies up to `max` devices into `out`, strongest first, dropping anything not
// heard from in the last few seconds. Returns how many were written.
// Safe to call from the UI task while the BLE host task is writing.
int snapshot(Device* out, int max);

// Distinct advertisers currently being tracked.
int activeCount();

// "Apple", "Samsung", ... or nullptr if the ID is unknown to us.
const char* companyName(uint16_t companyId);

// "public" (a permanent, burned-in MAC) or "random" (rotates periodically).
const char* addrTypeName(uint8_t addrType);

// Best guess at *what the thing is* rather than who made it -- "AirPods Pro",
// "Find My / AirTag", "iPhone/Mac nearby", "Fast Pair". Written into `out`.
// Falls back to the vendor name, then to "unknown device".
void deviceKind(const Device& d, char* out, size_t n);

// The last two octets of the address, e.g. "48:80". Not an identity -- random
// addresses rotate -- but within a session it reliably tells two devices of the
// same make apart, which the vendor name alone cannot do.
const char* addrSuffix(const Device& d);

}  // namespace ble_scan
