// Models.h — A4 procedural stylized models, hitbox-proportioned.
// Hitbox dims (from A3): Octane 120.5 x 86.7 x 38.7 uu, ball r=91.25 uu.
// Ownership: A4. No external geometry — 100% procedural boxes/capsules.
#pragma once
#include "render/Renderer.h"

namespace rl {

// Hitbox constants (uu) — must match A3 physics.
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

// W6 instrument (--debug-colliders): oriented car hitbox wireframes for both
// cars + the ball collision sphere (r = 91.25, i.e. NOT the 92.75 visual).
void DrawDebugColliders(Renderer& r, const Snapshot& player, const Snapshot& bot);

// Ball: RL-style white hexagon-panel sphere (procedural in-code texture),
// visual radius 92.75 (collision 91.25). Defined in Models.cpp.
void DrawBall(Renderer& r, const Snapshot& s);

} // namespace rl
