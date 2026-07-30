#include "blehid.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

namespace toolkit { namespace blehid {
namespace {

// Consumer-control HID: one report holding a 16-bit usage code. Press = send
// the code, release = send 0. This is the standard media-key report map.
const uint8_t kReportMap[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (0x3FF)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x3FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array)
    0xC0,              // End Collection
};

// Consumer usage codes.
constexpr uint16_t kPlayPause = 0x00CD;
constexpr uint16_t kNext      = 0x00B5;
constexpr uint16_t kPrev      = 0x00B6;
constexpr uint16_t kVolUp     = 0x00E9;
constexpr uint16_t kVolDown   = 0x00EA;
constexpr uint16_t kMute      = 0x00E2;

NimBLEHIDDevice*     g_hid    = nullptr;
NimBLECharacteristic* g_input = nullptr;
NimBLEServer*        g_server = nullptr;
volatile bool        g_connected = false;

// Brief on-screen flash of the last key sent.
const char* g_lastKey = "";
uint32_t    g_lastKeyAt = 0;

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override { g_connected = true; }
    void onDisconnect(NimBLEServer* s) override {
        g_connected = false;
        s->startAdvertising();  // become findable again
    }
};

void send(uint16_t code, const char* label) {
    if (!g_connected || !g_input) return;
    g_input->setValue(reinterpret_cast<uint8_t*>(&code), sizeof(code));
    g_input->notify();
    uint16_t zero = 0;
    g_input->setValue(reinterpret_cast<uint8_t*>(&zero), sizeof(zero));
    g_input->notify();
    g_lastKey   = label;
    g_lastKeyAt = millis();
}

// ---- button grid ----------------------------------------------------------
struct Button {
    Rect        r;
    const char* label;
    uint16_t    code;
};

constexpr int BW = 100, BH = 40, GAP = 6, TOP = 24, LEFT = 8;

const Button kButtons[] = {
    {{LEFT,                 TOP,          BW, BH}, "PREV",  kPrev},
    {{LEFT + BW + GAP,      TOP,          BW, BH}, "PLAY",  kPlayPause},
    {{LEFT + 2*(BW + GAP),  TOP,          BW, BH}, "NEXT",  kNext},
    {{LEFT,                 TOP + BH + GAP, BW, BH}, "VOL-",  kVolDown},
    {{LEFT + BW + GAP,      TOP + BH + GAP, BW, BH}, "MUTE",  kMute},
    {{LEFT + 2*(BW + GAP),  TOP + BH + GAP, BW, BH}, "VOL+",  kVolUp},
};
constexpr int kButtonCount = sizeof(kButtons) / sizeof(kButtons[0]);

void begin() {
    g_connected = false;
    g_lastKey   = "";

    NimBLEDevice::init("RF Remote");
    NimBLEDevice::setSecurityAuth(true, false, false);  // bond, no MITM/passkey

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(new ServerCb());

    g_hid = new NimBLEHIDDevice(g_server);
    g_input = g_hid->inputReport(1);
    g_hid->manufacturer()->setValue("DIY");
    g_hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
    g_hid->hidInfo(0x00, 0x01);
    g_hid->reportMap(const_cast<uint8_t*>(kReportMap), sizeof(kReportMap));
    g_hid->startServices();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(g_hid->hidService()->getUUID());
    adv->start();
}

void stop() {
    NimBLEDevice::deinit(true);
    g_hid = nullptr;
    g_input = nullptr;
    g_server = nullptr;
    g_connected = false;
}

void poll() {}

void draw(LGFX_Sprite& c) {
    drawHeader(c, "BLE REMOTE");

    c.setTextSize(1);
    c.setTextDatum(middle_right);
    if (g_connected) {
        c.setTextColor(COL_OK);
        c.drawString("paired", SCREEN_W - 6, HEADER_H / 2 - 1);
    } else {
        c.setTextColor(COL_WARN);
        c.drawString("pair me: 'RF Remote'", SCREEN_W - 6, HEADER_H / 2 - 1);
    }

    const bool en = g_connected;
    for (int i = 0; i < kButtonCount; ++i) {
        const Button& b = kButtons[i];
        c.fillRoundRect(b.r.x, b.r.y, b.r.w, b.r.h, 5, en ? COL_BAR : COL_HDR);
        c.drawRoundRect(b.r.x, b.r.y, b.r.w, b.r.h, 5, en ? COL_ACCENT : COL_GRID);
        c.setTextDatum(middle_center);
        c.setTextSize(2);
        c.setTextColor(en ? COL_TEXT : COL_GRID);
        c.drawString(b.label, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2);
    }

    c.setTextSize(1);
    c.setTextDatum(bottom_center);
    if (g_lastKey[0] && millis() - g_lastKeyAt < 800) {
        c.setTextColor(COL_ACCENT);
        char buf[24];
        snprintf(buf, sizeof(buf), "sent: %s", g_lastKey);
        c.drawString(buf, SCREEN_W / 2, SCREEN_H - 2);
    } else if (!g_connected) {
        c.setTextColor(COL_DIM);
        c.drawString("open Bluetooth settings and pair 'RF Remote'",
                     SCREEN_W / 2, SCREEN_H - 2);
    }
}

bool handleTap(int x, int y) {
    if (kBackBtn.contains(x, y)) return true;
    for (int i = 0; i < kButtonCount; ++i) {
        if (kButtons[i].r.contains(x, y)) {
            send(kButtons[i].code, kButtons[i].label);
            return false;
        }
    }
    return false;
}

}  // namespace

const Tool& tool() {
    static const Tool t{"BLE HID Remote",
                        "Bluetooth media / presenter remote",
                        begin, stop, poll, draw, handleTap};
    return t;
}

}}  // namespace toolkit::blehid
