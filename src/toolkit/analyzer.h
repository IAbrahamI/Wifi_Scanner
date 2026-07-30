#pragma once

#include "tool.h"

// 2.4 GHz channel analyzer: which channels are busy and which to put your
// router on. Passive repeated scans, no transmission.
namespace toolkit { namespace analyzer {
const Tool& tool();
}}  // namespace toolkit::analyzer
