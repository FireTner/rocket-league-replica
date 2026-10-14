// FX.cpp — procedural FX implementation.
#include "FX.h"
#include <GL/gl.h>
#include <cmath>

namespace rl {

namespace {
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

    // Push trail sample.
    for (int i = kTrail - 1; i > 0; i--) {
        trail_[i][0]=trail_[i-1][0]; trail_[i][1]=trail_[i-1][1]; trail_[i][2]=trail_[i-1][2];
    }
    trail_[0][0]=s.carPos[0]; trail_[0][1]=s.carPos[1]; trail_[0][2]=s.carPos[2];
    if (trailN_ < kTrail) trailN_++;
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
        // LIT (V6): the plate must read as a solid, not a flat decal. The trim
        // rings, ground halo and pickup burst stay UNLIT -- they are glows.
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
            // Orbs are LIT spheres (V6): a lit sphere reads as a ball; the
            // previous flat-shaded version was a disc.
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
    // RL ball ground indicator: the highlight under the ball with BOTH
    // circles — outer glow ring (0.98r..1.25r) + inner filled disc (0.62r),
    // light cyan-white, fading/growing as the ball rises (floor by ~1950 uu).
    const float rBall = 91.25f;
    float t = s.ballPos[2] / 1950.0f;
    if (t < 0) t = 0; if (t > 1) t = 1;
    float grow = 1.0f + 0.18f * t;
    float ringA = 0.9f + (0.12f - 0.9f) * t;
    float discA = 0.32f + (0.05f - 0.32f) * t;
    float bx = s.ballPos[0], by = s.ballPos[1];
    r.DrawRing(bx, by, 2.0f, rBall * 0.98f * grow, rBall * 1.25f * grow, 48,
               0.874f, 0.957f, 1.0f, ringA);
    r.DrawDisc(bx, by, 1.0f, rBall * 0.62f * grow, 48,
               0.874f, 0.957f, 1.0f, discA);
}

void FX::DrawBoostFlame(Renderer& r, const Snapshot& s) {
    // RL boost: twin exhaust cones, white-hot core -> yellow -> orange outer,
    // flickering length, plus faint smoke wisps. Oriented along -forward in
    // full 3D (works pitched/rolled), unlit emissive.
    if (!s.boosting || s.boost <= 0) return;
    float amt = s.boost / 100.0f; // scale flame by remaining boost
    float flick = 0.85f + 0.15f * std::sin(time_ * 43.0f)
                + 0.06f * std::sin(time_ * 91.0f);
    float len = (70.0f + 150.0f * amt) * flick;
    float wOut = 16.0f + 14.0f * amt, wCore = wOut * 0.5f;
    float fx = s.carForward[0], fy = s.carForward[1], fz = s.carForward[2];
    // Left vector for twin exhaust offsets (up x fwd), ~24 uu apart.
    float ux = s.carUp[0], uy = s.carUp[1], uz = s.carUp[2];
    float lx = uy * fz - uz * fy, ly = uz * fx - ux * fz, lz = ux * fy - uy * fx;
    float ll = std::sqrt(lx*lx + ly*ly + lz*lz);
    if (ll < 1e-6f) { lx = -fy; ly = fx; lz = 0; ll = std::sqrt(lx*lx+ly*ly); }
    lx /= ll; ly /= ll; lz /= ll;
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    for (int e = -1; e <= 1; e += 2) {
        float ex = s.carPos[0] - fx * 62.0f + lx * (e * 24.0f);
        float ey = s.carPos[1] - fy * 62.0f + ly * (e * 24.0f);
        float ez = s.carPos[2] - fz * 62.0f + lz * (e * 24.0f) - 4.0f;
        float tx = ex - fx * len, ty = ey - fy * len, tz = ez - fz * len;
        // Build a cone frame: pick side/up perpendicular to flame dir.
        float ax = -fx, ay = -fy, az = -fz;
        float sx = ay * uz - az * uy, sy = az * ux - ax * uz, sz2 = ax * uy - ay * ux;
        float sl = std::sqrt(sx*sx + sy*sy + sz2*sz2);
        if (sl < 1e-6f) { sx = lx; sy = ly; sz2 = lz; sl = 1; }
        sx /= sl; sy /= sl; sz2 /= sl;
        float vx = sy * az - sz2 * ay, vy = sz2 * ax - sx * az, vz = sx * ay - sy * ax;
        const int N = 10;
        // Outer cone (orange, translucent).
        glColor4f(1.0f, 0.45f, 0.08f, 0.75f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(tx, ty, tz);
        for (int i = 0; i <= N; i++) {
            float a = 2.0f * (float)M_PI * i / N;
            glVertex3f(ex + (std::cos(a) * sx + std::sin(a) * vx) * wOut,
                       ey + (std::cos(a) * sy + std::sin(a) * vy) * wOut,
                       ez + (std::cos(a) * sz2 + std::sin(a) * vz) * wOut);
        }
        glEnd();
        // Mid cone (yellow).
        glColor4f(1.0f, 0.80f, 0.25f, 0.85f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(ex - fx * len * 0.7f, ey - fy * len * 0.7f, ez - fz * len * 0.7f);
        for (int i = 0; i <= N; i++) {
            float a = 2.0f * (float)M_PI * i / N;
            glVertex3f(ex + (std::cos(a) * sx + std::sin(a) * vx) * wCore * 1.4f,
                       ey + (std::cos(a) * sy + std::sin(a) * vy) * wCore * 1.4f,
                       ez + (std::cos(a) * sz2 + std::sin(a) * vz) * wCore * 1.4f);
        }
        glEnd();
        // Hot core (near-white, short).
        glColor4f(1.0f, 0.97f, 0.85f, 0.95f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(ex - fx * len * 0.35f, ey - fy * len * 0.35f, ez - fz * len * 0.35f);
        for (int i = 0; i <= N; i++) {
            float a = 2.0f * (float)M_PI * i / N;
            glVertex3f(ex + (std::cos(a) * sx + std::sin(a) * vx) * wCore * 0.7f,
                       ey + (std::cos(a) * sy + std::sin(a) * vy) * wCore * 0.7f,
                       ez + (std::cos(a) * sz2 + std::sin(a) * vz) * wCore * 0.7f);
        }
        glEnd();
        // Exhaust glow sphere at the tip.
        r.DrawSphere(ex, ey, ez, wCore * 1.1f, 8, 1.0f, 0.85f, 0.5f, 0.8f);
    }
    // Smoke wisps trailing behind (grey, expanding, fading).
    for (int i = 0; i < 6; i++) {
        float ph = time_ * 7.0f + i * 1.7f;
        float back = 90.0f + 40.0f * i + 18.0f * std::sin(ph);
        float a = 0.22f * (1.0f - i / 6.0f);
        r.DrawSphere(s.carPos[0] - fx * back, s.carPos[1] - fy * back,
                     s.carPos[2] - fz * back + 8.0f + 6.0f * std::sin(ph * 1.3f),
                     8.0f + 4.0f * i, 6, 0.55f, 0.55f, 0.58f, a);
    }
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
    // Supersonic-slide sparks (orange embers mixed into the smoke).
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
    // Flip / stall read: fast rotation draws motion streak arcs around the
    // car (like RL's dodge blur); a stall — airborne, slow spin AFTER recent
    // rotation — draws a brief stabilization ring + puff so the cancel reads.
    // Stall latch is render-only (no sim coupling): high angSpeed arms it,
    // and near-zero spin while still airborne shows the ring briefly.
    static float stallT = 0, cool = 0;
    bool spinning = s.airborne && s.angSpeed > 6.0f;
    if (spinning) { stallT = 0.55f; cool = 0.0f; }
    if (cool > 0) cool -= 1.0f / 120.0f;
    float dt = 1.0f / 120.0f;
    if (stallT > 0) stallT -= dt;
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    if (spinning) {
        // 3 orthogonal streak arcs (forward/left/up planes), alpha by rate.
        float a = 0.25f + 0.45f * (s.angSpeed / 12.0f);
        if (a > 0.7f) a = 0.7f;
        const int N = 20;
        for (int plane = 0; plane < 3; plane++) {
            glColor4f(0.65f, 0.85f, 1.0f, a * (plane == 0 ? 1.0f : 0.55f));
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i <= N; i++) {
                float t = -0.9f + 1.8f * i / N + time_ * 2.0f;
                float c = std::cos(t) * 95.0f, q = std::sin(t) * 95.0f;
                float x = s.carPos[0], y = s.carPos[1], z = s.carPos[2] + 10.0f;
                if (plane == 0) { x += c * s.carForward[0]; y += c * s.carForward[1]; z += q; }
                else if (plane == 1) {
                    float lx = s.carUp[1]*s.carForward[2]-s.carUp[2]*s.carForward[1];
                    float ly = s.carUp[2]*s.carForward[0]-s.carUp[0]*s.carForward[2];
                    x += c * lx; y += c * ly; z += q * 0.6f;
                } else { x += c; y += q * 0.6f; }
                glVertex3f(x, y, z);
            }
            glEnd();
        }
    } else if (s.airborne && stallT > 0 && cool <= 0) {
        // Stall / flip-cancel freeze ring: thin cyan ring + soft puff.
        float k = stallT / 0.55f; // 1 -> 0
        r.DrawRing(s.carPos[0], s.carPos[1], s.carPos[2] + 10.0f,
                   90.0f + 60.0f * (1.0f - k), 96.0f + 60.0f * (1.0f - k),
                   32, 0.6f, 0.9f, 1.0f, 0.6f * k);
        r.DrawSphere(s.carPos[0], s.carPos[1], s.carPos[2] + 10.0f,
                     30.0f + 40.0f * (1.0f - k), 8,
                     0.7f, 0.9f, 1.0f, 0.25f * k);
        if (stallT <= 0) cool = 1.0f;
    }
    glDepthMask(GL_TRUE);
    if (r.LightingOn()) glEnable(GL_LIGHTING);
}

void FX::DrawTrail(Renderer& r, const Snapshot& s) {
    float fx = std::cos(s.carYaw), fy = std::sin(s.carYaw);
    if (s.carSpeed >= supersonic_) {
        // Fading ribbon quads behind car: alpha decays along trail.
        for (int i = 1; i < trailN_; i++) {
            float a = 0.35f * (1.0f - (float)i / kTrail);
            float w = 30.0f * (1.0f - (float)i / (kTrail * 1.5f));
            if (w < 4) w = 4;
            r.DrawBox(trail_[i][0], trail_[i][1], trail_[i][2] + 10,
                      w, w, 6.0f, s.carYaw, 0.6f, 0.8f, 1.0f, a);
        }
        // Supersonic wheel embers: deterministic (sim-time) sparks trailing
        // both rear wheels. No RNG — the rig must be capture-stable.
        float lx = -fy, ly = fx; // left
        for (int i = 0; i < 10; i++) {
            float ph = time_ * 9.0f + i * 2.399f;
            float back = 50.0f + 95.0f * std::fabs(std::sin(ph * 0.7f));
            float side = ((i & 1) ? 34.0f : -34.0f);
            float dx = s.carPos[0] - fx * back + lx * side;
            float dy = s.carPos[1] - fy * back + ly * side;
            float dz = s.carPos[2] - 12.0f + 16.0f * std::sin(ph);
            float a = 0.55f * (0.5f + 0.5f * std::sin(ph * 1.3f));
            r.DrawSphere(dx, dy, dz, 5.0f + 3.0f * std::fabs(std::sin(ph)), 6,
                         1.0f, 0.42f + 0.22f * std::sin(ph), 0.10f, a);
        }
    }
    if (explode_ > 0) {
        // Goal shockwave: an expanding ground blast ring plus two upright
        // rings around the ball, fading as it grows (radius/alpha exposed for
        // the VG8 gate).
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

} // namespace rl
