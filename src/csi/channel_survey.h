#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Picks a channel to sense on, by measuring rather than guessing.
//
//  Two numbers per channel, and they are not the same thing:
//
//    APs    how many access points beacon there. What a normal Wi-Fi scanner
//           shows you, and what people reach for.
//    Hz     how many CSI frames per second the radio actually gets there.
//
//  The second is the one that decides whether the radar works, and a busy
//  channel can still score zero. CSI is extracted from the OFDM training fields
//  in a frame, and plenty of 2.4 GHz routers still beacon at 1 Mbit/s using the
//  older DSSS modulation, which has no OFDM fields at all. Those APs are loud
//  and useless here. Only a live measurement separates the two.
// ---------------------------------------------------------------------------
namespace channel_survey {

// Seven 13px rows from y=49 stop clear of the footer at y=146.
constexpr int kMaxRows = 7;

struct Row {
    uint8_t channel;
    uint8_t apCount;
    int8_t  bestRssi;
    char    bestSsid[18];
    uint8_t bestBssid[6];  // who to aim the illuminator at on this channel
    float   csiHz;
    bool    probed;
};

enum class Phase { Idle, Scanning, Probing, Done };

// Starts a full survey: Wi-Fi scan, then a timed CSI dwell on each candidate
// channel. Takes the radio away from csi_capture until it completes.
void start();

// Drives the survey. Call from loop(); never blocks.
void poll();

Phase   phase();
int     rowCount();
const Row& row(int i);

// 0..1 through the current phase, for a progress readout.
float progress();

}  // namespace channel_survey
