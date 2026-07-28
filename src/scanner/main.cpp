// ===========================================================================
//  App 2 -- Dual WiFi + BLE Proximity Sniffer  (ota_1 @ 0x610000)
//
//  Three screens, driven entirely by touch:
//
//    OVERVIEW   split dashboard -- 2.4 GHz channel density on top, nearest BLE
//               advertisers below. Tap either half to open its list.
//    LIST       every AP / every BLE device, strongest first, scrollable.
//               Tap a row to open it.
//    DETAIL     everything known about one AP or one device.
//
//  Escape hatch: hold BOOT for 1.5s, or hold anywhere on screen for 3s.
// ===========================================================================

#include <Arduino.h>
#include <math.h>

#include "../board/board.h"
#include "../board/app_switch.h"
#include "ble_scan.h"
#include "wifi_scan.h"

namespace {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 170;

constexpr int HEADER_H = 18;
constexpr int BODY_TOP = 21;
constexpr int ROW_H    = 13;
constexpr int LIST_ROWS = 10;

constexpr uint16_t COL_BG     = 0x0000;
constexpr uint16_t COL_HEADER = 0x1082;
constexpr uint16_t COL_WIFI   = 0x05FF;  // cyan
constexpr uint16_t COL_BLE    = 0xFD20;  // amber
constexpr uint16_t COL_TEXT   = 0xFFFF;
constexpr uint16_t COL_DIM    = 0x7BEF;
constexpr uint16_t COL_GRID   = 0x2124;
constexpr uint16_t COL_BAR    = 0x18E3;

LGFX_Sprite canvas(&lcd);

enum class View { Overview, WifiList, BleList, WifiDetail, BleDetail };

View     g_view       = View::Overview;
int      g_scroll     = 0;
uint16_t g_accent     = COL_WIFI;

wifi_scan::Ap    g_selAp{};
ble_scan::Device g_selDev{};
bool             g_selStale = false;

// Mirror of the BLE table, refreshed once per frame so list and detail views
// agree with each other.
ble_scan::Device g_bleCache[ble_scan::kMaxDevices];
int              g_bleCount = 0;

// ------------------------------------------------------------- formatting ---

float rssiFraction(int8_t rssi) {
    float f = (rssi + 95.0f) / 65.0f;  // -95 dBm floor, -30 dBm "on top of it"
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

const char* signalWord(int8_t rssi) {
    if (rssi >= -50) return "excellent";
    if (rssi >= -60) return "strong";
    if (rssi >= -70) return "good";
    if (rssi >= -80) return "weak";
    return "very weak";
}

// Log-distance path loss, bucketed rather than printed as a number. The model
// assumes free space and a known transmit power; indoors, with walls and bodies
// in the way, the real figure is easily off by 2x. Bands are the honest way to
// show it.
const char* distanceBand(int8_t rssi, int8_t refAt1m) {
    float d = powf(10.0f, (refAt1m - rssi) / (10.0f * 2.5f));
    if (d < 1.0f)  return "under 1 m";
    if (d < 3.0f)  return "1 - 3 m";
    if (d < 10.0f) return "3 - 10 m";
    return "over 10 m";
}

void formatAge(uint32_t sinceMs, char* out, size_t n) {
    uint32_t age = millis() - sinceMs;
    if (age < 1000) snprintf(out, n, "just now");
    else            snprintf(out, n, "%lus ago", age / 1000);
}

// ------------------------------------------------------------- chrome ------

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

constexpr Rect kBackBtn = {0, 0, 58, HEADER_H};
constexpr Rect kUpBtn   = {SCREEN_W - 58, 0, 29, HEADER_H};
constexpr Rect kDownBtn = {SCREEN_W - 29, 0, 29, HEADER_H};

void drawHeader(const char* title, bool back, bool pager, int shown, int total) {
    canvas.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER);
    canvas.setTextSize(1);

    if (back) {
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(g_accent);
        canvas.drawString("< BACK", 6, HEADER_H / 2 - 1);
    } else {
        canvas.setTextDatum(middle_left);
        canvas.setTextColor(COL_TEXT);
        canvas.drawString("SNIFFER", 6, HEADER_H / 2 - 1);
    }

    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_TEXT);
    canvas.drawString(title, SCREEN_W / 2, HEADER_H / 2 - 1);

    if (pager) {
        canvas.drawFastVLine(kUpBtn.x, 2, HEADER_H - 4, COL_GRID);
        canvas.drawFastVLine(kDownBtn.x, 2, HEADER_H - 4, COL_GRID);
        bool canUp   = g_scroll > 0;
        bool canDown = g_scroll + shown < total;
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(canUp ? g_accent : COL_GRID);
        canvas.drawString("^", kUpBtn.x + kUpBtn.w / 2, HEADER_H / 2 - 1);
        canvas.setTextColor(canDown ? g_accent : COL_GRID);
        canvas.drawString("v", kDownBtn.x + kDownBtn.w / 2, HEADER_H / 2 - 1);
    }
}

// One label/value line in a detail view, with an optional dim note underneath.
int detailLine(int y, const char* label, const char* value, const char* note = nullptr) {
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(label, 8, y);
    canvas.setTextColor(COL_TEXT);
    canvas.drawString(value, 84, y);
    if (note) {
        canvas.setTextColor(COL_GRID);
        canvas.drawString(note, 84, y + 9);
        return y + 20;
    }
    return y + 12;
}

// ------------------------------------------------------------- overview ----

void drawOverview() {
    const auto& s = wifi_scan::snapshot();

    char hdr[40];
    snprintf(hdr, sizeof(hdr), "AP:%u  BLE:%d", s.total, g_bleCount);
    canvas.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HEADER);
    canvas.setTextSize(1);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COL_TEXT);
    canvas.drawString("SNIFFER", 6, HEADER_H / 2 - 1);
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(hdr, SCREEN_W - 6, HEADER_H / 2 - 1);

    // ---- WiFi density chart -------------------------------------------------
    constexpr int WIFI_TOP  = HEADER_H + 12;
    constexpr int WIFI_H    = 52;
    constexpr int WIFI_BASE = WIFI_TOP + WIFI_H;

    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_WIFI);
    canvas.drawString("2.4GHz DENSITY", 6, HEADER_H + 2);

    canvas.setTextDatum(top_right);
    canvas.setTextColor(COL_DIM);
    if (s.total > 0) {
        canvas.drawString("tap to list >", SCREEN_W - 6, HEADER_H + 2);
    } else {
        char buf[40];
        snprintf(buf, sizeof(buf), "sweep %lu, %lu failed", s.sweeps, s.failures);
        canvas.drawString(buf, SCREEN_W - 6, HEADER_H + 2);
    }

    uint16_t peak = 1;
    for (int ch = 1; ch <= 13; ++ch) peak = max(peak, s.perChannel[ch]);

    canvas.drawFastHLine(6, WIFI_BASE, SCREEN_W - 12, COL_GRID);

    const int slotW = (SCREEN_W - 12) / 13;
    const int barW  = slotW - 4;
    for (int ch = 1; ch <= 13; ++ch) {
        int x = 6 + (ch - 1) * slotW + 2;
        int h = (s.perChannel[ch] * (WIFI_H - 10)) / peak;
        if (h > 0) canvas.fillRect(x, WIFI_BASE - h, barW, h, COL_WIFI);

        bool primary = (ch == 1 || ch == 6 || ch == 11);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(primary ? COL_WIFI : COL_GRID);
        canvas.drawNumber(ch, x + barW / 2, WIFI_BASE + 2);

        if (s.perChannel[ch] > 0) {
            canvas.setTextDatum(bottom_center);
            canvas.setTextColor(COL_TEXT);
            canvas.drawNumber(s.perChannel[ch], x + barW / 2, WIFI_BASE - h - 1);
        }
    }

    // ---- BLE proximity preview ---------------------------------------------
    constexpr int BLE_TOP   = WIFI_BASE + 16;
    constexpr int BLE_ROW_H = 11;
    constexpr int BLE_ROWS  = (SCREEN_H - BLE_TOP - 2) / BLE_ROW_H;

    canvas.drawFastHLine(0, BLE_TOP - 12, SCREEN_W, COL_GRID);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_BLE);
    canvas.drawString("BLE PROXIMITY", 6, BLE_TOP - 10);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(COL_DIM);
    canvas.drawString("tap to list >", SCREEN_W - 6, BLE_TOP - 10);

    if (g_bleCount == 0) {
        canvas.setTextDatum(top_left);
        canvas.setTextColor(COL_DIM);
        canvas.drawString("no advertisers in range", 6, BLE_TOP + 2);
        return;
    }

    int n = min(g_bleCount, BLE_ROWS);
    for (int i = 0; i < n; ++i) {
        const auto& d = g_bleCache[i];
        int y = BLE_TOP + i * BLE_ROW_H;

        int w = static_cast<int>(rssiFraction(d.rssi) * (SCREEN_W - 12));
        canvas.fillRect(6, y, w, BLE_ROW_H - 2, COL_BAR);

        char kind[32];
        ble_scan::deviceKind(d, kind, sizeof(kind));
        canvas.setTextDatum(top_left);
        canvas.setTextColor(COL_TEXT);
        canvas.drawString(d.name[0] ? d.name : kind, 9, y + 1);

        canvas.setTextColor(COL_DIM);
        canvas.drawString(ble_scan::addrSuffix(d), 216, y + 1);

        char buf[12];
        snprintf(buf, sizeof(buf), "%d", d.rssi);
        canvas.setTextDatum(top_right);
        canvas.setTextColor(COL_BLE);
        canvas.drawString(buf, SCREEN_W - 8, y + 1);
    }
}

// ------------------------------------------------------------- lists -------

// Shared row chrome: a signal-strength bar behind the text, so relative
// proximity reads without having to parse the dBm number.
void listRow(int y, int8_t rssi, const char* left, const char* mid,
             const char* right, uint16_t accent) {
    int w = static_cast<int>(rssiFraction(rssi) * (SCREEN_W - 12));
    canvas.fillRect(6, y, w, ROW_H - 2, COL_BAR);

    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_TEXT);
    canvas.drawString(left, 9, y + 2);

    if (mid) {
        canvas.setTextColor(COL_DIM);
        canvas.drawString(mid, 216, y + 2);
    }

    canvas.setTextDatum(top_right);
    canvas.setTextColor(accent);
    canvas.drawString(right, SCREEN_W - 8, y + 2);
}

void drawWifiList() {
    const auto& s = wifi_scan::snapshot();
    int shown = min(static_cast<int>(s.apCount) - g_scroll, LIST_ROWS);
    if (shown < 0) shown = 0;

    char title[24];
    snprintf(title, sizeof(title), "WIFI  %u APs", s.apCount);
    drawHeader(title, true, true, shown, s.apCount);

    if (s.apCount == 0) {
        canvas.setTextDatum(top_left);
        canvas.setTextColor(COL_DIM);
        canvas.drawString("no access points found yet", 8, BODY_TOP + 4);
        return;
    }

    for (int i = 0; i < shown; ++i) {
        const auto& ap = s.aps[g_scroll + i];
        char ch[12], rssi[12];
        snprintf(ch, sizeof(ch), "ch%u", ap.channel);
        snprintf(rssi, sizeof(rssi), "%d", ap.rssi);

        char ssid[27];
        strncpy(ssid, ap.ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';

        listRow(BODY_TOP + i * ROW_H, ap.rssi, ssid, ch, rssi, COL_WIFI);
    }
}

void drawBleList() {
    int shown = min(g_bleCount - g_scroll, LIST_ROWS);
    if (shown < 0) shown = 0;

    char title[24];
    snprintf(title, sizeof(title), "BLE  %d devices", g_bleCount);
    drawHeader(title, true, true, shown, g_bleCount);

    if (g_bleCount == 0) {
        canvas.setTextDatum(top_left);
        canvas.setTextColor(COL_DIM);
        canvas.drawString("no advertisers in range", 8, BODY_TOP + 4);
        return;
    }

    for (int i = 0; i < shown; ++i) {
        const auto& d = g_bleCache[g_scroll + i];

        // A self-chosen name beats anything we can infer; otherwise fall back
        // to what the advertisement says the device *is*.
        char kind[32];
        ble_scan::deviceKind(d, kind, sizeof(kind));
        const char* label = d.name[0] ? d.name : kind;

        char rssi[12];
        snprintf(rssi, sizeof(rssi), "%d", d.rssi);

        // The address tail is the discriminator: two devices of the same make
        // are otherwise indistinguishable in a list.
        listRow(BODY_TOP + i * ROW_H, d.rssi, label, ble_scan::addrSuffix(d),
                rssi, COL_BLE);
    }
}

// ------------------------------------------------------------- details -----

void drawWifiDetail() {
    drawHeader("ACCESS POINT", true, false, 0, 0);

    const auto& ap = g_selAp;
    int y = BODY_TOP + 2;

    y = detailLine(y, "Network", ap.ssid);
    y = detailLine(y, "BSSID", ap.bssid, "the router's radio MAC address");

    char buf[48];
    snprintf(buf, sizeof(buf), "%u  (%u MHz)", ap.channel,
             wifi_scan::channelFreqMhz(ap.channel));
    y = detailLine(y, "Channel", buf);

    snprintf(buf, sizeof(buf), "%d dBm  (%s)", ap.rssi, signalWord(ap.rssi));
    y = detailLine(y, "Signal", buf);

    y = detailLine(y, "Distance", distanceBand(ap.rssi, -40), "rough estimate only");
    y = detailLine(y, "Security", wifi_scan::authName(ap.auth));

    if (g_selStale) {
        canvas.setTextDatum(bottom_left);
        canvas.setTextColor(COL_DIM);
        canvas.drawString("out of range - last known values", 8, SCREEN_H - 2);
    }
}

void drawBleDetail() {
    drawHeader("BLE DEVICE", true, false, 0, 0);

    const auto& d = g_selDev;
    int y = BODY_TOP + 2;
    char buf[64];

    char kind[32];
    ble_scan::deviceKind(d, kind, sizeof(kind));
    y = detailLine(y, "Looks like", kind);

    y = detailLine(y, "Name", d.name[0] ? d.name : "(not advertised)");

    const char* vendor = ble_scan::companyName(d.companyId);
    if (vendor) {
        y = detailLine(y, "Vendor", vendor);
    } else if (d.companyId != 0xFFFF) {
        snprintf(buf, sizeof(buf), "unknown (0x%04X)", d.companyId);
        y = detailLine(y, "Vendor", buf);
    } else {
        y = detailLine(y, "Vendor", "(not advertised)");
    }

    y = detailLine(y, "Address", d.addr);

    const char* note = (d.addrType == 1 || d.addrType == 3)
                           ? "rotates every ~15 min - not an identity"
                           : "permanent, burned into the chip";
    y = detailLine(y, "Addr type", ble_scan::addrTypeName(d.addrType), note);

    snprintf(buf, sizeof(buf), "%d dBm  (%s)", d.rssi, signalWord(d.rssi));
    y = detailLine(y, "Signal", buf);

    snprintf(buf, sizeof(buf), "%s  (rough)",
             distanceBand(d.rssi, d.txPower == 127 ? -59 : d.txPower));
    y = detailLine(y, "Distance", buf);

    char age[24];
    formatAge(d.lastSeenMs, age, sizeof(age));
    snprintf(buf, sizeof(buf), "%u adverts, %s", d.hits, age);
    y = detailLine(y, "Seen", buf);

    // The undecoded payload, so anything this build cannot name is still
    // visible rather than silently dropped.
    if (d.mfgLen > 2) {
        char hex[40];
        int  p = 0;
        for (int i = 2; i < d.mfgLen && p < static_cast<int>(sizeof(hex)) - 3; ++i) {
            p += snprintf(hex + p, sizeof(hex) - p, "%02X", d.mfg[i]);
        }
        y = detailLine(y, "Raw", hex);
    }

    if (g_selStale) {
        canvas.setTextDatum(bottom_left);
        canvas.setTextColor(COL_DIM);
        canvas.drawString("out of range - last known values", 8, SCREEN_H - 2);
    }
}

// ------------------------------------------------------------- input -------

int rowAt(int y) {
    if (y < BODY_TOP) return -1;
    int row = (y - BODY_TOP) / ROW_H;
    return row < LIST_ROWS ? row : -1;
}

// Keeps the open detail view tracking live measurements instead of freezing on
// whatever the values were at the moment it was opened.
void refreshSelection() {
    if (g_view == View::WifiDetail) {
        const auto& s = wifi_scan::snapshot();
        for (int i = 0; i < s.apCount; ++i) {
            if (strcmp(s.aps[i].bssid, g_selAp.bssid) == 0) {
                g_selAp    = s.aps[i];
                g_selStale = false;
                return;
            }
        }
        g_selStale = true;
    } else if (g_view == View::BleDetail) {
        for (int i = 0; i < g_bleCount; ++i) {
            if (strcmp(g_bleCache[i].addr, g_selDev.addr) == 0) {
                g_selDev   = g_bleCache[i];
                g_selStale = false;
                return;
            }
        }
        g_selStale = true;
    }
}

void handleTap(int x, int y) {
    switch (g_view) {
        case View::Overview:
            // The chart occupies the top of the screen, the BLE preview the
            // bottom; split the tap the same way.
            if (y < 100) {
                g_view   = View::WifiList;
                g_accent = COL_WIFI;
            } else {
                g_view   = View::BleList;
                g_accent = COL_BLE;
            }
            g_scroll = 0;
            break;

        case View::WifiList: {
            if (kBackBtn.contains(x, y)) { g_view = View::Overview; return; }
            const auto& s = wifi_scan::snapshot();
            if (kUpBtn.contains(x, y)) {
                g_scroll = max(0, g_scroll - LIST_ROWS);
                return;
            }
            if (kDownBtn.contains(x, y)) {
                if (g_scroll + LIST_ROWS < s.apCount) g_scroll += LIST_ROWS;
                return;
            }
            int row = rowAt(y);
            if (row >= 0 && g_scroll + row < s.apCount) {
                g_selAp    = s.aps[g_scroll + row];
                g_selStale = false;
                g_view     = View::WifiDetail;
            }
            break;
        }

        case View::BleList: {
            if (kBackBtn.contains(x, y)) { g_view = View::Overview; return; }
            if (kUpBtn.contains(x, y)) {
                g_scroll = max(0, g_scroll - LIST_ROWS);
                return;
            }
            if (kDownBtn.contains(x, y)) {
                if (g_scroll + LIST_ROWS < g_bleCount) g_scroll += LIST_ROWS;
                return;
            }
            int row = rowAt(y);
            if (row >= 0 && g_scroll + row < g_bleCount) {
                g_selDev   = g_bleCache[g_scroll + row];
                g_selStale = false;
                g_view     = View::BleDetail;
            }
            break;
        }

        case View::WifiDetail:
            g_view = View::WifiList;
            break;

        case View::BleDetail:
            g_view = View::BleList;
            break;
    }
}

void render() {
    canvas.fillSprite(COL_BG);
    switch (g_view) {
        case View::Overview:   drawOverview();   break;
        case View::WifiList:   drawWifiList();   break;
        case View::BleList:    drawBleList();    break;
        case View::WifiDetail: drawWifiDetail(); break;
        case View::BleDetail:  drawBleDetail();  break;
    }
    canvas.pushSprite(0, 0);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    board::begin(1);  // landscape 320x170, USB-C on the right

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(SCREEN_W, SCREEN_H);

    canvas.fillSprite(COL_BG);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_TEXT);
    canvas.drawString("bringing up radios...", SCREEN_W / 2, SCREEN_H / 2);
    canvas.pushSprite(0, 0);

    // WiFi before BLE: the coexistence manager wants the WiFi driver installed
    // first, otherwise the BT controller grabs the front-end and WiFi init
    // comes back with ESP_ERR_INVALID_STATE.
    wifi_scan::begin();
    ble_scan::begin();
}

void loop() {
    wifi_scan::poll();

    // Only the UI task may touch the I2C bus, so the touch escape hatch is fed
    // from here rather than polled in the background watchdog.
    int32_t tx, ty;
    bool touching = lcd.getTouch(&tx, &ty);
    app_switch::feedTouchHold(touching);

    // Tap = press and release in the same place, quickly. The duration check
    // keeps a long-press (the escape hatch) from also firing a selection when
    // the finger finally lifts.
    static bool     wasTouching = false;
    static int32_t  pressX = 0, pressY = 0;
    static uint32_t pressAt = 0;

    if (touching && !wasTouching) {
        pressX  = tx;
        pressY  = ty;
        pressAt = millis();
        // Logged so an off-panel touch region (e.g. a bezel "home" circle) can
        // be identified from the serial monitor and mapped to an action.
        log_i("touch @ %d,%d", pressX, pressY);
    } else if (!touching && wasTouching) {
        if (millis() - pressAt < 600) {
            handleTap(pressX, pressY);
            render();
        }
    }
    wasTouching = touching;

    static uint32_t lastDraw = 0;
    if (millis() - lastDraw >= 250) {
        lastDraw   = millis();
        g_bleCount = ble_scan::snapshot(g_bleCache, ble_scan::kMaxDevices);
        refreshSelection();
        render();
    }

    delay(10);
}
