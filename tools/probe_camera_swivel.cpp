// probe_camera_swivel.cpp — R1 probe for CAMERA_PORT_PROMPT #4
// (sphere-orbit swivel + snap-back).
//
// Port of upstream docs/carballer-facts/rl_camera.cpp:101-121 (placeEye:
// sphere orbit R=hypot(dist,height), elev=e0-pitchOff, floor clamp via asin,
// elev clamp +/-1.5, unabsorbed pitch returned as tilt) and :373-396
// (swivel clamp -0.8/+0.7, snap-back at 2x rate after 0.05 s idle gated by
// `snap`). Against OUR src/camera/Camera.h API (60 Hz Update).
//
// Ours today: orbit exists but pitch clamp is +/-1.0 rad, release dies back
// exponentially (6/s, 0.05 deadzone) with no snap setting, the eye floor is
// a flat want[2]<40 clamp, and unabsorbed pitch is dropped (no tilt).
//
// Build: g++ -std=c++20 -I src tools/probe_camera_swivel.cpp src/camera/Camera.cpp -lGL -o /tmp/probe_swivel && /tmp/probe_swivel
// No SDL, no GL context (Update/Eye/Center never touch GL).
//
// PASS criteria (post-fix):
//  S1/S2: after release, swivel offsets snap back to EXACTLY 0 quickly
//         (linear 2x-rate return zeroes 0.7 rad in ~0.06 s at speed 5;
//         exponential 6/s still holds ~0.16 rad at 0.25 s and never
//         exact-zeroes before ~1.7 s).
//  S3:    pitch clamp is asymmetric -0.8/+0.7 (upstream), not +/-1.0.
//  S4:    held look-up from the ground rides the eye at the floor (~16 uu,
//         not the flat 40) and the unabsorbed pitch reaches the view as
//         tilt (view pitches up > 0.15 rad vs the no-swivel pose).
//  S5:    rest geometry with no swivel is unchanged (regression guard,
//         passes before AND after).
#include <cmath>
#include <cstdio>

#include "camera/Camera.h"

static constexpr float kPi = 3.14159265f;
static constexpr float kDt = 1.0f / 60.0f;

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

static float viewPitch(const float e[3], const float c[3]) {
    float dx = c[0] - e[0], dy = c[1] - e[1], dz = c[2] - e[2];
    return std::atan2(dz, std::sqrt(dx * dx + dy * dy));
}

int main() {
    std::printf("probe_camera_swivel #4 (R1: sphere-orbit swivel + snap-back)\n");
    float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};

    // S1: pitch snap-back — full look-up 0.5 s, then release 0.6 s.
    // Post: linear 2x return zeroes the offset in a few frames (exact 0).
    // Pre: exp(-6/s) die-back still holds ~0.03 rad at 0.6 s.
    {
        rl::Camera c;
        c.Update(kDt, car, 0.0f, ball, true);
        for (int i = 0; i < 30; ++i)
            c.Update(kDt, car, 0.0f, ball, true, 0.0f, false, 0.0f, 1.0f);
        float held = c.SwivelPitch();
        for (int i = 0; i < 36; ++i)
            c.Update(kDt, car, 0.0f, ball, true);
        float back = c.SwivelPitch();
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "held %.3f rad, 0.6 s after release %.4f rad",
                      held, back);
        std::printf("[S1 pitch snap-back] %s\n", detail);
        CHECK(held > 0.5f, "held look-up reaches the clamp region", detail);
        CHECK(back == 0.0f, "released pitch snaps exactly to 0", detail);
    }

    // S2: yaw snap-back — full-right 0.3 s, then release 0.4 s.
    // Post: exact 0. Pre: ~0.16 rad remains.
    {
        rl::Camera c;
        c.Update(kDt, car, 0.0f, ball, true);
        for (int i = 0; i < 18; ++i)
            c.Update(kDt, car, 0.0f, ball, true, 0.0f, false, 1.0f, 0.0f);
        float held = c.SwivelYaw();
        for (int i = 0; i < 24; ++i)
            c.Update(kDt, car, 0.0f, ball, true);
        float back = c.SwivelYaw();
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "held %.3f rad, 0.4 s after release %.4f rad",
                      held, back);
        std::printf("[S2 yaw snap-back] %s\n", detail);
        CHECK(held > 1.0f, "held yaw swivels well off-center", detail);
        CHECK(back == 0.0f, "released yaw snaps exactly to 0", detail);
    }

    // S3: asymmetric pitch clamp -0.8/+0.7 (upstream placeEye/swivel).
    // Pre: symmetric +/-1.0.
    {
        rl::Camera c;
        c.Update(kDt, car, 0.0f, ball, true);
        for (int i = 0; i < 60; ++i)
            c.Update(kDt, car, 0.0f, ball, true, 0.0f, false, 0.0f, -1.0f);
        float down = c.SwivelPitch();
        rl::Camera c2;
        c2.Update(kDt, car, 0.0f, ball, true);
        for (int i = 0; i < 60; ++i)
            c2.Update(kDt, car, 0.0f, ball, true, 0.0f, false, 0.0f, 1.0f);
        float up = c2.SwivelPitch();
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "down %.3f rad (want -0.8), up %.3f rad (want +0.7)",
                      down, up);
        std::printf("[S3 pitch clamp] %s\n", detail);
        CHECK(std::fabs(down - -0.8f) < 0.02f, "look-down clamps at -0.8",
              detail);
        CHECK(std::fabs(up - 0.7f) < 0.02f, "look-up clamps at +0.7",
              detail);
    }

    // S4: floor ride + tilt — grounded, held look-up 1 s (settled).
    // Post: eye rides ~16 uu (asin floor clamp) and the unabsorbed pitch
    // reaches the view as tilt (view up > 0.15 rad vs no-swivel).
    // Pre: flat eye clamp at exactly 40, no tilt.
    {
        rl::Camera c;
        c.Update(kDt, car, 0.0f, ball, true);
        for (int i = 0; i < 60; ++i)
            c.Update(kDt, car, 0.0f, ball, true);
        float e0[3], c0[3];
        c.Eye(e0);
        c.Center(c0);
        float p0 = viewPitch(e0, c0);
        for (int i = 0; i < 60; ++i)
            c.Update(kDt, car, 0.0f, ball, true, 0.0f, false, 0.0f, 1.0f);
        float e1[3], c1[3];
        c.Eye(e1);
        c.Center(c1);
        float p1 = viewPitch(e1, c1);
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "eye z %.1f uu (want ~16), view pitch %+.3f -> %+.3f rad",
                      e1[2], p0, p1);
        std::printf("[S4 floor ride + tilt] %s\n", detail);
        CHECK(e1[2] < 30.0f && e1[2] > 8.0f,
              "held look-up rides the eye at the floor (~16 uu)", detail);
        CHECK(p1 - p0 > 0.15f,
              "unabsorbed pitch reaches the view as tilt", detail);
    }

    // S5: rest geometry guard — no swivel input, eye/center identical to
    // the pre-#4 build (passes before AND after; fails on any regression).
    {
        rl::Camera c;
        float b[3] = {1000, 0, 100};
        c.Update(kDt, car, 0.0f, b, true);
        float eye[3], ctr[3];
        c.Eye(eye);
        c.Center(ctr);
        char detail[128];
        std::snprintf(detail, sizeof(detail), "eye (%.2f, %.2f, %.2f)",
                      eye[0], eye[1], eye[2]);
        std::printf("[S5 rest geometry] %s\n", detail);
        CHECK(std::fabs(eye[0] - -269.63f) < 0.05f &&
                  std::fabs(eye[1]) < 0.01f &&
                  std::fabs(eye[2] - 131.13f) < 0.05f,
              "ballcam rest eye == base geometry", detail);
        CHECK(ctr[0] == b[0] && ctr[1] == b[1] && ctr[2] == b[2],
              "ball target locks exactly", detail);
        (void)kPi;
    }

    std::printf(failures == 0 ? "PROBE PASS\n" : "PROBE FAIL\n");
    return failures == 0 ? 0 : 1;
}
