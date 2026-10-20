// Camera.cpp — RL chase-cam replication.
//
// Behavior provenance (facts only, no code copied — see CROSS_CHECK.md §5):
//  - Rocket Science #2 (https://rocketscience.fyi/know/videos/camera-settings):
//    FOV slider 60..110; dynamic FOV +5 with speed and +5 more at supersonic;
//    stiffness = dynamic distance (+100 max at stiffness 0; supersonic
//    distance = distance + (1-stiffness)*114.7) + dynamic angle (+2 max at 0,
//    e.g. -5deg -> -3deg) + eye smoothing ONLY with ballcam off; swivel turns
//    90 deg in 1.3 s at 1.0 and the slider is linear.
//  - BakkesModSDK ProfileCameraSettings/CameraSave (field list only):
//    FOV/Height/Pitch/Distance/Stiffness/SwivelSpeed/TransitionSpeed +
//    InvertSwivelPitch/CameraShake.
//  - CarBaller settings.h (RL ranges only): swivel 1..10, transition 1..2.
// Blend SHAPE stays linear (upstream rl_camera.cpp:398-403 is linear too —
// a constant per-frame step toward the target mode). Ported 2026-10-20 (#6
// transition re-time, docs/carballer-facts/rl_camera.cpp:398-403): the RATE
// is 0.5+2.5*transitionSpeed units/s, so 1.2 swings in ~0.29 s (was the flat
// transitionSpeed units/s, ~0.83 s at 1.2). No INI key, no speed/FOV change.
// Ported 2026-10-20 (#4 sphere-orbit swivel + snap-back,
// docs/carballer-facts/rl_camera.cpp:101-121,373-396): pitch clamp
// -0.8/+0.7, linear snap-back at 2x rate past 0.05 s idle (additive `snap`
// key), elevation floor clamp + view tilt remainder. The swivel RATE stays
// Rocket Science #2 (90 deg/1.3 s); the angle slider stays in the eye
// placement (not the view pitch). Ported 2026-10-20 (#5 one-euro
// look-target filter, docs/carballer-facts/rl_camera.cpp:466-489):
// cutoff 1.2+1.8*(1-blend)+0.01*derivS Hz, derivS = 3 Hz EMA of target
// speed; ballcam keeps the 1.2 Hz chatter floor, car-cam the 3.0 Hz
// tight floor. No INI key, no speed/FOV change.
#include "Camera.h"
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rl {
namespace {

// Swivel rate: 90 deg in 1.3 s at swivel 1.0, linear in the slider.
constexpr float kSwivelRate = (3.14159265f * 0.5f) / 1.3f; // rad/s per unit
// #4 sphere-orbit swivel + snap-back (port of rl_camera.cpp:101-121,
// 373-396): asymmetric orbit-pitch clamp, snap-back idle gate + return
// floor, eye floor/ride heights, elevation stop (keeps the eye behind
// the car, cos > 0). The swivel RATE stays ours (Rocket Science #2,
// 90 deg/1.3 s — cited; upstream's 0.7x is tuned, see CAMERA_NOTES.md).
constexpr float kSwivelPitchLo = -0.8f; // rad: look-down orbit stop
constexpr float kSwivelPitchHi = 0.7f;  // rad: look-up orbit stop
constexpr float kSwivelIdleMin = 0.05f; // s idle before snap-back starts
constexpr float kSwivelIdleEps = 0.004f;// stick noise still counts as idle
constexpr float kSnapFloor = 6.0f;      // rad/s: snap return-rate floor
constexpr float kEyeFloorZ = 16.0f;     // uu: eye stays above arena floor
constexpr float kCarRideHeight = 17.0f; // uu: car center above its surface
constexpr float kMaxElev = 1.5f;        // rad: orbit elevation stop
constexpr float kViewPitchMax = 1.4f;   // rad: tilt-augmented view clamp
constexpr float kSuperSpeed = 2200.0f;    // G8 supersonic threshold (caller flag)
constexpr float kPi = 3.14159265f;
// #1 ball-aim slew (port of rl_camera.cpp:47,74 kBallAimEps/kBallAimSlew):
// hold when ball horiz < 5 uu (bearing is noise on the vertical axis),
// chase the unwrapped bearing at 20 rad/s so overhead/roof crosses swing
// over ~0.15 s instead of teleporting in one frame.
constexpr float kBallAimEps = 5.0f;
constexpr float kBallAimSlew = 20.0f;
// #2 car-cam air-velocity heading (port of rl_camera.cpp:218-244,270-355):
// ground = rigid facing; air = eased onto the horizontal velocity heading,
// blend 200->900 uu/s (smoothstep), rate 2.5+7*stiffness; pitch-immune axis
// w/ half-turn continuity (air only — ground re-reads the raw bearing).
constexpr float kVelDirMin = 200.0f;
constexpr float kVelDirMax = 900.0f;
constexpr float kYawFollowRate = 12.0f; // ground: rigid (per-second ease)
constexpr float kAirEaseBase = 2.5f;    // 1/s at stiffness 0
constexpr float kAirEaseStiff = 7.0f;   // 1/s per unit of stiffness
constexpr float kAxisHalfTurn = 0.5f * kPi;
constexpr float kAxisMinFxy = 0.15f;    // nose projection too short: hold axis
// #3 stiffness world-space lag (port of rl_camera.cpp:87-92,413-464):
// followRate owns the eye follower in ALL modes; the rig-speed term caps
// the boost-run trail at kRigLagCap instead of settling v/rate behind.
constexpr float kFollowBase = 1.5f;     // 1/s at stiffness 0
constexpr float kFollowStiff = 14.0f;   // 1/s per unit of stiffness
constexpr float kRigLagCap = 40.0f;     // uu: trail saturation cap
// #5 one-euro look-target filter (port of rl_camera.cpp:94-100,466-489):
// the MIXED look target is low-passed with a speed-adaptive cutoff so the
// resting ball's contact micro-chatter keeps the cutoff low while real
// fast motion (swivels, flicks, camera swings) snaps it open.
constexpr float kTargetBaseFc = 1.2f;    // Hz: ballcam floor (kills chatter)
constexpr float kTargetCarBoost = 1.8f;  // Hz: extra tightness at blend 0
constexpr float kTargetDerivGain = 0.01f;// Hz per uu/s of target speed
constexpr float kTargetDerivHz = 3.0f;   // Hz: EMA smoothing of target speed

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

float wrapAngle(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a <= -kPi) a += 2.0f * kPi;
    return a;
}

} // namespace

Camera::Camera() = default;

// Minimal [camera] INI shim (schema v1 provisional, owned by A5).
// Accepts: fov, distance, height, angle, stiffness, ballcam,
// transition_speed, swivel_speed, shake, invert_swivel.
// If A5's schema renames keys, extend the alias table below and note it.
bool Camera::LoadFromIni(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char line[256]; bool inCam = false; bool any = false;
    auto trim = [](char* s) -> char* {
        while (*s==' '||*s=='\t') s++;
        char* e = s + std::strlen(s);
        while (e > s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\n'||e[-1]=='\r')) { *--e = 0; }
        return s;
    };
    while (std::fgets(line, sizeof(line), f)) {
        char* s = trim(line);
        if (!*s || *s==';' || *s=='#') continue;
        if (*s=='[') { inCam = (std::strncmp(s,"[camera]",8)==0); continue; }
        if (!inCam) continue;
        char* eq = std::strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* k = trim(s); char* v = trim(eq+1);
        float fv = (float)std::atof(v);
        if (!std::strcmp(k,"fov")) { params_.fov = fv; any = true; }
        else if (!std::strcmp(k,"distance")) { params_.distance = fv; any = true; }
        else if (!std::strcmp(k,"height")) { params_.height = fv; any = true; }
        else if (!std::strcmp(k,"angle")) { params_.angle = fv; any = true; }
        else if (!std::strcmp(k,"stiffness")) { params_.stiffness = fv; any = true; }
        else if (!std::strcmp(k,"transition_speed")) { params_.transitionSpeed = fv; any = true; }
        else if (!std::strcmp(k,"swivel_speed")) { params_.swivelSpeed = fv; any = true; }
        else if (!std::strcmp(k,"shake")) {
            params_.shake = (fv != 0 || !std::strcmp(v,"true") || !std::strcmp(v,"on"));
            any = true;
        }
        else if (!std::strcmp(k,"invert_swivel")) {
            params_.invertSwivel = (fv != 0 || !std::strcmp(v,"true") || !std::strcmp(v,"on"));
            any = true;
        }
        else if (!std::strcmp(k,"snap")) {
            params_.snap = (fv != 0 || !std::strcmp(v,"true") || !std::strcmp(v,"on"));
            any = true;
        }
        else if (!std::strcmp(k,"ballcam")) {
            params_.ballCam = (fv != 0 || !std::strcmp(v,"true") || !std::strcmp(v,"on"));
            any = true;
        }
        // NOTE(shim): if A5 renames e.g. distance->cam_dist, add alias here.
    }
    std::fclose(f);
    // Clamp to RL 1:1 ranges (FOV 60-110, Distance 100-400, Height 40-200,
    // Angle -30..15, Stiffness 0..1, Transition 1..2, Swivel 1..10).
    if (params_.fov < 60) params_.fov = 60; if (params_.fov > 110) params_.fov = 110;
    if (params_.distance < 100) params_.distance = 100; if (params_.distance > 400) params_.distance = 400;
    if (params_.height < 40) params_.height = 40; if (params_.height > 200) params_.height = 200;
    if (params_.angle < -30) params_.angle = -30; if (params_.angle > 15) params_.angle = 15;
    if (params_.stiffness < 0) params_.stiffness = 0; if (params_.stiffness > 1) params_.stiffness = 1;
    if (params_.transitionSpeed < 1) params_.transitionSpeed = 1;
    if (params_.transitionSpeed > 2) params_.transitionSpeed = 2;
    if (params_.swivelSpeed < 1) params_.swivelSpeed = 1;
    if (params_.swivelSpeed > 10) params_.swivelSpeed = 10;
    return any;
}

void Camera::Update(float dt, const float carPos[3], float carYaw,
                    const float ballPos[3], bool ballCamOn, float carSpeed,
                    bool supersonic, float swivelX, float swivelY,
                    const float carFwd[3], const float carVel[3],
                    bool airborne) {
    params_.ballCam = ballCamOn;
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f; // hitch guard: a stalled frame never swings far
    float stiff = clampf(params_.stiffness, 0.0f, 1.0f);

    // #4 sphere-orbit swivel + snap-back (rl_camera.cpp:373-396). The
    // offsets swing the rig around the car (yaw) and on the rig sphere
    // (pitch). Once the stick goes idle past kSwivelIdleMin, the offsets
    // return linearly at 2x swivel rate (kSnapFloor floor) — RL's "Snap
    // Camera to Default" — gated by the additive `snap` key. Tiny inputs
    // count as idle so stick noise can't hold the offset off-center.
    float sy = params_.invertSwivel ? -swivelY : swivelY;
    float swivelRate =
        kSwivelRate * clampf(params_.swivelSpeed, 1.0f, 10.0f); // rad/s
    yawOff_ += swivelX * swivelRate * dt;
    pitchOff_ =
        clampf(pitchOff_ + sy * swivelRate * dt, kSwivelPitchLo,
               kSwivelPitchHi);
    while (yawOff_ > kPi) yawOff_ -= 2 * kPi;
    while (yawOff_ < -kPi) yawOff_ += 2 * kPi;
    if (std::fabs(swivelX) > kSwivelIdleEps ||
        std::fabs(sy) > kSwivelIdleEps)
        swivelIdle_ = 0.0f;
    else
        swivelIdle_ += dt;
    if (params_.snap && swivelIdle_ > kSwivelIdleMin && dt > 0.0f) {
        float snapStep = std::fmax(swivelRate * 2.0f, kSnapFloor) * dt;
        yawOff_ -= clampf(yawOff_, -snapStep, snapStep);
        pitchOff_ -= clampf(pitchOff_, -snapStep, snapStep);
    }

    // Transition blend: 0 = car-cam, 1 = ball-cam, advancing at the RL
    // transition-speed rate 0.5+2.5*speed units/s (rl_camera.cpp:398-403),
    // so 1.2 swings in ~0.29 s.
    float blendTgt = ballCamOn ? 1.0f : 0.0f;
    if (!init_) {
        blend_ = blendTgt;
    } else if (dt > 0) {
        float tRate =
            0.5f + clampf(params_.transitionSpeed, 0.2f, 2.0f) * 2.5f;
        float step = tRate * dt;
        if (blend_ < blendTgt) blend_ = (blend_ + step < blendTgt) ? blend_ + step : blendTgt;
        else blend_ = (blend_ - step > blendTgt) ? blend_ - step : blendTgt;
    }

    // RL stiffness dynamics (Rocket Science #2): distance and angle breathe
    // with speed, scaled by (1-stiffness); the eye itself only lags with
    // ballcam OFF. Dynamic FOV rides along (+5 with speed, +5 supersonic)
    // and is independent of every slider.
    float frac = clampf(carSpeed / kSuperSpeed, 0.0f, 1.0f);
    float dynDist = (1.0f - stiff) * (100.0f * frac + (supersonic ? 14.7f : 0.0f));
    float dynAng = (1.0f - stiff) * 2.0f * frac; // degrees, toward zero
    effFov_ = params_.fov + 5.0f * frac + (supersonic ? 5.0f : 0.0f);
    float effDist = params_.distance + dynDist;
    float ang = (params_.angle + dynAng) * kPi / 180.0f;
    float back = effDist * std::cos(ang);
    float up0 = params_.height - effDist * std::sin(ang);
    // #4 sphere-orbit eye (rl_camera.cpp:105-121). R/e0 is our rig sphere
    // (dynamics + angle slider stay in the eye placement, per #1-#3);
    // pitchOff_ shifts the elevation, the floor stops the orbit (ground:
    // the surface the car rests on; air: the arena floor only, so the full
    // swing stays available where look-up is most used), kMaxElev keeps
    // the eye behind the car, and the pitch the orbit could not absorb
    // returns as tilt — the caller adds it to the view pitch so the
    // control never goes dead.
    float r = std::sqrt(back * back + up0 * up0);
    float e0 = std::atan2(up0, back);
    float elev = e0 - pitchOff_;
    {
        bool onGround = !airborne;
        float minZ = onGround
            ? std::fmax(kEyeFloorZ, carPos[2] - kCarRideHeight)
            : kEyeFloorZ;
        float sinMin = clampf((minZ - carPos[2]) / r, -1.0f, 1.0f);
        elev = std::fmax(elev, std::asin(sinMin));
        elev = clampf(elev, -kMaxElev, kMaxElev);
    }
    float tilt = pitchOff_ - (e0 - elev);
    float backS = r * std::cos(elev), upS = r * std::sin(elev);
    float cy = std::cos(yawOff_), syaw = std::sin(yawOff_);

    // #2 car-cam air-velocity heading (rl_camera.cpp:218-244,270-355).
    // Pitch-immune facing axis: the nose bearing, kept continuous frame to
    // frame so a pitch past vertical (which flips the bearing 180 deg with
    // no yaw turn) cannot orbit the camera. Held while the nose is within
    // ~81 deg of straight up/down (projection is not a direction there).
    // On the GROUND the raw bearing is re-read every frame (wheels define
    // the attitude), which also heals any air hold at landing.
    // The camera then sits behind the facing on the ground, or the blended
    // direction of travel in the air (smoothstep 200->900 uu/s on the unit
    // circle), eased rigidly (12/s) or with stiffness inertia (2.5+7/s).
    {
        float noseX = carFwd ? carFwd[0] : std::cos(carYaw);
        float noseY = carFwd ? carFwd[1] : std::sin(carYaw);
        float fxy = carFwd ? std::sqrt(noseX * noseX + noseY * noseY) : 1.0f;
        if (!carFwd || fxy > kAxisMinFxy) {
            float bearing = carFwd ? std::atan2(noseY, noseX) : carYaw;
            if (!airborne || !hasAxisYaw_) {
                axisYaw_ = bearing;
                hasAxisYaw_ = true;
            } else {
                float d = wrapAngle(bearing - axisYaw_);
                axisYaw_ = (std::fabs(d) <= kAxisHalfTurn)
                    ? bearing : wrapAngle(bearing + kPi);
            }
        }
        // (else: nose near-vertical — hold the last good axis.)
        float targetYaw = hasAxisYaw_ ? axisYaw_ : carYaw;
        if (airborne && carVel) {
            float speed2d =
                std::sqrt(carVel[0] * carVel[0] + carVel[1] * carVel[1]);
            float w = clampf((speed2d - kVelDirMin) / (kVelDirMax - kVelDirMin),
                             0.0f, 1.0f);
            w = w * w * (3.0f - 2.0f * w); // ease the handover, no kink
            if (w > 0.0f) {
                float velYaw = std::atan2(carVel[1], carVel[0]);
                float mx = std::cos(axisYaw_) +
                           w * (std::cos(velYaw) - std::cos(axisYaw_));
                float my = std::sin(axisYaw_) +
                           w * (std::sin(velYaw) - std::sin(axisYaw_));
                if (mx * mx + my * my > 1e-6f) targetYaw = std::atan2(my, mx);
            }
        }
        float yawRate = airborne ? (kAirEaseBase + kAirEaseStiff * stiff)
                                 : kYawFollowRate;
        if (!hasCarCam_) {
            carYaw_ = targetYaw; // anchor with the first-frame eye snap
            hasCarCam_ = true;
        } else if (dt > 0) {
            carYaw_ += wrapAngle(targetYaw - carYaw_) *
                       (1.0f - std::exp(-yawRate * dt));
        }
    }
    // Car-cam facing = the EASED heading (#2); the ball rig keeps its own
    // slewed aim (#1) below.
    float fx = std::cos(carYaw_), fy = std::sin(carYaw_);
    // #1 ball-aim slew (rl_camera.cpp:138-166): keep the bearing on one
    // continuous branch. measYaw_ unwraps the raw bearing frame to frame
    // (always the short way), ballYaw_ chases it at kBallAimSlew. Hold when
    // the ball sits on the car's vertical axis (bearing is noise).
    bool firstAim = !hasBallAim_;
    float slewStep = kBallAimSlew * dt;
    {
        float toBx = ballPos[0] - carPos[0], toBy = ballPos[1] - carPos[1];
        float horiz = std::sqrt(toBx * toBx + toBy * toBy);
        bool reliable = horiz > kBallAimEps;
        if (reliable) {
            float phi = std::atan2(toBy, toBx);
            if (firstAim) {
                ballYaw_ = measYaw_ = phi;  // reset/preset: anchor, don't ease
                hasBallAim_ = true;
            } else {
                measYaw_ = hasPrevPhi_ ? measYaw_ + wrapAngle(phi - prevPhi_) : phi;
                float d = measYaw_ - ballYaw_;
                if (d > slewStep) d = slewStep;
                else if (d < -slewStep) d = -slewStep;
                ballYaw_ += d;
            }
            prevPhi_ = phi;
            hasPrevPhi_ = true;
        } else if (firstAim) {
            ballYaw_ = measYaw_ = carYaw;  // no bearing: take car facing
            hasBallAim_ = true;
        }
        // (otherwise: hold — the bearing is noise.)
        if (ballYaw_ > kPi) { ballYaw_ -= 2 * kPi; measYaw_ -= 2 * kPi; }
        else if (ballYaw_ < -kPi) { ballYaw_ += 2 * kPi; measYaw_ += 2 * kPi; }
    }
    // One rig evaluation: behind-axis (bx,by) + look target. Ball rig sits
    // on the SLEWED car->ball axis (#1, 5 uu hold + 20 rad/s chase); car rig
    // sits behind facing. lookBehind mirrors both (held J rear view).
    auto rig = [&](bool ballMode, float want[3], float tgt[3]) {
        float bx, by;
        if (ballMode) {
            // Behind-axis = opposite the slewed ball bearing.
            bx = -std::cos(ballYaw_); by = -std::sin(ballYaw_);
        } else {
            bx = fx; by = fy;
        }
        float s = ballMode ? 1.0f : -1.0f;
        if (lookBehind_) s = -s;
        // Swivel yaw rotates the behind-axis around the car.
        float rx = bx * cy - by * syaw, ry = bx * syaw + by * cy;
        want[0] = carPos[0] + s * rx * backS;
        want[1] = carPos[1] + s * ry * backS;
        want[2] = carPos[2] + upS; // floor-kept by the #4 elevation clamp
        // (minZ 16 uu); the old flat 40 uu clamp is superseded by it.
        if (lookBehind_) {
            tgt[0] = carPos[0] - fx * 400; tgt[1] = carPos[1] - fy * 400;
            tgt[2] = carPos[2] + 40;
        } else if (ballMode) {
            // #1 vertical aim (rl_camera.cpp:172-179): same slew, minus the
            // branch (elevation is single-valued). Kills the dribble/roof
            // teleport kick; at rest it converges to the exact ball (our
            // angle slider stays in the eye placement, not the view pitch).
            float toTx = ballPos[0] - want[0];
            float toTy = ballPos[1] - want[1];
            float toTz = ballPos[2] - want[2];
            float len2d = std::sqrt(toTx * toTx + toTy * toTy);
            float measPitch = std::atan2(toTz, len2d > 1.0f ? len2d : 1.0f);
            if (firstAim) ballPitch_ = measPitch;
            else {
                float dp = measPitch - ballPitch_;
                if (dp > slewStep) dp = slewStep;
                else if (dp < -slewStep) dp = -slewStep;
                ballPitch_ += dp;
            }
            float lookYaw = ballYaw_ + yawOff_;
            // #4 tilt remainder (rl_camera.cpp:181): the pitch the orbit
            // could not absorb tilts the view. The angle slider stays in
            // the eye placement (not the view pitch — #1 note above).
            float pitch = clampf(ballPitch_ + tilt, -kViewPitchMax,
                                 kViewPitchMax);
            float cp = std::cos(pitch);
            float dx = cp * std::cos(lookYaw);
            float dy = cp * std::sin(lookYaw);
            float dz = std::sin(pitch);
            float len = std::sqrt(toTx * toTx + toTy * toTy + toTz * toTz);
            float dist = len > 1000.0f ? len : 1000.0f;
            tgt[0] = want[0] + dx * dist;
            tgt[1] = want[1] + dy * dist;
            tgt[2] = want[2] + dz * dist;
        } else {
            // Car-cam look direction follows the swivel yaw (free look);
            // the #4 tilt remainder pitches the view (rl_camera.cpp:364).
            // Rebuilt on the same ray, so with tilt == 0 the target is
            // the tuned 400-ahead/40-up point exactly.
            float ax = carPos[0] + (fx * cy - fy * syaw) * 400.0f;
            float ay = carPos[1] + (fx * syaw + fy * cy) * 400.0f;
            float az = carPos[2] + 40.0f;
            float dx = ax - want[0], dy = ay - want[1], dz = az - want[2];
            float len2d = std::sqrt(dx * dx + dy * dy);
            float basePitch =
                std::atan2(dz, len2d > 1.0f ? len2d : 1.0f);
            float lookYawC = std::atan2(dy, dx);
            float pitchC = clampf(basePitch + tilt, -kViewPitchMax,
                                  kViewPitchMax);
            float len =
                std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1.0f) len = 1.0f;
            float cpc = std::cos(pitchC);
            tgt[0] = want[0] + cpc * std::cos(lookYawC) * len;
            tgt[1] = want[1] + cpc * std::sin(lookYawC) * len;
            tgt[2] = want[2] + std::sin(pitchC) * len;
        }
    };
    float ballWant[3], ballTgt[3], carWant[3], carTgt[3];
    rig(true, ballWant, ballTgt);
    rig(false, carWant, carTgt);
    float want[3], tgt[3];
    for (int i = 0; i < 3; i++) {
        want[i] = carWant[i] + (ballWant[i] - carWant[i]) * blend_;
        tgt[i] = carTgt[i] + (ballTgt[i] - carTgt[i]) * blend_;
    }

    if (!init_) { // snap on first frame (anchors the follower, no swing)
        eye_[0]=want[0]; eye_[1]=want[1]; eye_[2]=want[2];
        center_[0]=tgt[0]; center_[1]=tgt[1]; center_[2]=tgt[2];
        smoothedEye_[0]=want[0]; smoothedEye_[1]=want[1]; smoothedEye_[2]=want[2];
        prevWantEye_[0]=want[0]; prevWantEye_[1]=want[1]; prevWantEye_[2]=want[2];
        prevRigOffZ_=want[2]-carPos[2];
        hasSmoothed_=true;
        // #5 anchors the target filter too, so rest geometry is exact.
        smoothTarget_[0]=tgt[0]; smoothTarget_[1]=tgt[1]; smoothTarget_[2]=tgt[2];
        prevWantT_[0]=tgt[0]; prevWantT_[1]=tgt[1]; prevWantT_[2]=tgt[2];
        derivS_=0.0f;
        hasSmoothTarget_=true;
        init_ = true;
        return;
    }
    // #3 stiffness world-space lag (rl_camera.cpp:413-464). The MIXED ideal
    // eye (both rigs blended above) passes through smoothing in EVERY mode
    // — ballcam included — at followRate = 1.5+14*stiffness per second.
    // Vertical runs at half rate while airborne, so jumps/aerials linger
    // near the takeoff spot instead of climbing rigidly. The rig-speed term
    // saturates the trail at kRigLagCap: rigid at boost speed like RL,
    // while rest and low-speed recentering keep the stiffness response.
    // The vertical term runs on rig-relative height (want.z - car.z), which
    // splits the swivel's own vertical orbit (same saturation as
    // horizontal) off from the car's climb (keeps the slow linger). With
    // no vertical swivel the split term is identically zero. Per-frame
    // factors stay in (0,1) via 1-exp(-rate*dt). Speed/FOV dynamics above
    // (dynDist/dynAng/effFov_) are untouched.
    float followRate = kFollowBase + stiff * kFollowStiff;
    float baseRateZ = airborne ? followRate * 0.5f : followRate;
    float dMax = dt > 1e-4f ? dt : 1e-4f;
    float rigDx = want[0] - prevWantEye_[0];
    float rigDy = want[1] - prevWantEye_[1];
    float rigSpeed2d =
        std::sqrt(rigDx * rigDx + rigDy * rigDy) / dMax;
    float rateX = followRate + rigSpeed2d / kRigLagCap;
    prevWantEye_[0]=want[0]; prevWantEye_[1]=want[1]; prevWantEye_[2]=want[2];
    float rigOffZ = want[2] - carPos[2];
    float rigVz = (rigOffZ - prevRigOffZ_) / dMax;
    prevRigOffZ_ = rigOffZ;
    float rateZ = baseRateZ + std::fabs(rigVz) / kRigLagCap;
    float kx = 1.0f - std::exp(-rateX * dt);
    float kz = 1.0f - std::exp(-rateZ * dt);
    if (kx < 0) kx = 0; if (kx > 1) kx = 1;
    if (kz < 0) kz = 0; if (kz > 1) kz = 1;
    smoothedEye_[0] += (want[0] - smoothedEye_[0]) * kx;
    smoothedEye_[1] += (want[1] - smoothedEye_[1]) * kx;
    smoothedEye_[2] += (want[2] - smoothedEye_[2]) * kz;
    eye_[0]=smoothedEye_[0]; eye_[1]=smoothedEye_[1]; eye_[2]=smoothedEye_[2];
    // #5 one-euro filter on the look-at point (rl_camera.cpp:466-489).
    // The ball's resting contact with the car micro-bounces it several
    // times a second; aiming straight at it wobbled the whole view (the
    // perceived "car rumble" while dribbling). A fixed low cutoff would
    // also lag real motion, so the cutoff rises with the target's
    // (smoothed) speed: chatter keeps the cutoff low, swivels/flicks snap
    // it open fast. Car-cam (blend 0) keeps the higher floor so the view
    // stays tightly on the aim through maneuvers; ballcam (blend 1) keeps
    // the low floor that kills the resting ball's contact chatter. Speed
    // /FOV dynamics above (dynDist/dynAng/effFov_) are untouched.
    if (dt <= 0.0f) {
        center_[0]=smoothTarget_[0]; center_[1]=smoothTarget_[1];
        center_[2]=smoothTarget_[2];
        return;
    }
    if (!hasSmoothTarget_) {
        smoothTarget_[0]=tgt[0]; smoothTarget_[1]=tgt[1];
        smoothTarget_[2]=tgt[2];
        prevWantT_[0]=tgt[0]; prevWantT_[1]=tgt[1]; prevWantT_[2]=tgt[2];
        derivS_=0.0f;
        hasSmoothTarget_=true;
    } else {
        float rawDx = tgt[0]-prevWantT_[0], rawDy = tgt[1]-prevWantT_[1],
              rawDz = tgt[2]-prevWantT_[2];
        float rawDeriv =
            std::sqrt(rawDx*rawDx + rawDy*rawDy + rawDz*rawDz) / dMax;
        prevWantT_[0]=tgt[0]; prevWantT_[1]=tgt[1]; prevWantT_[2]=tgt[2];
        derivS_ += (rawDeriv - derivS_) *
                   (1.0f - std::exp(-2.0f * kPi * kTargetDerivHz * dt));
        float fc = kTargetBaseFc + kTargetCarBoost * (1.0f - blend_) +
                   kTargetDerivGain * derivS_;
        float k = 1.0f - std::exp(-2.0f * kPi * fc * dt);
        if (k < 0) k = 0; if (k > 1) k = 1;
        smoothTarget_[0] += (tgt[0] - smoothTarget_[0]) * k;
        smoothTarget_[1] += (tgt[1] - smoothTarget_[1]) * k;
        smoothTarget_[2] += (tgt[2] - smoothTarget_[2]) * k;
    }
    center_[0]=smoothTarget_[0]; center_[1]=smoothTarget_[1];
    center_[2]=smoothTarget_[2];
}

static void LookAt(const float eye[3], const float c[3]) {
    float f[3] = { c[0]-eye[0], c[1]-eye[1], c[2]-eye[2] };
    float l = std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if (l < 1e-6f) return;
    f[0]/=l; f[1]/=l; f[2]/=l;
    float up[3] = {0,0,1};
    float s[3] = { f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0] };
    l = std::sqrt(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]);
    if (l < 1e-6f) return;
    s[0]/=l; s[1]/=l; s[2]/=l;
    float u[3] = { s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0] };
    float m[16] = {
        s[0], u[0], -f[0], 0,
        s[1], u[1], -f[1], 0,
        s[2], u[2], -f[2], 0,
        0, 0, 0, 1 };
    glMultMatrixf(m);
    glTranslatef(-eye[0], -eye[1], -eye[2]);
}

void Camera::SetFreeView(const float eye[3], const float center[3]) {
    eye_[0] = eye[0];
    eye_[1] = eye[1];
    eye_[2] = eye[2];
    center_[0] = center[0];
    center_[1] = center[1];
    center_[2] = center[2];
    init_ = true;
}

void Camera::Apply(int viewW, int viewH) const {
    float aspect = viewH > 0 ? (float)viewW / (float)viewH : 16.0f/9.0f;
    float fovY = effFov_;
    // RL FOV slider is horizontal-ish; our projection is vertical fov.
    // Convert approx: keep horizontal extent constant vs 16:9 reference.
    // fovY = 2*atan(tan(fovX/2)/aspect). Treat slider as fovX at 16:9.
    {
        float fx = fovY * kPi/180.0f;
        float fy = 2.0f * std::atan(std::tan(fx*0.5f) / aspect * (16.0f/9.0f));
        fovY = fy * 180.0f / kPi;
        if (fovY < 40) fovY = 40; if (fovY > 130) fovY = 130;
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float zn = 10.0f, zf = 40000.0f;
    float t = zn * std::tan(fovY * 0.5f * kPi/180.0f);
    float r = t * aspect;
    glFrustum(-r, r, -t, t, zn, zf);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    LookAt(eye_, center_);
}

} // namespace rl
