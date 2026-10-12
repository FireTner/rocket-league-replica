// Camera.cpp — RL chase-cam replication.
#include "Camera.h"
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rl {

Camera::Camera() = default;

// Minimal [camera] INI shim (schema v1 provisional, owned by A5).
// Accepts: fov, distance, height, angle, stiffness, ballcam.
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
        else if (!std::strcmp(k,"ballcam")) {
            params_.ballCam = (fv != 0 || !std::strcmp(v,"true") || !std::strcmp(v,"on"));
            any = true;
        }
        // NOTE(shim): if A5 renames e.g. distance->cam_dist, add alias here.
    }
    std::fclose(f);
    // Clamp to RL 1:1 ranges (FOV 60-130, Distance 100-400, Height 40-200,
    // Angle -30..15, Stiffness 0..1).
    if (params_.fov < 60) params_.fov = 60; if (params_.fov > 130) params_.fov = 130;
    if (params_.distance < 100) params_.distance = 100; if (params_.distance > 400) params_.distance = 400;
    if (params_.height < 40) params_.height = 40; if (params_.height > 200) params_.height = 200;
    if (params_.angle < -30) params_.angle = -30; if (params_.angle > 15) params_.angle = 15;
    if (params_.stiffness < 0) params_.stiffness = 0; if (params_.stiffness > 1) params_.stiffness = 1;
    return any;
}

void Camera::Update(float dt, const float carPos[3], float carYaw,
                    const float ballPos[3], bool ballCamOn) {
    params_.ballCam = ballCamOn;
    float fx = std::cos(carYaw), fy = std::sin(carYaw);
    // 1:1 RL geometry: eye sits behind the car at Distance, raised by Height,
    // pitched down by Angle. Ball-cam: the behind-axis is car->ball (camera
    // stays on the far side of the ball); car-cam: behind-axis is car facing.
    float ax = fx, ay = fy; // behind-axis (unit, horizontal)
    if (ballCamOn) {
        float dx = carPos[0] - ballPos[0], dy = carPos[1] - ballPos[1];
        float dl = std::sqrt(dx * dx + dy * dy);
        if (dl > 50.0f) {
            ax = dx / dl;
            ay = dy / dl;
        }
        // else: ball under the car — keep car facing (avoids axis flip).
    }
    float ang = params_.angle * (float)M_PI / 180.0f;
    float back = params_.distance * std::cos(ang);
    float up = params_.height - params_.distance * std::sin(ang);
    // Eye is on the opposite side of the look direction: ball-cam looks
    // car->ball so the eye sits on the car->ball axis behind the car (+);
    // car-cam looks along facing so the eye sits behind facing (-).
    float s = ballCamOn ? 1.0f : -1.0f;
    float want[3] = { carPos[0] + s*ax*back, carPos[1] + s*ay*back, carPos[2] + up };
    if (want[2] < 40.0f) want[2] = 40.0f; // never under floor
    // Look target: ball (ball-cam) or point ahead of car.
    float tgt[3];
    if (ballCamOn) { tgt[0]=ballPos[0]; tgt[1]=ballPos[1]; tgt[2]=ballPos[2]; }
    else { tgt[0]=carPos[0]+fx*400; tgt[1]=carPos[1]+fy*400; tgt[2]=carPos[2]+40; }

    if (!init_) { // snap on first frame
        eye_[0]=want[0]; eye_[1]=want[1]; eye_[2]=want[2];
        center_[0]=tgt[0]; center_[1]=tgt[1]; center_[2]=tgt[2];
        init_ = true;
        return;
    }
    // Stiffness as exponential smoothing: k = 1 - exp(-rate*dt),
    // rate mapped from 0..1 -> 2..20 /s (RL-snappy at high stiffness).
    float rate = 2.0f + params_.stiffness * 18.0f;
    float k = 1.0f - std::exp(-rate * dt);
    if (k < 0) k = 0; if (k > 1) k = 1;
    for (int i = 0; i < 3; i++) {
        eye_[i] += (want[i] - eye_[i]) * k;
        center_[i] += (tgt[i] - center_[i]) * (ballCamOn ? 1.0f : k); // ball locks instantly
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
    float fovY = params_.fov;
    // RL FOV slider is horizontal-ish; our projection is vertical fov.
    // Convert approx: keep horizontal extent constant vs 16:9 reference.
    // fovY = 2*atan(tan(fovX/2)/aspect). Treat slider as fovX at 16:9.
    {
        float fx = fovY * (float)M_PI/180.0f;
        float fy = 2.0f * std::atan(std::tan(fx*0.5f) / aspect * (16.0f/9.0f));
        fovY = fy * 180.0f / (float)M_PI;
        if (fovY < 40) fovY = 40; if (fovY > 120) fovY = 120;
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float zn = 10.0f, zf = 40000.0f;
    float t = zn * std::tan(fovY * 0.5f * (float)M_PI/180.0f);
    float r = t * aspect;
    glFrustum(-r, r, -t, t, zn, zf);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    LookAt(eye_, center_);
}

} // namespace rl
