// Chase-rig invariants for src/camera/Camera.cpp.
// Rest pose, short-arc aim, swivel snap vs latch, transition, speed pull, rear view.
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

static constexpr float kPi = 3.14159265f;
static constexpr float kDeg = kPi / 180.0f;

static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
static float len3(const float v[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}
static float dist3(const float a[3], const float b[3]) {
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return len3(d);
}
static float wrap(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a <= -kPi) a += 2.0f * kPi;
    return a;
}
static float lookYaw(const float eye[3], const float ctr[3]) {
    return std::atan2(ctr[1] - eye[1], ctr[0] - eye[0]);
}
static float lookPitch(const float eye[3], const float ctr[3]) {
    float dx = ctr[0] - eye[0], dy = ctr[1] - eye[1], dz = ctr[2] - eye[2];
    return std::atan2(dz, std::sqrt(dx * dx + dy * dy));
}

// Eye opposite `bearing` (rad, +X = 0): back = distance*cos(angle),
// up = height - distance*sin(angle). That is the rig at rest.
struct Rig {
    float back, up, radius;
};
static Rig rigOf(const rl::CameraParams& p) {
    Rig r;
    float ang = p.angle * kDeg;
    r.back = p.distance * std::cos(ang);
    r.up = p.height - p.distance * std::sin(ang);
    r.radius = std::sqrt(r.back * r.back + r.up * r.up);
    return r;
}
static void restEye(const rl::CameraParams& p, const float car[3], float bearing, float out[3]) {
    Rig r = rigOf(p);
    out[0] = car[0] - std::cos(bearing) * r.back;
    out[1] = car[1] - std::sin(bearing) * r.back;
    out[2] = car[2] + r.up;
}
static void focusOf(const rl::CameraParams& p, const float car[3], float out[3]) {
    out[0] = car[0];
    out[1] = car[1];
    out[2] = car[2] + p.height;
}

int main() {
    const float frame = 1.0f / 60.0f;

    std::printf("[1] compiled defaults\n");
    {
        rl::Camera c;
        CHECK(c.Params().transitionSpeed == 1.5f, "transition_speed == 1.5");
        CHECK(c.Params().swivelSpeed == 5.0f, "swivel_speed == 5.0");
        CHECK(c.Params().shake == false, "shake == false");
        CHECK(c.Params().invertSwivel == false, "invert_swivel == false");
        CHECK(c.Params().snap == true, "snap == true");
        CHECK(c.Params().airHeading == false, "air_heading == false");
        CHECK(c.BallCam() == true, "ballcam default ON");
    }

    std::printf("[2] rest pose == distance/height/angle\n");
    {
        struct Case { float distance, height, angle; };
        const Case cases[3] = {
            {270.0f, 100.0f, -3.0f},
            {350.0f, 150.0f, -12.0f},
            {140.0f, 60.0f, 5.0f},
        };
        for (int k = 0; k < 3; ++k) {
            rl::Camera c;
            c.Params().distance = cases[k].distance;
            c.Params().height = cases[k].height;
            c.Params().angle = cases[k].angle;
            float car[3] = {300.0f, -200.0f, 17.0f};
            float ball[3] = {car[0] + 1000.0f, car[1], 100.0f};
            c.Update(frame, car, 0.0f, ball, false);
            float eye[3], want[3];
            c.Eye(eye);
            restEye(c.Params(), car, 0.0f, want);
            CHECK(near(eye[0], want[0], 0.05f) && near(eye[1], want[1], 0.05f) &&
                      near(eye[2], want[2], 0.05f),
                  "car-cam rest eye == distance/height/angle");
        }
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(frame, car, 0.0f, ball, false);
        float eye[3], ctr[3];
        c.Eye(eye);
        c.Center(ctr);
        CHECK(near(lookYaw(eye, ctr), 0.0f, 0.02f), "rest look yaw follows the car");
        CHECK(c.EffFov() == c.Params().fov, "rest FOV == slider");
    }

    std::printf("[3] aim lags a moved ball; the eye stays on the car\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(frame, car, 0.0f, ball, true);
        ball[0] = 1000.0f;
        ball[1] = 500.0f;
        const float target = std::atan2(ball[1] - car[1], ball[0] - car[0]);
        c.Update(frame, car, 0.0f, ball, true);
        float eye[3], ctr[3];
        c.Eye(eye);
        c.Center(ctr);
        CHECK(std::fabs(wrap(lookYaw(eye, ctr) - target)) > 0.15f,
              "one frame does not finish a ball-yaw swing");
        for (int i = 0; i < 180; ++i) c.Update(frame, car, 0.0f, ball, true);
        c.Eye(eye);
        c.Center(ctr);
        CHECK(near(lookYaw(eye, ctr), target, 0.05f), "settled aim lands on the ball yaw");
        float focus[3];
        focusOf(c.Params(), car, focus);
        CHECK(near(dist3(eye, focus), c.Params().distance, 0.5f),
              "settled eye stays one distance from the focus");

        rl::Camera cc;
        float carB[3] = {0, 0, 17};
        cc.Update(frame, carB, 0.0f, ball, false);
        float e0[3];
        cc.Eye(e0);
        carB[0] = 100.0f;
        cc.Update(frame, carB, 0.0f, ball, false);
        float e1[3];
        cc.Eye(e1);
        CHECK(near(e1[0] - e0[0], 100.0f, 0.5f), "the eye translates with the car");
        cc.Update(frame, carB, 0.5f * kPi, ball, false);
        cc.Eye(e1);
        cc.Center(ctr);
        CHECK(std::fabs(lookYaw(e1, ctr) - 0.5f * kPi) > 0.2f,
              "one frame does not finish a car-yaw turn");
        for (int i = 0; i < 180; ++i) cc.Update(frame, carB, 0.5f * kPi, ball, false);
        cc.Eye(e1);
        cc.Center(ctr);
        CHECK(near(lookYaw(e1, ctr), 0.5f * kPi, 0.05f), "car-cam yaw catches the nose");
    }

    std::printf("[4] at rest the eye orbits the focus at the slider distance\n");
    {
        float car[3] = {0, 0, 17};
        rl::Camera c;
        float ball[3] = {1000, 0, 100};
        bool onRadius = true;
        for (int i = 0; i < 180; ++i) {
            float a = 0.05f * static_cast<float>(i);
            ball[0] = 1000.0f * std::cos(a);
            ball[1] = 1000.0f * std::sin(a);
            ball[2] = 100.0f;
            c.Update(frame, car, 0.0f, ball, true);
            float eye[3], focus[3];
            c.Eye(eye);
            focusOf(c.Params(), car, focus);
            if (!near(dist3(eye, focus), c.Params().distance, 0.5f)) onRadius = false;
        }
        CHECK(onRadius, "level ball-cam keeps the eye on the distance sphere");
    }

    std::printf("[5] a bearing flip takes the short arc\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(frame, car, 0.0f, ball, true);
        float a = 200.0f * kDeg;
        ball[0] = 1000.0f * std::cos(a);
        ball[1] = 1000.0f * std::sin(a);
        float prev = 0.0f, travel = 0.0f;
        for (int i = 0; i < 180; ++i) {
            c.Update(frame, car, 0.0f, ball, true);
            float eye[3], ctr[3];
            c.Eye(eye);
            c.Center(ctr);
            float bearing = lookYaw(eye, ctr);
            travel += wrap(bearing - prev);
            prev = bearing;
        }
        CHECK(std::fabs(travel) < kPi, "aim travel stays under 180 deg");
        CHECK(near(std::fabs(wrap(prev - a)), 0.0f, 0.05f), "aim lands on the flipped bearing");
    }

    std::printf("[6] swivel tracks the stick, returns, or holds when snap is off\n");
    {
        float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};
        rl::Camera c;
        c.Params().swivelSpeed = 10.0f;
        c.Update(frame, car, 0.0f, ball, true);
        for (int i = 0; i < 180; ++i)
            c.Update(frame, car, 0.0f, ball, true, 0.0f, false, 0.0f, -1.0f);
        float held = c.SwivelPitch();
        for (int i = 0; i < 60; ++i)
            c.Update(frame, car, 0.0f, ball, true, 0.0f, false, 0.0f, -1.0f);
        CHECK(near(c.SwivelPitch(), held, 1e-4f), "held pitch settles");
        CHECK(held < -0.8f && std::fabs(held) < 0.5f * kPi, "look-down stops short of vertical");

        float off[2] = {0.0f, 0.0f};
        const float speeds[2] = {2.0f, 8.0f};
        for (int s = 0; s < 2; ++s) {
            rl::Camera cs;
            cs.Params().swivelSpeed = speeds[s];
            cs.Update(frame, car, 0.0f, ball, true);
            for (int i = 0; i < 8; ++i)
                cs.Update(frame, car, 0.0f, ball, true, 0.0f, false, 1.0f, 0.0f);
            off[s] = cs.SwivelYaw();
        }
        CHECK(off[0] < 0.0f && off[1] < off[0], "a faster swivel is closer to the stick target");

        rl::Camera c2;
        c2.Update(frame, car, 0.0f, ball, true);
        for (int i = 0; i < 30; ++i)
            c2.Update(frame, car, 0.0f, ball, true, 0.0f, false, 1.0f, 0.0f);
        CHECK(c2.SwivelYaw() < -0.1f, "swivel moved the rig");
        for (int i = 0; i < 180; ++i) c2.Update(frame, car, 0.0f, ball, true);
        CHECK(std::fabs(c2.SwivelYaw()) < 1e-3f && std::fabs(c2.SwivelPitch()) < 1e-3f,
              "snap returns a released stick to zero");

        rl::Camera c3;
        c3.Params().snap = false;
        c3.Update(frame, car, 0.0f, ball, true);
        for (int i = 0; i < 30; ++i)
            c3.Update(frame, car, 0.0f, ball, true, 0.0f, false, 1.0f, 0.0f);
        float heldOff = c3.SwivelYaw();
        for (int i = 0; i < 120; ++i) c3.Update(frame, car, 0.0f, ball, true);
        CHECK(near(c3.SwivelYaw(), heldOff, 1e-4f), "snap off holds the offset");
    }

    std::printf("[7] the ball-cam blend is bounded and monotone\n");
    {
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        rl::Camera c;
        c.Update(frame, car, 0.0f, ball, true);
        CHECK(c.Blend() == 1.0f, "starts blended to ball-cam");
        c.Update(frame, car, 0.0f, ball, false);
        float afterOne = c.Blend();
        CHECK(afterOne < 1.0f && afterOne > 0.0f, "one frame moves the blend");
        bool monotone = true, bounded = true;
        int frames = 1;
        for (int i = 0; i < 180 && c.Blend() >= 0.05f; ++i) {
            float before = c.Blend();
            c.Update(frame, car, 0.0f, ball, false);
            float after = c.Blend();
            if (after > before + 1e-6f) monotone = false;
            if (after < 0.0f || after > 1.0f) bounded = false;
            ++frames;
        }
        CHECK(monotone && bounded, "blend decreases inside [0,1]");
        CHECK(c.Blend() < 0.05f, "blend falls under 0.05");
        CHECK(frames <= 16, "default 1.5 transition finishes within 16 frames");
        int framesFast = 0;
        rl::Camera cf;
        cf.Params().transitionSpeed = 2.0f;
        cf.Update(frame, car, 0.0f, ball, true);
        while (cf.Blend() >= 0.05f && framesFast < 200) {
            cf.Update(frame, car, 0.0f, ball, false);
            ++framesFast;
        }
        CHECK(framesFast < frames, "faster transition_speed finishes sooner");
        rl::Camera cz;
        cz.Update(frame, car, 0.0f, ball, true);
        cz.Update(0.0f, car, 0.0f, ball, false);
        CHECK(cz.Blend() == 1.0f, "dt == 0 leaves the blend alone");
    }

    std::printf("[8] forward speed lengthens the rig; field of view opens\n");
    {
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        Rig rig = rigOf(rl::CameraParams{});
        {
            rl::Camera c;
            c.Params().stiffness = 1.0f;
            c.Update(frame, car, 0.0f, ball, true, 2200.0f, true);
            float eye[3];
            c.Eye(eye);
            float horiz = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1]);
            CHECK(near(horiz, rig.back, 0.05f), "stiffness 1 ignores forward speed");
        }
        float prevHoriz = -1.0f;
        bool monotone = true;
        const float speeds[5] = {0.0f, 550.0f, 1100.0f, 1650.0f, 2200.0f};
        for (int i = 0; i < 5; ++i) {
            rl::Camera c;
            c.Params().stiffness = 0.0f;
            c.Update(frame, car, 0.0f, ball, true, speeds[i], speeds[i] >= 2200.0f);
            float eye[3];
            c.Eye(eye);
            float horiz = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1]);
            if (prevHoriz >= 0.0f && horiz < prevHoriz - 1e-3f) monotone = false;
            prevHoriz = horiz;
        }
        CHECK(monotone, "distance grows with forward speed");
        rl::Camera c0, c1;
        c0.Params().stiffness = 0.0f;
        c1.Params().stiffness = 1.0f;
        c0.Update(frame, car, 0.0f, ball, true, 2200.0f, true);
        c1.Update(frame, car, 0.0f, ball, true, 2200.0f, true);
        CHECK(c0.EffFov() == c1.EffFov(), "FOV kick ignores stiffness");
        CHECK(c0.EffFov() > c0.Params().fov, "supersonic opens the FOV");
    }

    std::printf("[9] look-behind mirrors the rig\n");
    {
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        float eye[3], ctr[3];
        {
            rl::Camera c;
            c.Update(frame, car, 0.0f, ball, true);
            c.Eye(eye);
            c.Center(ctr);
            CHECK(eye[0] < car[0] && ctr[0] > car[0], "chase cam looks forward from behind");
        }
        {
            rl::Camera c;
            c.SetLookBehind(true);
            c.Update(frame, car, 0.0f, ball, true);
            c.Eye(eye);
            c.Center(ctr);
            CHECK(eye[0] > car[0] && ctr[0] < car[0], "look-behind sits in front, looking back");
        }
        {
            rl::Camera c;
            c.SetLookBehind(true);
            c.Update(frame, car, 0.0f, ball, false);
            c.Eye(eye);
            c.Center(ctr);
            CHECK(eye[0] > car[0] && ctr[0] < car[0], "car-cam look-behind mirrors too");
            c.SetLookBehind(false);
            c.Update(frame, car, 0.0f, ball, false);
            c.Eye(eye);
            c.Center(ctr);
            CHECK(eye[0] < car[0] && ctr[0] > car[0], "releasing look-behind restores the chase");
        }
    }

    std::printf("[10] held look-up stays above the floor and pitches the view\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};
        c.Update(frame, car, 0.0f, ball, true);
        float e0[3], t0[3];
        c.Eye(e0);
        c.Center(t0);
        float p0 = lookPitch(e0, t0);
        bool aboveFloor = true;
        for (int i = 0; i < 120; ++i) {
            c.Update(frame, car, 0.0f, ball, true, 0.0f, false, 0.0f, 1.0f);
            float e[3];
            c.Eye(e);
            if (e[2] < 10.0f) aboveFloor = false;
        }
        float e1[3], t1[3];
        c.Eye(e1);
        c.Center(t1);
        CHECK(aboveFloor, "the eye stays at least 10 uu up");
        CHECK(lookPitch(e1, t1) > p0 + 0.2f, "look-up pitches the view");
    }

    std::printf("[11] air heading is off unless selected\n");
    {
        float car[3] = {0, 0, 40}, ball[3] = {0, 0, 100};
        float fwd[3] = {1, 0, 0}, vel[3] = {0, 1200, 0};
        auto yawAfter = [&](bool heading) {
            rl::Camera c;
            c.Params().airHeading = heading;
            c.Params().stiffness = 1.0f;
            for (int i = 0; i < 180; ++i)
                c.Update(frame, car, 0.0f, ball, false, 1200.0f, false, 0.0f, 0.0f, fwd, vel,
                         true);
            float eye[3], ctr[3];
            c.Eye(eye);
            c.Center(ctr);
            return lookYaw(eye, ctr);
        };
        CHECK(near(yawAfter(false), 0.0f, 0.05f), "default air heading keeps the nose");
        CHECK(near(yawAfter(true), 0.5f * kPi, 0.08f), "selected air heading follows velocity");
    }

    std::printf("[12] the horizon stays level\n");
    {
        float ball[3] = {1000, 0, 100};
        float fwd[3] = {1, 0, 0};
        float side[3] = {0, 1, 0};
        float down[3] = {0, 0, -1};
        auto level = [](rl::Camera& c, const char* msg) {
            float eye[3], ctr[3], up[3];
            c.Eye(eye);
            c.Center(ctr);
            c.Up(up);
            float dir[3] = {ctr[0] - eye[0], ctr[1] - eye[1], ctr[2] - eye[2]};
            float len = len3(dir);
            dir[0] /= len;
            dir[1] /= len;
            dir[2] /= len;
            // Screen-right is horizontal: its z component is 0. Up still points upward.
            float rz = up[0] * dir[1] - up[1] * dir[0];
            CHECK(std::fabs(rz) < 1e-4f && up[2] > 0.0f, msg);
        };
        float carFloor[3] = {0, 0, 17};
        rl::Camera rolled;
        rolled.Update(frame, carFloor, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f, fwd, nullptr,
                      false, down);
        level(rolled, "a grounded 180 deg roll keeps a level horizon");

        float carAir[3] = {0, 0, 400};
        rl::Camera takeoff;
        takeoff.Update(frame, carFloor, 0.0f, ball, false);
        for (int i = 0; i < 60; ++i)
            takeoff.Update(frame, carAir, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f, fwd,
                           nullptr, true, side);
        level(takeoff, "a 90 deg roll after takeoff keeps a level horizon");

        rl::Camera aerial;
        aerial.Update(frame, carAir, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f, fwd, nullptr,
                      true, down);
        level(aerial, "an upside-down aerial keeps a level horizon");

        for (int i = 0; i < 120; ++i)
            aerial.Update(frame, carFloor, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f, fwd,
                          nullptr, false, down);
        level(aerial, "landing keeps a level horizon");

        float wallFwd[3] = {0, 1, 0};
        float carWall[3] = {0, 0, 200};
        rl::Camera wall;
        wall.Params().stiffness = 1.0f;
        for (int i = 0; i < 30; ++i)
            wall.Update(frame, carWall, 0.0f, ball, false, 0.0f, false, 0.0f, 0.0f, wallFwd,
                        nullptr, false, side);
        level(wall, "a wall does not roll the horizon");
        float eye[3], ctr[3];
        wall.Eye(eye);
        wall.Center(ctr);
        CHECK(near(lookYaw(eye, ctr), 0.5f * kPi, 0.05f), "car cam on a wall follows the nose yaw");
    }

    std::printf(failures == 0 ? "CAMERA PASS\n" : "CAMERA FAIL\n");
    return failures == 0 ? 0 : 1;
}
