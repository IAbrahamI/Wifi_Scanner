#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Active illumination without joining the network.
//
//  The ESP32 does not hand up CSI for unicast frames addressed to other
//  stations, which is why a neighbour's 2 GB download produces nothing: every
//  one of those frames is addressed to their laptop. Only frames addressed to
//  *us* (or broadcast) generate CSI, and broadcast traffic is a few Hz at best.
//
//  So we make the AP talk to us. A probe request is the one frame an
//  unassociated station may legitimately send -- it is what every phone emits
//  while scanning -- and the AP answers with a probe response addressed to the
//  requester. Send them at a steady rate and the reply stream becomes the
//  illumination the radar needs.
//
//  The supported-rates element deliberately advertises OFDM rates only, with no
//  802.11b rates at all. An AP must reply at a rate the station claims to
//  support, so declaring ourselves an OFDM-only station forces the response out
//  of 1 Mbit/s DSSS -- which carries no OFDM training fields and therefore no
//  CSI. That is the failure that makes beacons useless here.
//
//  Etiquette: this transmits. Point it at your own access point. The rate is
//  deliberately modest -- comparable to a phone scanning -- and it neither
//  associates nor disrupts anything.
// ---------------------------------------------------------------------------
namespace illuminator {

// Probe requests per second. Enough to drive the radar, low enough to be
// unremarkable traffic on the channel.
constexpr uint32_t kRateHz = 30;

void begin();

// The access point to elicit responses from. Also used as the destination, so
// only that AP answers rather than every AP on the channel.
void setTarget(const uint8_t bssid[6]);

void setEnabled(bool on);
bool enabled();
bool hasTarget();

// Emits a probe request when one is due. Call from loop(); never blocks.
void poll();

uint32_t sent();

// Frames the driver refused. Separating this from `sent` is what distinguishes
// "we never transmitted" from "we transmitted and the AP's reply carried no
// CSI" -- two very different problems with identical symptoms on screen.
uint32_t failed();

}  // namespace illuminator
