#include "ble_scan.h"

#include <NimBLEDevice.h>
#include <algorithm>

namespace ble_scan {
namespace {

constexpr uint32_t kStaleMs = 20000;

Device            g_devices[kMaxDevices];
int               g_count = 0;
SemaphoreHandle_t g_lock  = nullptr;

// Bluetooth SIG company identifiers, assigned to whoever built the radio stack.
// This is the one field that survives MAC randomisation -- a phone rotating its
// address every 15 minutes still says "Apple" in every advertisement it sends.
struct Company {
    uint16_t    id;
    const char* name;
};

const Company kCompanies[] = {
    {0x0006, "Microsoft"}, {0x0075, "Samsung"},   {0x004C, "Apple"},
    {0x00E0, "Google"},    {0x0171, "Amazon"},    {0x02E5, "Espressif"},
    {0x0059, "Nordic"},    {0x000F, "Broadcom"},  {0x000A, "Qualcomm"},
    {0x0002, "Intel"},     {0x005D, "Realtek"},   {0x0087, "Garmin"},
    {0x02D0, "Fitbit"},    {0x012D, "Sony"},      {0x0157, "Huami/Amazfit"},
    {0x038F, "Xiaomi"},    {0x027D, "Huawei"},    {0x01DA, "Logitech"},
    {0x000C, "Bose"},      {0x0131, "Cypress"},   {0x0499, "Ruuvi"},
    {0x0118, "Tile"},      {0x0141, "Fossil"},    {0x0110, "Nippon Seiki"},
    {0x0030, "ST Micro"},  {0x004F, "APT/Airoha"},{0x0180, "JBL/Harman"},
};

// Advertisements arrive on the NimBLE host task, so everything in here runs off
// the UI task and must take the lock.
//
// Non-const pointer because NimBLE 1.4's accessors are not const-qualified.
void record(NimBLEAdvertisedDevice* d) {
    std::string addr = d->getAddress().toString();

    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;

    int slot = -1;
    for (int i = 0; i < g_count; ++i) {
        if (strncmp(g_devices[i].addr, addr.c_str(), sizeof(g_devices[i].addr)) == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        if (g_count < kMaxDevices) {
            slot = g_count++;
        } else {
            // Table full: evict whichever entry we have not heard from longest.
            slot = 0;
            for (int i = 1; i < g_count; ++i) {
                if (g_devices[i].lastSeenMs < g_devices[slot].lastSeenMs) slot = i;
            }
        }
        g_devices[slot]           = Device{};
        g_devices[slot].txPower   = 127;     // sentinel: not advertised
        g_devices[slot].companyId = 0xFFFF;
        strncpy(g_devices[slot].addr, addr.c_str(), sizeof(g_devices[slot].addr) - 1);
        g_devices[slot].name[0] = '\0';
    }

    Device& dev = g_devices[slot];
    dev.rssi       = d->getRSSI();
    dev.lastSeenMs = millis();
    dev.addrType   = d->getAddress().getType();
    if (dev.hits < 0xFFFF) dev.hits++;

    if (d->haveName()) {
        std::string n = d->getName();
        if (!n.empty()) {
            strncpy(dev.name, n.c_str(), kNameLen - 1);
            dev.name[kNameLen - 1] = '\0';
        }
    }

    if (d->haveTXPower()) dev.txPower = d->getTXPower();

    if (d->haveServiceUUID()) {
        NimBLEUUID u = d->getServiceUUID();
        if (u.bitSize() == 16) dev.serviceUuid = u.getNative()->u16.value;
    }

    if (d->haveManufacturerData()) {
        std::string md = d->getManufacturerData();
        // First two bytes are the company ID, little-endian. Keep the rest raw:
        // it is where the actually-identifying detail lives.
        if (md.size() >= 2) {
            dev.companyId = static_cast<uint8_t>(md[0]) |
                            (static_cast<uint8_t>(md[1]) << 8);
        }
        dev.mfgLen = static_cast<uint8_t>(std::min(md.size(), sizeof(dev.mfg)));
        memcpy(dev.mfg, md.data(), dev.mfgLen);
    }

    xSemaphoreGive(g_lock);
}

class Callbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* d) override { record(d); }
};

}  // namespace

void begin() {
    g_lock = xSemaphoreCreateMutex();

    NimBLEDevice::init("");

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new Callbacks(), /*wantDuplicates=*/true);
    scan->setActiveScan(false);  // passive: listen only, never probe
    // Interval 160 * 0.625ms = 100ms, window 30 * 0.625ms = ~19ms -- a ~19%
    // duty cycle. Deliberately modest: both radios share one 2.4 GHz front-end,
    // and a greedier BLE window starves the WiFi sweeps of the uninterrupted
    // dwell time they need to hear a beacon.
    scan->setInterval(160);
    scan->setWindow(30);
    scan->setMaxResults(0);  // callbacks only; do not buffer results in RAM

    scan->start(0 /* run forever */, nullptr, false);
}

int snapshot(Device* out, int max) {
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(50)) != pdTRUE) return 0;

    uint32_t now = millis();

    // Compact away stale entries in place.
    int w = 0;
    for (int i = 0; i < g_count; ++i) {
        if (now - g_devices[i].lastSeenMs < kStaleMs) g_devices[w++] = g_devices[i];
    }
    g_count = w;

    // Sort before slicing, otherwise a caller asking for fewer rows than we
    // track would get an arbitrary subset rather than the strongest signals.
    std::sort(g_devices, g_devices + g_count, [](const Device& a, const Device& b) {
        return a.rssi > b.rssi;
    });

    int n = std::min(g_count, max);
    memcpy(out, g_devices, n * sizeof(Device));

    xSemaphoreGive(g_lock);
    return n;
}

int activeCount() {
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
    int n = g_count;
    xSemaphoreGive(g_lock);
    return n;
}

const char* companyName(uint16_t companyId) {
    if (companyId == 0xFFFF) return nullptr;
    for (const auto& c : kCompanies) {
        if (c.id == companyId) return c.name;
    }
    return nullptr;
}

namespace {

// Apple stuffs a "Continuity" TLV into its manufacturer data: a type byte, a
// length, then a payload. The type alone says a lot more than "Apple" does --
// it distinguishes a phone advertising its presence from headphones from an
// AirTag. Reverse-engineered and widely documented; not an official spec.
struct AppleModel {
    uint16_t    id;
    const char* name;
};

// Model IDs from the Proximity Pairing (0x07) payload.
const AppleModel kAppleModels[] = {
    {0x0220, "AirPods (1st)"},   {0x0F20, "AirPods (2nd)"},
    {0x1320, "AirPods (3rd)"},   {0x0E20, "AirPods Pro"},
    {0x1420, "AirPods Pro 2"},   {0x0A20, "AirPods Max"},
    {0x0320, "Powerbeats 3"},    {0x0B20, "Powerbeats Pro"},
    {0x0520, "BeatsX"},          {0x0620, "Beats Solo 3"},
    {0x0920, "Beats Studio 3"},  {0x1020, "Beats Flex"},
};

void appleKind(const Device& d, char* out, size_t n) {
    // mfg = [0..1] company ID, [2] type, [3] length, [4..] payload
    if (d.mfgLen < 4) {
        snprintf(out, n, "Apple device");
        return;
    }

    switch (d.mfg[2]) {
        case 0x02: snprintf(out, n, "iBeacon");            return;
        case 0x05: snprintf(out, n, "AirDrop");            return;
        case 0x06: snprintf(out, n, "HomeKit");            return;
        case 0x08: snprintf(out, n, "Hey Siri");           return;
        case 0x09: snprintf(out, n, "AirPlay target");     return;
        case 0x0A: snprintf(out, n, "AirPlay source");     return;
        case 0x0B: snprintf(out, n, "Apple Watch");        return;
        case 0x0C: snprintf(out, n, "Handoff");            return;
        case 0x0D: snprintf(out, n, "Hotspot target");     return;
        case 0x0E: snprintf(out, n, "Hotspot source");     return;
        case 0x0F: snprintf(out, n, "Nearby action");      return;
        case 0x10: snprintf(out, n, "iPhone/Mac nearby");  return;
        case 0x12: snprintf(out, n, "Find My / AirTag");   return;

        case 0x07: {  // Proximity Pairing -- headphones announce their model
            if (d.mfgLen >= 7) {
                uint16_t model = (d.mfg[5] << 8) | d.mfg[6];
                for (const auto& m : kAppleModels) {
                    if (m.id == model) {
                        snprintf(out, n, "%s", m.name);
                        return;
                    }
                }
                snprintf(out, n, "Apple audio %04X", model);
                return;
            }
            snprintf(out, n, "Apple audio");
            return;
        }

        default:
            snprintf(out, n, "Apple type %02X", d.mfg[2]);
            return;
    }
}

struct ServiceName {
    uint16_t    uuid;
    const char* name;
};

// 16-bit service UUIDs that show up constantly in the wild.
const ServiceName kServices[] = {
    {0xFE2C, "Google Fast Pair"}, {0xFE9F, "Google"},
    {0xFEAA, "Eddystone beacon"}, {0xFD6F, "Exposure Notif"},
    {0xFEED, "Tile tracker"},     {0xFEEC, "Tile tracker"},
    {0xFD5A, "Samsung"},          {0xFE03, "Amazon"},
    {0xFE59, "Nordic DFU"},       {0xFD44, "Apple"},
    {0xFDF0, "Google"},           {0xFE07, "Microsoft"},
};

}  // namespace

void deviceKind(const Device& d, char* out, size_t n) {
    if (d.companyId == 0x004C) {  // Apple
        appleKind(d, out, n);
        return;
    }
    if (d.companyId == 0x0006 && d.mfgLen >= 3 && d.mfg[2] == 0x03) {
        snprintf(out, n, "Windows (Swift Pair)");
        return;
    }

    for (const auto& s : kServices) {
        if (s.uuid == d.serviceUuid) {
            snprintf(out, n, "%s", s.name);
            return;
        }
    }

    const char* vendor = companyName(d.companyId);
    if (vendor) {
        snprintf(out, n, "%s", vendor);
        return;
    }
    if (d.companyId != 0xFFFF) {
        snprintf(out, n, "vendor %04X", d.companyId);
        return;
    }
    snprintf(out, n, "unknown device");
}

const char* addrSuffix(const Device& d) {
    size_t len = strlen(d.addr);
    return len >= 5 ? d.addr + len - 5 : d.addr;
}

const char* addrTypeName(uint8_t addrType) {
    switch (addrType) {
        case 0:  return "public";
        case 1:  return "random";
        case 2:  return "public-ID";
        case 3:  return "random-ID";
        default: return "unknown";
    }
}

}  // namespace ble_scan
