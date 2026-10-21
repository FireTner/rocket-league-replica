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
//
// ── P0 layer split (MASTER_PROMPT_CAMERA_EXACT.md §3) ────────────────────────
// One evaluation = named layers with one job each, behind the unchanged public
// API (Update/Apply/Eye/Center/EffFov/Blend/SwivelYaw/Pitch/Params/
// SetFreeView/SetLookBehind). P0 is a refactor: every numeric expression below
// is the pre-split one, in the pre-split order, so the output is
// bit-identical to the CarBaller port (proved by /tmp/p0_golden diff, not by
// eye). Units are currently
//   UpdateSwivel/UpdateTransition/ClampPov .. radians, seconds, world uu
//   UpdateDynamics ....................... radians + degrees at angRad only
//   UpdateCarFacing/UpdateBallAim ........ radians, world uu
//   BallCamPov/CarPov/RearViewPov ........ writes TruthPov in ROTATOR units
//                                          (65536 = 360 deg) + world-space focus,
//                                          and carries the exact GL pair
//   BlendCameraState ..................... blends both (yaw on the short arc)
//   FollowPov ............................ radians/world uu (our #3/#5 filters)
//   Apply() .............................. degrees + GL only here (§3.5)
// P3 replaces the radians internals with rotator/UE-space math and the
// one-euro + EMA pair with RL InterpTo; P0 must not, or feel moves.
//
// Layer order vs RL (deliberate in P0, behavior-preserving): RL runs
// state POV → blend → free-look/swivel → clamp/knocks. Ours runs the swivel
// OFFSET STATE first because the rig sphere (radius, elevation clamp, tilt)
// already consumes it while the states place their eyes; the snapshot
// pipeline itself is state → blend → clamp. Moving the offset application
// after the blend is a behavior change (P4 slice 1/2, gated on a trace).
#include "Camera.h"
#include "TruthPov.h"
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
// ── decompiled constants deliberately NOT ported by P3 ──────────────────────
// Each is a REAL RL value (docs/CAMERA_EXACT_LOG.md Rounds 8/11/12) and each
// moves the feel with no trace scenario to gate it yet, so P3 records them
// here and leaves the tuned value in place. Porting any of them without its
// scenario would be an ungated feel change (§2.2 / Rule 5):
//   // decomp: TAGame.CameraState_TA.MaxFreeLookPitchDegrees = 75.0
//       RL clamps free-look pitch to a symmetric +-75 deg; ours is the
//       asymmetric kSwivelPitchLo/Hi = -0.8/+0.7 rad (-45.8/+40.1 deg).
//       -> P4 slice 2 (free look as its own layer), scenario swivel-sweep-release.
//   // decomp: ProjectX.CameraStateBlender_X.Transition.BlendParams
//       = (BlendFunction VTBlend_Cubic, BlendExp 2.0, bLockOutgoing false);
//       UpdateTransition below is a linear step with our invented
//       0.5+2.5*transitionSpeed rate.
//       -> P4 slice 3-adjacent (Round 12), gated on the ballcam_toggle scenario.
//   // decomp: TAGame.Camera_TA.GroundClampZOffset = 10.0
//       RL offsets the ground clamp trace by 10 uu; ours is kEyeFloorZ = 16.0
//       -> P4 slice 4 (field clip: ClipToField/bGroundClampTrace).
constexpr float kSuperSpeed = 2200.0f;    // G8 supersonic threshold (caller flag)
constexpr float kPi = 3.14159265f;
// §3 units: a UE rotator is 65536 units per 360 deg. Everything above the
// TruthPov boundary stays in radians (P3 converts); this pair is the only
// bridge, used when a snapshot is written or blended.
constexpr float kRotFull = 65536.0f;              // rotator units / 360 deg
constexpr float kRotPerRad = kRotFull / (2.0f * kPi);
constexpr float kRotHalf = 0.5f * kRotFull;
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

// §3.1 — rotator units (65536 = 360 deg) of a world-space look direction.
// UE space is X forward / Y right / Z up: pitch is about Y (+ = up), yaw about
// Z. Roll stays 0 (RL Camera_X::RemoveRoll; our rig never rolls). Our yaw is
// atan2(y, x) over a Z-up world; reconciling the FEEL of RL's handedness and
// any sign flip is P3's trace work, not a P0 guess.
void rotationFromDir(const float dir[3], float out[3]) {
    float len2d = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
    out[0] = std::atan2(dir[2], len2d) * kRotPerRad;
    out[1] = std::atan2(dir[1], dir[0]) * kRotPerRad;
    out[2] = 0.0f;
}

// Inverse of rotationFromDir (the RL direction of the same fact, §3.5): the UE
// view axis of a rotator, pitch about Y, yaw about Z, roll ignored (RemoveRoll).
// Used only to keep Pov().focus on the ray Pov().rotation names.
void forwardFromRotator(const float rot[3], float out[3]) {
    float p = rot[0] / kRotPerRad;
    float y = rot[1] / kRotPerRad;
    float cp = std::cos(p);
    out[0] = cp * std::cos(y);
    out[1] = cp * std::sin(y);
    out[2] = std::sin(p);
}

} // namespace

// ── §3 per-frame carriers ───────────────────────────────────────────────────
// Inputs come straight from Update(); each derived field is written by exactly
// one layer and read by a later one (noted per field). The structs are defined
// here, so Camera.h only needs the forward declarations: one owner, one TU.
struct Camera::Frame {
    // inputs (Update() arguments, already clamped)
    float dt = 0.0f;             // s, clamped to [0, 0.1] by Update()
    const float* carPos = nullptr;
    const float* ballPos = nullptr;
    float carYaw = 0.0f;         // rad, 0 = +X
    const float* carFwd = nullptr;   // unit nose vector (may be null)
    const float* carVel = nullptr;   // velocity uu/s (may be null)
    float carSpeed = 0.0f;       // uu/s (drives the RL speed dynamics)
    bool supersonic = false;     // caller's G8 flag (2200 uu/s)
    bool airborne = false;
    bool ballCamOn = false;
    float swivelX = 0.0f;        // [-1,1], caller-deadzoned stick
    float swivelY = 0.0f;
    float stiff = 0.0f;          // params_.stiffness clamped to [0,1]
    bool lookBehind = false;     // held rear view (SetLookBehind)
    // derived by UpdateCarFacing (§3.1), read by CarPov/RearViewPov
    float fx = 1.0f, fy = 0.0f;  // eased car facing = cos/sin(carYaw_)
    // derived by UpdateBallAim (§3.1), read by BallCamPov
    bool firstAim = false;       // this frame anchors the aim (no slew)
    float slewStep = 0.0f;       // rad, largest aim move allowed this frame
};

// §3.1 distance/FOV interp output (RL CameraState_Car_TA.UpdateDistance /
// .UpdateFOV / .SupersonicFOV). The FOV lands in effFov_ (EffFov()).
struct Camera::Dynamics {
    float frac = 0.0f;    // speed fraction of the supersonic threshold (reported)
    float effDist = 0.0f; // uu: slider distance + the speed-scaled offset
    float angRad = 0.0f;  // rad: eye pitch (angle + dynamic angle) (reported)
    float back = 0.0f;    // uu: horizontal rig offset behind the car
    float up0 = 0.0f;     // uu: vertical rig offset above the car
};

// §3.3+4 admitted eye orbit: the swivel offset applied to the rig sphere, then
// clamped (eye floor, ±kMaxElev). `tilt` is the pitch the clamp refused — it
// returns as view pitch so the look control never goes dead.
struct Camera::Orbit {
    float r = 0.0f;             // uu: rig sphere radius (reported: P4's clip)
    float elev = 0.0f;          // rad: admitted elevation (reported: P3 trace)
    float tilt = 0.0f;          // rad: unabsorbed pitch -> view tilt
    float backS = 0.0f;         // uu: horizontal eye offset at this elevation
    float upS = 0.0f;           // uu: vertical eye offset at this elevation
    float cy = 1.0f, sy = 0.0f; // cos/sin of the swivel yaw offset
};

// §3 one POV snapshot: the RL-shaped TruthPov plus the exact GL pair published
// through Eye()/Center(). `eye` is mathematically focus - forward*Distance and
// `center` is the far look-at point our fixed-function Apply() needs; both are
// carried rather than re-derived so the float path stays bit-identical to the
// pre-split camera (the P0 contract). P3 makes rotation authoritative and the
// carried pair can then go away.
struct Camera::PovSnapshot {
    TruthPov pov{};
    float eye[3] = {0.0f, 0.0f, 0.0f};
    float center[3] = {0.0f, 0.0f, 0.0f};
};

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
    // Clamp to the RL slider ranges. P3 (2026-10-20) confirmed every range below
    // against the decompiled `Default__Camera_TA` CDO
    // (`~/rl-decomp/decompiled/cdo_tables.txt`, docs/CAMERA_EXACT_LOG.md
    // Round 8) — they were guide-sourced before and are measured now, so the
    // values are unchanged and the provenance comments are new:
    //   // decomp: TAGame.Camera_TA.FOVLimits             (Min=60,  Max=110)
    //   // decomp: TAGame.Camera_TA.DistanceLimits        (Min=100, Max=400)
    //   // decomp: TAGame.Camera_TA.HeightLimits          (Min=40,  Max=200)
    //   // decomp: TAGame.Camera_TA.StiffnessLimits       (Min=0,   Max=1)
    //   // decomp: TAGame.Camera_TA.TransitionSpeedLimits (Min=1,   Max=2)
    //   // decomp: TAGame.Camera_TA.SwivelSpeedLimits     (Min=1,   Max=10)
    // NOT ported: `angle`. RL's is
    //   // decomp: TAGame.Camera_TA.AngleLimits            (Min=-15, Max=0)
    // while we (and src/settings/Settings.cpp, A5) accept -30..15. Narrowing it
    // here ALONE would make Settings accept an angle Camera then silently
    // rewrites (AGENTS.md Rule 6 / Rule 5 divergence), so it is reported as an
    // A5 schema item — Settings.cpp + docs/INI_SCHEMA.md + the menu + a failing
    // roundtrip test first. Our -3 default is legal in RL's range, so the rest
    // geometry is identical either way.
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

// §3.3 — swivel / free-look offset state: the right stick orbits the rig
// around the car (yaw) and on the rig sphere (pitch). Once the stick goes idle
// past kSwivelIdleMin the offsets return linearly at 2x swivel rate
// (kSnapFloor floor) — RL's "Snap Camera to Default" — gated by the additive
// `snap` key. Tiny inputs count as idle so stick noise can't hold the offset
// off-center. (P4 slice 1 replaces this 2x snap with RL's SwivelDieRate /
// SwivelFastSpeed / SwivelExtentFast/Slow decay; P0 keeps the tuned model.)
void Camera::UpdateSwivel(const Frame& f) {
    float sy = params_.invertSwivel ? -f.swivelY : f.swivelY;
    float swivelRate =
        kSwivelRate * clampf(params_.swivelSpeed, 1.0f, 10.0f); // rad/s
    yawOff_ += f.swivelX * swivelRate * f.dt;
    pitchOff_ =
        clampf(pitchOff_ + sy * swivelRate * f.dt, kSwivelPitchLo,
               kSwivelPitchHi);
    while (yawOff_ > kPi) yawOff_ -= 2 * kPi;
    while (yawOff_ < -kPi) yawOff_ += 2 * kPi;
    if (std::fabs(f.swivelX) > kSwivelIdleEps ||
        std::fabs(sy) > kSwivelIdleEps)
        swivelIdle_ = 0.0f;
    else
        swivelIdle_ += f.dt;
    if (params_.snap && swivelIdle_ > kSwivelIdleMin && f.dt > 0.0f) {
        float snapStep = std::fmax(swivelRate * 2.0f, kSnapFloor) * f.dt;
        yawOff_ -= clampf(yawOff_, -snapStep, snapStep);
        pitchOff_ -= clampf(pitchOff_, -snapStep, snapStep);
    }
}

// §3.2 — Transition alpha: 0 = car-cam, 1 = ball-cam, advancing at the RL
// transition-speed rate 0.5+2.5*speed per second (rl_camera.cpp:398-403), so
// 1.2 swings in ~0.29 s. RL's CameraStateBlender_X carries this as
// InterpRate/RemainingTime over a Transition; ours is the same scalar. The
// first frame snaps instead of blending (SnapTransition, see Update()).
void Camera::UpdateTransition(const Frame& f) {
    float blendTgt = f.ballCamOn ? 1.0f : 0.0f;
    if (!init_) {
        blend_ = blendTgt;
    } else if (f.dt > 0) {
        float tRate =
            0.5f + clampf(params_.transitionSpeed, 0.2f, 2.0f) * 2.5f;
        float step = tRate * f.dt;
        if (blend_ < blendTgt) blend_ = (blend_ + step < blendTgt) ? blend_ + step : blendTgt;
        else blend_ = (blend_ - step > blendTgt) ? blend_ - step : blendTgt;
    }
}

// §3.1 — distance/FOV interp (RL stiffness dynamics, Rocket Science #2):
// distance and angle breathe with speed, scaled by (1-stiffness); the eye
// itself only lags with ballcam OFF. Dynamic FOV rides along (+5 with speed,
// +5 supersonic) and is independent of every slider. At rest every delta is
// exactly zero, so static captures stay pixel-identical.
void Camera::UpdateDynamics(const Frame& f, Dynamics& d) {
    float frac = clampf(f.carSpeed / kSuperSpeed, 0.0f, 1.0f);
    float dynDist = (1.0f - f.stiff) * (100.0f * frac + (f.supersonic ? 14.7f : 0.0f));
    float dynAng = (1.0f - f.stiff) * 2.0f * frac; // degrees, toward zero
    effFov_ = params_.fov + 5.0f * frac + (f.supersonic ? 5.0f : 0.0f);
    float effDist = params_.distance + dynDist;
    float ang = (params_.angle + dynAng) * kPi / 180.0f;
    d.frac = frac;
    d.effDist = effDist;
    d.angRad = ang;
    d.back = effDist * std::cos(ang);
    d.up0 = params_.height - effDist * std::sin(ang);
}

// §3.1 — car facing / air-velocity heading (rl_camera.cpp:218-244,270-355).
// Pitch-immune facing axis: the nose bearing, kept continuous frame to frame so
// a pitch past vertical (which flips the bearing 180 deg with no yaw turn)
// cannot orbit the camera. Held while the nose is within ~81 deg of straight
// up/down (projection is not a direction there). On the GROUND the raw bearing
// is re-read every frame (wheels define the attitude), which also heals any air
// hold at landing. The camera then sits behind the facing on the ground, or the
// blended direction of travel in the air (smoothstep 200->900 uu/s on the unit
// circle), eased rigidly (12/s) or with stiffness inertia (2.5+7/s).
void Camera::UpdateCarFacing(Frame& f) {
    {
        float noseX = f.carFwd ? f.carFwd[0] : std::cos(f.carYaw);
        float noseY = f.carFwd ? f.carFwd[1] : std::sin(f.carYaw);
        float fxy = f.carFwd ? std::sqrt(noseX * noseX + noseY * noseY) : 1.0f;
        if (!f.carFwd || fxy > kAxisMinFxy) {
            float bearing = f.carFwd ? std::atan2(noseY, noseX) : f.carYaw;
            if (!f.airborne || !hasAxisYaw_) {
                axisYaw_ = bearing;
                hasAxisYaw_ = true;
            } else {
                float d = wrapAngle(bearing - axisYaw_);
                axisYaw_ = (std::fabs(d) <= kAxisHalfTurn)
                    ? bearing : wrapAngle(bearing + kPi);
            }
        }
        // (else: nose near-vertical — hold the last good axis.)
        float targetYaw = hasAxisYaw_ ? axisYaw_ : f.carYaw;
        if (f.airborne && f.carVel) {
            float speed2d =
                std::sqrt(f.carVel[0] * f.carVel[0] + f.carVel[1] * f.carVel[1]);
            float w = clampf((speed2d - kVelDirMin) / (kVelDirMax - kVelDirMin),
                             0.0f, 1.0f);
            w = w * w * (3.0f - 2.0f * w); // ease the handover, no kink
            if (w > 0.0f) {
                float velYaw = std::atan2(f.carVel[1], f.carVel[0]);
                float mx = std::cos(axisYaw_) +
                           w * (std::cos(velYaw) - std::cos(axisYaw_));
                float my = std::sin(axisYaw_) +
                           w * (std::sin(velYaw) - std::sin(axisYaw_));
                if (mx * mx + my * my > 1e-6f) targetYaw = std::atan2(my, mx);
            }
        }
        float yawRate = f.airborne ? (kAirEaseBase + kAirEaseStiff * f.stiff)
                                   : kYawFollowRate;
        if (!hasCarCam_) {
            carYaw_ = targetYaw; // anchor with the first-frame eye snap
            hasCarCam_ = true;
        } else if (f.dt > 0) {
            carYaw_ += wrapAngle(targetYaw - carYaw_) *
                       (1.0f - std::exp(-yawRate * f.dt));
        }
    }
    // Car-cam facing = the EASED heading (#2); the ball rig keeps its own
    // slewed aim (#1) in UpdateBallAim. Published for both state layers.
    f.fx = std::cos(carYaw_);
    f.fy = std::sin(carYaw_);
}

// §3.1 — ball-cam focus slew (rl_camera.cpp:138-166): keep the bearing on one
// continuous branch. measYaw_ unwraps the raw bearing frame to frame (always
// the short way), ballYaw_ chases it at kBallAimSlew. Hold when the ball sits
// on the car's vertical axis (bearing is noise). RL's equivalent is
// CameraState_BallCam_TA::UpdateFocus with .RotationRate; the 20 rad/s is ours
// (P3 fits the rate against a trace).
void Camera::UpdateBallAim(Frame& f) {
    f.firstAim = !hasBallAim_;
    f.slewStep = kBallAimSlew * f.dt;
    {
        float toBx = f.ballPos[0] - f.carPos[0], toBy = f.ballPos[1] - f.carPos[1];
        float horiz = std::sqrt(toBx * toBx + toBy * toBy);
        bool reliable = horiz > kBallAimEps;
        if (reliable) {
            float phi = std::atan2(toBy, toBx);
            if (f.firstAim) {
                ballYaw_ = measYaw_ = phi;  // reset/preset: anchor, don't ease
                hasBallAim_ = true;
            } else {
                measYaw_ = hasPrevPhi_ ? measYaw_ + wrapAngle(phi - prevPhi_) : phi;
                float d = measYaw_ - ballYaw_;
                if (d > f.slewStep) d = f.slewStep;
                else if (d < -f.slewStep) d = -f.slewStep;
                ballYaw_ += d;
            }
            prevPhi_ = phi;
            hasPrevPhi_ = true;
        } else if (f.firstAim) {
            ballYaw_ = measYaw_ = carYaw_;  // no bearing: take car facing
            hasBallAim_ = true;
        }
        // (otherwise: hold — the bearing is noise.)
        if (ballYaw_ > kPi) { ballYaw_ -= 2 * kPi; measYaw_ -= 2 * kPi; }
        else if (ballYaw_ < -kPi) { ballYaw_ += 2 * kPi; measYaw_ += 2 * kPi; }
    }
}

// §3.3+4 — orbit admission. R/e0 is our rig sphere (dynamics + angle slider
// stay in the eye placement, per #1-#3); pitchOff_ shifts the elevation, the
// floor stops the orbit (ground: the surface the car rests on; air: the arena
// floor only, so the full swing stays available where look-up is most used),
// kMaxElev keeps the eye behind the car, and the pitch the orbit could not
// absorb returns as tilt — the caller adds it to the view pitch so the control
// never goes dead. This is the eye-placement half of RL Camera_X::ClampPOV;
// its field clip (ClipToField/bGroundClampTrace/GroundClampZOffset) and knocks
// are the P4 slice.
void Camera::ClampPov(const Frame& f, const Dynamics& d, Orbit& o) const {
    float r = std::sqrt(d.back * d.back + d.up0 * d.up0);
    float e0 = std::atan2(d.up0, d.back);
    float elev = e0 - pitchOff_;
    {
        bool onGround = !f.airborne;
        float minZ = onGround
            ? std::fmax(kEyeFloorZ, f.carPos[2] - kCarRideHeight)
            : kEyeFloorZ;
        float sinMin = clampf((minZ - f.carPos[2]) / r, -1.0f, 1.0f);
        elev = std::fmax(elev, std::asin(sinMin));
        elev = clampf(elev, -kMaxElev, kMaxElev);
    }
    o.r = r;
    o.elev = elev;
    o.tilt = pitchOff_ - (e0 - elev);
    o.backS = r * std::cos(elev);
    o.upS = r * std::sin(elev);
    o.cy = std::cos(yawOff_);
    o.sy = std::sin(yawOff_);
}

// §3.4 — RL Camera_TA::ApplyRearCameraView (held J): look back over the car
// instead of forward. Both states share this look target; the eye behind-axis
// is mirrored by the caller. In our decomposition the rear view is applied
// while a state writes its POV; RL applies it after the state POV. It is the
// one place that derives rotation from a direction (two atan2s), because no
// state angle exists for the rear ray; only held keys take this path.
void Camera::RearViewPov(const Frame& f, const Dynamics& d,
                         PovSnapshot& out) const {
    out.center[0] = f.carPos[0] - f.fx * 400;
    out.center[1] = f.carPos[1] - f.fy * 400;
    out.center[2] = f.carPos[2] + 40;
    float dir[3] = { out.center[0] - out.eye[0], out.center[1] - out.eye[1],
                     out.center[2] - out.eye[2] };
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len < 1e-6f) return;
    dir[0] /= len; dir[1] /= len; dir[2] /= len;
    rotationFromDir(dir, out.pov.rotation);
    out.pov.distance = d.effDist;
    out.pov.fov = effFov_;
    out.pov.time = povTime_;
    out.pov.focus[0] = out.eye[0] + dir[0] * d.effDist;
    out.pov.focus[1] = out.eye[1] + dir[1] * d.effDist;
    out.pov.focus[2] = out.eye[2] + dir[2] * d.effDist;
}

// §3.1 — ball-cam state writes its desired POV: eye on the SLEWED car->ball
// axis (opposite the bearing, #1: 5 uu hold + 20 rad/s chase), focus on the
// ball through the slewed vertical aim, then the RL-shaped snapshot. Advances
// ballPitch_ (state, so this one is non-const) exactly as the pre-split rig did.
void Camera::BallCamPov(const Frame& f, const Dynamics& d, const Orbit& o,
                        PovSnapshot& out) {
    // Behind-axis = opposite the slewed ball bearing.
    float bx = -std::cos(ballYaw_);
    float by = -std::sin(ballYaw_);
    float s = 1.0f;
    if (f.lookBehind) s = -s;
    // Swivel yaw rotates the behind-axis around the car.
    float rx = bx * o.cy - by * o.sy, ry = bx * o.sy + by * o.cy;
    out.eye[0] = f.carPos[0] + s * rx * o.backS;
    out.eye[1] = f.carPos[1] + s * ry * o.backS;
    out.eye[2] = f.carPos[2] + o.upS; // floor-kept by ClampPov; the old flat
                                      // 40 uu clamp is superseded by it.
    if (f.lookBehind) { RearViewPov(f, d, out); return; }
    // #1 vertical aim (rl_camera.cpp:172-179): same slew, minus the branch
    // (elevation is single-valued). Kills the dribble/roof teleport kick; at
    // rest it converges to the exact ball (our angle slider stays in the eye
    // placement, not the view pitch).
    float toTx = f.ballPos[0] - out.eye[0];
    float toTy = f.ballPos[1] - out.eye[1];
    float toTz = f.ballPos[2] - out.eye[2];
    float len2d = std::sqrt(toTx * toTx + toTy * toTy);
    float measPitch = std::atan2(toTz, len2d > 1.0f ? len2d : 1.0f);
    if (f.firstAim) ballPitch_ = measPitch;
    else {
        float dp = measPitch - ballPitch_;
        if (dp > f.slewStep) dp = f.slewStep;
        else if (dp < -f.slewStep) dp = -f.slewStep;
        ballPitch_ += dp;
    }
    float lookYaw = ballYaw_ + yawOff_;
    // #4 tilt remainder (rl_camera.cpp:181): the pitch the orbit could not
    // absorb tilts the view. The angle slider stays in the eye placement (not
    // the view pitch — #1 note above). The clamp is ours; RL's symmetric
    // MaxFreeLookPitchDegrees is the P4 slice.
    float pitch = clampf(ballPitch_ + o.tilt, -kViewPitchMax, kViewPitchMax);
    float cp = std::cos(pitch);
    float dx = cp * std::cos(lookYaw);
    float dy = cp * std::sin(lookYaw);
    float dz = std::sin(pitch);
    float len = std::sqrt(toTx * toTx + toTy * toTy + toTz * toTz);
    float dist = len > 1000.0f ? len : 1000.0f;
    out.center[0] = out.eye[0] + dx * dist;
    out.center[1] = out.eye[1] + dy * dist;
    out.center[2] = out.eye[2] + dz * dist;
    // §3 POV snapshot: rotation = the aim just built, in rotator units;
    // distance = the RL distance setting; focus = eye + forward*distance.
    // Roll stays 0 (RemoveRoll — our rig never rolls).
    out.pov.rotation[0] = pitch * kRotPerRad;
    out.pov.rotation[1] = lookYaw * kRotPerRad;
    out.pov.rotation[2] = 0.0f;
    out.pov.distance = d.effDist;
    out.pov.fov = effFov_;
    out.pov.time = povTime_;
    out.pov.focus[0] = out.eye[0] + dx * d.effDist;
    out.pov.focus[1] = out.eye[1] + dy * d.effDist;
    out.pov.focus[2] = out.eye[2] + dz * d.effDist;
}

// §3.1 — car-cam state writes its desired POV: eye behind the eased facing at
// Distance/Height/Angle, focus on the 400-ahead/40-up point. The look direction
// follows the swivel yaw (free look) and the #4 tilt remainder pits the view;
// it is rebuilt on the same ray, so with tilt == 0 the target is the tuned
// 400/40 point exactly.
void Camera::CarPov(const Frame& f, const Dynamics& d, const Orbit& o,
                    PovSnapshot& out) const {
    float bx = f.fx, by = f.fy;
    float s = -1.0f;
    if (f.lookBehind) s = -s;
    float rx = bx * o.cy - by * o.sy, ry = bx * o.sy + by * o.cy;
    out.eye[0] = f.carPos[0] + s * rx * o.backS;
    out.eye[1] = f.carPos[1] + s * ry * o.backS;
    out.eye[2] = f.carPos[2] + o.upS;
    if (f.lookBehind) { RearViewPov(f, d, out); return; }
    float ax = f.carPos[0] + (f.fx * o.cy - f.fy * o.sy) * 400.0f;
    float ay = f.carPos[1] + (f.fx * o.sy + f.fy * o.cy) * 400.0f;
    float az = f.carPos[2] + 40.0f;
    float dx = ax - out.eye[0], dy = ay - out.eye[1], dz = az - out.eye[2];
    float len2d = std::sqrt(dx * dx + dy * dy);
    float basePitch =
        std::atan2(dz, len2d > 1.0f ? len2d : 1.0f);
    float lookYawC = std::atan2(dy, dx);
    float pitchC = clampf(basePitch + o.tilt, -kViewPitchMax,
                          kViewPitchMax);
    float len =
        std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1.0f) len = 1.0f;
    float cpc = std::cos(pitchC);
    float ux = cpc * std::cos(lookYawC);
    float uy = cpc * std::sin(lookYawC);
    float uz = std::sin(pitchC);
    out.center[0] = out.eye[0] + ux * len;
    out.center[1] = out.eye[1] + uy * len;
    out.center[2] = out.eye[2] + uz * len;
    // §3 POV snapshot (same shape as the ball-cam state above).
    out.pov.rotation[0] = pitchC * kRotPerRad;
    out.pov.rotation[1] = lookYawC * kRotPerRad;
    out.pov.rotation[2] = 0.0f;
    out.pov.distance = d.effDist;
    out.pov.fov = effFov_;
    out.pov.time = povTime_;
    out.pov.focus[0] = out.eye[0] + ux * d.effDist;
    out.pov.focus[1] = out.eye[1] + uy * d.effDist;
    out.pov.focus[2] = out.eye[2] + uz * d.effDist;
}

// §3.2 — blend whole POV snapshots across the Transition (RL
// CameraStateBlender_X::BlendCameraState + .BlendParams): focus, distance, FOV
// and rotation all blend; yaw takes the short arc (RL
// Camera_X::BlendCameraOrientations), pitch/roll lerp, and roll stays 0
// (RemoveRoll). Our alpha is the per-frame scalar UpdateTransition maintains,
// so this is the pre-split componentwise lerp, just expressed on the snapshot.
void Camera::BlendCameraState(const PovSnapshot& carPov,
                              const PovSnapshot& ballPov,
                              PovSnapshot& out) const {
    for (int i = 0; i < 3; ++i) {
        out.eye[i] =
            carPov.eye[i] + (ballPov.eye[i] - carPov.eye[i]) * blend_;
        out.center[i] =
            carPov.center[i] + (ballPov.center[i] - carPov.center[i]) * blend_;
        out.pov.focus[i] =
            carPov.pov.focus[i] +
            (ballPov.pov.focus[i] - carPov.pov.focus[i]) * blend_;
    }
    out.pov.distance =
        carPov.pov.distance + (ballPov.pov.distance - carPov.pov.distance) * blend_;
    out.pov.fov = carPov.pov.fov + (ballPov.pov.fov - carPov.pov.fov) * blend_;
    out.pov.time = povTime_;
    out.pov.rotation[0] =
        carPov.pov.rotation[0] +
        (ballPov.pov.rotation[0] - carPov.pov.rotation[0]) * blend_;
    float dyaw = ballPov.pov.rotation[1] - carPov.pov.rotation[1];
    while (dyaw > kRotHalf) dyaw -= kRotFull;
    while (dyaw < -kRotHalf) dyaw += kRotFull;
    out.pov.rotation[1] = carPov.pov.rotation[1] + dyaw * blend_;
    out.pov.rotation[2] = 0.0f;
}

// §3.2 — SnapTransition (RL Camera_X::SnapTransition): on the first frame there
// is nothing to blend from, so the camera anchors on the desired POV and every
// follower/filter state starts at the exact rest geometry (this is why static
// captures are pixel-identical to the pre-lag build).
void Camera::SnapTransition(const Frame& f, const PovSnapshot& p) {
    eye_[0] = p.eye[0]; eye_[1] = p.eye[1]; eye_[2] = p.eye[2];
    center_[0] = p.center[0]; center_[1] = p.center[1];
    center_[2] = p.center[2];
    smoothedEye_[0] = p.eye[0]; smoothedEye_[1] = p.eye[1];
    smoothedEye_[2] = p.eye[2];
    prevWantEye_[0] = p.eye[0]; prevWantEye_[1] = p.eye[1];
    prevWantEye_[2] = p.eye[2];
    prevRigOffZ_ = p.eye[2] - f.carPos[2];
    hasSmoothed_ = true;
    // #5 anchors the target filter too, so rest geometry is exact.
    smoothTarget_[0] = p.center[0]; smoothTarget_[1] = p.center[1];
    smoothTarget_[2] = p.center[2];
    prevWantT_[0] = p.center[0]; prevWantT_[1] = p.center[1];
    prevWantT_[2] = p.center[2];
    derivS_ = 0.0f;
    hasSmoothTarget_ = true;
    init_ = true;
}

// §3.1 interp — the state-layer interpolation. RL runs focus InterpTo inside
// each state (`1 - exp(-rate*dt)`); ours is still applied to the MIXED POV and
// is a two-filter stack, which is a different dynamic system — P3 replaces it
// against a trace, and P0 must not (the whole point of this split is to give
// that change a seam). Publishes the eye/center pair (§3.5's input).
void Camera::FollowPov(const Frame& f, PovSnapshot& p) {
    float dt = f.dt;
    float stiff = f.stiff;
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
    // factors stay in (0,1) via 1-exp(-rate*dt). Speed/FOV dynamics
    // (UpdateDynamics) are untouched.
    float followRate = kFollowBase + stiff * kFollowStiff;
    float baseRateZ = f.airborne ? followRate * 0.5f : followRate;
    float dMax = dt > 1e-4f ? dt : 1e-4f;
    float rigDx = p.eye[0] - prevWantEye_[0];
    float rigDy = p.eye[1] - prevWantEye_[1];
    float rigSpeed2d =
        std::sqrt(rigDx * rigDx + rigDy * rigDy) / dMax;
    float rateX = followRate + rigSpeed2d / kRigLagCap;
    prevWantEye_[0]=p.eye[0]; prevWantEye_[1]=p.eye[1]; prevWantEye_[2]=p.eye[2];
    float rigOffZ = p.eye[2] - f.carPos[2];
    float rigVz = (rigOffZ - prevRigOffZ_) / dMax;
    prevRigOffZ_ = rigOffZ;
    float rateZ = baseRateZ + std::fabs(rigVz) / kRigLagCap;
    float kx = 1.0f - std::exp(-rateX * dt);
    float kz = 1.0f - std::exp(-rateZ * dt);
    if (kx < 0) kx = 0; if (kx > 1) kx = 1;
    if (kz < 0) kz = 0; if (kz > 1) kz = 1;
    smoothedEye_[0] += (p.eye[0] - smoothedEye_[0]) * kx;
    smoothedEye_[1] += (p.eye[1] - smoothedEye_[1]) * kx;
    smoothedEye_[2] += (p.eye[2] - smoothedEye_[2]) * kz;
    eye_[0]=smoothedEye_[0]; eye_[1]=smoothedEye_[1]; eye_[2]=smoothedEye_[2];
    p.eye[0]=eye_[0]; p.eye[1]=eye_[1]; p.eye[2]=eye_[2];
    // #5 one-euro filter on the look-at point (rl_camera.cpp:466-489).
    // The ball's resting contact with the car micro-bounces it several
    // times a second; aiming straight at it wobbled the whole view (the
    // perceived "car rumble" while dribbling). A fixed low cutoff would
    // also lag real motion, so the cutoff rises with the target's
    // (smoothed) speed: chatter keeps the cutoff low, swivels/flicks snap
    // it open fast. Car-cam (blend 0) keeps the higher floor so the view
    // stays tightly on the aim through maneuvers; ballcam (blend 1) keeps
    // the low floor that kills the resting ball's contact chatter. Speed
    // /FOV dynamics (UpdateDynamics) are untouched.
    if (dt <= 0.0f) {
        center_[0]=smoothTarget_[0]; center_[1]=smoothTarget_[1];
        center_[2]=smoothTarget_[2];
        p.center[0]=center_[0]; p.center[1]=center_[1]; p.center[2]=center_[2];
        return;
    }
    if (!hasSmoothTarget_) {
        smoothTarget_[0]=p.center[0]; smoothTarget_[1]=p.center[1];
        smoothTarget_[2]=p.center[2];
        prevWantT_[0]=p.center[0]; prevWantT_[1]=p.center[1];
        prevWantT_[2]=p.center[2];
        derivS_=0.0f;
        hasSmoothTarget_=true;
    } else {
        float rawDx = p.center[0]-prevWantT_[0], rawDy = p.center[1]-prevWantT_[1],
              rawDz = p.center[2]-prevWantT_[2];
        float rawDeriv =
            std::sqrt(rawDx*rawDx + rawDy*rawDy + rawDz*rawDz) / dMax;
        prevWantT_[0]=p.center[0]; prevWantT_[1]=p.center[1];
        prevWantT_[2]=p.center[2];
        derivS_ += (rawDeriv - derivS_) *
                   (1.0f - std::exp(-2.0f * kPi * kTargetDerivHz * dt));
        float fc = kTargetBaseFc + kTargetCarBoost * (1.0f - blend_) +
                   kTargetDerivGain * derivS_;
        float k = 1.0f - std::exp(-2.0f * kPi * fc * dt);
        if (k < 0) k = 0; if (k > 1) k = 1;
        smoothTarget_[0] += (p.center[0] - smoothTarget_[0]) * k;
        smoothTarget_[1] += (p.center[1] - smoothTarget_[1]) * k;
        smoothTarget_[2] += (p.center[2] - smoothTarget_[2]) * k;
    }
    center_[0]=smoothTarget_[0]; center_[1]=smoothTarget_[1];
    center_[2]=smoothTarget_[2];
    p.center[0]=center_[0]; p.center[1]=center_[1]; p.center[2]=center_[2];
}

// §3.5 — publish the RL-shaped POV behind Pov(). The blended snapshot already
// carries the rotator aim (pitch/yaw/roll, 65536 = 360 deg) and RL's RIG
// Distance (slider + dynamics); FollowPov then moves only the eye/center pair,
// so re-anchor Focus on the eye this frame actually publishes. That keeps
//   eye_from_pov(Pov()) == Eye()
// true by construction and keeps Distance the rig distance — never
// |Eye()-Center()|, which is the distance to the LOOK TARGET (~1600 uu in
// ballcam; Round 14's finding). No GL, no behavior: nothing reads pov_ back.
void Camera::PublishPov(const PovSnapshot& p) {
    pov_ = p.pov;
    pov_.fov = effFov_;
    pov_.time = povTime_;
    float fwd[3];
    forwardFromRotator(pov_.rotation, fwd);
    for (int i = 0; i < 3; ++i)
        pov_.focus[i] = eye_[i] + fwd[i] * pov_.distance;
}

// One camera evaluation, one §3 step per named layer (see the header comment
// for the map and for where our order deliberately differs from RL's).
void Camera::Update(float dt, const float carPos[3], float carYaw,
                    const float ballPos[3], bool ballCamOn, float carSpeed,
                    bool supersonic, float swivelX, float swivelY,
                    const float carFwd[3], const float carVel[3],
                    bool airborne) {
    params_.ballCam = ballCamOn;
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f; // hitch guard: a stalled frame never swings far

    Frame f;
    f.dt = dt;
    f.carPos = carPos;
    f.ballPos = ballPos;
    f.carYaw = carYaw;
    f.carFwd = carFwd;
    f.carVel = carVel;
    f.carSpeed = carSpeed;
    f.supersonic = supersonic;
    f.airborne = airborne;
    f.ballCamOn = ballCamOn;
    f.swivelX = swivelX;
    f.swivelY = swivelY;
    f.stiff = clampf(params_.stiffness, 0.0f, 1.0f);
    f.lookBehind = lookBehind_;
    povTime_ += dt; // §3 POV clock (RL keeps one per state; ours is shared)

    // §3.3 — free-look/swivel offset state.
    UpdateSwivel(f);
    // §3.2 — Transition alpha (RemainingTime/InterpRate).
    UpdateTransition(f);
    // §3.1 — distance/FOV interp.
    Dynamics dyn;
    UpdateDynamics(f, dyn);
    // §3.1 — air/ground branch + air-velocity heading.
    UpdateCarFacing(f);
    // §3.1 — ball-cam focus slew.
    UpdateBallAim(f);
    // §3.3+4 — orbit admission / eye clamp.
    Orbit orb;
    ClampPov(f, dyn, orb);
    // §3.1 — both states write their desired POV. Both run every frame (as
    // the pre-split rig did), so the inactive state keeps its aim/heading
    // state warm and the next transition starts from a live pose.
    PovSnapshot carPov, ballPov, mixed;
    BallCamPov(f, dyn, orb, ballPov);
    CarPov(f, dyn, orb, carPov);
    // §3.2 — blend whole snapshots across the Transition.
    BlendCameraState(carPov, ballPov, mixed);
    if (!init_) { // §3.2 SnapTransition: first frame, anchor with no swing
        SnapTransition(f, mixed);
        PublishPov(mixed); // §3.5 — Pov() describes the pair just published
        return;
    }
    // §3.1 interp — the stiffness lag + target filter on the mixed POV.
    FollowPov(f, mixed);
    PublishPov(mixed); // §3.5
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
    // §3.5 — the free camera OWNS its pair, so its published POV is the pair
    // itself: Focus = Center, Rotation = the pair's direction, and Distance is
    // the LOOK distance (|Center - Eye|). This is the one path where Distance
    // is not RL's rig distance, and it is the only one that admits it: the
    // invariant eye_from_pov(Pov()) == Eye() still holds.
    pov_.focus[0] = center[0];
    pov_.focus[1] = center[1];
    pov_.focus[2] = center[2];
    float dx = center[0] - eye[0], dy = center[1] - eye[1],
          dz = center[2] - eye[2];
    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) {
        float dir[3] = { dx / len, dy / len, dz / len };
        rotationFromDir(dir, pov_.rotation);
        pov_.distance = len;
    }
    pov_.fov = effFov_;
    pov_.time = povTime_;
}

// §3.5 — the ONE place the evaluation becomes GL: projection from EffFov(),
// modelview from the eye/center pair. Everything above is unit-clean camera
// math; nothing above this line calls GL.
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
