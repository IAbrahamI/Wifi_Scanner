#pragma once

#include "tool.h"

// BLE "hot/cold" finder. Pick an advertiser, then walk it down with a
// full-screen signal-strength meter. Passive scanning only.
namespace toolkit { namespace spotlight {
const Tool& tool();
}}  // namespace toolkit::spotlight
