// Camera.h — A4 CAMERA: Rocket League chase-camera replication.
// Ownership: A4. Tunable live via config/settings.ini [camera] (schema v1
// provisional, owned by A5 — we read it opportunistically via a shim).
#pragma once

namespace rl {

struct CameraParams {
    float fov = 110.0f;      // degrees, RL default 110 (range 60-130)
    float distance = 270.0f; // uu behind car (RL default 270, range 100-400)
    float height = 100.0f;   // uu above car (RL default 100, range 40-200)
    float angle = -3.0f;     // degrees pitch down (RL default -3, range -30..15)
    float stiffness = 0.45f; // 0..1 response smoothing (RL default 0.45)
    bool ballCam = true;
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
    void Update(float dt, const float carPos[3], float carYaw,
                const float ballPos[3], bool ballCamOn);

    // Push GL_PROJECTION (fov/aspect) + GL_MODELVIEW (eye lookAt).
    void Apply(int viewW, int viewH) const;

    void ToggleBallCam() { params_.ballCam = !params_.ballCam; }
    bool BallCam() const { return params_.ballCam; }

    // Free-camera hook (game-owned flight model): override the smoothed
    // eye/look-at directly. Returns to chase on the next Update().
    void SetFreeView(const float eye[3], const float center[3]);

    const CameraParams& Params() const { return params_; }
    CameraParams& Params() { return params_; }
    void Eye(float out[3]) const { out[0]=eye_[0]; out[1]=eye_[1]; out[2]=eye_[2]; }
    void Center(float out[3]) const { out[0]=center_[0]; out[1]=center_[1]; out[2]=center_[2]; }

private:
    CameraParams params_;
    float eye_[3] = { -270, 0, 200 };
    float center_[3] = {0, 0, 100};
    bool init_ = false;
};

} // namespace rl
