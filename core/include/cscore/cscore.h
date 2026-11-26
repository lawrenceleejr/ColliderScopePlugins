// cscore: shared core of the ColliderScope plugin suite.
// Phase 1: path engine, oscillator, gate, track generators and ageing, hits,
// the CS Chamber voice and the scope view. YIN and the serializer arrive
// with CS Mux in Phase 3.
#pragma once

#include "cscore/chamber_engine.h"
#include "cscore/envelope.h"
#include "cscore/hits.h"
#include "cscore/lockfree.h"
#include "cscore/oscillator.h"
#include "cscore/path.h"
#include "cscore/pitch.h"
#include "cscore/rng.h"
#include "cscore/scope_view.h"
#include "cscore/tracks.h"

namespace cs {

// Suite version, e.g. "0.1.0".
const char* version() noexcept;

// XY audio rule: peak <= 0.95 per channel.
inline constexpr double kPeak = 0.95;

} // namespace cs
