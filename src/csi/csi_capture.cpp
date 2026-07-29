#include "csi_capture.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>
#include <string.h>

namespace csi_capture {
namespace {

// How fast the "steady state" estimate forgets, once it has settled. Slow
// enough that a person standing still does not get absorbed into the background
// within a few seconds, fast enough to track furniture moving or the AP
// drifting.
//
// Reaching that from a standing start takes ~1/kSlowAlpha frames, which at
// beacon rates is over ten seconds. During it the estimate is nowhere near the
// truth and `turbulence` reads far too high, so the first kWarmupFrames are
// treated as a warmup: alpha starts at 1 (a plain running mean) and tightens to
// kSlowAlpha, and calibration is refused until it is done.
constexpr float    kSlowAlpha    = 0.01f;

// 64 frames of a plain running mean is already a serviceable estimate; 150 was
// needlessly conservative and, on a quiet link, meant minutes of waiting.
constexpr uint32_t kWarmupFrames    = 64;

// ...and if the link is quiet enough that even that takes forever, settle for
// what we have rather than never becoming usable. Still needs a floor of real
// frames -- calibrating against three packets is worse than not calibrating.
constexpr uint32_t kWarmupTimeoutMs = 45000;
constexpr uint32_t kMinWarmupFrames = 20;

// A locked transmitter this quiet cannot drive the sensor. Give it a fair
// window to prove itself, then go and find a louder one.
constexpr float    kMinLockRate  = 2.0f;
constexpr uint32_t kLockAuditMs  = 12000;
constexpr uint8_t  kMaxRelocks   = 3;

// Smoothing on the output. Motion events last hundreds of ms; this trades a
// little latency for a signal that is not pure hash.
constexpr float kOutAlpha = 0.20f;

// A subcarrier below this mean amplitude is a guard band or the DC null. They
// carry no energy, so their "deviation" is division by noise.
constexpr float kActiveFloor = 2.0f;

constexpr uint32_t kLockWindowMs = 4000;
constexpr uint32_t kCalibrateMs  = 4000;
constexpr uint32_t kHistoryStepMs = 100;

// ---- written only by the Wi-Fi task, in the CSI callback -------------------
float             g_slowMean[kMaxSubcarriers];
float             g_turbulence      = 0.0f;
int               g_subcarriers     = 0;
volatile uint32_t g_framesSinceLock = 0;
volatile uint32_t g_totalFrames     = 0;  // every CSI frame, whatever the source

// ---- published to the UI task under g_mux ---------------------------------
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
float    g_pubTurbulence = 0.0f;
uint32_t g_pubPackets    = 0;
int8_t   g_pubRssi       = 0;
int      g_pubSubcarriers = 0;
uint8_t  g_pubAmp[kMaxSubcarriers];

// ---- transmitter selection -------------------------------------------------
struct Candidate {
    uint8_t  mac[6];
    uint16_t frames;
};
constexpr int kMaxCandidates = 12;

Candidate     g_candidates[kMaxCandidates];
int           g_candidateCount = 0;
volatile bool g_locked         = false;
uint8_t       g_lockMac[6]     = {0};
uint32_t      g_lockDeadline   = 0;

// ---- UI-task-owned state ---------------------------------------------------
uint8_t  g_channel     = 6;
float    g_baseline    = 0.0f;
bool     g_calibrated  = false;
bool     g_calibrating = false;
bool     g_calPending  = false;  // requested, waiting for the warmup to finish
bool     g_autoCalDone = false;
uint32_t g_calUntil    = 0;
double   g_calSum      = 0.0;
uint32_t g_calCount    = 0;

uint32_t g_lastRateAt    = 0;
uint32_t g_lastRateBase  = 0;
uint32_t g_lastTotalBase = 0;
float    g_packetRate    = 0.0f;
float    g_totalRate     = 0.0f;
uint32_t g_lockedAt      = 0;
uint8_t  g_relocks       = 0;

float    g_history[kHistoryLen];
int      g_historyCount = 0;
uint32_t g_lastHistoryAt = 0;

void resetLearning() {
    memset(g_slowMean, 0, sizeof(g_slowMean));
    g_turbulence      = 0.0f;
    g_candidateCount  = 0;
    g_locked          = false;
    g_framesSinceLock = 0;
    g_lockDeadline    = millis() + kLockWindowMs;
    g_historyCount    = 0;
    g_calibrated      = false;
    g_calibrating     = false;
    g_calPending      = false;
    g_autoCalDone     = false;
    g_relocks         = 0;
    g_lockedAt        = millis();

    portENTER_CRITICAL(&g_mux);
    g_pubPackets = 0;
    portEXIT_CRITICAL(&g_mux);
    g_lastRateBase = 0;
}

void noteCandidate(const uint8_t* mac) {
    for (int i = 0; i < g_candidateCount; ++i) {
        if (memcmp(g_candidates[i].mac, mac, 6) == 0) {
            g_candidates[i].frames++;
            return;
        }
    }
    if (g_candidateCount < kMaxCandidates) {
        memcpy(g_candidates[g_candidateCount].mac, mac, 6);
        g_candidates[g_candidateCount].frames = 1;
        g_candidateCount++;
    }
}

// Runs in the Wi-Fi task. Everything here is on the critical path of packet
// reception, so it stays to a single pass over 64 subcarriers and one short
// critical section at the end.
void onCsi(void* /*ctx*/, wifi_csi_info_t* info) {
    if (!info || !info->buf || info->len < 4) return;

    // Counted before the lock filter: comparing this against the locked-source
    // rate is what tells you whether the channel is quiet or we simply picked a
    // quiet transmitter on a busy channel.
    g_totalFrames++;

    if (!g_locked) {
        noteCandidate(info->mac);
        return;
    }
    if (memcmp(info->mac, g_lockMac, 6) != 0) return;

    const int8_t* buf = info->buf;
    int n = info->len / 2;
    if (n > kMaxSubcarriers) n = kMaxSubcarriers;

    // The hardware flags the first two subcarriers as garbage on some frames.
    const int start = info->first_word_invalid ? 2 : 0;

    const uint32_t frame = ++g_framesSinceLock;

    // Start as a plain running mean and tighten into the EWMA. Without this the
    // estimate spends its first ~1/kSlowAlpha frames far from the truth, and
    // anything measured against it in that window is meaningless.
    const float alpha = max(kSlowAlpha, 1.0f / frame);

    float sum   = 0.0f;
    int   used  = 0;
    float peak  = 1.0f;
    uint8_t bars[kMaxSubcarriers] = {0};

    for (int i = start; i < n; ++i) {
        // Raw CSI is interleaved int8 pairs, imaginary part first.
        const float im = buf[i * 2];
        const float re = buf[i * 2 + 1];
        const float amp = sqrtf(re * re + im * im);

        float& mean = g_slowMean[i];
        mean = (mean == 0.0f) ? amp : mean + alpha * (amp - mean);

        if (mean > kActiveFloor) {
            sum += fabsf(amp - mean) / mean;
            used++;
        }
        if (amp > peak) peak = amp;
        bars[i] = static_cast<uint8_t>(amp > 255.0f ? 255.0f : amp);
    }

    if (used > 0) {
        const float raw = sum / used;
        g_turbulence += kOutAlpha * (raw - g_turbulence);
    }
    g_subcarriers = n;

    // Rescale the display bars so a weak link still fills the widget.
    const float scale = 255.0f / peak;
    for (int i = 0; i < n; ++i) {
        bars[i] = static_cast<uint8_t>(bars[i] * scale);
    }

    portENTER_CRITICAL(&g_mux);
    g_pubTurbulence  = g_turbulence;
    g_pubSubcarriers = n;
    g_pubRssi        = info->rx_ctrl.rssi;
    g_pubPackets++;
    memcpy(g_pubAmp, bars, n);
    portEXIT_CRITICAL(&g_mux);
}

void pickTransmitter() {
    int best = -1;
    for (int i = 0; i < g_candidateCount; ++i) {
        if (best < 0 || g_candidates[i].frames > g_candidates[best].frames) best = i;
    }
    if (best < 0) {
        // Nothing heard at all -- keep listening rather than locking to noise.
        g_lockDeadline = millis() + kLockWindowMs;
        return;
    }

    memcpy(g_lockMac, g_candidates[best].mac, 6);
    memset(g_slowMean, 0, sizeof(g_slowMean));
    g_framesSinceLock = 0;
    g_lockedAt        = millis();
    g_locked          = true;  // set last: the callback starts work on this
    log_i("csi: locked to %02x:%02x:%02x:%02x:%02x:%02x (%u frames in window)",
          g_lockMac[0], g_lockMac[1], g_lockMac[2], g_lockMac[3], g_lockMac[4],
          g_lockMac[5], g_candidates[best].frames);
}

void pushHistory(float score) {
    if (g_historyCount < kHistoryLen) {
        g_history[g_historyCount++] = score;
    } else {
        memmove(g_history, g_history + 1, (kHistoryLen - 1) * sizeof(float));
        g_history[kHistoryLen - 1] = score;
    }
}

}  // namespace

void begin(uint8_t channel) {
    g_channel = channel;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    delay(100);

    // Promiscuous mode is what makes frames addressed to other stations visible
    // -- without it the radio drops beacons from APs we are not joined to, and
    // there is nothing left to measure.
    wifi_promiscuous_filter_t filter = {};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    wifi_csi_config_t cfg = {};
    cfg.lltf_en           = true;   // the 52-subcarrier legacy long training field
    cfg.htltf_en          = false;  // HT fields only appear on 802.11n frames
    cfg.stbc_htltf2_en    = false;
    cfg.ltf_merge_en      = true;
    cfg.channel_filter_en = true;
    cfg.manu_scale        = false;  // let the hardware pick the scaling
    cfg.shift             = 0;

    esp_err_t err = esp_wifi_set_csi_config(&cfg);
    if (err != ESP_OK) log_e("csi: set_csi_config failed: %s", esp_err_to_name(err));

    err = esp_wifi_set_csi_rx_cb(&onCsi, nullptr);
    if (err != ESP_OK) log_e("csi: set_csi_rx_cb failed: %s", esp_err_to_name(err));

    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) log_e("csi: enable failed: %s", esp_err_to_name(err));

    resetLearning();
    g_lastRateAt = millis();
}

void setChannel(uint8_t channel) {
    g_channel = channel;
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    resetLearning();
}

void useAssociatedLink(const uint8_t bssid[6], uint8_t channel) {
    // Associated STA mode already delivers CSI for frames from the AP, and
    // promiscuous mode would only add other people's frames we cannot use.
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_csi(true);

    g_channel = channel;
    resetLearning();

    // No search phase: the illuminator is the AP we just joined.
    memcpy(g_lockMac, bssid, 6);
    memset(g_slowMean, 0, sizeof(g_slowMean));
    g_framesSinceLock = 0;
    g_lockedAt        = millis();
    g_locked          = true;

    log_i("csi: using associated link %02x:%02x:%02x:%02x:%02x:%02x on ch%u",
          bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
}

void suspend() {
    esp_wifi_set_csi(false);
    esp_wifi_set_promiscuous(false);
}

void resume(uint8_t channel) {
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_csi(true);
    g_channel = channel;
    resetLearning();
}

uint32_t totalFrames() { return g_totalFrames; }

void relock() { resetLearning(); }

void calibrate() {
    // Queued, not started. poll() releases it once the steady-state estimate
    // has converged -- see kWarmupFrames.
    g_calPending  = true;
    g_calibrated  = false;
    g_calibrating = false;
}

void poll() {
    const uint32_t now = millis();

    if (!g_locked && now >= g_lockDeadline) pickTransmitter();

    portENTER_CRITICAL(&g_mux);
    const float    turbulence = g_pubTurbulence;
    const uint32_t packets    = g_pubPackets;
    portEXIT_CRITICAL(&g_mux);

    if (now - g_lastRateAt >= 1000) {
        const uint32_t total = g_totalFrames;
        g_packetRate    = (packets - g_lastRateBase) * 1000.0f / (now - g_lastRateAt);
        g_totalRate     = (total - g_lastTotalBase) * 1000.0f / (now - g_lastRateAt);
        g_lastRateBase  = packets;
        g_lastTotalBase = total;
        g_lastRateAt    = now;
    }

    // A transmitter that has gone quiet since we locked onto it will never
    // finish warmup. Drop it and re-run selection -- but give up after a few
    // tries rather than cycling forever on a genuinely dead channel.
    if (g_locked && !g_calibrated && g_relocks < kMaxRelocks &&
        now - g_lockedAt > kLockAuditMs && g_packetRate < kMinLockRate) {
        log_w("csi: locked source only %.1f Hz, re-selecting", g_packetRate);
        const uint8_t relocks = g_relocks + 1;
        resetLearning();
        g_relocks = relocks;
        return;
    }

    const uint32_t frames = g_framesSinceLock;
    const bool warm =
        g_locked && (frames >= kWarmupFrames ||
                     (now - g_lockedAt > kWarmupTimeoutMs && frames >= kMinWarmupFrames));

    // Calibrating is the step people forget, and forgetting it leaves the app
    // looking broken. Run it automatically the moment it becomes valid to.
    if (warm && !g_autoCalDone && !g_calibrated && !g_calibrating && !g_calPending) {
        g_autoCalDone = true;
        g_calPending  = true;
        log_i("csi: warm after %u frames, auto-calibrating", g_framesSinceLock);
    }

    if (g_calPending && warm) {
        g_calPending  = false;
        g_calibrating = true;
        g_calSum      = 0.0;
        g_calCount    = 0;
        g_calUntil    = now + kCalibrateMs;
    }

    if (g_calibrating) {
        g_calSum += turbulence;
        g_calCount++;
        if (now >= g_calUntil) {
            // Floor the baseline: dividing by a near-zero denominator turns
            // sensor noise into a motion alarm.
            g_baseline    = g_calCount ? max(1e-4, g_calSum / g_calCount) : 1e-4;
            g_calibrated  = true;
            g_calibrating = false;
            log_i("csi: baseline %.5f from %u samples", g_baseline, g_calCount);
        }
    }

    if (now - g_lastHistoryAt >= kHistoryStepMs) {
        g_lastHistoryAt = now;
        pushHistory(g_calibrated ? turbulence / g_baseline : 0.0f);
    }
}

void stats(Stats& out) {
    portENTER_CRITICAL(&g_mux);
    out.turbulence  = g_pubTurbulence;
    out.packets     = g_pubPackets;
    out.rssi        = g_pubRssi;
    out.subcarriers = g_pubSubcarriers;
    memcpy(out.ampBar, g_pubAmp, sizeof(out.ampBar));
    portEXIT_CRITICAL(&g_mux);

    out.baseline    = g_baseline;
    out.calibrated  = g_calibrated;
    out.calibrating = g_calibrating;
    out.calPending  = g_calPending;
    out.score       = g_calibrated ? out.turbulence / g_baseline : 0.0f;
    out.packetRate  = g_packetRate;
    out.totalRate   = g_totalRate;
    out.channel     = g_channel;
    out.locked      = g_locked;
    out.relocks     = g_relocks;

    const uint32_t frames = g_framesSinceLock;
    const uint32_t now    = millis();
    out.frames = frames;
    out.warm   = g_locked && (frames >= kWarmupFrames ||
                              (now - g_lockedAt > kWarmupTimeoutMs &&
                               frames >= kMinWarmupFrames));
    out.warmup = !g_locked ? 0.0f
                           : min(1.0f, static_cast<float>(frames) / kWarmupFrames);

    if (g_locked) {
        snprintf(out.sourceMac, sizeof(out.sourceMac),
                 "%02x:%02x:%02x:%02x:%02x:%02x", g_lockMac[0], g_lockMac[1],
                 g_lockMac[2], g_lockMac[3], g_lockMac[4], g_lockMac[5]);
    } else {
        snprintf(out.sourceMac, sizeof(out.sourceMac), "--");
    }
}

int history(float* out, int max) {
    int n = min(g_historyCount, max);
    memcpy(out, g_history + (g_historyCount - n), n * sizeof(float));
    return n;
}

}  // namespace csi_capture
