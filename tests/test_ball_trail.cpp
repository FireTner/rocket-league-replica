// Ball-trail colour, soft-touch, fade, and triangle cost.
// Offscreen GL. The speed floor is FX::kBallTrailSpeedFloor (UNVERIFIED).
// Soft and hard speeds are the contacts measured by test_last_touch.
#include <cstdint>
#include <cstdio>
#include <vector>

#include <GL/gl.h>

#include "camera/Camera.h"
#include "fx/FX.h"
#include "render/Renderer.h"

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            std::printf("  ok   %s\n", msg);                          \
        } else {                                                      \
            std::printf("  FAIL %s\n", msg);                          \
            ++failures;                                               \
        }                                                             \
    } while (0)

// test_last_touch.cpp, seed 1, fallback arena.
// Soft: throttle 0.25, no boost, car started at y=-220.
// Hard: throttle 1 + boost, car started at y=-2000. Peak over the hit
// tick and the next 15 ticks.
static constexpr float kSoftSpeed = 386.6f;
static constexpr float kHardSpeed = 2797.6f;
static constexpr int kW = 640;
static constexpr int kH = 360;

struct Pixels {
    int blue = 0;
    int orange = 0;
};

static Pixels Count(rl::Renderer& r) {
    std::vector<unsigned char> px(size_t(kW) * size_t(kH) * 3);
    glReadPixels(0, 0, kW, kH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    Pixels c;
    for (size_t i = 0; i < px.size(); i += 3) {
        int pr = px[i], pg = px[i + 1], pb = px[i + 2];
        if (pb > pr + 25 && pb > pg && pb > 50) c.blue++;
        if (pr > pb + 40 && pr > pg + 15 && pr > 50) c.orange++;
    }
    (void)r;
    return c;
}

struct Frame {
    Pixels px;
    uint64_t tris = 0;
    uint64_t draws = 0;
};

static Frame Draw(rl::Renderer& r, rl::Camera& cam, rl::FX& fx, rl::Snapshot& s) {
    r.BeginFrame(0, 0, 0);
    cam.Apply(kW, kH);
    fx.DrawBallTrail(r, s);
    Frame f;
    f.px = Count(r);
    r.EndFrame();
    rl::Renderer::Counters st = r.Stats3D();
    f.tris = st.tris;
    f.draws = st.drawCalls;
    return f;
}

int main() {
    std::printf("ball-trail\n");
    CHECK(kSoftSpeed < rl::FX::kBallTrailSpeedFloor, "soft speed is under the floor");
    CHECK(kHardSpeed > rl::FX::kBallTrailSpeedFloor, "hard speed is over the floor");
    CHECK(rl::FX::kBallTrailSpeedFloor != 2200.0f, "floor is not the car supersonic line");
    std::printf("  floor=%.0f (UNVERIFIED) soft=%.1f hard=%.1f\n",
                rl::FX::kBallTrailSpeedFloor, kSoftSpeed, kHardSpeed);

    rl::Renderer renderer;
    if (!renderer.Init(kW, kH, false)) {
        std::printf("BALL TRAIL FAIL (no GL context)\n");
        return 1;
    }
    renderer.SetLight(false);
    rl::Camera cam;
    const float eye[3] = {128.0f, -450.0f, 160.0f};
    const float center[3] = {128.0f, 0.0f, 120.0f};
    cam.SetFreeView(eye, center);

    const float step = kHardSpeed / 120.0f;
    rl::FX fx;
    rl::Snapshot s;
    s.ballTouched = true;
    s.ballTeam = 0;
    s.ballSpeed = kHardSpeed;
    s.ballPos[1] = 0;
    s.ballPos[2] = 120;
    Frame hard;
    for (int t = 1; t <= 8; t++) {
        s.tick = uint32_t(t);
        s.ballPos[0] = step * float(t);
        hard = Draw(renderer, cam, fx, s);
    }
    std::printf("  HARD blue=%d orange=%d tris=%llu draws=%llu\n",
                hard.px.blue, hard.px.orange,
                (unsigned long long)hard.tris, (unsigned long long)hard.draws);
    CHECK(hard.px.blue > 30 && hard.px.orange == 0, "hard hit is blue team paint");
    CHECK(hard.tris == 20 && hard.draws == 1, "fast shot is one 20-tri streak");

    s.ballTeam = 1;
    Frame switched;
    for (int t = 9; t <= 10; t++) {
        s.tick = uint32_t(t);
        s.ballPos[0] = step * float(t);
        switched = Draw(renderer, cam, fx, s);
    }
    std::printf("  SWITCH blue=%d orange=%d\n", switched.px.blue, switched.px.orange);
    CHECK(switched.px.orange > 30 && switched.px.blue == 0,
          "last touch orange recolours the streak");

    s.ballSpeed = kSoftSpeed;
    Frame faded;
    for (int t = 11; t <= 34; t++) {
        s.tick = uint32_t(t);
        faded = Draw(renderer, cam, fx, s);
    }
    std::printf("  FADE blue=%d orange=%d tris=%llu (after %.0f ticks under %.1f)\n",
                faded.px.blue, faded.px.orange,
                (unsigned long long)faded.tris, 24.0, kSoftSpeed);
    CHECK(faded.px.blue == 0 && faded.px.orange == 0 && faded.tris == 0,
          "streak is gone after the ball stays under the floor");

    rl::FX softFx;
    s.ballTouched = true;
    s.ballTeam = 0;
    s.ballSpeed = kSoftSpeed;
    s.ballPos[2] = 120;
    Frame soft;
    const float softStep = kSoftSpeed / 120.0f;
    for (int t = 1; t <= 8; t++) {
        s.tick = uint32_t(t);
        s.ballPos[0] = softStep * float(t);
        soft = Draw(renderer, cam, softFx, s);
    }
    std::printf("  SOFT blue=%d orange=%d tris=%llu draws=%llu speed=%.1f\n",
                soft.px.blue, soft.px.orange,
                (unsigned long long)soft.tris, (unsigned long long)soft.draws, kSoftSpeed);
    CHECK(soft.px.blue == 0 && soft.px.orange == 0 && soft.tris == 0 && soft.draws == 0,
          "soft touch draws nothing");

    rl::FX none;
    s.ballTouched = false;
    s.ballSpeed = kHardSpeed;
    Frame untouched;
    for (int t = 1; t <= 8; t++) {
        s.tick = uint32_t(t);
        s.ballPos[0] = step * float(t);
        untouched = Draw(renderer, cam, none, s);
    }
    std::printf("  UNTOUCHED blue=%d orange=%d tris=%llu\n",
                untouched.px.blue, untouched.px.orange,
                (unsigned long long)untouched.tris);
    CHECK(untouched.px.blue == 0 && untouched.tris == 0, "no trail before a touch");

    if (failures) {
        std::printf("BALL TRAIL FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("BALL TRAIL PASS\n");
    return 0;
}
