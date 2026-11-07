// Analytic Soccar collision and a visual mesh lofted from the same shell.
#pragma once

#include <cstdint>
#include <vector>

#include "ArenaConstants.h"

namespace Arena {

// distance < 0 is inside solid. normal points from the surface toward the query.
struct Contact {
    float distance;
    Vec3 normal;
    // 0 floor, 1 wall or fillet, 2 ceiling, 3 goal box, 4 post or crossbar.
    int feature;
};

Contact Collide(const Vec3& p);

// Pushes the sphere out of solid. outNormal is the deepest contact. True if it touched.
bool CollideSphere(Vec3& center, float radius, Vec3& outNormal, int maxIters = 4);

struct RayHit {
    float t;       // distance along dir; dir must be unit length
    Vec3 point;
    Vec3 normal;   // opposes the ray
    int feature;
};

// Nearest hit with t in [0, maxDist]. Posts and the crossbar are not raycast;
// CollideSphere resolves those edges.
bool Raycast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& out);

// True when |y| > GOAL_SCORE_BASE_Y + ballRadius and the ball is inside the mouth.
bool IsGoal(const Vec3& ballPos, float ballRadius = BALL_RADIUS);

struct FootprintQuery {
    float w;      // signed distance to the edge, positive inside
    float gx, gy; // outward normal of the closest edge
    int edge;     // 0:+X 1:+X+Y 2:+Y 3:-X+Y 4:-X 5:-X-Y 6:-Y 7:+X-Y
};
FootprintQuery QueryFootprint(float x, float y);
// Inside the octagon or inside a net box.
bool OverFloor(float x, float y);

struct Mesh {
    std::vector<Vec3> pos;
    std::vector<Vec3> nrm;
    std::vector<std::uint32_t> idx;
};
Mesh BuildVisualMesh(int rampSeg = 7, int ceilSeg = 6);

} // namespace Arena
