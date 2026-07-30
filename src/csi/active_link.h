#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
//  Active illumination by joining the network.
//
//  Everything an *unassociated* station can make an AP send -- beacons, probe
//  responses, auth responses -- is a management frame, and management frames go
//  out at the AP's lowest basic rate. On a typical 2.4 GHz router with 802.11b
//  compatibility left on, that is 1 Mbit/s DSSS: single-carrier, no subcarriers,
//  no CSI. Measured on real hardware: 2176 probe requests transmitted without a
//  single error produced 0 CSI frames.
//
//  Data frames to an *associated* station are different. They use 802.11n OFDM
//  rates, never DSSS, and they are addressed to us -- both conditions the CSI
//  engine requires. So: associate, then ping the router to manufacture a steady
//  stream of replies. ~100 Hz instead of 6, from a transmitter whose identity
//  and position are known.
//
//  The cost is that the gadget now transmits and needs credentials. What it
//  does not cost is the core idea: the *targets* being sensed still need no
//  connection to anything and carry no device.
// ---------------------------------------------------------------------------
namespace active_link {

// Loads credentials from NVS. Call once at startup, before configured().
void begin();

// True when a network has been provisioned.
bool configured();
const char* ssid();

// Stores credentials in NVS and uses them from now on. Passing an empty ssid
// clears them.
//
// NVS rather than a compiled-in constant: a password baked into the firmware
// lives in the .bin, in the build directory, and in every copy of the image
// that gets shared or flashed onto another board. In NVS it stays on the one
// device it was typed into, and changing networks does not mean a rebuild.
bool provision(const char* ssid, const char* password);

// Accepts provisioning commands on the serial console. Call from loop().
//
//   wifi <ssid> <password>   store and connect
//   wifi-clear               forget
//   wifi-status              report without revealing the password
void pollSerialProvisioning();

// Starts an asynchronous association. Watch state() for the outcome.
void connect();

enum class State { Idle, Connecting, Connected, Failed };
State state();

// Only meaningful once Connected.
uint8_t        channel();
const uint8_t* bssid();

// Drives association and keeps the ping stream running. Call from loop().
void poll();

// Echo replies received -- the frames that actually carry the CSI.
uint32_t replies();

// Drops the association and stops pinging.
void stop();

}  // namespace active_link
