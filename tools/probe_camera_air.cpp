// probe_camera_air.cpp — R1 probe for CAMERA_PORT_PROMPT #2 (car-cam
// air-velocity heading). Port of upstream rl_camera.cpp:218-355 behavior
// against OUR src/camera/Camera.h API.
//
// Bug (current): the car rig always sits behind carYaw (the nose), even
// airborne. Upstream: ground = rigid facing; air = eased onto the horizontal
// velocity heading (blend 200→900 uu/s, rate 2.5+7*stiffness), with a
// pitch-immune facing axis (half-turn continuity) so pitching past vertical
// cannot orbit the camera.
//
// Scenarios (car-cam, ballCamOn=false; 9-arg legacy API = ground truth today):
//  D: air flip — travel +X at 900 uu/s, nose +Y. Fixed: eye settles behind
//     the VELOCITY (-X). Broken: eye sits behind the NOSE (-Y, 90 deg off).
//  E: pitch through vertical — scalar nose bearing flips 0 -> PI in one
//     frame (what atan2(fwd.y,fwd.x) reports when the nose passes 90 deg
//     pitch). Fixed: eye holds (~0 travel). Broken: eye orbits 180 deg
//     around the car (~840 uu of travel).
//
// Build: g++ -std=c++20 -Isrc tools/probe_camera_air.cpp src/camera/Camera.cpp -lGL -o /tmp/probe_air && /tmp/probe_air
// No SDL, no GL context.
#include <cmath>
#include <cstdio>

#include "camera/Camera.h"

static int failures = 0;
#define CHECK(cond, msg, detail)                                        \
    do {                                                                \
        if (cond) {                                                     \
            std::printf("  ok   %s (%s)\n", msg, detail);               \
        } else {                                                        \
            std::printf("  FAIL %s (%s)\n", msg, detail);               \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static constexpr float kPi = 3.14159265f;

int main() {
    std::printf("probe_camera_air #2 (R1 probe: car-cam air-velocity heading)\n");
    const float dt = 1.0f / 60.0f;

    // D: air flip — airborne, velocity +X (900 uu/s), nose +Y (yaw PI/2).
    // Fixed: eye settles behind the VELOCITY (-X). Broken (nose-rigid):
    // eye sits behind the NOSE (-Y, 90 deg off). The 12-arg call is
    // required: the legacy call carries no velocity/airborne input, so it
    // cannot select the air branch at all.
    {
        rl::Camera c;
        float car[3] = {0, 0, 500}, ball[3] = {2000, 0, 500};
        float fwd[3] = {0, 1, 0}, vel[3] = {900, 0, 0};
        const float noseYaw = kPi / 2.0f;
        for (int i = 0; i < 90; ++i)  // 1.5 s to settle
            c.Update(dt, car, noseYaw, ball, false, 0.0f, false, 0.0f, 0.0f,
                     fwd, vel, true);
        float e[3];
        c.Eye(e);
        float ox = e[0] - car[0], oy = e[1] - car[1];
        float ol = std::sqrt(ox * ox + oy * oy);
        // Behind-velocity dir is -X: alignment = dot(offset, -velDir)/|offset|.
        float align = (ol > 1e-6f) ? (-ox / ol) : 0.0f;
        float offDeg = std::acos(align > 1 ? 1 : (align < -1 ? -1 : align)) *
                       180.0f / kPi;
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "eye offset (%.0f, %.0f), %.1f deg off behind-velocity",
                      ox, oy, offDeg);
        std::printf("[D air flip] %s\n", detail);
        CHECK(offDeg < 25.0f, "settled eye sits behind velocity (< 25 deg)",
              detail);
    }

    // E: pitch through vertical — airborne, near-zero horizontal speed so
    // the facing axis rules (w=0), nose vector pitching 0 -> 120 deg over
    // 0.5 s. The scalar bearing flips 0 -> PI mid-sweep (what atan2 reports
    // when cos(pitch) changes sign with no yaw turn). Fixed: half-turn
    // continuity holds the axis, eye travel < 100 uu. Broken: eye orbits
    // 180 deg around the car (~540+ uu). Ground calls intentionally do NOT
    // hold: rl_camera.cpp re-reads the raw bearing on the ground every
    // frame (wheels define attitude; landing heals air holds) — pitch
    // immunity is an air rule, so this runs airborne.
    {
        rl::Camera c;
        float car[3] = {0, 0, 500}, ball[3] = {2000, 0, 500};
        float vel[3] = {0, 0, 0};
        float fwd0[3] = {1, 0, 0};
        c.Update(dt, car, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f, fwd0,
                 vel, true);
        float e0[3];
        c.Eye(e0);
        float travel = 0.0f;
        float prev[3] = {e0[0], e0[1], e0[2]};
        for (int i = 1; i <= 30; ++i) {
            float p = (120.0f * i / 30) * kPi / 180.0f;
            float fwd[3] = {std::cos(p), 0.0f, std::sin(p)};
            float yaw = std::atan2(fwd[1], fwd[0]);
            c.Update(dt, car, yaw, ball, false, 0.0f, false, 0.0f, 0.0f,
                     fwd, vel, true);
            float e[3];
            c.Eye(e);
            float dx = e[0] - prev[0], dy = e[1] - prev[1],
                  dz = e[2] - prev[2];
            travel += std::sqrt(dx * dx + dy * dy + dz * dz);
            prev[0] = e[0];
            prev[1] = e[1];
            prev[2] = e[2];
        }
        char detail[128];
        std::snprintf(detail, sizeof(detail), "eye travel %.0f uu", travel);
        std::printf("[E pitch through vertical] %s\n", detail);
        CHECK(travel < 100.0f, "pure-pitch flip holds eye (< 100 uu)", detail);
    }

    std::printf(failures == 0 ? "PROBE PASS\n" : "PROBE FAIL\n");
    return failures == 0 ? 0 : 1;
}
