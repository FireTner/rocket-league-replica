// ArenaGeometry.h — analytic Soccar collision + visual mesh (stream A2).
//
// Model: convex octagon footprint (|x|<=4096, |y|<=5120, |x|+|y|<=8064)
// extruded through a height profile (floor -> R=256 quarter-round fillet ->
// vertical wall -> Rc=256 quarter-round -> ceiling at 2048), with a goal
// mouth cut from each back wall (|x|<892.755, z<642.775) opening into a
// drivable net box (|x|<=892.755, 5120<=|y|<=6000, 0<=z<=642.775).
// Goal posts / crossbar are sharp (zero-radius) edges — a documented
// approximation, see docs/arena_deviation.md.
#pragma once

#include <cstdint>
#include <vector>

#include "ArenaConstants.h"

namespace Arena {

// Signed-distance query result. distance < 0 means the point is inside solid
// (penetration depth = -distance); normal points from the surface toward the
// query point (the push-out direction for a penetrating sphere).
struct Contact {
    float distance;
    Vec3 normal;
    // Feature id that produced the contact (0=floor,1=wall/fillet,2=ceiling,
    // 3=goal box, 4=post/crossbar edge). For diagnostics only.
    int feature;
};

// Signed distance from `p` to the nearest solid arena surface.
Contact Collide(const Vec3& p);

// Push a sphere (center, radius) out of all solid surfaces (up to maxIters
// passes over profile + goal-box + post/crossbar features). Returns true and
// sets outNormal (normal of the deepest contact) if the sphere touched solid.
bool CollideSphere(Vec3& center, float radius, Vec3& outNormal, int maxIters = 4);

struct RayHit {
    float t;       // distance along dir (dir must be unit length)
    Vec3 point;    // origin + dir * t
    Vec3 normal;   // surface normal at the hit, opposing the ray
    int feature;   // same ids as Contact
};

// Analytic raycast against planes, fillet cylinders and goal-box faces.
// Returns the nearest hit with t in [0, maxDist], or false.
// NOTE: goal posts/crossbar (zero-radius edges) are NOT raycast targets;
// resolve them with CollideSphere. See ArenaAPI.md.
bool Raycast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& out);

// Goal detection (RocketSim IsBallScored rule:
// third_party/RocketSim/src/Sim/Arena/Arena.cpp:893-900):
// scores when |y| > GOAL_SCORE_BASE_Y + ballRadius while inside the mouth.
bool IsGoal(const Vec3& ballPos, float ballRadius = BALL_RADIUS);

// Footprint helpers (exposed for tests / sim integration).
struct FootprintQuery {
    float w;      // signed distance to footprint edge, + = inside
    float gx, gy; // footprint gradient (outward edge normal of closest edge)
    int edge;     // 0:+X 1:+X+Y 2:+Y(back) 3:-X+Y 4:-X 5:-X-Y 6:-Y(back) 7:+X-Y
};
FootprintQuery QueryFootprint(float x, float y);
// True when (x, y) is over floor: inside the footprint or inside a net box.
bool OverFloor(float x, float y);

// Low-poly visual mesh lofted from the same constants (flat/unlit-friendly:
// per-face normals duplicated per quad strip; no textures). Triangle count is
// O(edges * segments) — far below the 50k budget (a few hundred tris).
struct Mesh {
    std::vector<Vec3> pos;
    std::vector<Vec3> nrm;
    std::vector<std::uint32_t> idx;
};
Mesh BuildVisualMesh(int rampSeg = 7, int ceilSeg = 6);

} // namespace Arena
