#include "channel_survey.h"
#include "csi_capture.h"
#include "illuminator.h"

#include <WiFi.h>

namespace channel_survey {
namespace {

// Long enough for a stable rate estimate, short enough that surveying five
// channels does not feel like a wait.
constexpr uint32_t kDwellMs = 1200;

Phase g_phase = Phase::Idle;

Row g_rows[kMaxRows];
int g_rowCount = 0;
int g_probeIdx = 0;

uint32_t g_dwellStart  = 0;
uint32_t g_dwellFrames = 0;

// Channels always worth offering even with no APs on them: they are the
// non-overlapping trio, so they are where anything that does appear will be.
bool alwaysOffer(uint8_t ch) { return ch == 1 || ch == 6 || ch == 11; }

void buildRowsFromScan(int found) {
    struct Bucket {
        uint8_t count;
        int8_t  bestRssi;
        char    bestSsid[18];
        uint8_t bestBssid[6];
    };
    Bucket buckets[14] = {};
    for (auto& b : buckets) b.bestRssi = -127;

    for (int i = 0; i < found; ++i) {
        const int ch = WiFi.channel(i);
        if (ch < 1 || ch > 13) continue;

        Bucket& b = buckets[ch];
        b.count++;

        const int rssi = WiFi.RSSI(i);
        if (rssi > b.bestRssi) {
            b.bestRssi = rssi;
            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) ssid = "<hidden>";
            strncpy(b.bestSsid, ssid.c_str(), sizeof(b.bestSsid) - 1);
            b.bestSsid[sizeof(b.bestSsid) - 1] = '\0';
            if (const uint8_t* bssid = WiFi.BSSID(i)) memcpy(b.bestBssid, bssid, 6);
        }
    }

    g_rowCount = 0;
    for (uint8_t ch = 1; ch <= 13 && g_rowCount < kMaxRows; ++ch) {
        if (buckets[ch].count == 0 && !alwaysOffer(ch)) continue;

        Row& r = g_rows[g_rowCount++];
        r.channel  = ch;
        r.apCount  = buckets[ch].count;
        r.bestRssi = buckets[ch].count ? buckets[ch].bestRssi : 0;
        r.csiHz    = 0.0f;
        r.probed   = false;
        if (buckets[ch].count) {
            strncpy(r.bestSsid, buckets[ch].bestSsid, sizeof(r.bestSsid));
            memcpy(r.bestBssid, buckets[ch].bestBssid, 6);
        } else {
            strcpy(r.bestSsid, "-");
            memset(r.bestBssid, 0, 6);
        }
    }

    WiFi.scanDelete();
}

void beginDwell() {
    Row& r = g_rows[g_probeIdx];
    csi_capture::resume(r.channel);

    // Measure the channel the way it will actually be used. With the
    // illuminator on, the figure reflects the probe-response stream we can
    // create; with it off, only whatever happens to be on the air.
    if (r.apCount > 0) illuminator::setTarget(r.bestBssid);

    g_dwellFrames = csi_capture::totalFrames();
    g_dwellStart  = millis();
}

}  // namespace

void start() {
    g_phase    = Phase::Scanning;
    g_rowCount = 0;
    g_probeIdx = 0;

    // Scanning hops channels, so the promiscuous parking has to be released
    // first or the scan and the capture fight over the radio.
    csi_capture::suspend();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    WiFi.scanNetworks(true, true, true, 220);
}

void poll() {
    switch (g_phase) {
        case Phase::Idle:
        case Phase::Done:
            return;

        case Phase::Scanning: {
            const int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) return;

            buildRowsFromScan(n < 0 ? 0 : n);
            if (g_rowCount == 0) {
                g_phase = Phase::Done;
                return;
            }
            g_probeIdx = 0;
            g_phase    = Phase::Probing;
            beginDwell();
            return;
        }

        case Phase::Probing: {
            illuminator::poll();

            const uint32_t elapsed = millis() - g_dwellStart;
            if (elapsed < kDwellMs) return;

            Row& r   = g_rows[g_probeIdx];
            r.csiHz  = (csi_capture::totalFrames() - g_dwellFrames) * 1000.0f / elapsed;
            r.probed = true;
            log_i("survey: ch%u  %u APs  %.1f CSI Hz", r.channel, r.apCount, r.csiHz);

            if (++g_probeIdx >= g_rowCount) {
                g_phase = Phase::Done;
                return;
            }
            beginDwell();
            return;
        }
    }
}

Phase phase() { return g_phase; }

int rowCount() { return g_rowCount; }

const Row& row(int i) { return g_rows[i]; }

float progress() {
    if (g_phase == Phase::Probing && g_rowCount > 0) {
        return static_cast<float>(g_probeIdx) / g_rowCount;
    }
    return g_phase == Phase::Done ? 1.0f : 0.0f;
}

}  // namespace channel_survey
