// ArenaGeometry.cpp — analytic Soccar collision + visual mesh (stream A2).
#include "ArenaGeometry.h"

#include <cmath>

namespace Arena {
namespace {

// --- small math ------------------------------------------------------------
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Len2(float x, float y) { return std::sqrt(x * x + y * y); }
inline float Len3(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline Vec3 Norm3(Vec3 v) {
    float l = Len3(v);
    return l > 1e-12f ? v * (1.0f / l) : Vec3(0, 0, 1);
}

// --- footprint octagon ------------------------------------------------------
// Outward edge normals + offsets (n . p = c on the edge).
// Axis edges: c = 4096 / 5120. Chamfers: |x|+|y| = 8064 => c = 8064/sqrt(2).
constexpr float CHAMFER_C = 8064.0f * 0.7071067811865475f; // 5702.099...
constexpr float ENX[8] = {1, 0.7071067811865475f, 0, -0.7071067811865475f,
                          -1, -0.7071067811865475f, 0, 0.7071067811865475f};
constexpr float ENY[8] = {0, 0.7071067811865475f, 1, 0.7071067811865475f,
                          0, -0.7071067811865475f, -1, -0.7071067811865475f};
constexpr float EC[8] = {SIDE_WALL_X, CHAMFER_C, BACK_WALL_Y, CHAMFER_C,
                         SIDE_WALL_X, CHAMFER_C, BACK_WALL_Y, CHAMFER_C};

constexpr bool IsBackEdge(int e) { return e == 2 || e == 6; }

struct Seg2 { // 2D segment used for posts/crossbar (x, y, z endpoints)
    Vec3 a, b;
};

void GoalSegments(float goalSgn, Seg2 out[3]) {
    float y = goalSgn * BACK_WALL_Y;
    out[0] = {{-GOAL_HALF_WIDTH, y, 0}, {-GOAL_HALF_WIDTH, y, GOAL_HEIGHT}}; // post L
    out[1] = {{GOAL_HALF_WIDTH, y, 0}, {GOAL_HALF_WIDTH, y, GOAL_HEIGHT}};   // post R
    out[2] = {{-GOAL_HALF_WIDTH, y, GOAL_HEIGHT}, {GOAL_HALF_WIDTH, y, GOAL_HEIGHT}}; // bar
}

Vec3 ClosestOnSeg(const Vec3& p, const Seg2& s) {
    Vec3 ab = s.b - s.a;
    float denom = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    float t = denom > 1e-12f
        ? ((p.x - s.a.x) * ab.x + (p.y - s.a.y) * ab.y + (p.z - s.a.z) * ab.z) / denom
        : 0.0f;
    t = Clamp(t, 0.0f, 1.0f);
    return {s.a.x + ab.x * t, s.a.y + ab.y * t, s.a.z + ab.z * t};
}

// --- height profile query ----------------------------------------------------
// Profile plane: u = inward distance from wall, v = height.
// Returns closest surface point + inside flag + 2D outward-to-query normal.
struct ProfileHit {
    float dist;   // unsigned distance
    bool inside;  // query in free region
    float nu, nv; // unit vector from surface point to query
    int feature;  // 0 floor, 1 wall/fillet, 2 ceiling
};

ProfileHit QueryProfile(float w, float z) {
    const float R = RAMP_RADIUS, Rc = CEIL_RADIUS, H = CEILING_Z;
    ProfileHit best{1e30f, true, 0, 1, 0};

    auto consider = [&](float qu, float qv, bool inside, int feat) {
        float du = w - qu, dv = z - qv;
        float d = Len2(du, dv);
        if (d < best.dist) {
            if (d > 1e-9f) {
                best = {d, inside, du / d, dv / d, feat};
            } else {
                // Query exactly on the surface: use feature's free-side normal.
                float nu = 0, nv = 1;
                if (feat == 1) { nu = 1; nv = 0; }
                if (feat == 2) { nu = 0; nv = -1; }
                best = {0.0f, true, nu, nv, feat};
            }
        }
    };

    // Floor (tangent to fillet at u = R).
    consider(Clamp(w, R, 1e9f), 0.0f, z > 0.0f, 0);
    // Floor fillet arc: center (R, R), concave quarter-round from the floor
    // tangent (R, 0) at angle -pi/2 to the wall junction (0, R) at angle -pi.
    // (Was [-pi/2, +pi/2]: that arc bulges into the room and stole wall hits.)
    {
        float au = w - R, av = z - R;
        float a = std::atan2(av, au);
        const float kHalfPi = 1.5707963267948966f;
        const float kPi = 3.141592653589793f;
        a = Clamp(a, -kPi, -kHalfPi); // [-pi, -pi/2]
        consider(R + R * std::cos(a), R + R * std::sin(a), (au * au + av * av) < R * R, 1);
    }
    // Vertical wall.
    consider(0.0f, Clamp(z, R, H - Rc), w > 0.0f, 1);
    // Ceiling fillet arc: center (Rc, H - Rc), angles [pi/2, pi].
    {
        float au = w - Rc, av = z - (H - Rc);
        float a = std::atan2(av, au);
        const float kHalfPi = 1.5707963267948966f;
        const float kPi = 3.141592653589793f;
        a = Clamp(a, kHalfPi, kPi);
        consider(Rc + Rc * std::cos(a), (H - Rc) + Rc * std::sin(a),
                 (au * au + av * av) < Rc * Rc, 1);
    }
    // Ceiling (tangent to fillet at u = Rc).
    consider(Clamp(w, Rc, 1e9f), H, z < H, 2);
    return best;
}

// Contact.distance convention: POSITIVE in free space (distance to the
// nearest solid surface), NEGATIVE inside solid (penetration depth = -distance).
// When several candidates exist, the one with the smallest |distance| (the
// nearest surface) wins.

// --- goal box query -----------------------------------------------------------
// sgn = +1 (orange, +Y) or -1 (blue, -Y). Box interior is free; the 5 solid
// faces are the two side nets, the back net, the net roof, and the floor.
// Anything past the goal line but outside the box is exterior solid.
Contact QueryGoalBox(const Vec3& p, float sgn) {
    float yIn = sgn * p.y - BACK_WALL_Y; // 0 at line, GOAL_DEPTH at back
    float ax = std::fabs(p.x);
    bool inside = (ax < GOAL_HALF_WIDTH) && (yIn > 0.0f) && (yIn < GOAL_DEPTH) &&
                  (p.z > 0.0f) && (p.z < GOAL_HEIGHT);
    Contact c;
    if (inside) {
        float dSide = GOAL_HALF_WIDTH - ax;
        float dBack = GOAL_DEPTH - yIn;
        float dTop = GOAL_HEIGHT - p.z;
        float dFloor = p.z;
        c.distance = dSide;
        c.normal = {p.x > 0 ? -1.0f : 1.0f, 0, 0};
        c.feature = 3;
        if (dBack < c.distance) { c.distance = dBack; c.normal = {0, -sgn, 0}; }
        if (dTop < c.distance) { c.distance = dTop; c.normal = {0, 0, -1}; }
        if (dFloor < c.distance) { c.distance = dFloor; c.normal = {0, 0, 1}; }
        return c;
    }
    // Outside the box (roof/back/side exterior, or above the net past the
    // line): negative distance to the clamped box surface (exterior solid).
    float cx = Clamp(p.x, -GOAL_HALF_WIDTH, GOAL_HALF_WIDTH);
    float cy = sgn * Clamp(sgn * p.y, BACK_WALL_Y, BACK_WALL_Y + GOAL_DEPTH);
    float cz = Clamp(p.z, 0.0f, GOAL_HEIGHT);
    Vec3 q{cx, cy, cz};
    Vec3 d = p - q;
    float l = Len3(d);
    if (l > 1e-9f) {
        // Exterior solid: gradient points from the query toward the surface.
        c = {-l, d * (-1.0f / l), 3};
    } else {
        c = {0.0f, {0, -sgn, 0}, 3};
    }
    return c;
}

bool InMouth(float x, float z) { return std::fabs(x) < GOAL_HALF_WIDTH && z < GOAL_HEIGHT && z >= 0.0f; }

} // namespace

FootprintQuery QueryFootprint(float x, float y) {
    FootprintQuery q{-1e30f, 1, 0, 0};
    for (int e = 0; e < 8; ++e) {
        float d = x * ENX[e] + y * ENY[e] - EC[e];
        if (d > q.w) {
            q.w = d;
            q.gx = ENX[e];
            q.gy = ENY[e];
            q.edge = e;
        }
    }
    q.w = -q.w; // convert to +inside
    return q;
}

bool OverFloor(float x, float y) {
    FootprintQuery q = QueryFootprint(x, y);
    if (q.w >= 0.0f) return true;
    float ay = std::fabs(y);
    return (std::fabs(x) <= GOAL_HALF_WIDTH) && (ay >= BACK_WALL_Y) && (ay <= BACK_NET_Y);
}

Contact Collide(const Vec3& p) {
    // Gather candidates from every applicable feature; nearest surface wins.
    Contact best{1e30f, {0, 0, 1}, 0};
    auto consider = [&](const Contact& c) {
        if (std::fabs(c.distance) < std::fabs(best.distance)) best = c;
    };

    // Floor and ceiling (global over the footprint and both net boxes).
    // Normals are SDF gradients: floor d=z -> (0,0,1), ceiling d=H-z -> (0,0,-1).
    if (OverFloor(p.x, p.y)) {
        consider({p.z, {0, 0, 1}, 0});
        consider({CEILING_Z - p.z, {0, 0, -1}, 2});
    }

    float ay = std::fabs(p.y);
    if (ay > BACK_WALL_Y) {
        // Past the goal line: the net box (or exterior solid) rules.
        consider(QueryGoalBox(p, p.y > 0 ? 1.0f : -1.0f));
    } else {
        FootprintQuery q = QueryFootprint(p.x, p.y);
        // The wall/fillet profile is open across the goal mouth.
        bool mouthPassage = IsBackEdge(q.edge) && InMouth(p.x, p.z);
        if (!mouthPassage) {
            ProfileHit ph = QueryProfile(q.w, p.z);
            // u is the INWARD axis and q.gx/q.gy is the OUTWARD edge normal,
            // so the surface->query horizontal component is -g*nu. (+g*nu
            // pointed the wall normal into solid, so CollideSphere pushed
            // bodies deeper every iteration.)
            Vec3 n{-q.gx * ph.nu, -q.gy * ph.nu, ph.nv};
            // QueryProfile returns surface->query; the SDF gradient points the
            // opposite way on the solid side.
            if (!ph.inside) n = -n;
            consider({ph.inside ? ph.dist : -ph.dist, Norm3(n), ph.feature});
        }
    }

    // Goal posts / crossbar (sharp edges), both ends.
    Seg2 segs[3];
    for (int g = 0; g < 2; ++g) {
        GoalSegments(g == 0 ? 1.0f : -1.0f, segs);
        for (int s = 0; s < 3; ++s) {
            Vec3 c = ClosestOnSeg(p, segs[s]);
            Vec3 d = p - c;
            float l = Len3(d);
            Vec3 n = l > 1e-9f ? d * (1.0f / l) : Vec3(0, g == 0 ? -1.0f : 1.0f, 0);
            consider({l, n, 4}); // free-space distance to a sharp edge
        }
    }
    return best;
}

bool CollideSphere(Vec3& center, float radius, Vec3& outNormal, int maxIters) {
    bool touched = false;
    outNormal = {0, 0, 1};
    float deepest = 0.0f;
    for (int it = 0; it < maxIters; ++it) {
        Contact c = Collide(center);
        if (c.distance >= radius + 1e-3f) break; // 1mm tangent epsilon
        float push = radius - c.distance; // ~0 when exactly tangent
        center = center + c.normal * push;
        if (!touched || c.distance < deepest) {
            deepest = c.distance;
            outNormal = c.normal;
        }
        touched = true;
    }
    return touched;
}

bool IsGoal(const Vec3& ballPos, float ballRadius) {
    return std::fabs(ballPos.y) > GOAL_SCORE_BASE_Y + ballRadius &&
           std::fabs(ballPos.x) < GOAL_HALF_WIDTH && ballPos.z < GOAL_HEIGHT;
}

// --- raycast ------------------------------------------------------------------
namespace {

// 2D ray vs profile in one edge frame. u = lateral (kept for bounds check by
// caller via 3D edge segment), w = inward, z = up. Fills smallest t >= 0.
bool RayProfile(float w0, float z0, float dw, float dz, float& tBest, float& nu, float& nv,
                int& feat) {
    const float R = RAMP_RADIUS, Rc = CEIL_RADIUS, H = CEILING_Z;
    bool hit = false;
    auto consider = [&](float t, float nwu, float nvz, int f) {
        if (t >= 0.0f && t < tBest) {
            tBest = t;
            nu = nwu;
            nv = nvz;
            feat = f;
            hit = true;
        }
    };
    // Floor (w >= R).
    if (dz < -1e-9f) {
        float t = -z0 / dz;
        if (w0 + t * dw >= R) consider(t, 0, 1, 0);
    }
    // Vertical wall.
    if (dw < -1e-9f) {
        float t = -w0 / dw;
        float z = z0 + t * dz;
        if (z >= R && z <= H - Rc) consider(t, 1, 0, 1);
    }
    // Ceiling (w >= Rc).
    if (dz > 1e-9f) {
        float t = (H - z0) / dz;
        if (w0 + t * dw >= Rc) consider(t, 0, -1, 2);
    }
    // Floor fillet circle, center (R, R): valid arc is the concave quarter
    // from floor tangent (angle -pi/2) to wall junction (angle -pi).
    {
        float ou = w0 - R, ov = z0 - R;
        float a = dw * dw + dz * dz;
        float b = ou * dw + ov * dz;
        float c = ou * ou + ov * ov - R * R;
        float disc = b * b - a * c;
        if (a > 1e-12f && disc >= 0.0f) {
            float sq = std::sqrt(disc);
            for (int k = 0; k < 2; ++k) {
                float t = (-b + (k == 0 ? -sq : sq)) / a;
                if (t < 0.0f) continue;
                float hu = w0 + t * dw - R, hv = z0 + t * dz - R;
                float ang = std::atan2(hv, hu);
                const float kHalfPi = 1.5707963267948966f, kPi = 3.141592653589793f;
                if (ang > -kHalfPi || ang < -kPi) continue;
                float l = Len2(hu, hv);
                if (l < 1e-9f) continue;
                // Concave fillet: surface normal points away from center.
                consider(t, -hu / l, -hv / l, 1);
            }
        }
    }
    // Ceiling fillet circle, center (Rc, H - Rc).
    {
        float ou = w0 - Rc, ov = z0 - (H - Rc);
        float a = dw * dw + dz * dz;
        float b = ou * dw + ov * dz;
        float c = ou * ou + ov * ov - Rc * Rc;
        float disc = b * b - a * c;
        if (a > 1e-12f && disc >= 0.0f) {
            float sq = std::sqrt(disc);
            for (int k = 0; k < 2; ++k) {
                float t = (-b + (k == 0 ? -sq : sq)) / a;
                if (t < 0.0f) continue;
                float hu = w0 + t * dw - Rc, hv = z0 + t * dz - (H - Rc);
                float ang = std::atan2(hv, hu);
                const float kHalfPi = 1.5707963267948966f, kPi = 3.141592653589793f;
                if (ang < kHalfPi || ang > kPi) continue;
                float l = Len2(hu, hv);
                if (l < 1e-9f) continue;
                consider(t, -hu / l, -hv / l, 1);
            }
        }
    }
    return hit;
}

struct EdgeSeg { // edge endpoints for lateral bounds
    Vec3 a, b;
};

EdgeSeg EdgeEndpoints(int e) {
    // Intersect edge line with its two neighbours' lines.
    int ePrev = (e + 7) % 8, eNext = (e + 1) % 8;
    EdgeSeg s;
    auto inter = [](int i, int j, float& x, float& y) {
        float det = ENX[i] * ENY[j] - ENX[j] * ENY[i];
        x = (EC[i] * ENY[j] - EC[j] * ENY[i]) / det;
        y = (ENX[i] * EC[j] - ENX[j] * EC[i]) / det;
    };
    inter(ePrev, e, s.a.x, s.a.y);
    inter(e, eNext, s.b.x, s.b.y);
    s.a.z = s.b.z = 0;
    return s;
}

} // namespace

bool Raycast(const Vec3& origin, const Vec3& dir, float maxDist, RayHit& out) {
    bool found = false;
    out.t = maxDist;

    auto consider = [&](float t, const Vec3& n, int f) {
        if (t >= 0.0f && t <= out.t) {
            out.t = t;
            out.normal = n;
            out.feature = f;
            found = true;
        }
    };

    // Floor (footprint or net boxes).
    if (dir.z < -1e-9f) {
        float t = -origin.z / dir.z;
        float hx = origin.x + t * dir.x, hy = origin.y + t * dir.y;
        if (OverFloor(hx, hy)) consider(t, {0, 0, 1}, 0);
    }
    // Ceiling (footprint only; nets are below it).
    if (dir.z > 1e-9f) {
        float t = (CEILING_Z - origin.z) / dir.z;
        float hx = origin.x + t * dir.x, hy = origin.y + t * dir.y;
        if (QueryFootprint(hx, hy).w >= 0.0f) consider(t, {0, 0, -1}, 2);
    }

    // Walls + fillets, one edge frame at a time.
    for (int e = 0; e < 8; ++e) {
        float nx = ENX[e], ny = ENY[e];
        // Edge frame: inward = (-nx, -ny); lateral = (-ny, nx).
        float w0 = -(origin.x * nx + origin.y * ny - EC[e]);
        float dw = -(dir.x * nx + dir.y * ny);
        float tBest = out.t, nu = 1, nv = 0;
        int feat = 1;
        // Mouth test needs the hit x/z; do a first pass, then validate.
        if (!RayProfile(w0, origin.z, dw, dir.z, tBest, nu, nv, feat)) continue;
        float hx = origin.x + tBest * dir.x;
        float hy = origin.y + tBest * dir.y;
        float hz = origin.z + tBest * dir.z;
        // Lateral bounds of the edge segment.
        EdgeSeg seg = EdgeEndpoints(e);
        float uHit = hx * -ny + hy * nx;
        float ua = seg.a.x * -ny + seg.a.y * nx;
        float ub = seg.b.x * -ny + seg.b.y * nx;
        float ulo = ua < ub ? ua : ub, uhi = ua < ub ? ub : ua;
        if (uHit < ulo - 1e-3f || uHit > uhi + 1e-3f) continue;
        // Open mouth: a wall/fillet hit inside the mouth rect is not a hit —
        // the ray continues into the net box (tested below). Floor/ceiling
        // hits in the same frame are real (net floor / arena ceiling).
        if (IsBackEdge(e) && InMouth(hx, hz) && feat == 1) continue;
        Vec3 n{-nx * nu, -ny * nu, nv};
        consider(tBest, Norm3(n), feat);
    }

    // Goal-box interior faces (both nets): sides, back, roof.
    for (int g = 0; g < 2; ++g) {
        float sgn = g == 0 ? 1.0f : -1.0f;
        float yLine = sgn * BACK_WALL_Y, yBack = sgn * (BACK_WALL_Y + GOAL_DEPTH);
        // Sides x = +/-GHW.
        if (std::fabs(dir.x) > 1e-9f) {
            for (int s = 0; s < 2; ++s) {
                float xp = s == 0 ? -GOAL_HALF_WIDTH : GOAL_HALF_WIDTH;
                float t = (xp - origin.x) / dir.x;
                if (t < 0.0f || t > out.t) continue;
                float hy = origin.y + t * dir.y, hz = origin.z + t * dir.z;
                if (sgn * hy >= yLine && sgn * hy <= yBack && hz >= 0.0f && hz <= GOAL_HEIGHT) {
                    Vec3 n = dir.x > 0 ? Vec3{-1, 0, 0} : Vec3{1, 0, 0};
                    if (s == 0) n = dir.x > 0 ? Vec3{-1, 0, 0} : Vec3{1, 0, 0};
                    consider(t, n, 3);
                }
            }
        }
        // Back y = yBack.
        if (std::fabs(dir.y) > 1e-9f) {
            float t = (yBack - origin.y) / dir.y;
            if (t >= 0.0f && t <= out.t) {
                float hx = origin.x + t * dir.x, hz = origin.z + t * dir.z;
                if (std::fabs(hx) <= GOAL_HALF_WIDTH && hz >= 0.0f && hz <= GOAL_HEIGHT)
                    consider(t, dir.y * sgn > 0 ? Vec3{0, -sgn, 0} : Vec3{0, sgn, 0}, 3);
            }
        }
        // Roof z = GH.
        if (std::fabs(dir.z) > 1e-9f) {
            float t = (GOAL_HEIGHT - origin.z) / dir.z;
            if (t >= 0.0f && t <= out.t) {
                float hx = origin.x + t * dir.x, hy = origin.y + t * dir.y;
                if (std::fabs(hx) <= GOAL_HALF_WIDTH && sgn * hy >= yLine && sgn * hy <= yBack)
                    consider(t, dir.z > 0 ? Vec3{0, 0, -1} : Vec3{0, 0, 1}, 3);
            }
        }
    }

    if (!found) return false;
    out.point = origin + dir * out.t;
    // Face the normal against the ray.
    if (out.normal.x * dir.x + out.normal.y * dir.y + out.normal.z * dir.z > 0)
        out.normal = -out.normal;
    return true;
}

// --- visual mesh ---------------------------------------------------------------
Mesh BuildVisualMesh(int rampSeg, int ceilSeg) {
    Mesh m;
    auto quad = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& n) {
        std::uint32_t base = (std::uint32_t)m.pos.size();
        m.pos.insert(m.pos.end(), {a, b, c, d});
        m.nrm.insert(m.nrm.end(), {n, n, n, n});
        m.idx.insert(m.idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };

    // Profile polyline (u = inward from wall, v = height) + 2D normals.
    struct PP { float u, v, nu, nv; };
    std::vector<PP> prof;
    const float R = RAMP_RADIUS, Rc = CEIL_RADIUS, H = CEILING_Z;
    const float kHalfPi = 1.5707963267948966f;
    for (int i = 0; i <= rampSeg; ++i) { // floor fillet
        float phi = (float)i / rampSeg * kHalfPi;
        prof.push_back({R - R * std::sin(phi), R - R * std::cos(phi),
                        std::sin(phi), std::cos(phi)});
    }
    prof.push_back({0.0f, H - Rc, 1.0f, 0.0f}); // wall-top joint
    for (int i = 1; i <= ceilSeg; ++i) { // ceiling fillet (joint already added)
        float psi = (float)i / ceilSeg * kHalfPi;
        prof.push_back({Rc - Rc * std::cos(psi), (H - Rc) + Rc * std::sin(psi),
                        std::cos(psi), -std::sin(psi)});
    }
    // NOTE: prof runs floor-fillet (u=R..0, v=0..R) -> wall joint (0,R) is the
    // last fillet row -> wall-top joint (0,H-Rc) -> ceiling fillet, so
    // consecutive rows loft the full floor->wall->ceiling run.

    // Walls: one strip per footprint edge.
    for (int e = 0; e < 8; ++e) {
        EdgeSeg seg = EdgeEndpoints(e);
        float ix = -ENX[e], iy = -ENY[e]; // inward
        for (std::size_t r = 0; r + 1 < prof.size(); ++r) {
            Vec3 a{seg.a.x + ix * prof[r].u, seg.a.y + iy * prof[r].u, prof[r].v};
            Vec3 b{seg.b.x + ix * prof[r].u, seg.b.y + iy * prof[r].u, prof[r].v};
            Vec3 c{seg.b.x + ix * prof[r + 1].u, seg.b.y + iy * prof[r + 1].u, prof[r + 1].v};
            Vec3 d{seg.a.x + ix * prof[r + 1].u, seg.a.y + iy * prof[r + 1].u, prof[r + 1].v};
            Vec3 n{ix * (prof[r].nu + prof[r + 1].nu) * 0.5f,
                   iy * (prof[r].nu + prof[r + 1].nu) * 0.5f,
                   (prof[r].nv + prof[r + 1].nv) * 0.5f};
            quad(a, b, c, d, Norm3(n));
        }
    }

    // Floor: octagon fan (footprint vertices at line intersections).
    {
        std::vector<Vec3> v;
        for (int e = 0; e < 8; ++e) {
            EdgeSeg seg = EdgeEndpoints(e);
            v.push_back({seg.a.x, seg.a.y, 0});
        }
        std::uint32_t base = (std::uint32_t)m.pos.size();
        m.pos.push_back({0, 0, 0});
        m.nrm.push_back({0, 0, 1});
        for (auto& p : v) {
            m.pos.push_back(p);
            m.nrm.push_back({0, 0, 1});
        }
        for (int i = 0; i < 8; ++i)
            m.idx.insert(m.idx.end(), {base, base + 1 + i, base + 1 + (i + 1) % 8});
    }
    // Ceiling: same fan at H, facing down.
    {
        std::vector<Vec3> v;
        for (int e = 0; e < 8; ++e) {
            EdgeSeg seg = EdgeEndpoints(e);
            v.push_back({seg.a.x, seg.a.y, H});
        }
        std::uint32_t base = (std::uint32_t)m.pos.size();
        m.pos.push_back({0, 0, H});
        m.nrm.push_back({0, 0, -1});
        for (auto& p : v) {
            m.pos.push_back(p);
            m.nrm.push_back({0, 0, -1});
        }
        for (int i = 0; i < 8; ++i)
            m.idx.insert(m.idx.end(), {base, base + 1 + (i + 1) % 8, base + 1 + i});
    }
    // Goal boxes (sides face inward, backs face the field, roofs face down).
    for (int g = 0; g < 2; ++g) {
        float sgn = g == 0 ? 1.0f : -1.0f;
        float yL = sgn * BACK_WALL_Y, yB = sgn * (BACK_WALL_Y + GOAL_DEPTH);
        float x0 = -GOAL_HALF_WIDTH, x1 = GOAL_HALF_WIDTH;
        quad({x0, yL, 0}, {x0, yB, 0}, {x0, yB, GOAL_HEIGHT}, {x0, yL, GOAL_HEIGHT}, {1, 0, 0});
        quad({x1, yB, 0}, {x1, yL, 0}, {x1, yL, GOAL_HEIGHT}, {x1, yB, GOAL_HEIGHT}, {-1, 0, 0});
        quad({x0, yB, 0}, {x1, yB, 0}, {x1, yB, GOAL_HEIGHT}, {x0, yB, GOAL_HEIGHT},
             {0, -sgn, 0});
        quad({x0, yL, GOAL_HEIGHT}, {x1, yL, GOAL_HEIGHT}, {x1, yB, GOAL_HEIGHT},
             {x0, yB, GOAL_HEIGHT}, {0, 0, -1});
        quad({x0, yL, 0}, {x1, yL, 0}, {x1, yB, 0}, {x0, yB, 0}, {0, 0, 1});
    }
    return m;
}

} // namespace Arena
