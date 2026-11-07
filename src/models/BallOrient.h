// Ball orientation integrated from world angular velocity. RocketSim leaves rotMat at identity.
#pragma once

#include <cmath>

namespace rl {

// Column-major 3x3. Column 0/1/2 = local +X/+Y/+Z in world.
inline void SetBallRotIdentity(float rot[9]) {
    rot[0] = 1.0f; rot[1] = 0.0f; rot[2] = 0.0f;
    rot[3] = 0.0f; rot[4] = 1.0f; rot[5] = 0.0f;
    rot[6] = 0.0f; rot[7] = 0.0f; rot[8] = 1.0f;
}

inline bool BallRotIsIdentity(const float rot[9]) {
    return rot[0] == 1.0f && rot[1] == 0.0f && rot[2] == 0.0f &&
           rot[3] == 0.0f && rot[4] == 1.0f && rot[5] == 0.0f &&
           rot[6] == 0.0f && rot[7] == 0.0f && rot[8] == 1.0f;
}

// World direction of a body-fixed point. Same columns as BallOrientationMatrix.
inline void MapBallPoint(const float rot[9], float lx, float ly, float lz,
                         float& ox, float& oy, float& oz) {
    ox = rot[0] * lx + rot[3] * ly + rot[6] * lz;
    oy = rot[1] * lx + rot[4] * ly + rot[7] * lz;
    oz = rot[2] * lx + rot[5] * ly + rot[8] * lz;
}

// GL column-major 4x4 rotation (translation is the caller's).
inline void BallOrientationMatrix(const float rot[9], float m[16]) {
    m[0] = rot[0]; m[1] = rot[1]; m[2] = rot[2]; m[3] = 0.0f;
    m[4] = rot[3]; m[5] = rot[4]; m[6] = rot[5]; m[7] = 0.0f;
    m[8] = rot[6]; m[9] = rot[7]; m[10] = rot[8]; m[11] = 0.0f;
    m[12] = 0.0f; m[13] = 0.0f; m[14] = 0.0f; m[15] = 1.0f;
}

// Rodrigues step of the post-step world rate over the whole tick. Zero ω leaves rot untouched.
inline void IntegrateBallSpin(float rot[9], float wx, float wy, float wz, float dt) {
    float w2 = wx * wx + wy * wy + wz * wz;
    if (w2 == 0.0f || dt == 0.0f)
        return;
    float wlen = std::sqrt(w2);
    float theta = wlen * dt;
    float inv = 1.0f / wlen;
    float ax = wx * inv, ay = wy * inv, az = wz * inv;
    float c = std::cos(theta);
    float s = std::sin(theta);
    float t = 1.0f - c;
    float r00 = t * ax * ax + c;
    float r01 = t * ax * ay - s * az;
    float r02 = t * ax * az + s * ay;
    float r10 = t * ax * ay + s * az;
    float r11 = t * ay * ay + c;
    float r12 = t * ay * az - s * ax;
    float r20 = t * ax * az - s * ay;
    float r21 = t * ay * az + s * ax;
    float r22 = t * az * az + c;
    float next[9];
    for (int col = 0; col < 3; col++) {
        float x = rot[col * 3 + 0];
        float y = rot[col * 3 + 1];
        float z = rot[col * 3 + 2];
        next[col * 3 + 0] = r00 * x + r01 * y + r02 * z;
        next[col * 3 + 1] = r10 * x + r11 * y + r12 * z;
        next[col * 3 + 2] = r20 * x + r21 * y + r22 * z;
    }
    auto norm = [](float v[3]) {
        float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len < 1.0e-12f)
            return;
        float s = 1.0f / len;
        v[0] *= s;
        v[1] *= s;
        v[2] *= s;
    };
    norm(next + 0);
    float d = next[3] * next[0] + next[4] * next[1] + next[5] * next[2];
    next[3] -= d * next[0];
    next[4] -= d * next[1];
    next[5] -= d * next[2];
    norm(next + 3);
    next[6] = next[1] * next[5] - next[2] * next[4];
    next[7] = next[2] * next[3] - next[0] * next[5];
    next[8] = next[0] * next[4] - next[1] * next[3];
    for (int i = 0; i < 9; i++)
        rot[i] = next[i];
}

} // namespace rl
