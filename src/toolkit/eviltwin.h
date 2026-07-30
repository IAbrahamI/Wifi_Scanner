#pragma once

#include "tool.h"

// Evil-twin / rogue-AP detector. Flags networks broadcasting the same SSID from
// more than one radio, and — the stronger signal — the same SSID advertised
// with mismatched security. Passive scan only.
namespace toolkit { namespace eviltwin {
const Tool& tool();
}}  // namespace toolkit::eviltwin
