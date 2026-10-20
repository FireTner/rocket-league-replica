// probe_camera_transition.cpp — R1 probe for CAMERA_PORT_PROMPT #6
// (transition re-time).
//
// Upstream docs/carballer-facts/rl_camera.cpp:398-403 blends ballcam<->carcam
// at tRate = 0.5 + 2.5*transitionSpeed units/s, so the default 1.2 swings in
// ~0.29 s (1/3.5). Ours (src/camera/Camera.cpp) advances at transitionSpeed
// units/s flat, so 1.2 swings in ~0.83 s — floaty.
//
// Scenarios (default transition_speed 1.2, 60 Hz Update):
//  R1: first-frame blend step after a ballcam->carcam toggle. Fixed:
//      step = (0.5+2.5*1.2)/60 ~= 0.0583. Broken (flat speed): 1.2/60 = 0.02.
//  R2: full-swing time. Fixed: blend hits 0 within ~0.35 s (<= 25 frames).
//      Broken: needs ~50 frames (~0.83 s).
//  R3: range guard — speed 2.0 finishes faster than 1.0 (both pass; guards
//      against hardcoding the 1.2 rate).
//
// Build: g++ -std=c++20 -Isrc tools/probe_camera_transition.cpp src/camera/Camera.cpp -lGL -o /tmp/probe_trans && /tmp/probe_trans
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

static int framesToComplete(float speed) {
    rl::Camera c;
    c.Params().transitionSpeed = speed;
    float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
    const float dt = 1.0f / 60.0f;
    c.Update(dt, car, 0.0f, ball, true);
    int n = 0;
    for (int i = 0; i < 120; ++i) {
        c.Update(dt, car, 0.0f, ball, false);
        ++n;
        if (c.Blend() == 0.0f) break;
    }
    return n;
}

int main() {
    std::printf("probe_camera_transition #6 (R1 probe: transition re-time)\n");
    const float dt = 1.0f / 60.0f;

    // R1: first-frame step at the default 1.2.
    {
        rl::Camera c;  // default transition_speed == 1.2
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(dt, car, 0.0f, ball, true);
        c.Update(dt, car, 0.0f, ball, false);
        float step = 1.0f - c.Blend();
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "first-frame blend step %.4f (upstream 0.0583, flat 0.0200)",
                      step);
        std::printf("[R1 step] %s\n", detail);
        CHECK(step > 0.045f && step < 0.075f,
              "toggle steps at 0.5+2.5*speed rate", detail);
    }

    // R2: full-swing time at the default 1.2.
    {
        int n = framesToComplete(1.2f);
        char detail[128];
        std::snprintf(detail, sizeof(detail), "%d frames (%.2f s)", n,
                      n / 60.0f);
        std::printf("[R2 swing] %s\n", detail);
        CHECK(n <= 25, "full swing completes within ~0.35 s at 1.2", detail);
    }

    // R3: range guard — 2.0 finishes faster than 1.0.
    {
        int fast = framesToComplete(2.0f);
        int slow = framesToComplete(1.0f);
        char detail[128];
        std::snprintf(detail, sizeof(detail), "2.0: %d frames, 1.0: %d frames",
                      fast, slow);
        std::printf("[R3 range] %s\n", detail);
        CHECK(fast < slow, "higher transition_speed finishes faster", detail);
        CHECK(slow <= 40, "slow end (1.0) still completes within ~0.67 s",
              detail);
    }

    std::printf(failures == 0 ? "PROBE PASS\n" : "PROBE FAIL\n");
    return failures == 0 ? 0 : 1;
}
