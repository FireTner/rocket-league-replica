// Camera.h — A4 CAMERA: Rocket League chase-camera replication.
// Ownership: A4. Tunable live via config/settings.ini [camera] (schema v1
// provisional, owned by A5 — we read it opportunistically via a shim).
//
// Behavior provenance (facts only, no code copied):
//  - Rocket Science #2 (https://rocketscience.fyi/know/videos/camera-settings):
//    FOV 60..110; dynamic FOV +5 with speed, +5 more at supersonic; stiffness
//    = dynamic distance (+100 max at 0 stiffness; supersonic distance =
//    distance + (1-stiffness)*114.7) + dynamic angle (+2 max at 0) + eye
//    smoothing ONLY with ballcam off; swivel 90 deg per 1.3 s at 1.0, linear.
//  - BakkesModSDK ProfileCameraSettings/CameraSave (field list only):
//    FOV/Height/Pitch/Distance/Stiffness/SwivelSpeed/TransitionSpeed +
//    InvertSwivelPitch/CameraShake.
//  - CarBaller settings.h (RL ranges only): swivel 1..10, transition 1..2.
#pragma once

#include "TruthPov.h"

namespace rl {

struct CameraParams {
    float fov = 110.0f;      // degrees, RL default 110 (range 60-110)
    float distance = 270.0f; // uu behind car (RL default 270, range 100-400)
    float height = 100.0f;   // uu above car (RL default 100, range 40-200)
    // degrees pitch down. RL default -3. Our accept range is -30..15, which is
    // NOT RL's slider: `AngleLimits` is -15..0 (decomp: TAGame.Camera_TA.
    // AngleLimits, docs/CAMERA_EXACT_LOG.md Round 8). The range is shared with
    // src/settings/Settings.cpp (A5), so narrowing it is a cross-owner schema
    // change, not a camera one; the -3 default is legal either way.
    float angle = -3.0f;
    float stiffness = 0.45f; // 0..1 response smoothing (RL default 0.45)
    bool ballCam = true;
    float transitionSpeed = 1.2f; // 1..2 ballcam<->car-cam blend rate (RL 1.2)
    float swivelSpeed = 5.0f;     // 1..10 right-stick look rate (RL 5.0)
    bool shake = true;            // RL ships with camera shake ON
    bool invertSwivel = false;    // flip swivel pitch
    bool snap = true;             // snap swivel back to default on release
                                  // (RL "Snap Camera to Default"; schema v1
                                  // additive — no menu slider, INI-only)
};

class Camera {
public:
    Camera();

    // Reload [camera] keys from INI path if present; else keep compiled
    // defaults. Missing keys keep defaults. Safe to call live every N sec.
    // Returns true if file was found & parsed (even partially).
    bool LoadFromIni(const char* path = "config/settings.ini");

    // Per-render-frame update. carYaw in radians (0=+X), z-up units.
    // ballCamOn toggles tracking (key B at app level).
    // carSpeed in uu/s drives the RL dynamic distance/angle/FOV; supersonic
    // selects the extra kick (G8 threshold 2200, passed in by the caller).
    // swivelX/swivelY in [-1,1] = right-stick look (x=right, y=up), already
    // deadzoned by the caller. Trailing args default to the legacy call.
    // #2 air heading: carFwd = unit nose vector (pitch-immune axis source),
    // carVel = velocity uu/s (air travel-heading source), airborne selects
    // the air branch. All three default to legacy ground-on-nose behavior.
    void Update(float dt, const float carPos[3], float carYaw,
                const float ballPos[3], bool ballCamOn, float carSpeed = 0.0f,
                bool supersonic = false, float swivelX = 0.0f,
                float swivelY = 0.0f, const float carFwd[3] = nullptr,
                const float carVel[3] = nullptr, bool airborne = false);

    // Push GL_PROJECTION (fov/aspect) + GL_MODELVIEW (eye lookAt).
    void Apply(int viewW, int viewH) const;

    void ToggleBallCam() { params_.ballCam = !params_.ballCam; }
    bool BallCam() const { return params_.ballCam; }
    // Look-behind (held J): rear-view mirror of the chase cam. Set every
    // frame from the held key; Update() mirrors the behind-axis and looks
    // back over the car instead of forward.
    void SetLookBehind(bool b) { lookBehind_ = b; }
    bool LookBehind() const { return lookBehind_; }

    // Free-camera hook (game-owned flight model): override the smoothed
    // eye/look-at directly. Returns to chase on the next Update().
    void SetFreeView(const float eye[3], const float center[3]);

    // Transition blend: 0 = car-cam, 1 = ball-cam. Advances toward the
    // active mode at 0.5+2.5*transitionSpeed per second (#6, RL transition
    // slider; 1.2 swings in ~0.29 s).
    float Blend() const { return blend_; }
    // Current swivel orbit offsets (radians). Yaw wraps; pitch is clamped.
    float SwivelYaw() const { return yawOff_; }
    float SwivelPitch() const { return pitchOff_; }
    // Slider FOV plus the RL dynamic kick (+5 with speed, +5 supersonic).
    float EffFov() const { return effFov_; }
    // RL-shaped POV of the LAST evaluation (P3, Round 14 finding): RL's
    // `Distance` is the rig distance (slider + dynamics, ~270 uu), which the
    // public GL pair cannot express — `|Eye() - Center()|` is the distance to
    // the LOOK TARGET (~1600 uu in ballcam, and `Center()` sits on the aim ray
    // at max(|target - Eye|, 1000) uu, so Focus carries a ray-length term).
    // This is the accessor a trace recorder / the trace gate compares against.
    // It is anchored on the published eye, so
    //   eye_from_pov(Pov()) == Eye()        (exact up to float32)
    // holds unconditionally (measured 9.8e-4 uu over 600 frames).
    // `Rotation` agrees with `Center() - Eye()`'s direction in STEADY STATE
    // only (measured 1.25 deg — that is the one-euro target filter lagging the
    // aim by design). Mid-transition they can disagree far more (measured up to
    // 103.9 deg): BlendCameraState interpolates the rotation rotator AND the
    // focus point independently, so as the blended Center() sweeps near the eye
    // the look distance collapses and the two stop naming the same fact.
    // Consequence for consumers: read Focus/Distance and Rotation as SEPARATE
    // fields (the trace tools score them separately); never derive one from the
    // other. Purely additive: Eye()/Center()/EffFov() are untouched.
    const TruthPov& Pov() const { return pov_; }

    const CameraParams& Params() const { return params_; }
    CameraParams& Params() { return params_; }
    void Eye(float out[3]) const { out[0]=eye_[0]; out[1]=eye_[1]; out[2]=eye_[2]; }
    void Center(float out[3]) const { out[0]=center_[0]; out[1]=center_[1]; out[2]=center_[2]; }

private:
    // ── §3 layer pipeline (MASTER_PROMPT_CAMERA_EXACT.md; P0 = refactor only) ─
    // One camera evaluation runs these in order, each with a single job:
    //   UpdateSwivel        §3.3  free-look/swivel offset state (+ snap-back)
    //   UpdateTransition    §3.2  Transition alpha (RemainingTime/InterpRate)
    //   UpdateDynamics      §3.1  distance/FOV interp (UpdateDistance/UpdateFOV)
    //   UpdateCarFacing     §3.1  air/ground branch + air-velocity heading
    //   UpdateBallAim       §3.1  ball-cam focus slew (UpdateFocus)
    //   ClampPov            §3.3+4 orbit admission: eye floor + elevation stop
    //   BallCamPov/CarPov   §3.1  each state writes its DESIRED POV snapshot
    //   BlendCameraState    §3.2  blend whole snapshots across the Transition
    //   SnapTransition      §3.2  first frame / `snap`: skip the blend
    //   FollowPov           §3.1  our InterpTo stand-ins (#3 lag, #5 target)
    // Apply() is §3.5 — the only place GL sees the eye/center pair.
    // The per-frame structs live in Camera.cpp: this TU is the single owner.
    struct Frame;        // inputs + the values one layer derives for a later one
    struct Dynamics;     // §3.1 distance/FOV interp output
    struct Orbit;        // §3.3/4 admitted eye orbit (clamp + offset output)
    struct PovSnapshot;  // one POV snapshot (RL TruthPov + our GL eye/center)
    void UpdateSwivel(const Frame& f);
    void UpdateTransition(const Frame& f);
    void UpdateDynamics(const Frame& f, Dynamics& d);
    void UpdateCarFacing(Frame& f);
    void UpdateBallAim(Frame& f);
    void ClampPov(const Frame& f, const Dynamics& d, Orbit& o) const;
    void BallCamPov(const Frame& f, const Dynamics& d, const Orbit& o,
                    PovSnapshot& out);
    void CarPov(const Frame& f, const Dynamics& d, const Orbit& o,
                PovSnapshot& out) const;
    void RearViewPov(const Frame& f, const Dynamics& d,
                     PovSnapshot& out) const;
    void BlendCameraState(const PovSnapshot& carPov, const PovSnapshot& ballPov,
                          PovSnapshot& out) const;
    void SnapTransition(const Frame& f, const PovSnapshot& p);
    void FollowPov(const Frame& f, PovSnapshot& p);
    // §3.5 publish — the ONLY writer of pov_. Re-anchors the blended snapshot's
    // rotator aim + rig Distance on the eye this frame actually publishes, so
    // Pov() always describes the pair Eye()/Center() returned (see the public
    // comment). No GL, no behavior: it only fills the accessor's view.
    void PublishPov(const PovSnapshot& p);

    CameraParams params_;
    float eye_[3] = { -270, 0, 200 };
    float center_[3] = {0, 0, 100};
    bool init_ = false;
    bool lookBehind_ = false;
    float blend_ = 1.0f;   // 0 = car-cam, 1 = ball-cam
    float yawOff_ = 0.0f;  // swivel orbit yaw around the car (radians)
    float pitchOff_ = 0.0f;// swivel orbit pitch (radians, clamped -0.8/+0.7)
    float swivelIdle_ = 0.0f; // s since last swivel input (snap-back timer)
    float effFov_ = 110.0f;// slider FOV + dynamic kick
    // §3.5 published POV (see Pov()). Written once per evaluation by
    // PublishPov; `distance` is RL's RIG distance, not |Eye()-Center()|.
    TruthPov pov_{};
    // §3 POV clock: RL keeps one Time per camera state; ours is a single
    // evaluation clock shared by both states (P3 may split it per state).
    float povTime_ = 0.0f; // s, accumulated clamped dt
    // #1 ball-aim slew (port of CarBaller RLCamera ballYaw_/measYaw_/
    // ballPitch_, rl_camera.cpp:47,74,138-179): unwrapped bearing chased at
    // 20 rad/s, hold when ball horiz < 5 uu, vertical aim slewed too.
    float ballYaw_ = 0.0f;   // eased bearing car->ball (rad, unwrapped)
    float measYaw_ = 0.0f;   // same bearing, unwrapped (never slewed)
    float prevPhi_ = 0.0f;   // last reliable raw bearing
    float ballPitch_ = 0.0f; // eased elevation eye->ball (rad)
    bool hasBallAim_ = false;
    bool hasPrevPhi_ = false;
    // #2 car-cam air-velocity heading (port of CarBaller RLCamera carYaw_/
    // axisYaw_, rl_camera.cpp:218-244,270-355): ground = rigid facing; air
    // = eased onto the velocity heading, blend 200->900 uu/s, rate
    // 2.5+7*stiffness; pitch-immune axis w/ half-turn continuity.
    float carYaw_ = 0.0f;    // eased car-cam heading (rad, behind-axis ref)
    float axisYaw_ = 0.0f;   // pitch-immune facing axis (rad)
    bool hasCarCam_ = false; // heading state anchored
    bool hasAxisYaw_ = false;// axisYaw_ holds a real bearing
    // #3 stiffness world-space lag (port of CarBaller RLCamera smoother,
    // rl_camera.cpp:87-92,413-464): the MIXED ideal eye passes through
    // stiffness smoothing in ALL modes (ballcam included) at
    // followRate = 1.5+14*stiffness, vertical halved in air, rig-speed
    // saturation (trail caps at 40 uu), swivel-vertical split via
    // rig-relative Z (want.z - car.z).
    float smoothedEye_[3] = {0.0f, 0.0f, 0.0f};
    float prevWantEye_[3] = {0.0f, 0.0f, 0.0f}; // previous ideal eye
    float prevRigOffZ_ = 0.0f;  // previous rig-relative eye height
    bool hasSmoothed_ = false;
    // #5 one-euro look-target filter (port of CarBaller RLCamera
    // smoothTarget_/prevWantT_/derivS_, rl_camera.cpp:94-100,466-489):
    // cutoff fc = 1.2+1.8*(1-blend)+0.01*derivS Hz, derivS = 3 Hz EMA of
    // the target speed. Ballcam keeps the 1.2 Hz floor (kills the resting
    // ball's contact micro-chatter); car-cam tracks tightly (3.0 Hz
    // floor); fast motion opens the cutoff via the derivS term.
    float smoothTarget_[3] = {0.0f, 0.0f, 0.0f};
    float prevWantT_[3] = {0.0f, 0.0f, 0.0f}; // previous mixed ideal target
    float derivS_ = 0.0f;       // EMA of look-target speed (uu/s)
    bool hasSmoothTarget_ = false;
    // #4 sphere-orbit swivel + snap-back (port of CarBaller RLCamera
    // placeEye/swivel, rl_camera.cpp:101-121,373-396): pitch clamp is
    // asymmetric (-0.8/+0.7), release returns linearly at 2x swivel rate
    // (6 rad/s floor) after 0.05 s idle when params_.snap is on, the eye
    // rides the floor via an elevation clamp, and unabsorbed pitch reaches
    // view as tilt. swivelIdle_ owns the idle timer above.
};

} // namespace rl
