// probe_stiffness.cpp — R1 probe for CAMERA_PORT_PROMPT #3 (stiffness world-space lag).
// Port of upstream rl_camera.cpp:413-464 behavior against OUR src/camera/Camera.h.
//
// Bug (current): ballcam rigid + car-cam-only lag at 2+18*stiffness with no
// rig-speed saturation and no air vertical halving. Consequences:
//  F: boost run drags the eye ~v/rate (≈228 uu at 2300 uu/s, stiff 0.45).
//  G: ballcam jump shows zero linger (eye rides the car 1:1 vertically).
//  H: car-cam vertical follow is identical ground vs air (no 0.5x air halving).
//
// Post-fix (upstream mechanism): followRate = 1.5+14*stiffness, vertical halved
// in air, horizontal rate += rigSpeed2d/40 (kRigLagCap), vertical rate +=
// |rigVz|/40 where rigVz excludes the car's own motion (wantEye.z - carPos.z).
// Expected: F lag saturates ≈35 uu (<60), G first-frame eye dz ≈30 uu (<150),
// H air step ≈0.5x ground step.
//
// Build: g++ -std=c++20 -Isrc tools/probe_stiffness.cpp src/camera/Camera.cpp -lGL -o /tmp/probe_stiff && /tmp/probe_stiff
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

int main() {
    std::printf("probe_stiffness #3 (R1 probe)\n");
    const float dt = 1.0f / 60.0f;

    // F: boost run — car-cam, car driving +X at 2300 uu/s, carSpeed=0 to
    // isolate the eye follower from the Rocket-Science distance kick.
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {5000, 0, 100};
        float fwd[3] = {1, 0, 0}, vel[3] = {2300, 0, 0};
        c.Update(dt, car, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f,
                 fwd, vel, true);
        for (int i = 0; i < 300; ++i) {
            car[0] += 2300.0f * dt;  // 38.33 uu/frame
            c.Update(dt, car, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f,
                     fwd, vel, true);
        }
        float e[3];
        c.Eye(e);
        // Ideal eye: car - 269.63 (rest backS) at same height band.
        float idealX = car[0] - 269.63f;
        float lag = idealX - e[0];  // >0 = eye trails behind ideal
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "trail lag %.1f uu (eye %.0f vs ideal %.0f)", lag, e[0],
                      idealX);
        std::printf("[F boost run] %s\n", detail);
        CHECK(lag < 60.0f, "boost eye trails < 60 uu (saturates at 40)", detail);
    }

    // G: ballcam jump linger — ballcam, car teleports +500 z (jump/aerial).
    // Rigid ballcam moves the eye the full 500 in one frame; world-space lag
    // moves it only k*500 (~30 uu air, first frame).
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};
        c.Update(dt, car, 0.0f, ball, true);
        float e0[3];
        c.Eye(e0);
        car[2] = 517.0f;
        float fwd[3] = {1, 0, 0}, vel[3] = {0, 0, 500};
        c.Update(dt, car, 0.0f, ball, true, 0.0f, false, 0.0f, 0.0f,
                 fwd, vel, false);
        float e1[3];
        c.Eye(e1);
        float dz = e1[2] - e0[2];
        char detail[128];
        std::snprintf(detail, sizeof(detail), "first-frame eye dz %.1f uu", dz);
        std::printf("[G ballcam jump linger] %s\n", detail);
        CHECK(dz < 150.0f, "ballcam jump lingers (eye dz < 150 uu)", detail);
    }

    // H: air vertical halving — car-cam, same +200 z step ground vs air.
    // First-frame eye dz in air should be ~half the ground step.
    {
        float dzGround = 0, dzAir = 0;
        {
            rl::Camera c;
            float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};
            float fwd[3] = {1, 0, 0}, v0[3] = {0, 0, 0};
            c.Update(dt, car, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f,
                     fwd, v0, true);
            float e0[3];
            c.Eye(e0);
            car[2] = 217.0f;
            c.Update(dt, car, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f,
                     fwd, v0, true);
            float e1[3];
            c.Eye(e1);
            dzGround = e1[2] - e0[2];
        }
        {
            rl::Camera c;
            float car[3] = {0, 0, 500}, ball[3] = {2000, 0, 500};
            float fwd[3] = {1, 0, 0}, v0[3] = {0, 0, 0};
            c.Update(dt, car, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f,
                     fwd, v0, false);
            float e0[3];
            c.Eye(e0);
            car[2] = 700.0f;
            c.Update(dt, car, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f,
                     fwd, v0, false);
            float e1[3];
            c.Eye(e1);
            dzAir = e1[2] - e0[2];
        }
        char detail[128];
        std::snprintf(detail, sizeof(detail), "ground dz %.1f vs air dz %.1f",
                      dzGround, dzAir);
        std::printf("[H air vertical halving] %s\n", detail);
        CHECK(dzAir < dzGround * 0.75f, "air vertical follow < 0.75x ground",
              detail);
    }

    std::printf(failures == 0 ? "PROBE PASS\n" : "PROBE FAIL\n");
    return failures == 0 ? 0 : 1;
}
