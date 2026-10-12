// arena_test.cpp — standalone headless verification for stream A2.
// Proves the mission numbers: walls, floor, ceiling, goal box, corners,
// fillets, pads and the visual-mesh triangle budget. No dependencies beyond
// the two arena sources. Exit code 0 = all checks pass.
#include <cmath>
#include <cstdio>

#include "ArenaGeometry.h"

using namespace Arena;

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; std::printf("FAIL %d: ", __LINE__); std::printf(__VA_ARGS__); \
           std::printf("\n"); } \
} while (0)

static bool feq(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main() {
    // --- 1. side walls: raycast from center hits x = +/-4096 exactly ------
    for (int s = -1; s <= 1; s += 2) {
        RayHit h;
        bool ok = Raycast({0, 0, 500}, {(float)s, 0, 0}, 20000.0f, h);
        CHECK(ok, "side wall ray s=%d missed", s);
        if (ok) {
            CHECK(feq(h.point.x, s * 4096.0f, 1e-3f), "side wall x=%f, want %d",
                  h.point.x, s * 4096);
            CHECK(feq(h.t, 4096.0f, 1e-3f), "side wall t=%f, want 4096", h.t);
            CHECK(feq(h.normal.x, -(float)s, 1e-6f) && feq(h.normal.z, 0.0f, 1e-6f),
                  "side wall normal=(%f,%f,%f)", h.normal.x, h.normal.y, h.normal.z);
        }
    }
    // --- 2. back walls ------------------------------------------------------
    for (int s = -1; s <= 1; s += 2) {
        RayHit h;
        // Aim above the goal mouth so the wall (not the net) is hit.
        bool ok = Raycast({2000, 0, 500}, {0, (float)s, 0}, 20000.0f, h);
        CHECK(ok, "back wall ray s=%d missed", s);
        if (ok) CHECK(feq(h.point.y, s * 5120.0f, 1e-3f), "back wall y=%f, want %d",
                      h.point.y, s * 5120);
    }
    // --- 3. floor z = 0 ------------------------------------------------------
    {
        RayHit h;
        bool ok = Raycast({0, 0, 1000}, {0, 0, -1}, 20000.0f, h);
        CHECK(ok, "floor ray missed");
        if (ok) CHECK(feq(h.point.z, 0.0f, 1e-3f), "floor z=%f", h.point.z);
        Contact c = Collide({0, 0, 91.25f});
        CHECK(feq(c.distance, 91.25f, 1e-3f) && feq(c.normal.z, 1.0f, 1e-6f),
              "floor sdf d=%f n=(%f,%f,%f)", c.distance, c.normal.x, c.normal.y, c.normal.z);
    }
    // --- 4. ceiling z = 2048 --------------------------------------------------
    {
        RayHit h;
        bool ok = Raycast({0, 0, 1000}, {0, 0, 1}, 20000.0f, h);
        CHECK(ok, "ceiling ray missed");
        if (ok) CHECK(feq(h.point.z, 2048.0f, 1e-3f), "ceiling z=%f", h.point.z);
        Contact c = Collide({0, 0, 2048.0f - 50.0f});
        CHECK(feq(c.distance, 50.0f, 1e-3f) && feq(c.normal.z, -1.0f, 1e-6f),
              "ceiling sdf d=%f n_z=%f", c.distance, c.normal.z);
    }
    // --- 5. corner chamfer |x|+|y| = 8064 --------------------------------------
    {
        float d = 1.0f / std::sqrt(2.0f);
        RayHit h;
        bool ok = Raycast({0, 0, 500}, {d, d, 0}, 20000.0f, h);
        CHECK(ok, "corner ray missed");
        if (ok) {
            CHECK(feq(h.point.x + h.point.y, 8064.0f, 1e-2f), "corner sum=%f",
                  h.point.x + h.point.y);
            CHECK(feq(h.normal.x, -d, 1e-5f) && feq(h.normal.y, -d, 1e-5f),
                  "corner normal=(%f,%f,%f)", h.normal.x, h.normal.y, h.normal.z);
            // Corner wall straight run: side wall straight section ends at x=2944.
            CHECK(feq(h.point.x, h.point.y, 1e-2f), "corner hit symmetric x=%f y=%f",
                  h.point.x, h.point.y);
        }
        // Straight-run extent: ray down the side wall at x=4096 stays free to y=3968.
        FootprintQuery q = QueryFootprint(4096.0f - 1.0f, 3968.0f);
        CHECK(q.w > 0.0f, "side straight run ends before y=3968 (w=%f)", q.w);
        q = QueryFootprint(4095.0f, 3969.0f);
        CHECK(q.w < 0.0f, "chamfer starts after y=3968 (w=%f)", q.w);
    }
    // --- 6. floor fillet R = 256 ----------------------------------------------
    {
        // Closest point to the wall base interior corner region: query deep in
        // the fillet at 45 degrees; distance to arc center must equal R.
        Contact c = Collide({4096.0f - 256.0f + 100.0f, 0, 100.0f});
        // Point (3940+100=... ) — check against analytic arc instead:
        Contact c2 = Collide({4096.0f - 256.0f + 256.0f * 0.70710678f,
                              0, 256.0f - 256.0f * 0.70710678f + 50.0f});
        (void)c;
        // Direct: point on the arc mid-angle pushed 50 along the free-side
        // normal (+w = -x inward, +z up) -> d = 50.
        float ax = 4096.0f - 256.0f + 256.0f * 0.70710678f;
        float az = 256.0f - 256.0f * 0.70710678f;
        float nx = 0.70710678f, nz = 0.70710678f; // arc free-side normal at 45deg
        Contact c3 = Collide({ax - 50.0f * nx, 0, az + 50.0f * nz});
        CHECK(feq(c3.distance, 50.0f, 0.5f), "fillet 45deg d=%f want 50", c3.distance);
        CHECK(c2.distance > 0, "fillet region free");
    }
    // --- 7. goal mouth: half-width 892.755, height 642.775 ----------------------
    {
        // Post at x=892.755: sphere tangent to it from the field.
        // (Closest post point shares the query y/z: y must equal the wall
        // plane 5120, else the 3D distance exceeds the radius.)
        Vec3 ctr{GOAL_HALF_WIDTH - 91.25f, 5120.0f, 300.0f};
        Vec3 n;
        bool touched = CollideSphere(ctr, 91.25f, n);
        CHECK(touched && feq(ctr.x, GOAL_HALF_WIDTH - 91.25f, 0.5f),
              "post touch x=%f", ctr.x);
        // Crossbar at z=642.775.
        Vec3 ctr2{0, 5120.0f, GOAL_HEIGHT - 91.25f};
        touched = CollideSphere(ctr2, 91.25f, n);
        CHECK(touched && feq(ctr2.z, GOAL_HEIGHT - 91.25f, 0.5f),
              "bar touch z=%f", ctr2.z);
        // Mouth center is open: ball rolls through the goal line untouched.
        Vec3 ctr3{0, 5119.0f, 200.0f};
        touched = CollideSphere(ctr3, 91.25f, n);
        CHECK(!touched, "mouth center must be open (pushed to %f,%f,%f)",
              ctr3.x, ctr3.y, ctr3.z);
    }
    // --- 8. goal box depth 880 + IsGoal threshold -------------------------------
    {
        // Ray down the mouth hits the back net at y = 5120 + 880 = 6000.
        RayHit h;
        bool ok = Raycast({0, 5000, 300}, {0, 1, 0}, 20000.0f, h);
        CHECK(ok, "net back ray missed");
        if (ok) CHECK(feq(h.point.y, 6000.0f, 1e-3f), "net back y=%f", h.point.y);
        // Net interior is free and drivable (floor at z=0 inside the box).
        Contact c = Collide({0, 5500.0f, 91.25f});
        CHECK(feq(c.distance, 91.25f, 1e-3f), "net floor d=%f", c.distance);
        // Scoring rule: |y| > 5124.25 + r.
        CHECK(!IsGoal({0, 5215.0f, 300}, 91.25f), "5215 must not score");
        CHECK(IsGoal({0, 5216.0f, 300}, 91.25f), "5216 must score");
        CHECK(!IsGoal({2000, 6000.0f, 300}, 91.25f), "wide of mouth must not score");
        CHECK(!IsGoal({0, 6000.0f, 700}, 91.25f), "over the bar must not score");
    }
    // --- 9. pads: 34 entries, big-first order, z = 73 / 70 ----------------------
    {
        CHECK(NUM_PADS == 34, "NUM_PADS=%zu", NUM_PADS);
        CHECK(NUM_BIG_PADS == 6, "NUM_BIG_PADS=%zu", NUM_BIG_PADS);
        bool order = true;
        for (std::size_t i = 0; i < NUM_PADS; ++i) {
            float wantZ = i < 6 ? 73.0f : 70.0f;
            if (!feq(kPads[i].z, wantZ, 1e-6f)) order = false;
        }
        CHECK(order, "pad z-order (big 73 first, small 70 after)");
        CHECK(feq(kPads[0].x, -3584.0f, 1e-6f) && feq(kPads[0].y, 0.0f, 1e-6f),
              "pad[0]=(%f,%f)", kPads[0].x, kPads[0].y);
        CHECK(feq(kPads[6].x, 0.0f, 1e-6f) && feq(kPads[6].y, -4240.0f, 1e-6f),
              "pad[6]=(%f,%f)", kPads[6].x, kPads[6].y);
        // small[23]=(-940,3308) is kPads[29], small[24]=(940,3308) is
        // kPads[30], per RLConst.h:298-299 (3308, not the RLGym 3310 typo).
        CHECK(feq(kPads[29].x, -940.0f, 1e-6f) && feq(kPads[29].y, 3308.0f, 1e-6f),
              "pad[29]=(%f,%f)", kPads[29].x, kPads[29].y);
        CHECK(feq(kPads[30].x, 940.0f, 1e-6f) && feq(kPads[30].y, 3308.0f, 1e-6f),
              "pad[30]=(%f,%f)", kPads[30].x, kPads[30].y);
    }
    // --- 10. visual mesh: triangle budget -----------------------------------------
    {
        Mesh m = BuildVisualMesh();
        std::size_t tris = m.idx.size() / 3;
        std::printf("info: visual mesh verts=%zu tris=%zu\n", m.pos.size(), tris);
        CHECK(tris < 50000, "mesh tris=%zu exceeds budget", tris);
        CHECK(m.pos.size() == m.nrm.size(), "pos/nrm count match");
        // Mesh apex near the wall top stays within a few uu of the profile.
        bool sane = true;
        for (auto& v : m.pos)
            if (!(v.z >= -1e-3f && v.z <= CEILING_Z + 1e-3f)) sane = false;
        CHECK(sane, "mesh z-range sane");
    }

    // --- 11. wall SDF normal + sphere push-out point into free space -----------
    // Regression: the footprint branch of Collide() built the horizontal normal
    // as +g*nu (g = OUTWARD edge normal, nu = along the INWARD u axis), so wall
    // normals pointed INTO the wall. CollideSphere then pushed bodies deeper on
    // every iteration and fallback containment launched cars/ball through the
    // wall to a float-precision-locked "stuck inside" position.
    {
        // Solid side of the +X wall: SDF gradient must point toward free space,
        // i.e. -X, with the correct penetration depth.
        Contact c = Collide({4200.0f, 0.0f, 300.0f});
        CHECK(c.distance < 0.0f, "outside-wall d=%f must be negative", c.distance);
        CHECK(feq(c.distance, -104.0f, 0.5f), "outside-wall d=%f want -104", c.distance);
        CHECK(c.normal.x < -0.9f, "outside +X wall normal.x=%f must point inward",
              c.normal.x);
        // Mirrored wall.
        Contact cm = Collide({-4200.0f, 0.0f, 300.0f});
        CHECK(cm.normal.x > 0.9f, "outside -X wall normal.x=%f must point inward",
              cm.normal.x);
        // Free side of the +X wall: push-out must back the sphere OFF the wall,
        // never deeper into it.
        Vec3 ctr{4056.0f, 0.0f, 500.0f}; // 40uu from the wall, radius 50
        Vec3 n;
        bool touched = CollideSphere(ctr, 50.0f, n);
        CHECK(touched, "wall sphere not touched");
        CHECK(ctr.x <= 4056.0f + 0.5f, "sphere pushed into wall: x=%f", ctr.x);
        CHECK(feq(ctr.x, 4046.0f, 1.0f), "sphere x=%f want ~4046", ctr.x);
        CHECK(n.x < -0.9f, "sphere wall normal.x=%f must point inward", n.x);
        CHECK(feq(ctr.z, 500.0f, 1.0f), "sphere z=%f should stay at 500", ctr.z);
    }

    std::printf("%s: %d passed, %d failed\n", g_fail ? "RESULT FAIL" : "RESULT PASS",
                g_pass, g_fail);
    return g_fail ? 1 : 0;
}
