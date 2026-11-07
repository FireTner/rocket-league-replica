// Models.h — procedural car and ball.
#pragma once
#include "render/Renderer.h"

namespace rl {

// Hitbox extents, uu. The ball radius is the collision radius.
namespace dims {
    constexpr float kCarL = 120.5f;  // x length
    constexpr float kCarW = 86.7f;   // y width
    constexpr float kCarH = 38.7f;   // z height
    constexpr float kBallR = 91.25f;
}

// Draw stylized Octane-ish car: body box + cabin box + 4 procedural wheels
// (front pair steers, all four spin) + team-color trim. Orientation comes
// from the Snapshot's carForward/carUp, so pitch/roll are visible; wheel
// steer/spin are derived from carVel + tick only (no sim coupling).
void DrawCar(Renderer& r, const Snapshot& s, bool blueTeam = true);

// Procedural front-steer / four-wheel spin for one car slot (0 blue, 1 orange).
// Integrated from Snapshot velocity and tick only. The Fennec hubs use the
// same angles. Calling this twice in one tick for the same slot is stable:
// the second call sees lastTick == s.tick and adds no step.
struct WheelMotion { float steer = 0; float spin = 0; };
WheelMotion UpdateWheelMotion(const Snapshot& s, int slot);

// W6 instrument (--debug-colliders): oriented car hitbox wireframes for both
// cars + the ball collision sphere (r = 91.25, i.e. NOT the 92.75 visual).
void DrawDebugColliders(Renderer& r, const Snapshot& player, const Snapshot& bot);

// Ball: truncated icosahedron (white hexagons, black pentagons) on a
// procedural sphere, visual radius 92.75 (collision 91.25). The print is
// object-space. Drawn in body space, then oriented by Snapshot::ballRot
// (physics spin integrated in sim time). Defined in Models.cpp.
void DrawBall(Renderer& r, const Snapshot& s);

} // namespace rl
