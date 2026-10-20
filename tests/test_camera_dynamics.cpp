// Camera dynamics contract test: RL-authentic stiffness / transition / swivel.
// Provenance (numbers only, no code copied):
//  - Rocket Science #2 (https://rocketscience.fyi/know/videos/camera-settings):
//    FOV 60..110; dynamic FOV +5 with speed +5 supersonic; stiffness =
//    dynamic distance (+100 max at 0; supersonic dist = dist+(1-stiff)*114.7)
//    + dynamic angle (+2 max at 0) + smoothing ONLY with ballcam off;
//    swivel 90 deg per 1.3 s at 1.0, linear.
//  - BakkesModSDK ProfileCameraSettings/CameraSave (field list only).
//  - CarBaller settings.h (RL ranges: swivel 1..10, transition 1..2).
// Build: g++ -std=c++20 -I src tests/test_camera_dynamics.cpp
//        src/camera/Camera.cpp -lGL -o /tmp/t_cam && /tmp/t_cam
// No SDL, no GL context needed (Update/Eye/Center never touch GL).
#include <cmath>
#include <cstdio>

#include "camera/Camera.h"

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

static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

int main() {
    std::printf("[1] compiled defaults (RL 1:1 + new sliders)\n");
    {
        rl::Camera c;
        CHECK(c.Params().transitionSpeed == 1.2f, "transition_speed == 1.2");
        CHECK(c.Params().swivelSpeed == 5.0f, "swivel_speed == 5.0");
        CHECK(c.Params().shake == true, "shake == true");
        CHECK(c.Params().invertSwivel == false, "invert_swivel == false");
        CHECK(c.Params().snap == true, "snap == true");
    }

    std::printf("[2] rest geometry unchanged (speed 0 -> zero dynamics)\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        float eye[3], ctr[3];
        c.Eye(eye);
        c.Center(ctr);
        // back = 270*cos(-3deg) = 269.63, up = 100-270*sin(-3deg) = 114.13
        CHECK(near(eye[0], -269.63f, 0.05f) && near(eye[1], 0.0f, 0.01f) &&
                  near(eye[2], 131.13f, 0.05f),
              "ballcam rest eye == base geometry");
        CHECK(ctr[0] == ball[0] && ctr[1] == ball[1] && ctr[2] == ball[2],
              "ball target locks exactly");
        CHECK(c.EffFov() == 110.0f, "rest effective FOV == slider (110)");
    }

    std::printf("[3] ballcam aim slews at 20 rad/s + eye lags via stiffness (#1+#3)\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        ball[1] = 500.0f;  // 26.6 deg bearing swing — exceeds one 20 rad/s step
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        float eye[3], ctr[3];
        c.Eye(eye);
        c.Center(ctr);
        float horiz =
            std::sqrt(eye[0] * eye[0] + eye[1] * eye[1]);
        // #3: the ballcam eye now lags through the stiffness follower, so
        // mid-slew it rides the chord INSIDE the rig sphere (never a snap,
        // never outside it) — one 60 Hz frame keeps it within ~10 uu.
        CHECK(horiz < 269.63f && horiz > 260.0f,
              "ballcam eye lags inside the rig sphere");
        // #1: the aim slews at 20 rad/s, so one 60 Hz frame moves at most
        // 19.1 deg — the target lags the ball instead of snapping to it.
        bool lags = (ctr[0] != ball[0] || ctr[1] != ball[1] || ctr[2] != ball[2]);
        CHECK(lags, "moved ball target slews (not instant)");
        // ...but converges when the ball sits still (rest stays exact per [2]).
        for (int i = 0; i < 60; ++i)
            c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        c.Eye(eye);
        c.Center(ctr);
        horiz = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1]);
        CHECK(near(horiz, 269.63f, 0.5f), "settled ballcam eye back on sphere");
        float mdx = ctr[0] - ball[0], mdy = ctr[1] - ball[1], mdz = ctr[2] - ball[2];
        float miss = std::sqrt(mdx * mdx + mdy * mdy + mdz * mdz);
        CHECK(miss < 2.0f, "settled ball target converges to ball");
    }

    std::printf("[4] supersonic dynamics (stiffness 0: +114.7 dist, +2 angle, +10 FOV)\n");
    {
        rl::Camera c;
        c.Params().stiffness = 0.0f;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true, 2200.0f, true);
        float eye[3];
        c.Eye(eye);
        float horiz =
            std::sqrt(eye[0] * eye[0] + eye[1] * eye[1]);
        // effDist = 270+114.7 = 384.7, effAng = -1deg -> 384.7*cos(1deg)
        CHECK(near(horiz, 384.64f, 0.5f), "supersonic dynamic distance 1:1");
        CHECK(c.EffFov() == 120.0f, "supersonic dynamic FOV +10");
    }

    std::printf("[5] ballcam toggle blends at 0.5+2.5*speed (#6)\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        CHECK(c.Blend() == 1.0f, "starts blended to ballcam");
        c.Update(1.0f / 60.0f, car, 0.0f, ball, false);
        // #6 (rl_camera.cpp:398-403): tRate = 0.5+2.5*1.2 = 3.5/s, so one
        // 60 Hz frame moves blend by ~0.0583 (was flat speed/60 = 0.02).
        CHECK(c.Blend() > 0.92f && c.Blend() < 0.96f,
              "one frame moves blend by (0.5+2.5*speed)/60");
        for (int i = 0; i < 30; ++i)
            c.Update(1.0f / 60.0f, car, 0.0f, ball, false);
        CHECK(c.Blend() == 0.0f, "blend completes (~0.29 s at 1.2)");
    }

    std::printf("[6] swivel rate 90 deg / 1.3 s at 1.0 (linear)\n");
    {
        rl::Camera c;
        c.Params().swivelSpeed = 1.0f;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        for (int i = 0; i < 78; ++i)  // 1.3 s of full-right swivel
            c.Update(1.0f / 60.0f, car, 0.0f, ball, true, 0.0f, false, 1.0f, 0.0f);
        CHECK(near(c.SwivelYaw(), 3.14159265f / 2.0f, 0.05f),
              "full swivel turns 90 deg in 1.3 s");
    }

    std::printf("[7] car-cam eye stiffness lag with rig-speed saturation (#3)\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, false);
        float e0[3];
        c.Eye(e0);
        car[0] = 100.0f;
        c.Update(1.0f / 60.0f, car, 0.0f, ball, false);
        float e1[3];
        c.Eye(e1);
        float moved = e1[0] - e0[0];
        // #3: a fast rig step picks up the saturation term (rigSpeed/cap),
        // so the eye tracks tightly instead of settling v/rate behind.
        CHECK(moved > 60.0f && moved < 100.0f,
              "car-cam eye tracks a fast step (saturation)");
        // Low-speed stiffness response is intact: settle, then a 5 uu nudge
        // moves only a fraction in one frame (k~0.2 at the base rate).
        for (int i = 0; i < 120; ++i)
            c.Update(1.0f / 60.0f, car, 0.0f, ball, false);
        float e3[3];
        c.Eye(e3);
        CHECK(near(e3[0] - car[0], -269.63f, 0.5f),
              "car-cam eye converges to the rig at rest");
        car[0] = 105.0f;
        c.Update(1.0f / 60.0f, car, 0.0f, ball, false);
        float e4[3];
        c.Eye(e4);
        float nudge = e4[0] - e3[0];
        CHECK(nudge > 0.5f && nudge < 2.5f, "car-cam eye lags a nudge (k~0.2)");
    }

    std::printf("[8] sphere-orbit swivel + snap-back (#4)\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        // Asymmetric orbit-pitch clamp (-0.8/+0.7, rl_camera.cpp:380).
        for (int i = 0; i < 60; ++i)
            c.Update(1.0f / 60.0f, car, 0.0f, ball, true, 0.0f, false,
                     0.0f, -1.0f);
        CHECK(near(c.SwivelPitch(), -0.8f, 0.02f),
              "look-down clamps at -0.8");
        // Snap-back: release zeroes both offsets fast and exactly
        // (2x-rate linear return past 0.05 s idle, rl_camera.cpp:390-396).
        rl::Camera c2;
        c2.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        for (int i = 0; i < 18; ++i)
            c2.Update(1.0f / 60.0f, car, 0.0f, ball, true, 0.0f, false,
                      1.0f, 1.0f);
        CHECK(near(c2.SwivelPitch(), 0.7f, 0.02f),
              "look-up clamps at +0.7");
        for (int i = 0; i < 60; ++i)
            c2.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        CHECK(c2.SwivelPitch() == 0.0f && c2.SwivelYaw() == 0.0f,
              "released swivel snaps exactly to 0");
        // The additive `snap` key gates the return.
        rl::Camera c3;
        c3.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        c3.Params().snap = false;
        for (int i = 0; i < 18; ++i)
            c3.Update(1.0f / 60.0f, car, 0.0f, ball, true, 0.0f, false,
                      1.0f, 0.0f);
        for (int i = 0; i < 60; ++i)
            c3.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        CHECK(std::fabs(c3.SwivelYaw()) > 1.0f,
              "snap=false holds the offset");
        // Floor ride + tilt: held look-up from the ground keeps the eye at
        // ~16 uu (rl_camera.cpp:101-121) and the unabsorbed pitch tilts
        // the view instead of going dead.
        rl::Camera c4;
        c4.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        for (int i = 0; i < 60; ++i)
            c4.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        float e0[3], t0[3];
        c4.Eye(e0);
        c4.Center(t0);
        float b0x = t0[0] - e0[0], b0y = t0[1] - e0[1];
        float p0 = std::atan2(t0[2] - e0[2],
                              std::sqrt(b0x * b0x + b0y * b0y));
        for (int i = 0; i < 90; ++i)
            c4.Update(1.0f / 60.0f, car, 0.0f, ball, true, 0.0f, false,
                      0.0f, 1.0f);
        float e1[3], t1[3];
        c4.Eye(e1);
        c4.Center(t1);
        float b1x = t1[0] - e1[0], b1y = t1[1] - e1[1];
        float p1 = std::atan2(t1[2] - e1[2],
                              std::sqrt(b1x * b1x + b1y * b1y));
        CHECK(e1[2] > 8.0f && e1[2] < 30.0f,
              "held look-up rides the eye at the floor");
        CHECK(p1 - p0 > 0.15f, "unabsorbed pitch tilts the view");
    }

    std::printf(failures == 0 ? "CAMERA PASS\n" : "CAMERA FAIL\n");
    return failures == 0 ? 0 : 1;
}
