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

    std::printf("[3] ballcam eye is rigid (no stiffness lag, RL behavior)\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        ball[1] = 500.0f;
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        float eye[3], ctr[3];
        c.Eye(eye);
        c.Center(ctr);
        float horiz =
            std::sqrt(eye[0] * eye[0] + eye[1] * eye[1]);
        CHECK(near(horiz, 269.63f, 0.05f), "ballcam eye tracks with zero lag");
        CHECK(ctr[0] == ball[0] && ctr[1] == ball[1] && ctr[2] == ball[2],
              "moved ball target still exact");
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

    std::printf("[5] ballcam toggle blends at transition speed\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(1.0f / 60.0f, car, 0.0f, ball, true);
        CHECK(c.Blend() == 1.0f, "starts blended to ballcam");
        c.Update(1.0f / 60.0f, car, 0.0f, ball, false);
        CHECK(c.Blend() > 0.97f && c.Blend() < 0.99f,
              "one frame moves blend by transition/60");
        for (int i = 0; i < 60; ++i)
            c.Update(1.0f / 60.0f, car, 0.0f, ball, false);
        CHECK(c.Blend() == 0.0f, "blend completes (~0.83 s at 1.2)");
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

    std::printf("[7] car-cam eye still smooths (stiffness lag)\n");
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
        CHECK(moved > 10.0f && moved < 25.0f, "car-cam eye lags (k~0.155)");
    }

    std::printf(failures == 0 ? "CAMERA PASS\n" : "CAMERA FAIL\n");
    return failures == 0 ? 0 : 1;
}
