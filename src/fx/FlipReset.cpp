#include "fx/FlipReset.h"

#include <GL/gl.h>
#include <cmath>

namespace rl {

namespace {
// Roof offset and radius in uu. UNVERIFIED — presentation only.
constexpr float kLift = 48.0f;
constexpr float kRadius = 28.0f;
constexpr int kSegs = 16;
}

void DrawFlipReset(Renderer& r, const Snapshot& s) {
    if (!s.flipReset) return;
    float fx = s.carForward[0], fy = s.carForward[1], fz = s.carForward[2];
    float ux = s.carUp[0], uy = s.carUp[1], uz = s.carUp[2];
    float rx = uy * fz - uz * fy;
    float ry = uz * fx - ux * fz;
    float rz = ux * fy - uy * fx;
    float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rlen < 1e-4f) return;
    rx /= rlen; ry /= rlen; rz /= rlen;
    float cx = s.carPos[0] + ux * kLift;
    float cy = s.carPos[1] + uy * kLift;
    float cz = s.carPos[2] + uz * kLift;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glColor4f(0.55f, 0.95f, 1.0f, 0.85f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(cx, cy, cz);
    for (int i = 0; i <= kSegs; i++) {
        float t = 2.0f * 3.14159265f * i / kSegs;
        float c = std::cos(t) * kRadius;
        float sn = std::sin(t) * kRadius;
        glVertex3f(cx + fx * c + rx * sn,
                   cy + fy * c + ry * sn,
                   cz + fz * c + rz * sn);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    if (r.LightingOn()) glEnable(GL_LIGHTING);
}

} // namespace rl
