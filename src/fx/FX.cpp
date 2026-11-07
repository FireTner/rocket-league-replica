// FX.cpp — procedural FX implementation.
#include "FX.h"
#include "fx/FlipReset.h"
#include <GL/gl.h>
#include <cmath>

namespace rl {

namespace {
// A segment longer than kBallTrailMaxSeg is skipped, so a hitch leaves a gap.
constexpr float kBallTrailMaxLen = 280.0f;
constexpr float kBallTrailMaxSeg = 220.0f;
constexpr float kBallTrailHalfW = 12.0f;
constexpr float kBallTrailFadeSec = 0.12f;

void BallTrailQuad(const float* p0, const float* p1,
                   float sx, float sy, float sz, float w0, float w1,
                   float cr, float cg, float cb, float a0, float a1) {
    glColor4f(cr, cg, cb, a0);
    glVertex3f(p0[0] + sx * w0, p0[1] + sy * w0, p0[2] + sz * w0);
    glVertex3f(p0[0] - sx * w0, p0[1] - sy * w0, p0[2] - sz * w0);
    glColor4f(cr, cg, cb, a1);
    glVertex3f(p1[0] + sx * w1, p1[1] + sy * w1, p1[2] + sz * w1);
    glColor4f(cr, cg, cb, a0);
    glVertex3f(p0[0] - sx * w0, p0[1] - sy * w0, p0[2] - sz * w0);
    glColor4f(cr, cg, cb, a1);
    glVertex3f(p1[0] - sx * w1, p1[1] - sy * w1, p1[2] - sz * w1);
    glVertex3f(p1[0] + sx * w1, p1[1] + sy * w1, p1[2] + sz * w1);
}

// Vertical shockwave ring in the XZ (facing Y) or YZ (facing X) plane —
// DrawRing() only does the floor plane, and a goal shockwave reads as a
// spherical blast, so we add two upright rings around the ball center.
void RingVertical(float cx, float cy, float cz, float radius, bool yz,
                  int segs, float r, float g, float b, float a) {
    if (segs < 16) segs = 16;
    glDisable(GL_LIGHTING);
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segs; i++) {
        float t = 2.0f * (float)M_PI * i / segs;
        float c = std::cos(t) * radius, s = std::sin(t) * radius;
        if (yz) glVertex3f(cx, cy + c, cz + s);
        else glVertex3f(cx + c, cy, cz + s);
    }
    glEnd();
    glEnable(GL_LIGHTING);
}
} // namespace

FX::FX() {
    for (int i = 0; i < kTrail; i++) trail_[i][0]=trail_[i][1]=trail_[i][2]=0;
}

void FX::Update(float dt, const Snapshot& s) {
    time_ += dt;
    if (burst_ > 0) burst_ -= dt;
    if (demoFlash_ > 0) demoFlash_ -= dt;
    if (shake_ > 0) { shake_ -= dt * 2.0f; if (shake_ < 0) shake_ = 0; }
    if (explode_ > 0) {
        explode_ -= dt;
        if (explode_ < 0) explode_ = 0;
        // Slow-mo beat: 0.25x for first 1s, ease back to 1.0 over 2s.
        slowmo_ = explode_ > 1.0f ? 0.25f : 0.25f + 0.75f * (1.0f - explode_);
    } else slowmo_ = 1.0f;

    // Edge-trigger from snapshot.
    if (s.goalScored && !prevGoal_) OnGoal();
    prevGoal_ = s.goalScored;
    if (s.demoed && !prevDemo_) OnDemo();
    prevDemo_ = s.demoed;

    // One path sample per sim step. A repeated render frame (dt == 0) must
    // not push a duplicate and erase the streak. Sim time only.
    if (dt > 0.0f) {
        for (int i = kTrail - 1; i > 0; i--) {
            trail_[i][0]=trail_[i-1][0]; trail_[i][1]=trail_[i-1][1]; trail_[i][2]=trail_[i-1][2];
        }
        trail_[0][0]=s.carPos[0]; trail_[0][1]=s.carPos[1]; trail_[0][2]=s.carPos[2];
        if (trailN_ < kTrail) trailN_++;
    }
    wheelEmbers_.Update(dt, s);
}

void FX::OnPickup(bool big) { burst_ = big ? 0.6f : 0.3f; burstBig_ = big; }
void FX::OnGoal() { explode_ = 2.0f; slowmo_ = 0.25f; shake_ = 1.0f; }
void FX::OnDemo() { demoFlash_ = 1.0f; shake_ = 0.7f; }

float FX::ShockwaveRadius() const {
    if (explode_ <= 0) return 0.0f;
    float k = 1.0f - explode_ / 2.0f; // 0 at trigger -> 1 at the end
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    return 140.0f + 2600.0f * k;
}

float FX::ShockwaveAlpha() const {
    if (explode_ <= 0) return 0.0f;
    float k = 1.0f - explode_ / 2.0f;
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    return 0.85f * (1.0f - k);
}

void FX::ShakeOffset(float out[2]) const {
    // Deterministic (sim time), 5-8 uu at full amplitude, no RNG.
    out[0] = shake_ * 7.0f * std::sin(time_ * 61.0f);
    out[1] = shake_ * 7.0f * std::cos(time_ * 53.0f);
}

void FX::DrawPads(Renderer& r, const Pad* pads, int n) {
    // RL-style pads (proportions tied to the real pickup cylinders, RLConst
    // radii 208 big / 144 small): dark hexagonal plate, thin emissive trim
    // ring, floating orb, soft ground halo. Respawn = dim + grow-back.
    const float kHexStep = (float)M_PI / 3.0f; // 60 deg per hexagon side
    for (int i = 0; i < n; i++) {
        const Pad& p = pads[i];
        float plateR = p.big ? 116.0f : 66.0f;
        float orbR = p.big ? 54.0f : 28.0f;
        float floatZ = p.big ? 120.0f : 85.0f;
        float pulse = 0.9f + 0.1f * std::sin(time_ * (p.big ? 2.4f : 4.0f) + (float)i);
        float total = p.big ? 10.0f : 4.0f;
        float frac = p.active ? 1.0f : (1.0f - p.cooldown / total);
        if (frac < 0) frac = 0; if (frac > 1) frac = 1;
        float sc = 0.25f + 0.75f * frac * frac; // spin-up scale
        float pr = plateR * sc;
        // Dark hexagonal plate (6-sided cylinder, ~8 uu thick, on the floor).
        // The plate is lit. Trim, halo, and the pickup burst stay unlit.
        if (r.LightingOn()) glEnable(GL_LIGHTING);
        glBegin(GL_TRIANGLE_FAN);
        glColor4f(0.23f, 0.25f, 0.28f, 1.0f);
        glNormal3f(0, 0, 1);
        glVertex3f(p.x, p.y, 6.0f);
        for (int k = 0; k <= 6; k++) {
            float ang = kHexStep * k + (float)M_PI / 6.0f;
            glNormal3f(0, 0, 1);
            glVertex3f(p.x + std::cos(ang) * pr, p.y + std::sin(ang) * pr, 6.0f);
        }
        glEnd();
        glBegin(GL_TRIANGLE_STRIP); // plate side skirt
        for (int k = 0; k <= 6; k++) {
            float ang = kHexStep * k + (float)M_PI / 6.0f;
            float c = std::cos(ang), s = std::sin(ang);
            glColor4f(0.15f, 0.17f, 0.19f, 1.0f);
            glNormal3f(c, s, 0.0f);
            glVertex3f(p.x + c * pr, p.y + s * pr, 0.75f);
            glColor4f(0.23f, 0.25f, 0.28f, 1.0f);
            glNormal3f(c, s, 0.0f);
            glVertex3f(p.x + c * pr, p.y + s * pr, 6.0f);
        }
        glEnd();
        glDisable(GL_LIGHTING); // rings/halo are emissive glows
        // Emissive trim ring hugging the plate (orange; brighter on big pads).
        float rimR = plateR * 1.16f * sc;
        if (p.active) {
            float glow = 0.8f + 0.2f * std::sin(time_ * 5.0f + (float)i);
            r.DrawRing(p.x, p.y, 2.0f, rimR * 0.92f, rimR, 24,
                       1.0f, (p.big ? 0.70f : 0.78f) * glow,
                       (p.big ? 0.12f : 0.27f) * glow, 0.85f);
        } else {
            r.DrawRing(p.x, p.y, 2.0f, rimR * 0.92f, rimR, 24,
                       0.30f, 0.32f, 0.35f, 0.35f);
        }
        // Ground halo + floating orb when active.
        if (p.active) {
            r.DrawRing(p.x, p.y, 1.0f, plateR * 1.2f * sc, plateR * 1.65f * sc, 32,
                       1.0f, p.big ? 0.63f : 0.80f, p.big ? 0.09f : 0.27f,
                       (p.big ? 0.30f : 0.22f) * pulse);
            float oz = floatZ + 10.0f * std::sin(time_ * 2.0f + (float)i);
            if (r.LightingOn()) glEnable(GL_LIGHTING);
            r.DrawSphere(p.x, p.y, oz, orbR * pulse, p.big ? 12 : 10,
                         1.0f, p.big ? 0.72f : 0.82f, p.big ? 0.16f : 0.35f);
            glDisable(GL_LIGHTING); // hot core is emissive, not a lit surface
            r.DrawSphere(p.x, p.y, oz, orbR * 0.55f * pulse, p.big ? 10 : 8,
                         1.0f, 0.95f, 0.55f); // hot core
        }
        if (r.LightingOn()) glEnable(GL_LIGHTING);
    }
    // Pickup burst flash at car.
    if (burst_ > 0) {
        float k = burst_ / (burstBig_ ? 0.6f : 0.3f);
        float ring = 40.0f + 200.0f * (1.0f - k); // expanding ring, not a blob
        r.DrawRing(trail_[0][0], trail_[0][1], 8.0f, ring * 0.7f, ring, 32,
                   1.0f, burstBig_ ? 0.82f : 0.45f, 0.22f, 0.85f * k);
        r.DrawSphere(trail_[0][0], trail_[0][1], trail_[0][2] + 30,
                     40 + 60 * (1 - k), 8, 1.0f, 0.95f, 0.6f, 0.6f * k);
    }
}

void FX::DrawBallIndicator(Renderer& r, const Snapshot& s) {
    // Outer ring 0.98r..1.25r and inner disc 0.62r. Both fade as the ball rises.
    const float rBall = 91.25f;
    float t = s.ballPos[2] / 1950.0f;
    if (t < 0) t = 0; if (t > 1) t = 1;
    float grow = 1.0f + 0.18f * t;
    float ringA = 0.9f + (0.12f - 0.9f) * t;
    float discA = 0.32f + (0.05f - 0.32f) * t;
    float bx = s.ballPos[0], by = s.ballPos[1];
    // Polygon offset keeps the marker above the markings quad. Radii stay in world space.
    GLfloat prevFactor = 0.0f, prevUnits = 0.0f;
    glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &prevFactor);
    glGetFloatv(GL_POLYGON_OFFSET_UNITS, &prevUnits);
    GLboolean offsetWas = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    GLboolean maskWas = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &maskWas);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -4.0f);
    // Translucent decal: test against the ball and the floor, but do not
    // write depth. The ring is drawn first; a depth write would eat the disc
    // where a grazing view overlaps them, and would punch the marker into
    // effects drawn after it.
    glDepthMask(GL_FALSE);
    r.DrawRing(bx, by, 2.0f, rBall * 0.98f * grow, rBall * 1.25f * grow, 48,
               0.874f, 0.957f, 1.0f, ringA);
    r.DrawDisc(bx, by, 1.0f, rBall * 0.62f * grow, 48,
               0.874f, 0.957f, 1.0f, discA);
    glDepthMask(maskWas ? GL_TRUE : GL_FALSE);
    glPolygonOffset(prevFactor, prevUnits);
    if (!offsetWas) glDisable(GL_POLYGON_OFFSET_FILL);
}

void FX::DrawBoostFlame(Renderer& r, const Snapshot& s) {
    // Two crossed ribbons on -carForward. Length follows remaining boost.
    // Nozzle is just behind the Fennec rear face (local x = -65.9, nose +x).
    if (!s.boosting || s.boost <= 0.0f) return;
    float amt = s.boost / 100.0f;
    if (amt < 0.0f) amt = 0.0f;
    if (amt > 1.0f) amt = 1.0f;
    float flick = 0.94f + 0.06f * std::sin(time_ * 47.0f);
    // ~40 uu near empty, ~104 uu at full. Under one car length so it stays
    // an exhaust. The old plume was 70..220 uu plus smoke out past 300.
    float len = (40.0f + 64.0f * amt) * flick;
    float wNozzle = 7.0f + 3.0f * amt;
    float fx = s.carForward[0], fy = s.carForward[1], fz = s.carForward[2];
    float ux = s.carUp[0], uy = s.carUp[1], uz = s.carUp[2];
    float lx = uy * fz - uz * fy, ly = uz * fx - ux * fz, lz = ux * fy - uy * fx;
    float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    if (ll < 1e-6f) return;
    lx /= ll; ly /= ll; lz /= ll;
    const float kBack = 68.0f;
    const float kSide = 18.0f;
    const float kDown = 6.0f;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // bright exhaust, not a shaded solid
    const int kSteps = 6;
    for (int e = -1; e <= 1; e += 2) {
        float ox = s.carPos[0] - fx * kBack + lx * (e * kSide) - ux * kDown;
        float oy = s.carPos[1] - fy * kBack + ly * (e * kSide) - uy * kDown;
        float oz = s.carPos[2] - fz * kBack + lz * (e * kSide) - uz * kDown;
        // Two crossed ribbons (left and up). Same length, so they read as
        // one jet rather than a stack of cones.
        const float axes[2][3] = {{lx, ly, lz}, {ux, uy, uz}};
        for (int ax = 0; ax < 2; ax++) {
            float sx = axes[ax][0], sy = axes[ax][1], sz = axes[ax][2];
            glBegin(GL_TRIANGLE_STRIP);
            for (int i = 0; i <= kSteps; i++) {
                float t = float(i) / float(kSteps);
                float w = wNozzle * (1.0f - t);
                float wob = 2.5f * t * std::sin(time_ * 37.0f + e + t * 5.0f);
                float cr, cg, cb, ca;
                if (t < 0.30f) {
                    float u = t / 0.30f;
                    cr = 1.0f;
                    cg = 0.98f + (0.78f - 0.98f) * u;
                    cb = 0.92f + (0.28f - 0.92f) * u;
                    ca = 0.95f;
                } else {
                    float u = (t - 0.30f) / 0.70f;
                    cr = 1.0f;
                    cg = 0.78f + (0.28f - 0.78f) * u;
                    cb = 0.28f + (0.04f - 0.28f) * u;
                    ca = 0.90f * (1.0f - 0.85f * u);
                }
                float bx = ox - fx * len * t + lx * wob;
                float by = oy - fy * len * t + ly * wob;
                float bz = oz - fz * len * t + lz * wob;
                glColor4f(cr, cg, cb, ca);
                glVertex3f(bx + sx * w, by + sy * w, bz + sz * w);
                glVertex3f(bx - sx * w, by - sy * w, bz - sz * w);
            }
            glEnd();
        }
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    if (r.LightingOn()) glEnable(GL_LIGHTING);
}

void FX::DrawDrift(Renderer& r, const Snapshot& s) {
    // RL powerslide: white-grey tire smoke at the rear wheels + dark slide
    // marks when handbrake is held at speed on the ground. Also a light smoke
    // wisp on hard non-slide turns (high slip) so drift reads pre-slide.
    float f[3] = {s.carForward[0], s.carForward[1], s.carForward[2]};
    float u[3] = {s.carUp[0], s.carUp[1], s.carUp[2]};
    float l[3] = {u[1]*f[2]-u[2]*f[1], u[2]*f[0]-u[0]*f[2], u[0]*f[1]-u[1]*f[0]};
    float fwdSp = s.carVel[0]*f[0]+s.carVel[1]*f[1]+s.carVel[2]*f[2];
    float latSp = s.carVel[0]*l[0]+s.carVel[1]*l[1]+s.carVel[2]*l[2];
    float slip = std::fabs(latSp) / (std::fabs(fwdSp) + 250.0f);
    bool sliding = s.handbrake && !s.airborne && s.carSpeed > 350.0f;
    bool hardTurn = !s.airborne && slip > 0.45f && s.carSpeed > 800.0f;
    if (!sliding && !hardTurn) return;
    float density = sliding ? 1.0f : 0.4f;
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    for (int w = -1; w <= 1; w += 2) {
        for (int i = 0; i < 5; i++) {
            float ph = time_ * 11.0f + i * 2.1f + (w > 0 ? 3.1f : 0.0f);
            float back = 38.0f + 30.0f * i + 10.0f * std::sin(ph);
            float side = w * 44.0f + 8.0f * std::sin(ph * 1.7f);
            float dx = s.carPos[0] - f[0] * back + l[0] * side;
            float dy = s.carPos[1] - f[1] * back + l[1] * side;
            float dz = s.carPos[2] - 18.0f + 6.0f * i + 4.0f * std::sin(ph);
            if (dz < 4.0f) dz = 4.0f;
            float a = (0.42f - 0.07f * i) * density
                    * (0.6f + 0.4f * std::sin(ph * 1.3f));
            if (a < 0) a = 0;
            r.DrawSphere(dx, dy, dz, 7.0f + 4.5f * i, 6,
                         0.75f, 0.75f, 0.78f, a);
        }
        // Dark rubber mark at the contact patch while sliding.
        if (sliding)
            r.DrawDisc(s.carPos[0] - f[0]*38.0f + l[0]*(w*44.0f),
                       s.carPos[1] - f[1]*38.0f + l[1]*(w*44.0f),
                       1.5f, 16.0f, 10, 0.08f, 0.08f, 0.09f, 0.5f);
    }
    // Grind sparks. Separate from DrawTrail's black sparkles. Still requires the 2200 uu/s flag.
    if (sliding && s.carSpeed >= supersonic_) {
        for (int i = 0; i < 4; i++) {
            float ph = time_ * 17.0f + i * 2.7f;
            float back = 50.0f + 40.0f * std::fabs(std::sin(ph));
            float side = (i & 1 ? 44.0f : -44.0f);
            r.DrawSphere(s.carPos[0]-f[0]*back+l[0]*side,
                         s.carPos[1]-f[1]*back+l[1]*side,
                         s.carPos[2]-14.0f+8.0f*std::sin(ph), 4.5f, 5,
                         1.0f, 0.55f, 0.15f, 0.6f);
        }
    }
    glDepthMask(GL_TRUE);
    if (r.LightingOn()) glEnable(GL_LIGHTING);
}

void FX::DrawAir(Renderer& r, const Snapshot& s) {
    // The old spin arcs stay gone. This draws the flip-reset disc only
    // while Snapshot.flipReset is set (RocketSim HasFlipReset).
    DrawFlipReset(r, s);
}

void FX::DrawTrail(Renderer& r, const Snapshot& s) {
    // Streak and sparkles follow RocketSim's flag (Snapshot.supersonic),
    // including the 2100 maintain band. A bare speed >= 2200 check is not used.
    if (s.supersonic && trailN_ >= 2) {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDepthMask(GL_FALSE);
        // Crossed quads along the recent path, 320 uu.
        const float kStreakLen = 320.0f;
        float traveled = 0.0f;
        glBegin(GL_QUADS);
        for (int i = 0; i < trailN_ - 1; i++) {
            float dx = trail_[i + 1][0] - trail_[i][0];
            float dy = trail_[i + 1][1] - trail_[i][1];
            float dz = trail_[i + 1][2] - trail_[i][2];
            float seg = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (seg < 0.5f) continue;
            if (traveled >= kStreakLen) break;
            float u0 = traveled / kStreakLen;
            float u1 = (traveled + seg) / kStreakLen;
            if (u1 > 1.0f) u1 = 1.0f;
            dx /= seg; dy /= seg; dz /= seg;
            float hx = -dy, hy = dx, hz = 0.0f;
            float hl = std::sqrt(hx * hx + hy * hy);
            if (hl < 1e-4f) { hx = 1.0f; hy = 0.0f; hz = 0.0f; hl = 1.0f; }
            hx /= hl; hy /= hl;
            float vx = dy * hz - dz * hy;
            float vy = dz * hx - dx * hz;
            float vz = dx * hy - dy * hx;
            float vl = std::sqrt(vx * vx + vy * vy + vz * vz);
            if (vl < 1e-4f) { vx = 0.0f; vy = 0.0f; vz = 1.0f; vl = 1.0f; }
            vx /= vl; vy /= vl; vz /= vl;
            float w0 = 14.0f * (1.0f - u0);
            float w1 = 14.0f * (1.0f - u1);
            if (w0 < 2.0f) w0 = 2.0f;
            if (w1 < 2.0f) w1 = 2.0f;
            float a0 = 0.55f * (1.0f - u0);
            float a1 = 0.55f * (1.0f - u1);
            float s0[2][3] = {{hx, hy, hz}, {vx, vy, vz}};
            for (int ax = 0; ax < 2; ax++) {
                float sx = s0[ax][0], sy = s0[ax][1], sz = s0[ax][2];
                glColor4f(0.92f, 0.97f, 1.0f, a0);
                glVertex3f(trail_[i][0] + sx * w0, trail_[i][1] + sy * w0, trail_[i][2] + sz * w0);
                glVertex3f(trail_[i][0] - sx * w0, trail_[i][1] - sy * w0, trail_[i][2] - sz * w0);
                glColor4f(0.92f, 0.97f, 1.0f, a1);
                glVertex3f(trail_[i + 1][0] - sx * w1, trail_[i + 1][1] - sy * w1, trail_[i + 1][2] - sz * w1);
                glVertex3f(trail_[i + 1][0] + sx * w1, trail_[i + 1][1] + sy * w1, trail_[i + 1][2] + sz * w1);
            }
            traveled += seg;
        }
        glEnd();
        // Black sparkles. Not the streak quads and not DrawDrift's grind sparks.
        float fx = s.carForward[0], fy = s.carForward[1], fz = s.carForward[2];
        float ux = s.carUp[0], uy = s.carUp[1], uz = s.carUp[2];
        float lx = uy * fz - uz * fy, ly = uz * fx - ux * fz, lz = ux * fy - uy * fx;
        float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
        if (ll > 1e-6f) {
            lx /= ll; ly /= ll; lz /= ll;
            const float kHalf = 6.0f;   // sparkle half-size, uu
            const float kThick = 1.4f;  // arm thickness, uu
            glBegin(GL_QUADS);
            for (int i = 0; i < 8; i++) {
                float cycle = time_ * 5.5f + i * 0.37f;
                float u = cycle - std::floor(cycle);
                float back = 80.0f + 150.0f * u;
                float side = ((i & 1) ? 1.0f : -1.0f) * (10.0f + 22.0f * std::fabs(std::sin(cycle * 1.7f)));
                float lift = 8.0f + 16.0f * std::fabs(std::sin(cycle * 2.3f + i));
                float a = 0.90f * (1.0f - u);
                float px = s.carPos[0] - fx * back + lx * side + ux * lift;
                float py = s.carPos[1] - fy * back + ly * side + uy * lift;
                float pz = s.carPos[2] - fz * back + lz * side + uz * lift;
                glColor4f(0.0f, 0.0f, 0.0f, a);
                // Plus-sign in the car's left/up plane (faces back along
                // forward, so a chase camera sees it).
                glVertex3f(px + lx * kHalf + ux * kThick, py + ly * kHalf + uy * kThick, pz + lz * kHalf + uz * kThick);
                glVertex3f(px + lx * kHalf - ux * kThick, py + ly * kHalf - uy * kThick, pz + lz * kHalf - uz * kThick);
                glVertex3f(px - lx * kHalf - ux * kThick, py - ly * kHalf - uy * kThick, pz - lz * kHalf - uz * kThick);
                glVertex3f(px - lx * kHalf + ux * kThick, py - ly * kHalf + uy * kThick, pz - lz * kHalf + uz * kThick);
                glVertex3f(px + ux * kHalf + lx * kThick, py + uy * kHalf + ly * kThick, pz + uz * kHalf + lz * kThick);
                glVertex3f(px + ux * kHalf - lx * kThick, py + uy * kHalf - ly * kThick, pz + uz * kHalf - lz * kThick);
                glVertex3f(px - ux * kHalf - lx * kThick, py - uy * kHalf - ly * kThick, pz - uz * kHalf - lz * kThick);
                glVertex3f(px - ux * kHalf + lx * kThick, py - uy * kHalf + ly * kThick, pz - uz * kHalf + lz * kThick);
            }
            glEnd();
        }
        glDepthMask(GL_TRUE);
        if (r.LightingOn()) glEnable(GL_LIGHTING);
    }
    if (explode_ > 0) {
        // Expanding ground ring plus two upright rings, fading as they grow.
        float rad = ShockwaveRadius();
        float a = ShockwaveAlpha();
        r.DrawRing(s.ballPos[0], s.ballPos[1], 3.0f, rad * 0.82f, rad, 64,
                   1.0f, 0.68f, 0.22f, a);
        r.DrawRing(s.ballPos[0], s.ballPos[1], 3.5f, rad * 0.55f, rad * 0.66f, 64,
                   1.0f, 0.90f, 0.55f, a * 0.7f);
        RingVertical(s.ballPos[0], s.ballPos[1], s.ballPos[2], rad, false, 48,
                     1.0f, 0.78f, 0.35f, a * 0.8f);
        RingVertical(s.ballPos[0], s.ballPos[1], s.ballPos[2], rad, true, 48,
                     1.0f, 0.78f, 0.35f, a * 0.8f);
    }
    if (demoFlash_ > 0) {
        // Demolition feedback: bright core + expanding ring at the wreck.
        float k = demoFlash_;
        r.DrawSphere(s.carPos[0], s.carPos[1], s.carPos[2]+40, 200 * k, 10,
                     0.7f, 0.9f, 1.0f, 0.5f * k);
        r.DrawRing(s.carPos[0], s.carPos[1], 6.0f, 260.0f * (1.2f - k),
                   300.0f * (1.2f - k), 32, 0.85f, 0.95f, 1.0f, 0.7f * k);
    }
}

void FX::DrawWheelEmbers(Renderer& r, const Snapshot& s) {
    wheelEmbers_.Draw(r, s);
}

void FX::DrawBallTrail(Renderer& r, const Snapshot& s) {
    // Separate from DrawBallIndicator and from DrawTrail (car streak on
    // trail_[]). Samples live only in ballTrail_[].
    const bool newTick = !ballTrailHaveTick_ || s.tick != ballTrailTick_;
    float dt = 0.0f;
    if (ballTrailHaveTick_ && s.tick > ballTrailTick_)
        dt = float(s.tick - ballTrailTick_) * (1.0f / 120.0f);
    if (newTick) {
        ballTrailTick_ = s.tick;
        ballTrailHaveTick_ = true;
    }
    if (!s.ballTouched) {
        ballTrailFade_ = 0.0f;
        ballTrailN_ = 0;
        return;
    }
    const bool fast = s.ballSpeed >= kBallTrailSpeedFloor;
    if (newTick && fast) {
        for (int i = kBallTrail - 1; i > 0; --i) {
            ballTrail_[i][0] = ballTrail_[i - 1][0];
            ballTrail_[i][1] = ballTrail_[i - 1][1];
            ballTrail_[i][2] = ballTrail_[i - 1][2];
        }
        ballTrail_[0][0] = s.ballPos[0];
        ballTrail_[0][1] = s.ballPos[1];
        ballTrail_[0][2] = s.ballPos[2];
        if (ballTrailN_ < kBallTrail) ballTrailN_++;
    }
    if (fast) {
        ballTrailFade_ = 1.0f;
    } else if (ballTrailFade_ > 0.0f) {
        ballTrailFade_ -= dt / kBallTrailFadeSec;
        if (ballTrailFade_ < 0.0f) ballTrailFade_ = 0.0f;
    }
    if (ballTrailFade_ <= 0.0f || ballTrailN_ < 2) {
        if (ballTrailFade_ <= 0.0f) ballTrailN_ = 0;
        return;
    }
    // Same RGB as the car team paint (Models.cpp DrawCar, ledger Team paint).
    const bool orange = s.ballTeam == 1;
    const float cr = orange ? 0.85f : 0.10f;
    const float cg = orange ? 0.30f : 0.35f;
    const float cb = orange ? 0.08f : 0.90f;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glBegin(GL_TRIANGLES);
    int tris = 0;
    float walked = 0.0f;
    for (int i = 0; i + 1 < ballTrailN_; i++) {
        float dx = ballTrail_[i][0] - ballTrail_[i + 1][0];
        float dy = ballTrail_[i][1] - ballTrail_[i + 1][1];
        float dz = ballTrail_[i][2] - ballTrail_[i + 1][2];
        float seg = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (seg < 0.5f || seg > kBallTrailMaxSeg) continue;
        if (walked >= kBallTrailMaxLen) break;
        walked += seg;
        dx /= seg; dy /= seg; dz /= seg;
        float hx = 0.0f, hy = 0.0f, hz = 1.0f;
        if (std::fabs(dz) >= 0.9f) { hx = 1.0f; hy = 0.0f; hz = 0.0f; }
        float ax = dy * hz - dz * hy;
        float ay = dz * hx - dx * hz;
        float az = dx * hy - dy * hx;
        float al = std::sqrt(ax * ax + ay * ay + az * az);
        if (al < 1e-6f) continue;
        ax /= al; ay /= al; az /= al;
        float bx = dy * az - dz * ay;
        float by = dz * ax - dx * az;
        float bz = dx * ay - dy * ax;
        float u0 = float(i) / float(kBallTrail);
        float u1 = float(i + 1) / float(kBallTrail);
        float w0 = kBallTrailHalfW * (1.0f - u0);
        float w1 = kBallTrailHalfW * (1.0f - u1);
        float a0 = ballTrailFade_ * (1.0f - u0);
        float a1 = ballTrailFade_ * (1.0f - u1);
        BallTrailQuad(ballTrail_[i], ballTrail_[i + 1], ax, ay, az, w0, w1,
                      cr, cg, cb, a0, a1);
        BallTrailQuad(ballTrail_[i], ballTrail_[i + 1], bx, by, bz, w0, w1,
                      cr, cg, cb, a0, a1);
        tris += 4;
    }
    glEnd();
    glDepthMask(GL_TRUE);
    if (r.LightingOn()) glEnable(GL_LIGHTING);
    if (tris > 0) r.NoteExternalDraw(uint64_t(tris));
}

void FX::DrawContactSparks(Renderer& r, const ContactSparkFrame& c) {
    // Own immediate-mode draw. Does not push into the trail ribbon, the
    // black sparkle quads in DrawTrail, the boost flame, or DrawAir.
    if (c.awkward) {
        ballSparkUntil_ = time_ + kBallBurstSeconds;
        ballSparkX_ = c.hx;
        ballSparkY_ = c.hy;
        ballSparkZ_ = c.hz;
    }
    bool burst = time_ < ballSparkUntil_;
    if (!c.scrape && !burst)
        return;
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    if (c.scrape) {
        for (int p = 0; p < c.count; ++p) {
            for (int i = 0; i < 3; ++i) {
                float ph = time_ * 19.0f + p * 2.3f + i * 1.7f;
                float dist = 8.0f + 36.0f * std::fabs(std::sin(ph));
                float lift = 3.0f + 8.0f * std::fabs(std::sin(ph * 1.3f));
                float px = c.x[p] + c.bx * dist + c.nx[p] * lift;
                float py = c.y[p] + c.by * dist + c.ny[p] * lift;
                float pz = c.z[p] + c.bz * dist + c.nz[p] * lift;
                if (pz < 2.0f)
                    pz = 2.0f;
                // White-yellow. Not DrawTrail's black sparkles.
                r.DrawSphere(px, py, pz, 3.2f + 1.4f * float(2 - i), 6,
                             1.0f, 0.90f - 0.08f * i, 0.40f, 0.75f - 0.18f * i);
            }
        }
    }
    if (burst) {
        float k = (ballSparkUntil_ - time_) / kBallBurstSeconds;
        if (k < 0) k = 0;
        if (k > 1) k = 1;
        float grow = 1.0f - k;
        for (int i = 0; i < 6; ++i) {
            float ang = i * (3.14159265f / 3.0f);
            float rad = 8.0f + 32.0f * grow;
            r.DrawSphere(ballSparkX_ + std::cos(ang) * rad,
                         ballSparkY_ + std::sin(ang) * rad,
                         ballSparkZ_ + 6.0f + 14.0f * grow,
                         3.0f + 5.0f * k, 6, 1.0f, 0.92f, 0.50f, 0.8f * k);
        }
    }
    glDepthMask(GL_TRUE);
    if (r.LightingOn())
        glEnable(GL_LIGHTING);
}

} // namespace rl
