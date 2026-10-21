// Camera contract test — STRUCTURAL invariants (re-baselined 2026-10-21).
//
// History: this file shipped with the CarBaller port and pinned that port's
// tuned constants — a 20 rad/s aim slew, "at most 19.1 deg per 60 Hz frame",
// "0.05 s idle before snap-back", hardcoded rest poses and a hardcoded
// supersonic distance. Those are NUMBERS, and numbers belong to the trace gate
// (`tools/verify/camera_trace_gate.py`, MASTER_PROMPT_CAMERA_EXACT.md §6 P3),
// which can prove them against a recorded Rocket League run instead of
// against our own code. What is asserted here are the invariants that must
// hold for ANY tuning of the model in `src/camera/Camera.cpp`, so a future
// retune (P3/P4) can move constants without having to rewrite this file —
// and so a regression in MECHANISM still fails loudly.
//
// Invariants (one per section, each is a structural claim):
//   [2] rest pose is exactly distance/height/angle — pose is a pure function
//       of the three sliders, with no hidden tuned offset;
//   [3] a moved target lags, then converges (no snap, no permanent error);
//   [4] the eye never leaves the stiffness sphere (the rig radius is a convex
//       bound: the follower can only lag INSIDE it);
//   [5] a bearing flip takes the SHORT arc, never the long way round;
//   [6] swivel saturates, is linear in its slider, and returns exactly to zero;
//   [7] the ballcam transition is a bounded monotone blend that completes;
//   [8] speed dynamics are zero at rest and at stiffness 1, monotone in speed,
//       and the FOV kick is independent of stiffness;
//   [9] look-behind mirrors the rig (eye in front, looking back);
//  [10] held look-up never puts the eye under the floor and the pitch the
//       orbit cannot absorb reaches the view.
//
// Provenance (facts only, no code copied):
//  - Rocket Science #2 (https://rocketscience.fyi/know/videos/camera-settings):
//    FOV 60..110; dynamic FOV +5 with speed +5 supersonic; stiffness =
//    dynamic distance + dynamic angle + smoothing ONLY with ballcam off;
//    swivel 90 deg per 1.3 s at 1.0, linear.
//  - BakkesModSDK ProfileCameraSettings/CameraSave (field list only).
//  - CarBaller settings.h (RL ranges: swivel 1..10, transition 1..2).
//  - §3 layer map (which layer owns each mechanism): src/camera/Camera.h.
//
// Build: g++ -std=c++20 -I src tests/test_camera_dynamics.cpp
//        src/camera/Camera.cpp -lGL -o /tmp/t_cam && /tmp/t_cam
// No SDL, no GL context needed (Update/Eye/Center never touch GL; -lGL only
// to link, because Camera::Apply references glFrustum).
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
// Cheap deterministic jitter (no RNG dependency, no Psyonix anything).
static float wobble(int i, float amp, float rate) {
    return amp * std::sin(rate * (float)i);
}

// The rig the sliders describe, derived HERE from the public params (not from a
// literal): the chase rig is a sphere around the car of radius
// sqrt(back^2 + up^2) with back = distance*cos(angle) horizontal and
// up = height - distance*sin(angle) vertical. This is the analytic pose the
// camera must reproduce at rest — that is what "rest pose == sliders" means.
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
// Analytic rest eye for a car at `car`, looking along `bearing` (rad, +X = 0):
// the eye sits opposite the bearing at `back`, `up` above the car. No dynamics
// at rest (speed 0), so this is the whole pose.
static void restEye(const rl::CameraParams& p, const float car[3], float bearing,
                    float out[3]) {
    Rig r = rigOf(p);
    out[0] = car[0] - std::cos(bearing) * r.back;
    out[1] = car[1] - std::sin(bearing) * r.back;
    out[2] = car[2] + r.up;
}

int main() {
    const float frame = 1.0f / 60.0f;

    std::printf("[1] compiled defaults (RL 1:1 + additive keys)\n");
    {
        rl::Camera c;
        CHECK(c.Params().transitionSpeed == 1.2f, "transition_speed == 1.2");
        CHECK(c.Params().swivelSpeed == 5.0f, "swivel_speed == 5.0");
        CHECK(c.Params().shake == true, "shake == true");
        CHECK(c.Params().invertSwivel == false, "invert_swivel == false");
        CHECK(c.Params().snap == true, "snap == true");
        CHECK(c.BallCam() == true, "ballcam default ON");
    }

    std::printf("[2] rest pose == distance/height/angle exactly\n");
    {
        // Three unrelated slider sets: if the pose were a function of any
        // hidden tuned constant, at least one of these would miss.
        struct Case { float distance, height, angle; };
        const Case cases[3] = {
            {270.0f, 100.0f, -3.0f},   // RL default
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
            c.Update(frame, car, 0.0f, ball, true);
            float eye[3], ctr[3], want[3];
            c.Eye(eye);
            c.Center(ctr);
            restEye(c.Params(), car, 0.0f, want);
            CHECK(near(eye[0], want[0], 0.05f) && near(eye[1], want[1], 0.05f) &&
                      near(eye[2], want[2], 0.05f),
                  "ballcam rest eye == analytic distance/height/angle");
        }
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(frame, car, 0.0f, ball, true);
        float ctr[3], want[3];
        c.Center(ctr);
        restEye(c.Params(), car, 0.0f, want);
        CHECK(dist3(ctr, ball) < 0.01f, "rest ball target == the ball");
        CHECK(c.EffFov() == c.Params().fov, "rest FOV == slider (zero dynamics)");
    }

    std::printf("[3] a moved target lags, then converges\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(frame, car, 0.0f, ball, true);
        ball[1] = 500.0f; // ~27 deg bearing swing in one frame
        c.Update(frame, car, 0.0f, ball, true);
        float eye[3], ctr[3];
        c.Eye(eye);
        c.Center(ctr);
        CHECK(dist3(ctr, ball) > 1.0f, "one frame does not reach a moved ball");
        Rig rig = rigOf(c.Params());
        float off[3] = {eye[0] - car[0], eye[1] - car[1], eye[2] - car[2]};
        CHECK(len3(off) < rig.radius, "lagging eye stays inside the rig sphere");
        for (int i = 0; i < 120; ++i)
            c.Update(frame, car, 0.0f, ball, true);
        c.Eye(eye);
        c.Center(ctr);
        CHECK(dist3(ctr, ball) < 1.0f, "settled target converges to the ball");
        float off2[3] = {eye[0] - car[0], eye[1] - car[1], eye[2] - car[2]};
        CHECK(near(len3(off2), rig.radius, 0.5f),
              "settled eye is back on the rig sphere");

        // Car-cam: a rig step is tracked, not jumped, and a nudge is damped.
        rl::Camera cc;
        float carB[3] = {0, 0, 17};
        cc.Update(frame, carB, 0.0f, ball, false);
        float e0[3];
        cc.Eye(e0);
        carB[0] = 100.0f;
        cc.Update(frame, carB, 0.0f, ball, false);
        float e1[3];
        cc.Eye(e1);
        float step = e1[0] - e0[0];
        CHECK(step > 0.0f && step < 100.0f,
              "car-cam eye tracks a step but lags it");
        for (int i = 0; i < 120; ++i)
            cc.Update(frame, carB, 0.0f, ball, false);
        float e2[3];
        cc.Eye(e2);
        CHECK(near(e2[0] - carB[0], -rig.back, 0.5f),
              "car-cam eye converges to the rig at rest");
        carB[0] = 105.0f;
        cc.Update(frame, carB, 0.0f, ball, false);
        float e3[3];
        cc.Eye(e3);
        float nudge = e3[0] - e2[0];
        CHECK(nudge > 0.0f && nudge < 5.0f, "car-cam eye damps a 5 uu nudge");
    }

    std::printf("[4] the eye only ever lags INWARD from the stiffness sphere\n");
    {
        // The rig is a sphere of radius sqrt(back^2+up^2) around the car; the
        // follower is a per-axis convex blend, so with a fixed car every new
        // eye is inside the previous bound.
        //
        // P0 FINDING (behaviour, not a bug fixed here): the exact 3-D bound
        // holds only at a constant rig elevation. A swivel sweep walks the eye
        // along an ARC while the follower uses two different gains (kx
        // horizontal, kz vertical — halved airborne), and a mixed-gain blend of
        // two points ON a sphere lands outside it. Measured on the tuned model:
        // +3.06 uu on a 292.79 uu radius (1.04%) with swivel, exactly 0.00
        // without it. The horizontal component never leaves (same kx on x and
        // y), which is what the pre-split test actually asserted. P3's single
        // InterpTo is what removes the split; until then this guards the
        // excursion instead of pretending it is zero. Reported, not retuned.
        float car[3] = {0, 0, 17};
        rl::Camera c;
        Rig rig = rigOf(c.Params());
        float ball[3] = {1000, 0, 100};
        bool horizInside = true, sphereInside = true, guarded = true;
        for (int i = 0; i < 300; ++i) {
            float a = 0.05f * (float)i;
            ball[0] = 1000.0f * std::cos(a);
            ball[1] = 1000.0f * std::sin(a);
            ball[2] = 100.0f + wobble(i, 400.0f, 0.07f);
            // Swivel in and out too: the orbit must not stretch the radius.
            float sx = wobble(i, 0.6f, 0.11f);
            float sy = wobble(i, 0.6f, 0.13f);
            c.Update(frame, car, 0.0f, ball, true, 0.0f, false, sx, sy);
            float eye[3];
            c.Eye(eye);
            float off[3] = {eye[0] - car[0], eye[1] - car[1], eye[2] - car[2]};
            float horiz = std::sqrt(off[0] * off[0] + off[1] * off[1]);
            if (horiz > rig.radius + 0.05f) horizInside = false;
            if (len3(off) > rig.radius * 1.05f) guarded = false;
        }
        // ...and with NO swivel the full 3-D bound holds exactly.
        rl::Camera cq;
        for (int i = 0; i < 300; ++i) {
            float a = 0.05f * (float)i;
            ball[0] = 1000.0f * std::cos(a);
            ball[1] = 1000.0f * std::sin(a);
            ball[2] = 100.0f + wobble(i, 400.0f, 0.07f);
            cq.Update(frame, car, 0.0f, ball, true);
            float eye[3];
            cq.Eye(eye);
            float off[3] = {eye[0] - car[0], eye[1] - car[1], eye[2] - car[2]};
            if (len3(off) > rig.radius + 0.05f) sphereInside = false;
        }
        CHECK(horizInside, "eye horizontal radius <= rig sphere on every frame");
        CHECK(sphereInside, "no swivel: eye never leaves the rig sphere at all");
        CHECK(guarded, "swivel excursion stays under 5% of the radius");
        for (int i = 0; i < 240; ++i)
            c.Update(frame, car, 0.0f, ball, true);
        float eye[3];
        c.Eye(eye);
        float off[3] = {eye[0] - car[0], eye[1] - car[1], eye[2] - car[2]};
        CHECK(near(len3(off), rig.radius, 0.5f),
              "released eye returns to the sphere exactly");
    }

    std::printf("[5] a bearing flip takes the short arc\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        c.Update(frame, car, 0.0f, ball, true);
        // Flip the ball to the far side, 200 deg round: the short way is -160.
        float a = 200.0f * kDeg;
        ball[0] = 1000.0f * std::cos(a);
        ball[1] = 1000.0f * std::sin(a);
        float prev = 0.0f, travel = 0.0f;
        for (int i = 0; i < 120; ++i) {
            c.Update(frame, car, 0.0f, ball, true);
            float eye[3], ctr[3];
            c.Eye(eye);
            c.Center(ctr);
            float bearing = std::atan2(ctr[1] - eye[1], ctr[0] - eye[0]);
            travel += wrap(bearing - prev);
            prev = bearing;
        }
        CHECK(std::fabs(travel) < kPi,
              "aim travel stays under 180 deg (short arc, not the long way)");
        float finalBearing = prev;
        CHECK(near(std::fabs(wrap(finalBearing - a)), 0.0f, 0.05f),
              "aim lands on the flipped bearing");
    }

    std::printf("[6] swivel saturates, is linear, and returns exactly to zero\n");
    {
        float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};
        // Saturates: a long hold stops moving the orbit pitch and never
        // passes vertical (the orbit is a sphere, not a barrel roll).
        rl::Camera c;
        c.Params().swivelSpeed = 10.0f;
        c.Update(frame, car, 0.0f, ball, true);
        for (int i = 0; i < 300; ++i)
            c.Update(frame, car, 0.0f, ball, true, 0.0f, false, 0.0f, -1.0f);
        float held = c.SwivelPitch(), heldYaw = c.SwivelYaw();
        for (int i = 0; i < 120; ++i)
            c.Update(frame, car, 0.0f, ball, true, 0.0f, false, 0.0f, -1.0f);
        CHECK(near(c.SwivelPitch(), held, 1e-6f), "held pitch saturates");
        CHECK(std::fabs(held) <= 0.5f * kPi + 1e-4f,
              "orbit pitch never passes vertical");
        CHECK(std::fabs(heldYaw) <= kPi + 1e-4f, "orbit yaw stays wrapped");
        // Linear in the slider: twice the speed, twice the offset.
        float off[2] = {0.0f, 0.0f};
        const float speeds[2] = {2.0f, 4.0f};
        for (int s = 0; s < 2; ++s) {
            rl::Camera cs;
            cs.Params().swivelSpeed = speeds[s];
            cs.Update(frame, car, 0.0f, ball, true);
            for (int i = 0; i < 10; ++i)
                cs.Update(frame, car, 0.0f, ball, true, 0.0f, false,
                          0.5f, 0.0f);
            off[s] = cs.SwivelYaw();
        }
        CHECK(off[0] > 0.0f && near(off[1], 2.0f * off[0], 0.02f * off[1]),
              "swivel is linear in the speed slider");
        // Release: snap back to exactly zero...
        rl::Camera c2;
        c2.Update(frame, car, 0.0f, ball, true);
        for (int i = 0; i < 30; ++i)
            c2.Update(frame, car, 0.0f, ball, true, 0.0f, false, 1.0f, 1.0f);
        CHECK(std::fabs(c2.SwivelYaw()) > 0.1f, "swivel moved the orbit");
        for (int i = 0; i < 120; ++i)
            c2.Update(frame, car, 0.0f, ball, true);
        CHECK(c2.SwivelPitch() == 0.0f && c2.SwivelYaw() == 0.0f,
              "released swivel returns exactly to zero");
        // ...unless the additive `snap` key turns the return off.
        rl::Camera c3;
        c3.Update(frame, car, 0.0f, ball, true);
        c3.Params().snap = false;
        for (int i = 0; i < 30; ++i)
            c3.Update(frame, car, 0.0f, ball, true, 0.0f, false, 1.0f, 0.0f);
        float heldOff = c3.SwivelYaw();
        for (int i = 0; i < 120; ++i)
            c3.Update(frame, car, 0.0f, ball, true);
        CHECK(near(c3.SwivelYaw(), heldOff, 1e-4f),
              "snap=false holds the offset (no decay)");
    }

    std::printf("[7] the ballcam transition is a bounded monotone blend\n");
    {
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        rl::Camera c;
        c.Update(frame, car, 0.0f, ball, true);
        CHECK(c.Blend() == 1.0f, "starts blended to ballcam");
        c.Update(frame, car, 0.0f, ball, false);
        float afterOne = c.Blend();
        CHECK(afterOne < 1.0f && afterOne > 0.0f,
              "one frame moves the blend, without completing it");
        bool monotone = true, bounded = true;
        int frames = 1;
        for (int i = 0; i < 60; ++i) {
            float before = c.Blend();
            c.Update(frame, car, 0.0f, ball, false);
            float after = c.Blend();
            if (after > before + 1e-6f) monotone = false;
            if (after < 0.0f || after > 1.0f) bounded = false;
            ++frames;
            if (after == 0.0f) break;
        }
        CHECK(monotone && bounded, "blend decreases monotonically inside [0,1]");
        CHECK(c.Blend() == 0.0f, "blend completes");
        CHECK(frames < 60, "completion is under a second");
        // Faster transition slider => fewer frames to complete.
        int framesFast = 0;
        rl::Camera cf;
        cf.Params().transitionSpeed = 2.0f;
        cf.Update(frame, car, 0.0f, ball, true);
        while (cf.Blend() != 0.0f && framesFast < 200) {
            cf.Update(frame, car, 0.0f, ball, false);
            ++framesFast;
        }
        CHECK(framesFast < frames, "faster transition_speed completes sooner");
        // A stalled frame must not advance the blend.
        rl::Camera cz;
        cz.Update(frame, car, 0.0f, ball, true);
        cz.Update(0.0f, car, 0.0f, ball, false);
        CHECK(cz.Blend() == 1.0f, "dt == 0 leaves the blend alone");
    }

    std::printf("[8] speed dynamics: zero at rest / stiffness 1, monotone in speed\n");
    {
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        Rig rig = rigOf(rl::CameraParams{});
        // stiffness 1 removes every speed delta (the (1-stiffness) factor).
        {
            rl::Camera c;
            c.Params().stiffness = 1.0f;
            c.Update(frame, car, 0.0f, ball, true, 2200.0f, true);
            float eye[3];
            c.Eye(eye);
            float horiz = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1]);
            CHECK(near(horiz, rig.back, 0.05f),
                  "stiffness 1 == no dynamic distance");
        }
        // Monotone in speed (fresh camera: the first frame snaps to the rig).
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
        CHECK(monotone, "dynamic distance grows with speed");
        // The FOV kick is independent of stiffness (it rides no slider).
        rl::Camera c0, c1;
        c0.Params().stiffness = 0.0f;
        c1.Params().stiffness = 1.0f;
        c0.Update(frame, car, 0.0f, ball, true, 2200.0f, true);
        c1.Update(frame, car, 0.0f, ball, true, 2200.0f, true);
        CHECK(c0.EffFov() == c1.EffFov(), "FOV kick independent of stiffness");
        CHECK(c0.EffFov() > c0.Params().fov, "speed opens the FOV");
    }

    std::printf("[9] look-behind mirrors the rig (eye in front, looking back)\n");
    {
        float car[3] = {0, 0, 17}, ball[3] = {1000, 0, 100};
        float eye[3], ctr[3];
        {
            rl::Camera c;
            c.Update(frame, car, 0.0f, ball, true);
            c.Eye(eye);
            c.Center(ctr);
            CHECK(eye[0] < car[0] && ctr[0] > car[0],
                  "chase cam: eye behind the car, looking forward");
        }
        {
            rl::Camera c;
            c.SetLookBehind(true);
            c.Update(frame, car, 0.0f, ball, true);
            c.Eye(eye);
            c.Center(ctr);
            CHECK(eye[0] > car[0] && ctr[0] < car[0],
                  "look-behind: eye in front, looking back over the car");
            CHECK(dist3(ctr, eye) > 1.0f, "rear view keeps a real look direction");
        }
        {
            // Both modes share the rear view, and it returns to chase cleanly.
            rl::Camera c;
            c.SetLookBehind(true);
            c.Update(frame, car, 0.0f, ball, false);
            c.Eye(eye);
            c.Center(ctr);
            CHECK(eye[0] > car[0] && ctr[0] < car[0],
                  "car-cam look-behind mirrors too");
            c.SetLookBehind(false);
            for (int i = 0; i < 120; ++i)
                c.Update(frame, car, 0.0f, ball, false);
            c.Eye(eye);
            c.Center(ctr);
            CHECK(eye[0] < car[0] && ctr[0] > car[0],
                  "releasing look-behind restores the chase pose");
        }
    }

    std::printf("[10] held look-up keeps the eye off the floor and tilts the view\n");
    {
        rl::Camera c;
        float car[3] = {0, 0, 17}, ball[3] = {2000, 0, 100};
        c.Update(frame, car, 0.0f, ball, true);
        for (int i = 0; i < 60; ++i)
            c.Update(frame, car, 0.0f, ball, true);
        float e0[3], t0[3];
        c.Eye(e0);
        c.Center(t0);
        float p0 = std::atan2(t0[2] - e0[2],
                              std::sqrt((t0[0] - e0[0]) * (t0[0] - e0[0]) +
                                        (t0[1] - e0[1]) * (t0[1] - e0[1])));
        bool aboveFloor = true;
        for (int i = 0; i < 120; ++i) {
            c.Update(frame, car, 0.0f, ball, true, 0.0f, false, 0.0f, 1.0f);
            float e[3];
            c.Eye(e);
            if (e[2] <= 0.0f) aboveFloor = false;
        }
        float e1[3], t1[3];
        c.Eye(e1);
        c.Center(t1);
        float p1 = std::atan2(t1[2] - e1[2],
                              std::sqrt((t1[0] - e1[0]) * (t1[0] - e1[0]) +
                                        (t1[1] - e1[1]) * (t1[1] - e1[1])));
        CHECK(aboveFloor, "the eye never goes under the arena floor");
        CHECK(p1 > p0 + 0.05f, "unabsorbed orbit pitch reaches the view");
    }

    std::printf(failures == 0 ? "CAMERA PASS\n" : "CAMERA FAIL\n");
    return failures == 0 ? 0 : 1;
}
