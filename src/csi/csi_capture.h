#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Channel State Information capture.
//
//  Every Wi-Fi frame the radio receives carries a per-subcarrier estimate of
//  what the air did to it on the way in. A body moving through the space
//  between transmitter and receiver perturbs those estimates. Watch them over
//  time and you have a motion sensor that needs nothing on the target.
//
//  Two things about this that the concept doc glosses over, and that shape the
//  whole design here:
//
//  1. CSI is only meaningful *per link*. Each transmitter has its own channel
//     response, so mixing frames from several APs produces noise that looks
//     exactly like motion. We lock onto one source MAC and ignore the rest.
//
//  2. It needs a steady packet rate. Ambient user traffic is far too bursty to
//     difference against. Beacons are not: every AP emits one about 10x a
//     second, forever, whether anyone is connected or not. That is the clock
//     this runs on, and it is why the receiver stays parked on one channel.
// ---------------------------------------------------------------------------
namespace csi_capture {

constexpr int kMaxSubcarriers = 64;
constexpr int kHistoryLen     = 160;  // one pixel column each, for the graph

struct Stats {
    float    turbulence;   // mean per-subcarrier deviation from steady state
    float    baseline;     // what `turbulence` reads in an empty room
    float    score;        // turbulence / baseline. 1.0 = nothing happening
    bool     calibrated;
    bool     calibrating;
    bool     calPending;   // calibration asked for, waiting on warmup
    float    warmup;       // 0..1 progress of the steady-state estimate
    bool     warm;         // true once the estimate has converged
    uint32_t packets;      // CSI frames accepted from the locked source
    float    packetRate;   // Hz from the locked source -- what actually feeds us
    float    totalRate;    // Hz across every source on the channel
    uint32_t frames;       // frames since lock, for warmup progress
    uint8_t  relocks;      // times we gave up on a too-quiet transmitter
    int8_t   rssi;
    uint8_t  channel;
    bool     locked;
    char     sourceMac[18];
    int      subcarriers;
    uint8_t  ampBar[kMaxSubcarriers];  // latest amplitudes, normalised 0..255
};

// Puts the radio into promiscuous mode on `channel` and starts capturing.
void begin(uint8_t channel);

// Re-parks the receiver. Drops the current lock and all learnt state, since a
// different channel means a different set of transmitters.
void setChannel(uint8_t channel);

// Takes CSI from the AP we are associated to instead of sniffing the channel.
// Promiscuous mode goes off, and the transmitter is known up front -- so this
// skips candidate selection entirely and locks immediately.
void useAssociatedLink(const uint8_t bssid[6], uint8_t channel);

// Releases the radio so a normal Wi-Fi scan can run (scanning hops channels,
// which is incompatible with sitting in promiscuous mode on one).
void suspend();

// Retakes the radio on `channel` and starts over.
void resume(uint8_t channel);

// Every CSI frame seen since boot, from any source. Differencing this over a
// dwell is how the channel survey measures what a channel is actually worth --
// AP count does not predict it, because CSI only comes from OFDM frames.
uint32_t totalFrames();

// Forgets the current transmitter and re-runs auto-selection.
void relock();

// Spends the next few seconds learning what "empty room" looks like, and uses
// that as the denominator for `score`. Stand out of the path while it runs.
//
// If the steady-state estimate has not converged yet the request is queued
// rather than run: calibrating against an unsettled estimate bakes in a far too
// large baseline, and every later reading then divides down below 1.0 and never
// trips a threshold again.
void calibrate();

// Housekeeping: packet rate, lock timeout, calibration timer, graph history.
// Call from loop(); never blocks.
void poll();

// Copies the current state out. Safe to call while the Wi-Fi task is writing.
void stats(Stats& out);

// Newest-last turbulence scores for the scrolling graph. Returns the count.
int history(float* out, int max);

}  // namespace csi_capture
