// probe_camera.cpp — R1 probe for CAMERA_PORT_PROMPT #1 (ball-aim slew).
// Port of upstream tools/probe_camera.cpp degenerate cases (see
// docs/carballer-facts/rl_camera.cpp:62-74 — overhead crossing moved eye
// 516 uu / 75 deg in one frame pre-fix, roof 225/50, overhead drift 534/95).
// Against OUR src/camera/Camera.h API (carPos/carYaw/ballPos, 60 Hz Update).
//
// Build: g++ -std=c++20 -I src tools/probe_camera.cpp src/camera/Camera.cpp -lGL -o /tmp/probe_cam && /tmp/probe_cam
// No SDL, no GL context (Update/Eye/Center never touch GL).
//
// #1 PASS criteria (post-fix): max single-frame eye jump < 150 uu AND view
// turn < 25 deg in scenarios A–C. At kBallAimSlew=20 rad/s, one 60 Hz frame
// slews at most 0.333 rad (19 deg) => eye moves ~90 uu on our 270-dist rig,
// so 150 uu / 25 deg separates pre-fix snap (~500 uu / 75+ deg) from fixed.
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

static float eyeDist(const float a[3], const float b[3]) {
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static float viewTurnDeg(const float e0[3], const float c0[3],
                         const float e1[3], const float c1[3]) {
    float d0[3] = {c0[0] - e0[0], c0[1] - e0[1], c0[2] - e0[2]};
    float d1[3] = {c1[0] - e1[0], c1[1] - e1[1], c1[2] - e1[2]};
    float l0 = std::sqrt(d0[0]*d0[0]+d0[1]*d0[1]+d0[2]*d0[2]);
    float l1 = std::sqrt(d1[0]*d1[0]+d1[1]*d1[1]+d1[2]*d1[2]);
    if (l0 < 1e-6f || l1 < 1e-6f) return 0.0f;
    float dot = (d0[0]*d1[0]+d0[1]*d1[1]+d0[2]*d1[2]) / (l0*l1);
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    return std::acos(dot) * 180.0f / 3.14159265f;
}

// Drive one scenario: warm up on ballA, then step to ballB in a single
// 60 Hz frame and measure the eye jump + view turn.
static void runCase(const char* name, const float ballA[3], const float ballB[3]) {
    rl::Camera c;
    float car[3] = {0, 0, 17};
    const float dt = 1.0f / 60.0f;
    c.Update(dt, car, 0.0f, ballA, true);
    float e0[3], c0[3];
    c.Eye(e0); c.Center(c0);
    c.Update(dt, car, 0.0f, ballB, true);
    float e1[3], c1[3];
    c.Eye(e1); c.Center(c1);
    float jump = eyeDist(e0, e1);
    float turn = viewTurnDeg(e0, c0, e1, c1);
    char detail[128];
    std::snprintf(detail, sizeof(detail), "eye jump %.1f uu, view turn %.1f deg", jump, turn);
    std::printf("[%s] %s\n", name, detail);
    CHECK(jump < 150.0f, "single-frame eye jump < 150 uu", detail);
    CHECK(turn < 25.0f, "single-frame view turn < 25 deg", detail);
}

int main() {
    std::printf("probe_camera #1+#2 (R1 probes: ball-aim slew, air-velocity heading)\n");
    // A: overhead crossing — ball jumps from +60 to -60 uu horizontal
    // (both outside the 5 uu hold, so the raw atan2 bearing flips ~180 deg
    // in one frame). Pre-fix: eye teleports ~540 uu / ~173 deg.
    {
        float a[3] = {60, 0, 150}, b[3] = {-60, 0, 150};
        runCase("A overhead crossing", a, b);
    }
    // B: dribble teleport — ball on the roof (aim holds) drops ahead into
    // the ball-axis rig. Pre-fix: same ~540 uu snap.
    {
        float a[3] = {0, 0, 80}, b[3] = {200, 0, 80};
        runCase("B dribble teleport", a, b);
    }
    // C: overhead drift — ball swings past the nose at grazing range
    // (+60,+60 -> -60,+60, bearing sweeps ~90 deg in one frame).
    // Pre-fix: rigid snap; fixed: slewed ~19 deg/frame.
    {
        float a[3] = {60, 60, 300}, b[3] = {-60, 60, 300};
        runCase("C overhead drift", a, b);
    }
    // D: air flip (#2) — airborne, velocity +X at 900 uu/s, nose +Y
    // (carFwd +Y, scalar yaw PI/2). Fixed: eye settles behind the VELOCITY
    // (-X, < 25 deg off). Broken (nose-rigid): eye sits behind the NOSE
    // (-Y, 90 deg off). Needs the 12-arg API: the legacy call carries no
    // velocity/airborne input, so it cannot select the air branch at all.
    {
        rl::Camera c;
        float car[3] = {0, 0, 500}, ball[3] = {2000, 0, 500};
        float fwd[3] = {0, 1, 0}, vel[3] = {900, 0, 0};
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 90; ++i)  // 1.5 s to settle
            c.Update(dt, car, kPi / 2.0f, ball, false, 0.0f, false, 0.0f,
                     0.0f, fwd, vel, true);
        float e[3];
        c.Eye(e);
        float ox = e[0] - car[0], oy = e[1] - car[1];
        float ol = std::sqrt(ox * ox + oy * oy);
        float align = (ol > 1e-6f) ? (-ox / ol) : 0.0f;  // behind -X?
        if (align > 1.0f) align = 1.0f;
        if (align < -1.0f) align = -1.0f;
        float offDeg = std::acos(align) * 180.0f / kPi;
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "eye offset (%.0f, %.0f), %.1f deg off behind-velocity",
                      ox, oy, offDeg);
        std::printf("[D air flip] %s\n", detail);
        CHECK(offDeg < 25.0f, "settled eye sits behind velocity (< 25 deg)",
              detail);
    }
    // E: pitch through vertical (#2) — airborne, near-zero horizontal speed
    // so the facing axis rules (w=0), nose vector pitching 0 -> 120 deg over
    // 0.5 s. The scalar bearing flips 0 -> PI mid-sweep; fixed: half-turn
    // continuity holds the axis and the eye travels < 100 uu. Broken: the
    // eye orbits 180 deg around the car (~540+ uu). The vector input is
    // essential: past 90 deg cos(pitch) flips the bearing with no yaw turn.
    {
        rl::Camera c;
        float car[3] = {0, 0, 500}, ball[3] = {2000, 0, 500};
        float vel[3] = {0, 0, 0};
        const float dt = 1.0f / 60.0f;
        float fwd0[3] = {1, 0, 0};
        c.Update(dt, car, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f, fwd0,
                 vel, true);
        float prev[3];
        c.Eye(prev);
        float travel = 0.0f;
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
        CHECK(travel < 100.0f, "pure-pitch sweep holds eye (< 100 uu)",
              detail);
    }
    std::printf(failures == 0 ? "PROBE PASS\n" : "PROBE FAIL\n");
    return failures == 0 ? 0 : 1;
}
