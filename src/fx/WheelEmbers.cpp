#include "fx/WheelEmbers.h"

#include <GL/gl.h>
#include <cmath>

namespace rl {

namespace {
// UNVERIFIED. Short on purpose so the mark stays at the tyre, not a trail.
constexpr float kEmberLife = 0.10f;
constexpr float kEmitGap = 0.05f;
constexpr float kDrift = 160.0f; // uu/s back along the car forward
constexpr float kHalf = 2.2f;

int FindSlot(const WheelEmber* embers) {
    for (int i = 0; i < kWheelEmberCap; ++i)
        if (!embers[i].alive)
            return i;
    return -1;
}
} // namespace

bool WheelContactPoint(const Snapshot& s, int wheel, float out[3]) {
    if (wheel < 0 || wheel > 3)
        return false;
    float f[3] = {s.carForward[0], s.carForward[1], s.carForward[2]};
    float u[3] = {s.carUp[0], s.carUp[1], s.carUp[2]};
    float l[3] = {u[1] * f[2] - u[2] * f[1],
                  u[2] * f[0] - u[0] * f[2],
                  u[0] * f[1] - u[1] * f[0]};
    float sx = (wheel & 1) ? -1.0f : 1.0f;
    float sy = (wheel & 2) ? -1.0f : 1.0f;
    out[0] = s.carPos[0] + f[0] * sx * kWheelEmberHalfL + l[0] * sy * kWheelEmberHalfW
             - u[0] * kWheelEmberRadius;
    out[1] = s.carPos[1] + f[1] * sx * kWheelEmberHalfL + l[1] * sy * kWheelEmberHalfW
             - u[1] * kWheelEmberRadius;
    out[2] = s.carPos[2] + f[2] * sx * kWheelEmberHalfL + l[2] * sy * kWheelEmberHalfW
             - u[2] * kWheelEmberRadius;
    return true;
}

void WheelEmbers::Update(float dt, const Snapshot& s) {
    if (dt <= 0.0f)
        return;
    for (int i = 0; i < kWheelEmberCap; ++i) {
        WheelEmber& e = embers_[i];
        if (!e.alive)
            continue;
        e.life -= dt;
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        e.z += e.vz * dt;
        if (e.life <= 0.0f)
            e.alive = false;
    }
    // Contact only. Airborne wheels are not on the ground. The flag is
    // Snapshot.supersonic (2200 start, hold to 2100), not a second speed test.
    if (!s.supersonic || s.airborne)
        return;
    emitWait_ -= dt;
    if (emitWait_ > 0.0f)
        return;
    emitWait_ = kEmitGap;
    float f[3] = {s.carForward[0], s.carForward[1], s.carForward[2]};
    for (int w = 0; w < 4; ++w) {
        int slot = FindSlot(embers_);
        if (slot < 0)
            return;
        float p[3];
        if (!WheelContactPoint(s, w, p))
            continue;
        WheelEmber& e = embers_[slot];
        e.x = p[0];
        e.y = p[1];
        e.z = p[2];
        e.vx = -f[0] * kDrift;
        e.vy = -f[1] * kDrift;
        e.vz = -f[2] * kDrift;
        e.life = kEmberLife;
        e.maxLife = kEmberLife;
        e.alive = true;
    }
}

int WheelEmbers::AliveCount() const {
    int n = 0;
    for (int i = 0; i < kWheelEmberCap; ++i)
        if (embers_[i].alive)
            n++;
    return n;
}

void WheelEmbers::Draw(Renderer& r, const Snapshot& s) const {
    if (AliveCount() == 0)
        return;
    float u[3] = {s.carUp[0], s.carUp[1], s.carUp[2]};
    float f[3] = {s.carForward[0], s.carForward[1], s.carForward[2]};
    float l[3] = {u[1] * f[2] - u[2] * f[1],
                  u[2] * f[0] - u[0] * f[2],
                  u[0] * f[1] - u[1] * f[0]};
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (int i = 0; i < kWheelEmberCap; ++i) {
        const WheelEmber& e = embers_[i];
        if (!e.alive || e.maxLife <= 0.0f)
            continue;
        float k = e.life / e.maxLife;
        if (k < 0.0f) k = 0.0f;
        if (k > 1.0f) k = 1.0f;
        // Warm orange fading toward yellow-white. UNVERIFIED colour.
        glColor4f(1.0f, 0.42f + 0.45f * k, 0.08f + 0.25f * k, 0.80f * k);
        float px = e.x, py = e.y, pz = e.z;
        glVertex3f(px + l[0] * kHalf + u[0] * kHalf,
                   py + l[1] * kHalf + u[1] * kHalf,
                   pz + l[2] * kHalf + u[2] * kHalf);
        glVertex3f(px + l[0] * kHalf - u[0] * kHalf,
                   py + l[1] * kHalf - u[1] * kHalf,
                   pz + l[2] * kHalf - u[2] * kHalf);
        glVertex3f(px - l[0] * kHalf - u[0] * kHalf,
                   py - l[1] * kHalf - u[1] * kHalf,
                   pz - l[2] * kHalf - u[2] * kHalf);
        glVertex3f(px - l[0] * kHalf + u[0] * kHalf,
                   py - l[1] * kHalf + u[1] * kHalf,
                   pz - l[2] * kHalf + u[2] * kHalf);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    if (r.LightingOn())
        glEnable(GL_LIGHTING);
}

} // namespace rl
