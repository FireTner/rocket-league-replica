// Flip-reset disc. Shown only while Snapshot.flipReset is set, which is
// RocketSim CarState::HasFlipReset (airborne flip granted by the wheels).
// Procedural fixed-function. Disc radius and colour are UNVERIFIED: no
// game function sizes this marker.
#pragma once
#include "render/Renderer.h"

namespace rl {

void DrawFlipReset(Renderer& r, const Snapshot& s);

} // namespace rl
