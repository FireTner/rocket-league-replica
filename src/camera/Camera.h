// Camera.h — chase camera. z-up world, horizontal field of view.
//
// The eye sits behind a focus point above the car. Yaw and pitch ease toward
// the nose, or toward the ball while ball cam is on. Stiffness is that ease.
// Distance grows with forward speed. Supersonic widens the field of view.
// The horizon stays level. Air roll, a wall, and the ceiling do not tilt it.
// Look is yaw and pitch only. The eye stays at least 10 uu above the floor.
//
// The right stick offsets the view (up to 130° yaw and 60° pitch). snap
// returns that offset when the stick is released; with snap off the offset
// holds. airHeading, off by default, blends an airborne heading from the nose
// toward velocity between 200 and 900 uu/s.
//
// Shake is off unless selected. The free camera is applied by the game after Update().
#pragma once

#include "TruthPov.h"

namespace rl {

struct CameraParams {
    float fov = 110.0f;           // horizontal degrees, 60..110
    float distance = 270.0f;      // uu, 100..400
    float height = 100.0f;        // uu above the car, 40..200
    float angle = -3.0f;          // degrees, -30..15. Negative looks down.
    float stiffness = 0.45f;      // 0..1, how fast yaw and pitch catch the target
    bool ballCam = true;
    float transitionSpeed = 1.5f; // ball-cam blend, 1..2. Rate is 0.5+2.5*speed /s.
    float swivelSpeed = 5.0f;     // how fast the stick offset eases, 1..10
    bool shake = false;           // the game reads this; Update() does not shake
    bool invertSwivel = false;    // flip stick pitch
    bool snap = true;             // released stick returns the swivel
    bool airHeading = false;      // airborne heading blends toward velocity
};

class Camera {
public:
    Camera();

    // Reload [camera] from an INI file. Missing keys keep the current values.
    // Returns true when the file exists and has at least one camera key.
    bool LoadFromIni(const char* path = "config/settings.ini");

    // One frame. carYaw is radians, 0 = +X. Positions are z-up uu.
    // swivelX/swivelY are in [-1, 1] (x = right, y = up), already deadzoned.
    // Null carFwd uses carYaw and a level nose. Null carVel uses the nose
    // times carSpeed. Null carUp is world up.
    // airborne is the explicit ground flag (false = on a surface). It only
    // selects the nose-pitch blend. It does not roll the horizon.
    void Update(float dt, const float carPos[3], float carYaw,
                const float ballPos[3], bool ballCamOn, float carSpeed = 0.0f,
                bool supersonic = false, float swivelX = 0.0f,
                float swivelY = 0.0f, const float carFwd[3] = nullptr,
                const float carVel[3] = nullptr, bool airborne = false,
                const float carUp[3] = nullptr);

    // Projection from the horizontal field of view, then the view matrix.
    void Apply(int viewW, int viewH) const;

    void ToggleBallCam() { params_.ballCam = !params_.ballCam; }
    bool BallCam() const { return params_.ballCam; }
    // Held look-behind. The eye moves in front of the car and looks back.
    void SetLookBehind(bool b) { lookBehind_ = b; }
    bool LookBehind() const { return lookBehind_; }

    // Free-camera pose. Levels up. The chase state resumes on the next Update().
    void SetFreeView(const float eye[3], const float center[3]);
    // Moves the rendered eye and look-at and keeps the chase up vector.
    void SetRenderedPose(const float eye[3], const float center[3]);

    // 0 = car cam, 1 = ball cam. The blend eases; it does not snap to 0 or 1.
    float Blend() const { return static_cast<float>(rig_.ballCamBlend); }
    float SwivelYaw() const { return static_cast<float>(rig_.swivelYaw); }
    float SwivelPitch() const { return static_cast<float>(rig_.swivelPitch); }
    // Horizontal degrees, including the speed kick.
    float EffFov() const { return static_cast<float>(rig_.fovHorizontal); }
    const TruthPov& Pov() const { return pov_; }

    const CameraParams& Params() const { return params_; }
    CameraParams& Params() { return params_; }
    void Eye(float out[3]) const { out[0]=eye_[0]; out[1]=eye_[1]; out[2]=eye_[2]; }
    void Center(float out[3]) const { out[0]=center_[0]; out[1]=center_[1]; out[2]=center_[2]; }
    void Up(float out[3]) const { out[0]=up_[0]; out[1]=up_[1]; out[2]=up_[2]; }

private:
    struct Rig {
        bool ready = false;
        double yaw = 0;
        double pitch = 0;
        double yawStage = 0;
        double pitchStage = 0;
        double swivelYaw = 0;
        double swivelPitch = 0;
        double groundBlend = 1;
        double ballCamBlend = 1;
        double fovHorizontal = 110;
    };

    void WriteRay(const float eye[3], const float center[3]);

    CameraParams params_;
    Rig rig_{};
    float eye_[3] = {-270.0f, 0.0f, 200.0f};
    float center_[3] = {0.0f, 0.0f, 100.0f};
    float up_[3] = {0.0f, 0.0f, 1.0f};
    bool lookBehind_ = false;
    TruthPov pov_{};
    float povTime_ = 0.0f;
};

} // namespace rl
