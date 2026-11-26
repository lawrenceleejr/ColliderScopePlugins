// cscore: shared core of the ColliderScope plugin suite.
// Path engine, oscillator, gate, generators, hits, YIN, serializer and scope
// view arrive in Phase 1 onward; Phase 0 carries only what the skeleton needs.
#pragma once

#include "cscore/pitch.h"

namespace cs {

// Suite version, e.g. "0.1.0".
const char* version() noexcept;

// XY audio rule: peak <= 0.95 per channel.
inline constexpr double kPeak = 0.95;

} // namespace cs
