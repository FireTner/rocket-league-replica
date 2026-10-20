// probe_camera_target.cpp — R1 probe for CAMERA_PORT_PROMPT #5
// (one-euro look-target filter).
//
// Port of upstream docs/carballer-facts/rl_camera.cpp:466-489 behavior
// against OUR src/camera/Camera.h API (60 Hz Update).
//
// Bug (current, src/camera/Camera.cpp): the ballcam look target tracks the
// slewed aim (#1) rigidly (center_ = tgt every frame), so the ball's resting
// contact micro-chatter (several small bounces/second while dribbling)
// wobbles the whole view frame-to-frame ("car rumble" while dribbling).
// The car-cam target rides the stiffness follower instead. Upstream: the
// MIXED look target passes through a one-euro filter — cutoff
// fc = 1.2 + 1.8*(1-blend) + 0.01*derivS Hz (derivS = 3 Hz EMA of target
// speed): chatter keeps the cutoff low, swivels/flicks snap it open fast.
// Car-cam (blend 0) tracks tightly at a 3.0 Hz floor; ballcam (blend 1)
// keeps the 1.2 Hz floor that kills the resting ball's contact chatter.
//
// Scenarios (default stiffness 0.45, 60 Hz Update):
//  T1: ballcam micro-step — settled, ball teleports +8 uu laterally.
//      Fixed: first-frame center step is a FRACTION of the settled step
//      (one-euro k = 1-exp(-2π·fc·dt) < 1). Broken (rigid): ratio == 1.
//  T2: ballcam chatter — settled, ball bobs ±3 uu at 6 Hz for 2 s.
//      Fixed: center peak-to-peak is attenuated vs the rigid mapping.
//      Broken: center reproduces the full rigid swing every frame.
//  T3: rest convergence guard — static ball settles exactly (both pass).
//  T4: real motion passes — 600 uu teleport converges fast (both pass;
//      guards against over-filtering).
//
// Build: g++ -std=c++20 -Isrc tools/probe_camera_target.cpp src/camera/Camera.cpp -lGL -o /tmp/probe_target && /tmp/probe_target
// No SDL, no GL context (Update/Eye/Center never touch GL).
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

static float dist3(const float a[3], const float b[3]) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

int main() {
    std::printf("probe_camera_target #5 (R1 probe: one-euro look-target filter)\n");
    const float dt = 1.0f / 60.0f;

    // T1: micro-step response — first-frame fraction of the settled step.
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {400, 0, 110};
        for (int i = 0; i < 90; ++i)
            c.Update(dt, car, 0.0f, ball, true);
        float c0[3];
        c.Center(c0);
        ball[1] += 8.0f;  // one-frame lateral micro-step (dribble bounce)
        c.Update(dt, car, 0.0f, ball, true);
        float c1[3];
        c.Center(c1);
        float d1 = dist3(c1, c0);
        for (int i = 0; i < 120; ++i)
            c.Update(dt, car, 0.0f, ball, true);
        float c2[3];
        c.Center(c2);
        float dTot = dist3(c2, c0);
        float ratio = dTot > 1e-6f ? d1 / dTot : 0.0f;
        char detail[160];
        std::snprintf(detail, sizeof(detail),
                       "first-frame %.2f uu of %.2f uu settled (ratio %.3f)",
                       d1, dTot, ratio);
        std::printf("[T1 micro-step] %s\n", detail);
        CHECK(dTot > 1.0f, "micro-step settles to a real displacement", detail);
        CHECK(ratio < 0.85f, "first frame takes a fraction (filtered)", detail);
    }

    // T2: sustained chatter — 6 Hz ±3 uu lateral bob, center swing attenuated.
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {400, 0, 110};
        for (int i = 0; i < 90; ++i)
            c.Update(dt, car, 0.0f, ball, true);
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        for (int i = 0; i < 120; ++i) {  // 2 s of chatter
            float t = (90 + i) * dt;
            ball[1] = 3.0f * std::sin(2.0f * 3.14159265f * 6.0f * t);
            c.Update(dt, car, 0.0f, ball, true);
            float cc[3];
            c.Center(cc);
            for (int k = 0; k < 3; ++k) {
                if (cc[k] < lo[k]) lo[k] = cc[k];
                if (cc[k] > hi[k]) hi[k] = cc[k];
            }
        }
        float swing = dist3(hi, lo);
        char detail[160];
        std::snprintf(detail, sizeof(detail),
                       "center peak-to-peak %.2f uu under +-3 uu / 6 Hz bob",
                       swing);
        std::printf("[T2 chatter] %s\n", detail);
        // Rigid mapping reproduces the bob amplified through the
        // eye+dir*max(len,1000) projection (~2.5x at this range ≈ 15 uu);
        // the one-euro floor must hold it clearly below that.
        CHECK(swing < 10.0f, "chatter swing attenuated (< 10 uu)", detail);
    }

    // T3: rest convergence guard — static scene settles onto the aim.
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(dt, car, 0.0f, ball, true);
        for (int i = 0; i < 90; ++i)
            c.Update(dt, car, 0.0f, ball, true);
        float e[3], cc[3];
        c.Eye(e);
        c.Center(cc);
        // Rebuild the exact aim: center must lie on the eye->ball ray.
        float dx = ball[0] - e[0], dy = ball[1] - e[1], dz = ball[2] - e[2];
        float ex = cc[0] - e[0], ey = cc[1] - e[1], ez = cc[2] - e[2];
        float dl = std::sqrt(dx*dx + dy*dy + dz*dz);
        float el = std::sqrt(ex*ex + ey*ey + ez*ez);
        float cosA = (dx*ex + dy*ey + dz*ez) / (dl * el);
        char detail[160];
        std::snprintf(detail, sizeof(detail), " settled cos(angle) %.6f", cosA);
        std::printf("[T3 rest aim] %s\n", detail);
        CHECK(cosA > 0.9999f, "settled target on the eye->ball ray", detail);
    }

    // T4: real motion passes — 600 uu teleport converges within 1.5 s.
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {400, 0, 110};
        for (int i = 0; i < 90; ++i)
            c.Update(dt, car, 0.0f, ball, true);
        ball[0] += 600.0f;
        ball[1] += 200.0f;
        for (int i = 0; i < 90; ++i)
            c.Update(dt, car, 0.0f, ball, true);
        float e[3], cc[3];
        c.Eye(e);
        c.Center(cc);
        float dx = ball[0] - e[0], dy = ball[1] - e[1], dz = ball[2] - e[2];
        float ex = cc[0] - e[0], ey = cc[1] - e[1], ez = cc[2] - e[2];
        float dl = std::sqrt(dx*dx + dy*dy + dz*dz);
        float el = std::sqrt(ex*ex + ey*ey + ez*ez);
        float cosA = (dx*ex + dy*ey + dz*ez) / (dl * el);
        char detail[160];
        std::snprintf(detail, sizeof(detail), "cos(angle) %.6f after 1.5 s",
                      cosA);
        std::printf("[T4 teleport] %s\n", detail);
        CHECK(cosA > 0.999f, "teleport re-aims within 1.5 s", detail);
    }

    std::printf(failures == 0 ? "PROBE PASS\n" : "PROBE FAIL\n");
    return failures == 0 ? 0 : 1;
}
