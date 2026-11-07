// Supersonic wheel embers. Presentation only. Own particles: not the car
// position ribbon, not the boost streak, not contact sparks.
// Length, count, and colour are UNVERIFIED (no live capture).
#pragma once

#include "render/Renderer.h"

namespace rl {

// Visual wheel patch in the car frame used by DrawCar (Models.cpp).
// +x forward, +y left, contact is one wheel radius down the car up axis.
// Octane-ish radius 17 uu matches the procedural wheel, not a sim query.
constexpr float kWheelEmberHalfL = 120.5f * 0.32f;
constexpr float kWheelEmberHalfW = 86.7f * 0.52f;
constexpr float kWheelEmberRadius = 17.0f;
constexpr int kWheelEmberCap = 16;

struct WheelEmber {
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;
    float life = 0;
    float maxLife = 0;
    bool alive = false;
};

// wheel is 0..3. Out of range leaves out unchanged and returns false.
bool WheelContactPoint(const Snapshot& s, int wheel, float out[3]);

class WheelEmbers {
public:
    void Update(float dt, const Snapshot& s);
    void Draw(Renderer& r, const Snapshot& s) const;
    int AliveCount() const;

private:
    WheelEmber embers_[kWheelEmberCap];
    float emitWait_ = 0;
};

} // namespace rl
