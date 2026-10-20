// probe_camera_stiffness.cpp — R1 probe for CAMERA_PORT_PROMPT #3
// (stiffness world-space lag). Port of upstream rl_camera.cpp:413-464
// behavior against OUR src/camera/Camera.h API.
//
// Bug (current): ballcam eye is RIGID (stiffness only breathes
// distance/angle/FOV); car-cam lag is a single rate 2+18*stiffness with no
// rig-speed saturation, so boost drags the eye ~v/rate behind the car.
// Upstream: the MIXED ideal eye passes through stiffness smoothing with
// followRate = 1.5+14*stiffness, vertical halved in the air, rig-speed
// saturation (kRigLagCap = 40 uu) so boost-run lag saturates instead of
// growing, and the swivel's own vertical motion split out (rig-relative Z)
// so jumps/aerials keep their slow linger.
//
// Scenarios (default stiffness 0.45, 60 Hz Update):
//  F: boost run — car-cam, car drives +X at 2300 uu/s (supersonic
//     dynamics on). Fixed: horizontal trail saturates (< 80 uu).
//     Broken (car-cam-only 2+18s lag): trail settles at v/rate =
//     2300/10.1 ~= 228 uu. (Ballcam today is rigid = 0 trail, so this
//     runs car-cam where the old lag lives.)
//  G: jump linger — ballcam, airborne, car teleports +250z in one frame.
//     Fixed: the eye lingers (first-frame eye dz < 100 uu, slow kz from
//     the halved vertical rate). Broken (rigid ballcam): eye snaps the
//     full 250 uu in one frame.
//
// Build: g++ -std=c++20 -Isrc tools/probe_camera_stiffness.cpp src/camera/Camera.cpp -lGL -o /tmp/probe_stiff && /tmp/probe_stiff
// No SDL, no GL context (Update/Eye/Center never touch GL).
#include <cmath>
#include <cstdio>

#include "camera/Camera.h"

static constexpr float kPi = 3.14159265f;

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

int main() {
    std::printf("probe_camera_stiffness #3 (R1 probe: stiffness world-space lag)\n");
    const float dt = 1.0f / 60.0f;

    // F: boost run — car-cam, constant +X velocity 2300 uu/s.
    // Rigid-behind offset at supersonic (stiffness 0.45, Rocket-Science #2
    // dynamics, kept by #3): dynDist = 0.55*(100+14.7) = 63.085,
    // effDist = 333.085, effAng = -1.9 deg -> back = 332.9 uu.
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {5000, 0, 100};
        constexpr float kSpeed = 2300.0f;
        constexpr float kRigidBack = 332.9f;
        for (int i = 0; i < 240; ++i) {  // 4 s: blend + lag fully settle
            car[0] += kSpeed * dt;
            c.Update(dt, car, 0.0f, ball, false, kSpeed, true);
        }
        float e[3];
        c.Eye(e);
        float trail = (car[0] - e[0]) - kRigidBack;
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                       "steady-state boost trail %.1f uu (rigid back %.1f)",
                       trail, kRigidBack);
        std::printf("[F boost run] %s\n", detail);
        CHECK(trail < 80.0f, "boost trail saturates (< 80 uu)", detail);
    }

    // G: jump linger — ballcam, airborne, car teleports +250z one frame.
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};
        float fwd[3] = {1, 0, 0}, vel[3] = {0, 0, 600};
        c.Update(dt, car, 0.0f, ball, true, 0.0f, false, 0.0f, 0.0f,
                 fwd, vel, true);
        float e0[3];
        c.Eye(e0);
        car[2] += 250.0f;  // the jump: rig moves with the car in one frame
        c.Update(dt, car, 0.0f, ball, true, 0.0f, false, 0.0f, 0.0f,
                 fwd, vel, true);
        float e1[3];
        c.Eye(e1);
        float dz = e1[2] - e0[2];
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                       "first-frame eye dz %.1f uu on a 250 uu jump", dz);
        std::printf("[G jump linger] %s\n", detail);
        CHECK(dz < 100.0f, "jump eye lingers (first-frame dz < 100 uu)",
              detail);
    }

    std::printf(failures == 0 ? "PROBE PASS\n" : "PROBE FAIL\n");
    return failures == 0 ? 0 : 1;
}
