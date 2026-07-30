// ===========================================================================
//  App 1 -- Wi-Fi CSI Radar Tracker  (ota_0 @ 0x210000)
//
//  Passive motion sensing. The radio parks on one channel, locks onto whichever
//  transmitter it hears most (usually the nearest AP's beacon), and watches how
//  that link's per-subcarrier channel response wobbles. A body moving through
//  the path between them shows up as a spike.
//
//  Two views, toggled from the footer:
//    RADAR  proximity bands as concentric rings. A detection lights the whole
//           ring, never a dot -- one antenna gives no bearing, and drawing a
//           blip at some angle would be inventing information.
//    GRAPH  the raw turbulence trace, which is what you want when tuning
//           thresholds or checking that the link is healthy.
//
//  Workflow: wait for LOCK, stand out of the way, tap CALIBRATE, then move.
//
//  Escape hatch: press either side button, or hold the screen for 3s.
// ===========================================================================

#include <Arduino.h>
#include <math.h>

#include "../board/board.h"
#include "../board/app_switch.h"
#include "active_link.h"
#include "channel_survey.h"
#include "csi_capture.h"
#include "illuminator.h"
#include "wifi_setup.h"

namespace {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 170;

constexpr int HEADER_H = 18;
constexpr int BODY_TOP = 20;
constexpr int FOOT_TOP = 146;

constexpr uint16_t COL_BG    = 0x0000;
constexpr uint16_t COL_HDR   = 0x1082;
constexpr uint16_t COL_TEXT  = 0xFFFF;
constexpr uint16_t COL_DIM   = 0x7BEF;
constexpr uint16_t COL_GRID  = 0x2124;
constexpr uint16_t COL_CLEAR = 0x07E0;  // green
constexpr uint16_t COL_NEAR  = 0xFFE0;  // yellow
constexpr uint16_t COL_MOVE  = 0xF800;  // red
constexpr uint16_t COL_TRACE = 0x05FF;  // cyan

// Multiples of the calibrated quiet-room baseline. Deliberately generous: an
// empty room does not sit at exactly 1.0, it breathes.
constexpr float kFaintScore    = 1.35f;
constexpr float kPresenceScore = 1.8f;
constexpr float kMotionScore   = 3.2f;

constexpr float kGraphMax = 6.0f;  // graph vertical range, same units

LGFX_Sprite canvas(&lcd);

csi_capture::Stats g_stats;
float              g_history[csi_capture::kHistoryLen];
int                g_historyCount = 0;

enum class View { Picker, WifiSetup, Radar, Graph };
View g_view = View::Picker;

// Only meaningful once a channel has been chosen from the survey.
uint8_t g_channel = 6;

// ---- radar widget ---------------------------------------------------------
constexpr int RADAR_CX = 76;
constexpr int RADAR_CY = 82;
constexpr int RADAR_R  = 56;

// Ring radii, outermost = faintest disturbance. There are three because the
// score thresholds give three distinguishable levels, not because we can
// resolve three distances.
constexpr int kRingR[3] = {19, 37, RADAR_R};
const char*   kRingName[3] = {"NEAR", "MID", "FAR"};

float g_ringLevel[3] = {0, 0, 0};  // brightness, decays after a detection
float g_sweepAngle   = 0.0f;

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

constexpr Rect kChanBtn = {0, FOOT_TOP, 96, SCREEN_H - FOOT_TOP};
constexpr Rect kViewBtn = {110, FOOT_TOP, 100, SCREEN_H - FOOT_TOP};
constexpr Rect kCalBtn  = {SCREEN_W - 106, FOOT_TOP, 106, SCREEN_H - FOOT_TOP};

// Picker screen. The active-mode banner sits above the channel list.
constexpr int  BANNER_TOP = 21;
constexpr int  BANNER_H   = 14;
constexpr int  PICK_TOP   = 49;
constexpr int  PICK_ROW_H = 13;
constexpr Rect kBannerBtn = {0, BANNER_TOP, SCREEN_W - 62, BANNER_H};
constexpr Rect kSetupBtn  = {SCREEN_W - 62, BANNER_TOP, 62, BANNER_H};
constexpr Rect kRescanBtn = {SCREEN_W - 96, FOOT_TOP, 96, SCREEN_H - FOOT_TOP};
constexpr Rect kProbeBtn  = {0, FOOT_TOP, 150, SCREEN_H - FOOT_TOP};

uint16_t dim(uint16_t c, float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    const int r = static_cast<int>(((c >> 11) & 0x1F) * f);
    const int g = static_cast<int>(((c >> 5) & 0x3F) * f);
    const int b = static_cast<int>((c & 0x1F) * f);
    return (r << 11) | (g << 5) | b;
}

// Which ring a disturbance of this strength lights up. A louder perturbation
// means the body is coupling more strongly into the link -- which usually
// means nearer the transmitter-receiver path, but is also affected by how much
// of the body is moving. Hence bands, not metres.
int bandFor(float score) {
    if (score >= kMotionScore)   return 0;  // NEAR
    if (score >= kPresenceScore) return 1;  // MID
    if (score >= kFaintScore)    return 2;  // FAR
    return -1;
}

const char* stateLabel(const csi_capture::Stats& s) {
    if (!s.locked)      return "SEARCHING";
    if (!s.warm)        return "WARMING UP";
    if (s.calibrating)  return "CALIBRATING";
    if (!s.calibrated)  return "NEEDS CAL";
    if (s.score >= kMotionScore)   return "MOTION";
    if (s.score >= kPresenceScore) return "DISTURBANCE";
    if (s.score >= kFaintScore)    return "FAINT";
    return "CLEAR";
}

uint16_t stateColor(const csi_capture::Stats& s) {
    if (!s.locked || !s.warm || !s.calibrated) return COL_DIM;
    if (s.calibrating)              return COL_NEAR;
    if (s.score >= kMotionScore)    return COL_MOVE;
    if (s.score >= kPresenceScore)  return COL_NEAR;
    return COL_CLEAR;
}

// ---- channel picker -------------------------------------------------------

// Colour-codes the one number that decides whether the radar will work.
uint16_t hzColor(float hz) {
    if (hz >= 20.0f) return COL_CLEAR;
    if (hz >= 5.0f)  return COL_NEAR;
    return COL_MOVE;
}

void drawPicker() {
    canvas.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HDR);
    canvas.setTextSize(1);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COL_TEXT);
    canvas.drawString("SELECT CHANNEL", 6, HEADER_H / 2 - 1);

    const auto phase = channel_survey::phase();
    canvas.setTextDatum(middle_right);
    canvas.setTextColor(COL_DIM);
    if (phase == channel_survey::Phase::Scanning) {
        canvas.drawString("scanning for APs...", SCREEN_W - 6, HEADER_H / 2 - 1);
    } else if (phase == channel_survey::Phase::Probing) {
        char buf[32];
        snprintf(buf, sizeof(buf), "measuring CSI  %.0f%%",
                 channel_survey::progress() * 100.0f);
        canvas.drawString(buf, SCREEN_W - 6, HEADER_H / 2 - 1);
    } else {
        canvas.drawString("tap a channel", SCREEN_W - 6, HEADER_H / 2 - 1);
    }

    // Active mode banner. Offered first because on most home networks it is the
    // only option that produces a usable frame rate at all.
    const auto linkState = active_link::state();
    const bool busy      = linkState == active_link::State::Connecting;
    const uint16_t col   = linkState == active_link::State::Failed ? COL_MOVE
                           : busy                                  ? COL_NEAR
                           : active_link::configured()             ? COL_CLEAR
                                                                   : COL_DIM;
    canvas.drawRect(6, BANNER_TOP, SCREEN_W - 12, BANNER_H, col);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(col);

    char banner[64];
    if (busy) {
        snprintf(banner, sizeof(banner), "joining %s ...", active_link::ssid());
    } else if (linkState == active_link::State::Failed) {
        snprintf(banner, sizeof(banner), "join failed - tap to retry");
    } else if (active_link::configured()) {
        snprintf(banner, sizeof(banner), "[ JOIN %s ]  ~100Hz", active_link::ssid());
    } else {
        snprintf(banner, sizeof(banner), "no network set - tap SET UP");
    }
    canvas.drawString(banner, 12, BANNER_TOP + BANNER_H / 2);

    canvas.setTextDatum(middle_right);
    canvas.setTextColor(COL_TRACE);
    canvas.drawString(active_link::configured() ? "SET UP >" : "SET UP >",
                      SCREEN_W - 10, BANNER_TOP + BANNER_H / 2);

    // Column headings. "Hz" gets called out because it is the column that
    // matters and the one nobody expects to differ from AP count.
    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_DIM);
    canvas.drawString("CH", 9, PICK_TOP - 12);
    canvas.drawString("APs", 46, PICK_TOP - 12);
    canvas.setTextColor(COL_TRACE);
    canvas.drawString("CSI Hz", 96, PICK_TOP - 12);
    canvas.setTextColor(COL_DIM);
    canvas.drawString("STRONGEST AP", 168, PICK_TOP - 12);

    const int n = channel_survey::rowCount();
    if (n == 0) {
        canvas.setTextColor(COL_DIM);
        canvas.drawString(phase == channel_survey::Phase::Done
                              ? "no networks found - try RESCAN"
                              : "surveying...",
                          9, PICK_TOP + 4);
    }

    for (int i = 0; i < n; ++i) {
        const auto& r = channel_survey::row(i);
        const int   y = PICK_TOP + i * PICK_ROW_H;

        canvas.setTextDatum(top_left);
        char buf[24];

        snprintf(buf, sizeof(buf), "%u", r.channel);
        canvas.setTextColor(COL_TEXT);
        canvas.drawString(buf, 9, y);

        snprintf(buf, sizeof(buf), "%u", r.apCount);
        canvas.setTextColor(r.apCount ? COL_TEXT : COL_GRID);
        canvas.drawString(buf, 46, y);

        if (r.probed) {
            snprintf(buf, sizeof(buf), "%.0f", r.csiHz);
            canvas.setTextColor(hzColor(r.csiHz));
        } else {
            snprintf(buf, sizeof(buf), "--");
            canvas.setTextColor(COL_GRID);
        }
        canvas.drawString(buf, 96, y);

        canvas.setTextColor(COL_DIM);
        canvas.drawString(r.bestSsid, 168, y);

        if (r.apCount) {
            snprintf(buf, sizeof(buf), "%d", r.bestRssi);
            canvas.setTextDatum(top_right);
            canvas.setTextColor(COL_DIM);
            canvas.drawString(buf, SCREEN_W - 8, y);
        }
    }

    canvas.drawFastHLine(0, FOOT_TOP - 2, SCREEN_W, COL_GRID);
    canvas.setTextSize(1);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(illuminator::enabled() ? COL_CLEAR : COL_DIM);
    canvas.drawString(illuminator::enabled() ? "[ PROBE: ON ]" : "[ PROBE: OFF ]", 8,
                      FOOT_TOP + 10);

    canvas.setTextDatum(middle_center);
    if (illuminator::enabled()) {
        // tx counts settle the question the CSI Hz column cannot: if tx climbs
        // and err stays 0, the probes are going out and the AP's replies simply
        // carry no CSI.
        char buf[40];
        snprintf(buf, sizeof(buf), "tx %lu  err %lu", illuminator::sent(),
                 illuminator::failed());
        canvas.setTextColor(illuminator::failed() ? COL_MOVE : COL_GRID);
        canvas.drawString(buf, SCREEN_W / 2 + 10, FOOT_TOP + 10);
    } else {
        canvas.setTextColor(COL_GRID);
        canvas.drawString("green = usable", SCREEN_W / 2 + 10, FOOT_TOP + 10);
    }

    canvas.setTextDatum(middle_right);
    canvas.setTextColor(COL_TRACE);
    canvas.drawString("[ RESCAN ]", SCREEN_W - 8, FOOT_TOP + 10);
}

// ---- chrome ---------------------------------------------------------------

void drawHeader() {
    canvas.fillRect(0, 0, SCREEN_W, HEADER_H, COL_HDR);
    canvas.setTextSize(1);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COL_TEXT);
    canvas.drawString("CSI RADAR", 6, HEADER_H / 2 - 1);
    if (active_link::state() == active_link::State::Connected) {
        canvas.setTextColor(COL_CLEAR);
        canvas.drawString("+LINK", 62, HEADER_H / 2 - 1);
    } else if (illuminator::enabled()) {
        canvas.setTextColor(COL_CLEAR);
        canvas.drawString("+PROBE", 62, HEADER_H / 2 - 1);
    }

    // locked/total frame rate. If the two are close the channel itself is
    // quiet; if total is much higher we locked onto the wrong transmitter.
    char buf[56];
    snprintf(buf, sizeof(buf), "ch%u  %.0f/%.0f Hz  %ddBm", g_stats.channel,
             g_stats.packetRate, g_stats.totalRate, g_stats.rssi);
    canvas.setTextDatum(middle_right);
    // A starved packet rate invalidates everything below, so flag it here.
    canvas.setTextColor(g_stats.packetRate < 5.0f ? COL_MOVE : COL_DIM);
    canvas.drawString(buf, SCREEN_W - 6, HEADER_H / 2 - 1);
}

void drawFooter() {
    canvas.drawFastHLine(0, FOOT_TOP - 2, SCREEN_W, COL_GRID);
    canvas.setTextSize(1);

    char buf[24];
    snprintf(buf, sizeof(buf), "[ CH %u ]", g_stats.channel);
    canvas.setTextDatum(middle_left);
    canvas.setTextColor(COL_TRACE);
    canvas.drawString(buf, 8, FOOT_TOP + 10);

    canvas.setTextDatum(middle_center);
    canvas.setTextColor(COL_TRACE);
    canvas.drawString(g_view == View::Radar ? "[ GRAPH ]" : "[ RADAR ]",
                      kViewBtn.x + kViewBtn.w / 2, FOOT_TOP + 10);

    canvas.setTextDatum(middle_right);
    canvas.setTextColor(g_stats.calibrating ? COL_NEAR : COL_TRACE);
    canvas.drawString(g_stats.calibrating ? "[ WAIT... ]" : "[ CALIBRATE ]",
                      SCREEN_W - 8, FOOT_TOP + 10);
}

// ---- radar view -----------------------------------------------------------

void updateRings() {
    // Decay first, then re-arm whichever band is currently active. Gives a blip
    // that lingers for about a second instead of strobing at the frame rate.
    for (float& level : g_ringLevel) level *= 0.88f;

    if (g_stats.locked && g_stats.calibrated && !g_stats.calibrating) {
        const int band = bandFor(g_stats.score);
        if (band >= 0) g_ringLevel[band] = 1.0f;
    }

    g_sweepAngle += 12.0f;
    if (g_sweepAngle >= 360.0f) g_sweepAngle -= 360.0f;
}

void drawRadar() {
    // Sweep trail. Purely an idiom -- the receiver is not steering a beam, and
    // the wedge angle carries no information about where anything is.
    canvas.fillArc(RADAR_CX, RADAR_CY, 0, RADAR_R, g_sweepAngle - 42.0f,
                   g_sweepAngle, dim(COL_CLEAR, 0.10f));
    canvas.fillArc(RADAR_CX, RADAR_CY, 0, RADAR_R, g_sweepAngle - 16.0f,
                   g_sweepAngle, dim(COL_CLEAR, 0.20f));

    const float rad = g_sweepAngle * DEG_TO_RAD;
    canvas.drawLine(RADAR_CX, RADAR_CY, RADAR_CX + cosf(rad) * RADAR_R,
                    RADAR_CY + sinf(rad) * RADAR_R, dim(COL_CLEAR, 0.55f));

    // Rings, brightest where a disturbance was last detected.
    for (int i = 0; i < 3; ++i) {
        const float    level = g_ringLevel[i];
        const uint16_t col   = level > 0.05f
                                   ? dim(stateColor(g_stats), 0.35f + 0.65f * level)
                                   : COL_GRID;
        canvas.drawCircle(RADAR_CX, RADAR_CY, kRingR[i], col);
        if (level > 0.4f) {
            canvas.drawCircle(RADAR_CX, RADAR_CY, kRingR[i] - 1, col);
            canvas.drawCircle(RADAR_CX, RADAR_CY, kRingR[i] + 1, col);
        }
    }

    canvas.drawPixel(RADAR_CX, RADAR_CY, COL_CLEAR);
    canvas.fillCircle(RADAR_CX, RADAR_CY, 2, dim(COL_CLEAR, 0.8f));

    // Ring labels, tucked along the upper-left diagonal where the sweep is
    // least likely to sit behind them.
    canvas.setTextSize(1);
    canvas.setTextDatum(middle_center);
    for (int i = 0; i < 3; ++i) {
        canvas.setTextColor(g_ringLevel[i] > 0.05f ? COL_TEXT : COL_GRID);
        canvas.drawString(kRingName[i], RADAR_CX, RADAR_CY - kRingR[i] + 5);
    }

    // ---- readout panel ------------------------------------------------------
    constexpr int PX = 148;

    canvas.setTextDatum(top_left);
    canvas.setTextSize(1);
    canvas.setTextColor(stateColor(g_stats));
    canvas.drawString(stateLabel(g_stats), PX, BODY_TOP + 6);

    char buf[32];

    if (g_stats.calibrated) {
        snprintf(buf, sizeof(buf), "%.2fx", g_stats.score);
        canvas.setTextSize(2);
        canvas.setTextColor(COL_TEXT);
        canvas.drawString(buf, PX, BODY_TOP + 20);
        canvas.setTextSize(1);

        const int band = bandFor(g_stats.score);
        canvas.setTextColor(COL_DIM);
        canvas.drawString(band >= 0 ? kRingName[band] : "nothing in path", PX,
                          BODY_TOP + 44);
    } else if (g_stats.locked && !g_stats.warm) {
        snprintf(buf, sizeof(buf), "%.0f%%", g_stats.warmup * 100.0f);
        canvas.setTextSize(2);
        canvas.setTextColor(COL_DIM);
        canvas.drawString(buf, PX, BODY_TOP + 20);
        canvas.setTextSize(1);
        canvas.setTextColor(COL_DIM);
        snprintf(buf, sizeof(buf), "%lu frames", g_stats.frames);
        canvas.drawString(buf, PX, BODY_TOP + 44);
    } else if (g_stats.calibrating) {
        canvas.setTextColor(COL_NEAR);
        canvas.drawString("stand clear", PX, BODY_TOP + 24);
    } else if (!g_stats.locked) {
        // Never leave SEARCHING unexplained: a dead channel and a busy one we
        // have not settled on yet look identical otherwise.
        canvas.setTextColor(g_stats.totalRate < 1.0f ? COL_MOVE : COL_DIM);
        if (g_stats.totalRate < 1.0f) {
            canvas.drawString("no CSI on ch", PX, BODY_TOP + 22);
            canvas.drawString("tap CH, pick", PX, BODY_TOP + 32);
            canvas.drawString("a green one", PX, BODY_TOP + 42);
        } else {
            snprintf(buf, sizeof(buf), "%.0f Hz on air", g_stats.totalRate);
            canvas.drawString(buf, PX, BODY_TOP + 24);
            canvas.drawString("choosing source", PX, BODY_TOP + 34);
        }
    }

    // The raw figure, always. It moves as soon as CSI is flowing, so it is the
    // one number that tells you the pipeline works before calibration makes the
    // score meaningful.
    snprintf(buf, sizeof(buf), "raw %.4f", g_stats.turbulence);
    canvas.setTextColor(COL_DIM);
    canvas.drawString(buf, PX, BODY_TOP + 54);

    // The caveats belong on the screen, not just in the docs -- this is the
    // display most likely to be over-read.
    canvas.setTextColor(COL_GRID);
    canvas.drawString("bands = signal strength,", PX, BODY_TOP + 66);
    canvas.drawString("not metres", PX, BODY_TOP + 76);
    canvas.drawString("no bearing: 1 antenna", PX, BODY_TOP + 90);

    canvas.setTextColor(COL_DIM);
    canvas.drawString(g_stats.sourceMac, PX, BODY_TOP + 108);
}

// ---- graph view -----------------------------------------------------------

void drawGraph() {
    constexpr int STATE_TOP = 22;
    constexpr int STATE_H   = 24;
    constexpr int GRAPH_TOP = 50;
    constexpr int GRAPH_H   = 58;
    constexpr int BARS_TOP  = 118;
    constexpr int BARS_H    = 22;

    const uint16_t col = stateColor(g_stats);
    canvas.drawRect(6, STATE_TOP, SCREEN_W - 12, STATE_H, col);
    canvas.setTextDatum(middle_left);
    canvas.setTextSize(2);
    canvas.setTextColor(col);
    canvas.drawString(stateLabel(g_stats), 12, STATE_TOP + STATE_H / 2);

    canvas.setTextSize(1);
    canvas.setTextDatum(middle_right);
    char buf[48];
    if (g_stats.calibrated) {
        snprintf(buf, sizeof(buf), "%.2fx  raw %.4f  base %.4f", g_stats.score,
                 g_stats.turbulence, g_stats.baseline);
    } else if (g_stats.locked && !g_stats.warm) {
        snprintf(buf, sizeof(buf), "warmup %.0f%%  raw %.4f", g_stats.warmup * 100.0f,
                 g_stats.turbulence);
    } else {
        snprintf(buf, sizeof(buf), "raw %.4f", g_stats.turbulence);
    }
    canvas.setTextColor(g_stats.calibrated ? COL_TEXT : COL_DIM);
    canvas.drawString(buf, SCREEN_W - 12, STATE_TOP + STATE_H / 2);

    canvas.drawRect(6, GRAPH_TOP, SCREEN_W - 12, GRAPH_H, COL_GRID);
    const int left   = 7;
    const int width  = SCREEN_W - 14;
    const int bottom = GRAPH_TOP + GRAPH_H - 1;

    auto yFor = [&](float score) {
        float f = score / kGraphMax;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        return bottom - static_cast<int>(f * (GRAPH_H - 2));
    };

    canvas.drawFastHLine(left, yFor(kPresenceScore), width, COL_GRID);
    canvas.drawFastHLine(left, yFor(kMotionScore), width, COL_GRID);
    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_GRID);
    canvas.drawString("motion", left + 2, yFor(kMotionScore) + 1);

    if (g_historyCount >= 2) {
        const int n     = min(g_historyCount, width);
        const int xBase = left + width - n;
        for (int i = 1; i < n; ++i) {
            const float a = g_history[g_historyCount - n + i - 1];
            const float b = g_history[g_historyCount - n + i];
            canvas.drawLine(xBase + i - 1, yFor(a), xBase + i, yFor(b), COL_TRACE);
        }
    } else {
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(COL_DIM);
        canvas.drawString(g_stats.calibrated ? "collecting..." : "not calibrated",
                          SCREEN_W / 2, GRAPH_TOP + GRAPH_H / 2);
    }

    canvas.setTextDatum(top_left);
    canvas.setTextColor(COL_DIM);
    canvas.drawString("SUBCARRIERS", 6, BARS_TOP - 10);

    // Frame geometry, plus how many frames from the locked AP were dropped for
    // having a different one. A large drop count means the AP is mixing HT and
    // non-HT frames and we are only using a fraction of them.
    canvas.setTextDatum(top_right);
    canvas.setTextColor(COL_GRID);
    snprintf(buf, sizeof(buf), "%u sc / %luB   dropped %lu", g_stats.subcarriers,
             static_cast<unsigned long>(g_stats.frameLen), g_stats.wrongShape);
    canvas.drawString(buf, SCREEN_W - 6, BARS_TOP - 10);

    const int n = g_stats.subcarriers;
    if (n > 0) {
        const int barW = max(1, (SCREEN_W - 12) / n);
        for (int i = 0; i < n; ++i) {
            const int h = (g_stats.ampBar[i] * BARS_H) / 255;
            if (h <= 0) continue;  // guard bands and the DC null
            canvas.fillRect(6 + i * barW, BARS_TOP + BARS_H - h,
                            max(1, barW - 1), h, COL_TRACE);
        }
    }
}

void render() {
    if (g_view == View::WifiSetup) {
        wifi_setup::draw(canvas);  // owns its own background fill
        canvas.pushSprite(0, 0);
        return;
    }
    canvas.fillSprite(COL_BG);
    if (g_view == View::Picker) {
        drawPicker();
    } else {
        drawHeader();
        if (g_view == View::Radar) drawRadar(); else drawGraph();
        drawFooter();
    }
    canvas.pushSprite(0, 0);
}

void handlePickerTap(int x, int y) {
    if (kSetupBtn.contains(x, y)) {
        wifi_setup::start();
        g_view = View::WifiSetup;
        return;
    }
    if (kBannerBtn.contains(x, y) && active_link::configured()) {
        illuminator::setEnabled(false);  // the ping stream replaces it
        active_link::connect();
        return;
    }
    if (kRescanBtn.contains(x, y)) {
        channel_survey::start();
        return;
    }
    if (kProbeBtn.contains(x, y)) {
        // Toggling re-surveys, because the measured Hz means something
        // different with the illuminator on than with it off.
        illuminator::setEnabled(!illuminator::enabled());
        channel_survey::start();
        return;
    }
    if (channel_survey::phase() != channel_survey::Phase::Done) return;
    if (y < PICK_TOP) return;

    const int idx = (y - PICK_TOP) / PICK_ROW_H;
    if (idx < 0 || idx >= channel_survey::rowCount()) return;

    const auto& r = channel_survey::row(idx);
    g_channel = r.channel;
    if (r.apCount > 0) illuminator::setTarget(r.bestBssid);
    csi_capture::resume(g_channel);  // survey left the radio parked elsewhere
    g_view = View::Radar;
}

void handleTap(int x, int y) {
    if (g_view == View::WifiSetup) {
        wifi_setup::handleTap(x, y);
        return;
    }
    if (g_view == View::Picker) {
        handlePickerTap(x, y);
        return;
    }
    if (kCalBtn.contains(x, y)) {
        csi_capture::calibrate();
        return;
    }
    if (kViewBtn.contains(x, y)) {
        g_view = (g_view == View::Radar) ? View::Graph : View::Radar;
        return;
    }
    if (kChanBtn.contains(x, y)) {
        // Back to the survey rather than blind-cycling 1/6/11 -- picking a
        // channel you cannot see the yield of is how you end up stuck on
        // SEARCHING with no idea why.
        channel_survey::start();
        g_view = View::Picker;
        return;
    }
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
    canvas.drawString("starting CSI capture...", SCREEN_W / 2, SCREEN_H / 2);
    canvas.pushSprite(0, 0);

    csi_capture::begin(g_channel);
    active_link::begin();
    illuminator::begin();
    // On by default: passive ambient CSI is a few Hz at best, because the radio
    // only reports frames addressed to us or broadcast.
    illuminator::setEnabled(true);
    channel_survey::start();
}

void loop() {
    active_link::pollSerialProvisioning();

    if (g_view == View::WifiSetup) {
        wifi_setup::poll();

        const auto st = wifi_setup::stage();
        if (st == wifi_setup::Stage::Done) {
            // Straight into NVS, then wipe the RAM copy.
            active_link::provision(wifi_setup::chosenSsid(),
                                   wifi_setup::enteredPassword());
            wifi_setup::clear();
            illuminator::setEnabled(false);
            active_link::connect();
            g_view = View::Picker;
        } else if (st == wifi_setup::Stage::Cancelled) {
            wifi_setup::clear();
            channel_survey::start();
            g_view = View::Picker;
        }
    } else if (g_view == View::Picker) {
        active_link::poll();
        // Association hands us a known transmitter on a known channel, so the
        // survey becomes irrelevant the moment it succeeds.
        if (active_link::state() == active_link::State::Connected) {
            g_channel = active_link::channel();
            csi_capture::useAssociatedLink(active_link::bssid(), g_channel);
            g_view = View::Radar;
        } else {
            channel_survey::poll();
        }
    } else {
        csi_capture::poll();
        illuminator::poll();
        active_link::poll();
    }

    int32_t tx, ty;
    const bool touching = lcd.getTouch(&tx, &ty);
    app_switch::feedTouchHold(touching);

    static bool     wasTouching = false;
    static int32_t  pressX = 0, pressY = 0;
    static uint32_t pressAt = 0;

    if (touching && !wasTouching) {
        pressX  = tx;
        pressY  = ty;
        pressAt = millis();
    } else if (!touching && wasTouching) {
        if (millis() - pressAt < 600) handleTap(pressX, pressY);
    }
    wasTouching = touching;

    static uint32_t lastDraw = 0;
    if (millis() - lastDraw >= 80) {
        lastDraw       = millis();
        csi_capture::stats(g_stats);
        g_historyCount = csi_capture::history(g_history, csi_capture::kHistoryLen);
        updateRings();
        render();
    }

    delay(5);
}
