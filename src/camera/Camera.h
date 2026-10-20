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

namespace rl {

struct CameraParams {
    float fov = 110.0f;      // degrees, RL default 110 (range 60-110)
    float distance = 270.0f; // uu behind car (RL default 270, range 100-400)
    float height = 100.0f;   // uu above car (RL default 100, range 40-200)
    float angle = -3.0f;     // degrees pitch down (RL default -3, range -30..15)
    float stiffness = 0.45f; // 0..1 response smoothing (RL default 0.45)
    bool ballCam = true;
    float transitionSpeed = 1.2f; // 1..2 ballcam<->car-cam blend rate (RL 1.2)
    float swivelSpeed = 5.0f;     // 1..10 right-stick look rate (RL 5.0)
    bool shake = true;            // RL ships with camera shake ON
    bool invertSwivel = false;    // flip swivel pitch
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
    void Update(float dt, const float carPos[3], float carYaw,
                const float ballPos[3], bool ballCamOn, float carSpeed = 0.0f,
                bool supersonic = false, float swivelX = 0.0f,
                float swivelY = 0.0f);

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
    // active mode at transitionSpeed per second (RL transition slider).
    float Blend() const { return blend_; }
    // Current swivel orbit offsets (radians). Yaw wraps; pitch is clamped.
    float SwivelYaw() const { return yawOff_; }
    float SwivelPitch() const { return pitchOff_; }
    // Slider FOV plus the RL dynamic kick (+5 with speed, +5 supersonic).
    float EffFov() const { return effFov_; }

    const CameraParams& Params() const { return params_; }
    CameraParams& Params() { return params_; }
    void Eye(float out[3]) const { out[0]=eye_[0]; out[1]=eye_[1]; out[2]=eye_[2]; }
    void Center(float out[3]) const { out[0]=center_[0]; out[1]=center_[1]; out[2]=center_[2]; }

private:
    CameraParams params_;
    float eye_[3] = { -270, 0, 200 };
    float center_[3] = {0, 0, 100};
    bool init_ = false;
    bool lookBehind_ = false;
    float blend_ = 1.0f;   // 0 = car-cam, 1 = ball-cam
    float yawOff_ = 0.0f;  // swivel orbit yaw around the car (radians)
    float pitchOff_ = 0.0f;// swivel orbit pitch (radians, clamped)
    float effFov_ = 110.0f;// slider FOV + dynamic kick
};

} // namespace rl
