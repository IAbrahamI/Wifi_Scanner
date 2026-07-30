#pragma once

#include "tool.h"

// Deauthentication-attack detector. A flood of 802.11 deauth/disassoc
// management frames is the signature of a jam or a forced-disconnect attack.
// Purely a receiver -- it counts frames and never transmits.
namespace toolkit { namespace deauth {
const Tool& tool();
}}  // namespace toolkit::deauth
