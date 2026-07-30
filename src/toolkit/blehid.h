#pragma once

#include "tool.h"

// Turns the gadget into a Bluetooth media/presenter remote: it advertises as a
// BLE HID consumer-control device, pairs with a PC/phone, and the touch buttons
// send play/pause, track, volume and slide commands. This is the one tool that
// transmits and acts as a peripheral rather than a passive receiver.
namespace toolkit { namespace blehid {
const Tool& tool();
}}  // namespace toolkit::blehid
