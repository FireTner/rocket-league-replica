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
// TUNED (no published source, marked as such): swivel pitch clamp, swivel
// die-back rate, transition blend shape (linear), floor clamp.
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
constexpr float kSwivelPitchMax = 1.0f;   // TUNED orbit-pitch clamp (radians)
constexpr float kSwivelDie = 6.0f;        // TUNED exponential return on release
constexpr float kSuperSpeed = 2200.0f;    // G8 supersonic threshold (caller flag)
constexpr float kPi = 3.14159265f;

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
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
                    bool supersonic, float swivelX, float swivelY) {
    params_.ballCam = ballCamOn;
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f; // hitch guard: a stalled frame never swings far
    float stiff = clampf(params_.stiffness, 0.0f, 1.0f);

    // Swivel orbit (right-stick look): yaw swings the rig around the car,
    // pitch tilts the eye on the rig sphere. On release the offsets die back
    // to zero (RL springs back; rate TUNED — the SDK exposes a SwivelDieRate
    // but its value is unpublished).
    float sy = params_.invertSwivel ? -swivelY : swivelY;
    if (swivelX * swivelX + sy * sy > 0.05f * 0.05f) {
        float w = kSwivelRate * clampf(params_.swivelSpeed, 1.0f, 10.0f) * dt;
        yawOff_ += swivelX * w;
        pitchOff_ = clampf(pitchOff_ + sy * w, -kSwivelPitchMax, kSwivelPitchMax);
        while (yawOff_ > kPi) yawOff_ -= 2 * kPi;
        while (yawOff_ < -kPi) yawOff_ += 2 * kPi;
    } else if (dt > 0) {
        float d = std::exp(-kSwivelDie * dt);
        yawOff_ *= d;
        pitchOff_ *= d;
        if (std::fabs(yawOff_) < 1e-4f) yawOff_ = 0;
        if (std::fabs(pitchOff_) < 1e-4f) pitchOff_ = 0;
    }

    // Transition blend: 0 = car-cam, 1 = ball-cam, advancing at the RL
    // transition-speed slider (units/s, so 1.2 swings in ~0.83 s).
    float blendTgt = ballCamOn ? 1.0f : 0.0f;
    if (!init_) {
        blend_ = blendTgt;
    } else if (dt > 0) {
        float step = clampf(params_.transitionSpeed, 0.2f, 2.0f) * dt;
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
    // Swivel sphere orbit: pitch offset rotates the eye elevation.
    float r = std::sqrt(back * back + up0 * up0);
    float elev = std::atan2(up0, back) - pitchOff_;
    float backS = r * std::cos(elev), upS = r * std::sin(elev);
    float cy = std::cos(yawOff_), syaw = std::sin(yawOff_);

    float fx = std::cos(carYaw), fy = std::sin(carYaw);
    // One rig evaluation: behind-axis (bx,by) + look target. Ball rig sits
    // on the car->ball axis (with the 50 uu anti-flip fallback); car rig
    // sits behind facing. lookBehind mirrors both (held J rear view).
    auto rig = [&](bool ballMode, float want[3], float tgt[3]) {
        float bx, by;
        if (ballMode) {
            float dx = carPos[0] - ballPos[0], dy = carPos[1] - ballPos[1];
            float dl = std::sqrt(dx * dx + dy * dy);
            if (dl > 50.0f) { bx = dx / dl; by = dy / dl; }
            else { bx = fx; by = fy; } // ball under car: keep facing
        } else {
            bx = fx; by = fy;
        }
        float s = ballMode ? 1.0f : -1.0f;
        if (lookBehind_) s = -s;
        // Swivel yaw rotates the behind-axis around the car.
        float rx = bx * cy - by * syaw, ry = bx * syaw + by * cy;
        want[0] = carPos[0] + s * rx * backS;
        want[1] = carPos[1] + s * ry * backS;
        want[2] = carPos[2] + upS;
        if (want[2] < 40.0f) want[2] = 40.0f; // never under floor
        if (lookBehind_) {
            tgt[0] = carPos[0] - fx * 400; tgt[1] = carPos[1] - fy * 400;
            tgt[2] = carPos[2] + 40;
        } else if (ballMode) {
            tgt[0] = ballPos[0]; tgt[1] = ballPos[1]; tgt[2] = ballPos[2];
        } else {
            // Car-cam look direction follows the swivel yaw (free look).
            tgt[0] = carPos[0] + (fx * cy - fy * syaw) * 400;
            tgt[1] = carPos[1] + (fx * syaw + fy * cy) * 400;
            tgt[2] = carPos[2] + 40;
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

    if (!init_) { // snap on first frame
        eye_[0]=want[0]; eye_[1]=want[1]; eye_[2]=want[2];
        center_[0]=tgt[0]; center_[1]=tgt[1]; center_[2]=tgt[2];
        init_ = true;
        return;
    }
    if (ballCamOn && blend_ >= 1.0f) {
        // RL ballcam: the rig is rigid (stiffness only breathes distance /
        // angle / FOV); the ball target locks instantly.
        for (int i = 0; i < 3; i++) { eye_[i] = want[i]; center_[i] = tgt[i]; }
        return;
    }
    // Car-cam (and any transition swing): exponential stiffness lag.
    // rate maps 0..1 -> 2..20 /s (RL-snappy at high stiffness).
    float rate = 2.0f + stiff * 18.0f;
    float k = 1.0f - std::exp(-rate * dt);
    if (k < 0) k = 0; if (k > 1) k = 1;
    for (int i = 0; i < 3; i++) {
        eye_[i] += (want[i] - eye_[i]) * k;
        center_[i] += (tgt[i] - center_[i]) * k;
    }
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
