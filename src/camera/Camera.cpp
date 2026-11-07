// Camera.cpp — chase camera. The rig math is y-up; the published pose is z-up.
#include "Camera.h"

#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rl {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kVelDirMin = 200.0f;
constexpr float kVelDirMax = 900.0f;
constexpr float kAxisMinFxy = 0.15f;
constexpr float kRotFull = 65536.0f;
constexpr float kRotPerRad = kRotFull / (2.0f * kPi);
constexpr float kLookDistance = 4000.0f;

constexpr double kDPi = 3.14159265358979323846;
constexpr double kDegToRad = kDPi / 180.0;
constexpr double kMaxDt = 0.05;
constexpr double kPitchLimit = 85.0 * kDegToRad;
constexpr double kBallPitchThreshold = 22.0 * kDegToRad;
constexpr double kSwivelYaw = 130.0 * kDegToRad;
constexpr double kSwivelPitch = 60.0 * kDegToRad;
constexpr double kFloorClearance = 10.0;
constexpr double kGroundHeight = 40.0;
constexpr double kUprightDot = 0.45;
constexpr double kStiffnessScale = 2.427;
constexpr double kFastFollow = 13.39;
constexpr double kSlowFollow = 2.03;
constexpr double kFovFollow = 8.0;
constexpr double kFovReferenceSpeed = 2300.0;
constexpr double kApproachExponentLimit = 1.5;
constexpr double kUnitSquaredEpsilon = 1e-12;
constexpr double kStickIdle = 1e-3;

struct DVec {
    double x = 0;
    double y = 0;
    double z = 0;
};

struct DQuat {
    double x = 0;
    double y = 0;
    double z = 0;
    double w = 1;
};

struct Pose {
    DVec position{};
    DVec direction{};
    DVec up{0, 1, 0};
};

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

DVec ToChase(float x, float y, float z) {
    return {static_cast<double>(y), static_cast<double>(z), static_cast<double>(x)};
}

void FromChase(DVec v, float out[3]) {
    out[0] = static_cast<float>(v.z);
    out[1] = static_cast<float>(v.x);
    out[2] = static_cast<float>(v.y);
}

DVec Sub(DVec a, DVec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
DVec Mul(DVec v, double s) { return {v.x * s, v.y * s, v.z * s}; }
double Dot(DVec a, DVec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
DVec Cross(DVec a, DVec b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double Length(DVec v) { return std::sqrt(Dot(v, v)); }
DVec Normalize(DVec v) {
    const double len = Length(v);
    if (len < 1e-8) return {0, 1, 0};
    return Mul(v, 1.0 / len);
}
double Approach(double dt, double rate) {
    if (dt <= 0.0 || rate <= 0.0) return 0.0;
    const double exponent = -dt * rate;
    if (exponent < -kApproachExponentLimit) return 1.0 - std::exp(exponent);
    constexpr double coefficients[] = {
        9.99999999999955480e-01, 4.99999999992513988e-01, 1.66666666467975372e-01,
        4.16666645985531922e-02, 8.33332223638922828e-03, 1.38885398359088930e-03,
        1.98343865632642601e-04, 2.47137216580100278e-05, 2.68272393118890004e-06,
        2.36924264979441471e-07, 1.27552782712000742e-08};
    constexpr int last = static_cast<int>(sizeof(coefficients) / sizeof(coefficients[0])) - 1;
    double series = coefficients[last];
    for (int index = last - 1; index >= 0; --index) series = series * exponent + coefficients[index];
    return -exponent * series;
}

double LerpAngle(double from, double to, double t) {
    return from + std::remainder(to - from, 2.0 * kDPi) * t;
}
double WrapAngle(double angle) { return std::remainder(angle, 2.0 * kDPi); }
double MeasureYaw(DVec forward) { return std::atan2(forward.x, forward.z); }
double MeasurePitch(DVec forward) { return std::asin(std::clamp(forward.y, -1.0, 1.0)); }

struct Basis {
    DVec up{0, 1, 0};
    DVec forward{0, 0, 1};
};

DQuat QuatFromBasis(DVec right, DVec up, DVec forward) {
    const double m00 = right.x, m01 = up.x, m02 = forward.x;
    const double m10 = right.y, m11 = up.y, m12 = forward.y;
    const double m20 = right.z, m21 = up.z, m22 = forward.z;
    DQuat q;
    const double trace = m00 + m11 + m22;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q.w = (m21 - m12) / s;
        q.x = 0.25 * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25 * s;
        q.z = (m12 + m21) / s;
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25 * s;
    }
    return q;
}

DQuat Orientation(DVec forward, DVec up) {
    forward = Normalize(forward);
    const double along = Dot(up, forward);
    up = {up.x - forward.x * along, up.y - forward.y * along, up.z - forward.z * along};
    if (Dot(up, up) < 1e-12) {
        const DVec hint = std::fabs(forward.y) < 0.9 ? DVec{0.0, 1.0, 0.0} : DVec{1.0, 0.0, 0.0};
        const double h = Dot(hint, forward);
        up = {hint.x - forward.x * h, hint.y - forward.y * h, hint.z - forward.z * h};
    }
    up = Normalize(up);
    return QuatFromBasis(Cross(up, forward), up, forward);
}

Basis MakeBasis(DQuat rotation) {
    const double lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y +
                                 rotation.z * rotation.z + rotation.w * rotation.w;
    if (lengthSquared < 1e-16) rotation = {};
    else if (std::abs(lengthSquared - 1.0) > kUnitSquaredEpsilon) {
        const double raw = std::sqrt(lengthSquared);
        rotation.x /= raw;
        rotation.y /= raw;
        rotation.z /= raw;
        rotation.w /= raw;
    }
    const double x = rotation.x, y = rotation.y, z = rotation.z, w = rotation.w;
    DVec up{2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x)};
    DVec forward{2.0 * (x * z + w * y), 2.0 * (y * z - w * x), 1.0 - 2.0 * (x * x + y * y)};
    Basis basis;
    basis.up = std::abs(Dot(up, up) - 1.0) <= kUnitSquaredEpsilon ? up : Normalize(up);
    basis.forward =
        std::abs(Dot(forward, forward) - 1.0) <= kUnitSquaredEpsilon ? forward : Normalize(forward);
    return basis;
}

double MeasureBallPitch(DVec from, DVec ball, double configuredPitch) {
    const DVec toBall = Sub(ball, from);
    const double horizontal = std::sqrt(toBall.x * toBall.x + toBall.z * toBall.z);
    const double pitched = std::atan2(toBall.y, horizontal);
    const double weight =
        std::clamp((std::abs(pitched) - kBallPitchThreshold) / kBallPitchThreshold, 0.0, 1.0);
    return configuredPitch + (pitched - configuredPitch) * 0.8 * weight;
}

double FovKick(double speed, bool supersonic) {
    const double bonus = supersonic ? 10.0 : speed / kFovReferenceSpeed * 5.0;
    return std::min(bonus, supersonic ? 40.0 : 25.0);
}

DVec MakeLookDirection(double yaw, double pitch) {
    const double cosinePitch = std::cos(pitch);
    return {std::sin(yaw) * cosinePitch, std::sin(pitch), std::cos(yaw) * cosinePitch};
}

void StepAngle(double& stage, double& current, double target, double dt, double stiffness) {
    const double scale = 1.0 + stiffness * kStiffnessScale;
    const double fast = stiffness >= 1.0 ? 1.0 : Approach(dt, scale * kFastFollow);
    const double slow = Approach(dt, scale * kSlowFollow);
    stage = LerpAngle(stage, target, fast);
    current = LerpAngle(current, stage, slow);
}

struct Frame {
    double dt = 0;
    bool ballCam = false;
    DVec carPosition{};
    DQuat carOrientation{};
    DVec ballPosition{};
    bool rearView = false;
    bool hasGround = true;
    bool onGround = true;
    DVec velocity{};
    bool supersonic = false;
    double lookX = 0;
    double lookY = 0;
    bool latchSwivel = false;
    double fov = 110;
    double distance = 270;
    double height = 100;
    double angleDeg = -3;
    double stiffness = 0.45;
    double swivelSpeed = 5;
    double transitionSpeed = 1.5;
    bool invertSwivel = false;
};

struct State {
    bool& ready;
    double& yaw;
    double& pitch;
    double& yawStage;
    double& pitchStage;
    double& swivelYaw;
    double& swivelPitch;
    double& groundBlend;
    double& ballCamBlend;
    double& fovHorizontal;
};

// Explicit flag wins. Otherwise low and upright counts as on a surface.
// Failing this only picks the airborne nose pitch. It does not roll the view.
bool ResolveGrounded(const Frame& in, DVec carUp) {
    if (in.hasGround) return in.onGround;
    return in.carPosition.y < kGroundHeight && carUp.y > kUprightDot;
}

Pose Evaluate(State s, const Frame& in) {
    const double dt = std::clamp(in.dt, 0.0, kMaxDt);
    const Basis basis = MakeBasis(in.carOrientation);
    const DVec velocity = in.velocity;
    // A supersonic flag leaves speed at 0 so the kick uses the supersonic branch.
    const double speed = in.supersonic ? 0.0 : Length(velocity);
    const double carYaw = MeasureYaw(basis.forward);
    const double nosePitch = MeasurePitch(basis.forward);
    const double configuredPitch = in.angleDeg * kDegToRad;
    const DVec focus{in.carPosition.x, in.carPosition.y + in.height, in.carPosition.z};
    const DVec ballOffset = Sub(in.ballPosition, focus);
    const double ballYaw = Dot(ballOffset, ballOffset) < 1e-16 ? 0.0 : MeasureYaw(ballOffset);
    const double ballPitch = MeasureBallPitch(focus, in.ballPosition, configuredPitch);
    const bool grounded = ResolveGrounded(in, basis.up);
    const double groundTarget = grounded ? 1.0 : 0.0;
    const double ballTarget = in.ballCam ? 1.0 : 0.0;
    const bool snap = !s.ready;
    if (snap) {
        s.ready = true;
        s.groundBlend = groundTarget;
        s.ballCamBlend = ballTarget;
    } else {
        // Rocket League moves the ball-cam blend at 0.5 + 2.5*transition per second
        // and finishes on the target. The slider on disk is 1.5.
        const double ballRate = 0.5 + 2.5 * std::clamp(in.transitionSpeed, 1.0, 2.0);
        const double ballStep = ballRate * dt;
        s.groundBlend = std::clamp(s.groundBlend + (grounded ? 2.0 : -4.0) * dt, 0.0, 1.0);
        if (s.ballCamBlend < ballTarget)
            s.ballCamBlend = std::min(s.ballCamBlend + ballStep, ballTarget);
        else if (s.ballCamBlend > ballTarget)
            s.ballCamBlend = std::max(s.ballCamBlend - ballStep, ballTarget);
    }
    const double chasedPitch = LerpAngle(nosePitch, configuredPitch, s.groundBlend);
    const double targetYaw = LerpAngle(carYaw, ballYaw, s.ballCamBlend);
    const double targetPitch = LerpAngle(chasedPitch, ballPitch, s.ballCamBlend);
    const double fovTarget = in.fov + FovKick(speed, in.supersonic);
    const double swivelYawTarget = std::clamp(in.lookX, -1.0, 1.0) * -kSwivelYaw;
    double lookY = std::clamp(in.lookY, -1.0, 1.0);
    if (in.invertSwivel) lookY = -lookY;
    const double swivelPitchTarget = lookY * kSwivelPitch;
    if (snap) {
        s.yawStage = s.yaw = targetYaw;
        s.pitchStage = s.pitch = targetPitch;
        s.swivelYaw = swivelYawTarget;
        s.swivelPitch = swivelPitchTarget;
        s.fovHorizontal = fovTarget;
    } else {
        StepAngle(s.yawStage, s.yaw, targetYaw, dt, in.stiffness);
        StepAngle(s.pitchStage, s.pitch, targetPitch, dt, in.stiffness);
        const bool holdSwivel = in.latchSwivel && std::abs(in.lookX) <= kStickIdle &&
                                std::abs(in.lookY) <= kStickIdle;
        if (!holdSwivel) {
            const double swivelAlpha = Approach(dt, std::clamp(in.swivelSpeed, 1.0, 10.0) * 3.0);
            s.swivelYaw = LerpAngle(s.swivelYaw, swivelYawTarget, swivelAlpha);
            s.swivelPitch = LerpAngle(s.swivelPitch, swivelPitchTarget, swivelAlpha);
        }
        s.fovHorizontal += (fovTarget - s.fovHorizontal) * Approach(dt, kFovFollow);
    }
    double renderYaw = WrapAngle(s.yaw + s.swivelYaw);
    double renderPitch = std::clamp(s.pitch + s.swivelPitch, -kPitchLimit, kPitchLimit);
    if (in.rearView) {
        renderYaw = WrapAngle(carYaw + kDPi);
        renderPitch = configuredPitch;
    }
    const DVec direction = MakeLookDirection(renderYaw, renderPitch);
    const double pull = std::max((1.0 - in.stiffness) * Dot(basis.forward, velocity) * 0.05, -50.0);
    const double distance = std::max(in.distance + pull, 1.0);
    Pose view;
    view.direction = direction;
    view.position = Sub(focus, Mul(direction, distance));
    view.position.y = std::max(view.position.y, kFloorClearance);
    // World up, squared against the look. Pitch leans the camera. Roll stays 0.
    const double horiz = std::sqrt(direction.x * direction.x + direction.z * direction.z);
    const DVec right = Mul(Cross(DVec{0, 1, 0}, direction), 1.0 / horiz);
    view.up = Cross(direction, right);
    return view;
}

void ApplyAirHeading(float& fx, float& fy, float& fz, float vx, float vy) {
    const float fxy = std::sqrt(fx * fx + fy * fy);
    if (fxy <= kAxisMinFxy) return;
    const float axis = std::atan2(fy, fx);
    const float speed2d = std::sqrt(vx * vx + vy * vy);
    float w = clampf((speed2d - kVelDirMin) / (kVelDirMax - kVelDirMin), 0.0f, 1.0f);
    w = w * w * (3.0f - 2.0f * w);
    if (w <= 0.0f) return;
    const float velYaw = std::atan2(vy, vx);
    const float mx = std::cos(axis) + w * (std::cos(velYaw) - std::cos(axis));
    const float my = std::sin(axis) + w * (std::sin(velYaw) - std::sin(axis));
    const float yaw = std::atan2(my, mx);
    const float pitch = std::atan2(fz, fxy);
    const float cp = std::cos(pitch);
    fx = cp * std::cos(yaw);
    fy = cp * std::sin(yaw);
    fz = std::sin(pitch);
}

void RotationFromDir(const float dir[3], float out[3]) {
    const float len2d = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
    out[0] = std::atan2(dir[2], len2d) * kRotPerRad;
    out[1] = std::atan2(dir[1], dir[0]) * kRotPerRad;
    out[2] = 0.0f;
}

bool ParseOn(float fv, const char* v) {
    return fv != 0.0f || std::strcmp(v, "true") == 0 || std::strcmp(v, "on") == 0;
}

void LookAt(const float eye[3], const float c[3], const float upIn[3]) {
    float f[3] = {c[0] - eye[0], c[1] - eye[1], c[2] - eye[2]};
    float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (l < 1e-6f) return;
    f[0] /= l; f[1] /= l; f[2] /= l;
    float s[3] = {f[1] * upIn[2] - f[2] * upIn[1], f[2] * upIn[0] - f[0] * upIn[2],
                  f[0] * upIn[1] - f[1] * upIn[0]};
    l = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (l < 1e-6f) return;
    s[0] /= l; s[1] /= l; s[2] /= l;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    float m[16] = {
        s[0], u[0], -f[0], 0,
        s[1], u[1], -f[1], 0,
        s[2], u[2], -f[2], 0,
        0, 0, 0, 1};
    glMultMatrixf(m);
    glTranslatef(-eye[0], -eye[1], -eye[2]);
}

} // namespace

Camera::Camera() = default;

bool Camera::LoadFromIni(const char* path) {
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char line[256];
    bool inCam = false;
    bool any = false;
    auto trim = [](char* s) -> char* {
        while (*s == ' ' || *s == '\t') s++;
        char* e = s + std::strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        return s;
    };
    while (std::fgets(line, sizeof(line), f)) {
        char* s = trim(line);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            inCam = std::strncmp(s, "[camera]", 8) == 0;
            continue;
        }
        if (!inCam) continue;
        char* eq = std::strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char* k = trim(s);
        char* v = trim(eq + 1);
        float fv = static_cast<float>(std::atof(v));
        if (!std::strcmp(k, "fov")) { params_.fov = fv; any = true; }
        else if (!std::strcmp(k, "distance")) { params_.distance = fv; any = true; }
        else if (!std::strcmp(k, "height")) { params_.height = fv; any = true; }
        else if (!std::strcmp(k, "angle")) { params_.angle = fv; any = true; }
        else if (!std::strcmp(k, "stiffness")) { params_.stiffness = fv; any = true; }
        else if (!std::strcmp(k, "transition_speed")) { params_.transitionSpeed = fv; any = true; }
        else if (!std::strcmp(k, "swivel_speed")) { params_.swivelSpeed = fv; any = true; }
        else if (!std::strcmp(k, "shake")) { params_.shake = ParseOn(fv, v); any = true; }
        else if (!std::strcmp(k, "invert_swivel")) { params_.invertSwivel = ParseOn(fv, v); any = true; }
        else if (!std::strcmp(k, "snap")) { params_.snap = ParseOn(fv, v); any = true; }
        else if (!std::strcmp(k, "air_heading")) { params_.airHeading = ParseOn(fv, v); any = true; }
        else if (!std::strcmp(k, "ballcam")) { params_.ballCam = ParseOn(fv, v); any = true; }
    }
    std::fclose(f);
    if (params_.fov < 60) params_.fov = 60;
    if (params_.fov > 110) params_.fov = 110;
    if (params_.distance < 100) params_.distance = 100;
    if (params_.distance > 400) params_.distance = 400;
    if (params_.height < 40) params_.height = 40;
    if (params_.height > 200) params_.height = 200;
    if (params_.angle < -30) params_.angle = -30;
    if (params_.angle > 15) params_.angle = 15;
    if (params_.stiffness < 0) params_.stiffness = 0;
    if (params_.stiffness > 1) params_.stiffness = 1;
    if (params_.transitionSpeed < 1) params_.transitionSpeed = 1;
    if (params_.transitionSpeed > 2) params_.transitionSpeed = 2;
    if (params_.swivelSpeed < 1) params_.swivelSpeed = 1;
    if (params_.swivelSpeed > 10) params_.swivelSpeed = 10;
    return any;
}

void Camera::WriteRay(const float eye[3], const float center[3]) {
    eye_[0] = eye[0];
    eye_[1] = eye[1];
    eye_[2] = eye[2];
    center_[0] = center[0];
    center_[1] = center[1];
    center_[2] = center[2];
    float dir[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len < 1e-6f) return;
    dir[0] /= len;
    dir[1] /= len;
    dir[2] /= len;
    RotationFromDir(dir, pov_.rotation);
    pov_.distance = len;
    pov_.focus[0] = center[0];
    pov_.focus[1] = center[1];
    pov_.focus[2] = center[2];
    pov_.fov = EffFov();
    pov_.time = povTime_;
}

void Camera::SetFreeView(const float eye[3], const float center[3]) {
    up_[0] = 0.0f;
    up_[1] = 0.0f;
    up_[2] = 1.0f;
    WriteRay(eye, center);
}

void Camera::SetRenderedPose(const float eye[3], const float center[3]) {
    WriteRay(eye, center);
}

void Camera::Update(float dt, const float carPos[3], float carYaw, const float ballPos[3],
                    bool ballCamOn, float carSpeed, bool supersonic, float swivelX, float swivelY,
                    const float carFwd[3], const float carVel[3], bool airborne,
                    const float carUp[3]) {
    params_.ballCam = ballCamOn;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    povTime_ += dt;
    float fx = carFwd ? carFwd[0] : std::cos(carYaw);
    float fy = carFwd ? carFwd[1] : std::sin(carYaw);
    float fz = carFwd ? carFwd[2] : 0.0f;
    const float ux = carUp ? carUp[0] : 0.0f;
    const float uy = carUp ? carUp[1] : 0.0f;
    const float uz = carUp ? carUp[2] : 1.0f;
    const float vx = carVel ? carVel[0] : fx * carSpeed;
    const float vy = carVel ? carVel[1] : fy * carSpeed;
    const float vz = carVel ? carVel[2] : fz * carSpeed;
    if (params_.airHeading && airborne) ApplyAirHeading(fx, fy, fz, vx, vy);
    Frame in;
    in.dt = dt;
    in.ballCam = ballCamOn;
    in.carPosition = ToChase(carPos[0], carPos[1], carPos[2]);
    in.ballPosition = ToChase(ballPos[0], ballPos[1], ballPos[2]);
    in.carOrientation = Orientation(ToChase(fx, fy, fz), ToChase(ux, uy, uz));
    in.rearView = lookBehind_;
    in.hasGround = true;
    in.onGround = !airborne;
    in.velocity = ToChase(vx, vy, vz);
    in.supersonic = supersonic;
    in.lookX = swivelX;
    in.lookY = swivelY;
    in.latchSwivel = !params_.snap;
    in.fov = params_.fov;
    in.distance = params_.distance;
    in.height = params_.height;
    in.angleDeg = params_.angle;
    in.stiffness = params_.stiffness;
    in.swivelSpeed = params_.swivelSpeed;
    in.transitionSpeed = params_.transitionSpeed;
    in.invertSwivel = params_.invertSwivel;
    State state{rig_.ready, rig_.yaw, rig_.pitch, rig_.yawStage, rig_.pitchStage,
                rig_.swivelYaw, rig_.swivelPitch, rig_.groundBlend, rig_.ballCamBlend,
                rig_.fovHorizontal};
    const Pose view = Evaluate(state, in);
    FromChase(view.position, eye_);
    float dir[3];
    FromChase(view.direction, dir);
    FromChase(view.up, up_);
    center_[0] = eye_[0] + dir[0] * kLookDistance;
    center_[1] = eye_[1] + dir[1] * kLookDistance;
    center_[2] = eye_[2] + dir[2] * kLookDistance;
    const float focus[3] = {carPos[0], carPos[1], carPos[2] + params_.height};
    const float dx = focus[0] - eye_[0];
    const float dy = focus[1] - eye_[1];
    const float dz = focus[2] - eye_[2];
    pov_.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    pov_.focus[0] = eye_[0] + dir[0] * pov_.distance;
    pov_.focus[1] = eye_[1] + dir[1] * pov_.distance;
    pov_.focus[2] = eye_[2] + dir[2] * pov_.distance;
    RotationFromDir(dir, pov_.rotation);
    pov_.fov = EffFov();
    pov_.time = povTime_;
}

void Camera::Apply(int viewW, int viewH) const {
    const float aspect = viewH > 0 ? static_cast<float>(viewW) / static_cast<float>(viewH) : 16.0f / 9.0f;
    const float safeAspect = aspect > 0.05f ? aspect : 0.05f;
    const float half = std::tan(EffFov() * 0.5f * kPi / 180.0f);
    float fovY = 2.0f * std::atan(half / safeAspect) * 180.0f / kPi;
    if (fovY < 40.0f) fovY = 40.0f;
    if (fovY > 130.0f) fovY = 130.0f;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float zn = 10.0f;
    const float zf = 40000.0f;
    const float t = zn * std::tan(fovY * 0.5f * kPi / 180.0f);
    const float r = t * aspect;
    glFrustum(-r, r, -t, t, zn, zf);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    LookAt(eye_, center_, up_);
}

} // namespace rl
