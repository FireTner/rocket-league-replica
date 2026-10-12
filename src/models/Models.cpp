// Models.cpp — procedural car + RL-style ball.
// Ball: hexagon-panel equirect texture generated in code (no assets), the
// RL ball look: white panels, gray seams, subtle top/bottom shading.
// Car: hitbox-proportioned stylized body (replaced by the Fennec GLB mesh
// when assets/models/fennec.bin is present — see Fennec.cpp).
#include "Models.h"
#include <GL/gl.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

namespace rl {

namespace {
// Modelview frame from world-space forward/up (unit): maps local
// +x = forward, +y = left, +z = up onto the car. Recovers pitch/roll that a
// bare yaw cannot express (car-orientation legibility, W3).
void CarFrame(const float pos[3], const float fwd[3], const float up[3]) {
    float f[3] = {fwd[0], fwd[1], fwd[2]};
    float u[3] = {up[0], up[1], up[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-6f) { f[0] = 1; f[1] = 0; f[2] = 0; fl = 1; }
    f[0] /= fl; f[1] /= fl; f[2] /= fl;
    float d = u[0] * f[0] + u[1] * f[1] + u[2] * f[2];
    u[0] -= d * f[0]; u[1] -= d * f[1]; u[2] -= d * f[2];
    float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ul < 1e-6f) { u[0] = 0; u[1] = 0; u[2] = 1; ul = 1; }
    u[0] /= ul; u[1] /= ul; u[2] /= ul;
    // left = up x forward (local +y), right = forward x up.
    float l[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2],
                  u[0] * f[1] - u[1] * f[0]};
    float m[16] = {
        f[0], f[1], f[2], 0,
        l[0], l[1], l[2], 0,
        u[0], u[1], u[2], 0,
        0, 0, 0, 1};
    glTranslatef(pos[0], pos[1], pos[2]);
    glMultMatrixf(m);
}

// Per-car wheel/steer visual state (render-only; keyed on team slot). Spin is
// integrated from the Snapshot's forward speed and tick delta, so it is
// deterministic and needs no sim coupling.
struct WheelVis { float spin = 0, steer = 0; uint64_t lastTick = 0; bool init = false; };
WheelVis g_wheel[2];

// One procedural wheel: tire tread + rim + a bright spoke bar that rotates
// with spin, so the spin is actually legible (a flat box would not be).
void DrawWheelLocal(float x, float y, float z, float steerRad, float spinRad,
                    float R, float width) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(steerRad * 180.0f / (float)M_PI, 0, 0, 1);
    glRotatef(spinRad * 180.0f / (float)M_PI, 0, 1, 0);
    const int N = 12;
    glDisable(GL_LIGHTING);
    glColor3f(0.045f, 0.045f, 0.055f);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= N; i++) {
        float a = 2.0f * (float)M_PI * i / N;
        float c = std::cos(a) * R, s2 = std::sin(a) * R;
        glVertex3f(c, -width * 0.5f, s2);
        glVertex3f(c, width * 0.5f, s2);
    }
    glEnd();
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float yy = sgn * (width * 0.5f + 0.6f);
        glColor3f(0.16f, 0.16f, 0.18f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, yy, 0);
        for (int i = 0; i <= N; i++) {
            float a = 2.0f * (float)M_PI * i / N;
            glVertex3f(std::cos(a) * R * 0.62f, yy, std::sin(a) * R * 0.62f);
        }
        glEnd();
        glColor3f(0.80f, 0.83f, 0.88f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0, yy, 0);
        for (int i = 0; i <= 8; i++) {
            float a = 2.0f * (float)M_PI * i / 8;
            glVertex3f(std::cos(a) * R * 0.28f, yy, std::sin(a) * R * 0.28f);
        }
        glEnd();
        glColor3f(0.93f, 0.94f, 0.97f);
        glBegin(GL_QUADS);
        glVertex3f(R * 0.30f, yy, -3.2f); glVertex3f(R * 0.92f, yy, -3.2f);
        glVertex3f(R * 0.92f, yy, 3.2f);  glVertex3f(R * 0.30f, yy, 3.2f);
        glEnd();
    }
    glEnable(GL_LIGHTING);
    glPopMatrix();
}
} // namespace

void DrawCar(Renderer& r, const Snapshot& s, bool blueTeam) {
    // Team-tinted body (reads at a glance like RL's blue/orange).
    float br = blueTeam ? 0.10f : 0.85f;
    float bg = blueTeam ? 0.35f : 0.30f;
    float bb = blueTeam ? 0.90f : 0.08f;
    float tr = blueTeam ? 0.55f : 1.0f;
    float tg = blueTeam ? 0.75f : 0.75f;
    float tb = blueTeam ? 1.0f : 0.45f;

    int slot = blueTeam ? 0 : 1;
    WheelVis& wv = g_wheel[slot];
    float dt = 0.0f;
    if (wv.init && s.tick > wv.lastTick)
        dt = float(s.tick - wv.lastTick) * (1.0f / 120.0f);
    if (dt > 0.25f) dt = 0.25f;
    wv.init = true;
    wv.lastTick = s.tick;

    // Local axes.
    float f[3] = {s.carForward[0], s.carForward[1], s.carForward[2]};
    float u[3] = {s.carUp[0], s.carUp[1], s.carUp[2]};
    float l[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2],
                  u[0] * f[1] - u[1] * f[0]};
    float fwdSp = s.carVel[0] * f[0] + s.carVel[1] * f[1] + s.carVel[2] * f[2];
    float latSp = s.carVel[0] * l[0] + s.carVel[1] * l[1] + s.carVel[2] * l[2];
    // Steer from the velocity slip angle (nose vs travel). Front wheels only;
    // smoothed so a single tick of noise cannot snap them.
    float target = -std::atan2(latSp, std::fabs(fwdSp) + 200.0f);
    if (target > 0.6f) target = 0.6f;
    if (target < -0.6f) target = -0.6f;
    float k = dt > 0 ? (dt * 14.0f > 1.0f ? 1.0f : dt * 14.0f) : 0.0f;
    wv.steer += (target - wv.steer) * k;

    const float R = 17.0f; // wheel radius: rest height 17 == wheel radius
    wv.spin += (fwdSp / R) * dt;
    if (wv.spin > 1000.0f) wv.spin -= 1000.0f;
    if (wv.spin < -1000.0f) wv.spin += 1000.0f;

    glPushMatrix();
    CarFrame(s.carPos, s.carForward, s.carUp);
    // Local space: +x forward, +y left, +z up (origin = RocketSim car origin).
    // Lower body (hitbox footprint, 60% height).
    r.DrawBox(0, 0, -dims::kCarH * 0.18f, dims::kCarL, dims::kCarW,
              dims::kCarH * 0.62f, 0, br, bg, bb);
    // Bright team trim stripe.
    r.DrawBox(0, 0, -dims::kCarH * 0.18f, dims::kCarL * 1.005f,
              dims::kCarW * 0.35f, dims::kCarH * 0.2f, 0, tr, tg, tb);
    // Cabin (upper box, offset rear, dark glass).
    r.DrawBox(-12.0f, 0, dims::kCarH * 0.28f, dims::kCarL * 0.45f,
              dims::kCarW * 0.62f, dims::kCarH * 0.5f, 0, 0.05f, 0.07f, 0.10f);
    // Wheels: front pair steers, all four spin.
    float hl = dims::kCarL * 0.32f, hw = dims::kCarW * 0.52f;
    DrawWheelLocal(hl, hw, 0, wv.steer, wv.spin, R, 9.0f);
    DrawWheelLocal(hl, -hw, 0, wv.steer, wv.spin, R, 9.0f);
    DrawWheelLocal(-hl, hw, 0, 0.0f, wv.spin, R, 9.0f);
    DrawWheelLocal(-hl, -hw, 0, 0.0f, wv.spin, R, 9.0f);
    glPopMatrix();
}

void DrawDebugColliders(Renderer& r, const Snapshot& player, const Snapshot& bot) {
    // Oriented car hitboxes (the Bullet box each car actually collides as).
    auto carWire = [&](const Snapshot& s, float cr, float cg, float cb) {
        glPushMatrix();
        CarFrame(s.carPos, s.carForward, s.carUp);
        r.DrawWireBox(0, 0, 0, dims::kCarL, dims::kCarW, dims::kCarH, cr, cg, cb, 0.95f);
        glPopMatrix();
    };
    carWire(player, 0.35f, 0.95f, 1.0f);
    carWire(bot, 1.0f, 0.72f, 0.25f);
    // Ball collision sphere (r = 91.25, NOT the visual 92.75).
    r.DrawWireSphere(player.ballPos[0], player.ballPos[1], player.ballPos[2],
                     dims::kBallR, 20, 1.0f, 0.35f, 0.9f, 0.95f);
}

// ---- RL ball: procedural hexagon-panel texture + textured sphere ----------

static const int kBtw = 512, kBth = 256; // equirect texture size

// Hexagon seam edges at (cx,cy) with circumradius R, wrapped horizontally.
static void HexSeam(unsigned char* img, float cx, float cy, float R,
                    int seamR, int seamG, int seamB, float thick) {
    for (int e = 0; e < 6; e++) {
        float a0 = (float)M_PI / 3.0f * e, a1 = (float)M_PI / 3.0f * (e + 1);
        float x0 = cx + std::cos(a0) * R, y0 = cy + std::sin(a0) * R;
        float x1 = cx + std::cos(a1) * R, y1 = cy + std::sin(a1) * R;
        int steps = (int)(R / 2.0f) + 2;
        for (int s = 0; s <= steps; s++) {
            float t = (float)s / steps;
            float px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
            int x = ((int)px % kBtw + kBtw) % kBtw;
            for (int dy = -(int)thick; dy <= (int)thick; dy++) {
                int y = ((int)py + dy) % kBth;
                if (y < 0) y += kBth;
                unsigned char* p = img + (y * kBtw + x) * 3;
                p[0] = seamR; p[1] = seamG; p[2] = seamB;
            }
        }
    }
}

static unsigned BuildBallTexture() {
    static unsigned tex = 0;
    if (tex) return tex;
    static unsigned char img[kBtw * kBth * 3];
    // Base: vertical gradient white (#f4f7fb -> #c3ccda), brightest mid-lats.
    // Clamp every channel: values may exceed 1.0 and would wrap in the cast.
    for (int y = 0; y < kBth; y++) {
        float v = (float)y / kBth;
        float k = 0.10f * (1 - std::abs(2 * v - 1)); // 0 at poles, 0.1 mid
        for (int x = 0; x < kBtw; x++) {
            unsigned char* p = img + (y * kBtw + x) * 3;
            p[0] = (unsigned char)(std::fmin(1.0f, 0.86f + k) * 255.0f);
            p[1] = (unsigned char)(std::fmin(1.0f, 0.89f + k) * 255.0f);
            p[2] = (unsigned char)(std::fmin(1.0f, 0.93f + k) * 255.0f);
        }
    }
    // Hex panel seams: staggered hex rows (RL ball ~6 panels around).
    float R = 52.0f;
    for (int row = 0; row < 4; row++) {
        float cy = (row + 0.5f) * kBth / 4.0f;
        for (int col = 0; col < 8; col++) {
            float cx = (col + (row & 1 ? 0.5f : 0.0f)) * kBtw / 8.0f;
            HexSeam(img, cx, cy, R, 138, 148, 165, 2.0f);
        }
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kBtw, kBth, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, img);
    if (std::getenv("RL_TEXDBG")) { // dump what GL actually holds
        static unsigned char back[kBtw * kBth * 3];
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, back);
        FILE* df = fopen("/tmp/balltex_dump.ppm", "wb");
        if (df) {
            fprintf(df, "P6\n%d %d\n255\n", kBtw, kBth);
            fwrite(back, 1, sizeof(back), df);
            fclose(df);
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    return tex;
}

void DrawBall(Renderer& r, const Snapshot& s) {
    // RL-style ball: white hexagon panels + gray seams. Visual radius 92.75
    // (collision 91.25). Textured sphere, ~1.1k tris.
    unsigned tex = BuildBallTexture();
    const int segs = 16;
    const float R = 92.75f;
    static bool s_noTex = std::getenv("RL_NOTEX") != nullptr; // debug
    glPushMatrix();
    glTranslatef(s.ballPos[0], s.ballPos[1], s.ballPos[2]);
    if (!s_noTex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
    }
    glColor3f(1, 1, 1);
    for (int i = 0; i < segs; i++) {
        float t0 = (float)i / segs * (float)M_PI;
        float t1 = (float)(i + 1) / segs * (float)M_PI;
        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= segs * 2; j++) {
            float p = (float)j / (segs * 2) * 2.0f * (float)M_PI;
            float x0 = std::sin(t0) * std::cos(p), y0 = std::sin(t0) * std::sin(p), z0 = std::cos(t0);
            float x1 = std::sin(t1) * std::cos(p), y1 = std::sin(t1) * std::sin(p), z1 = std::cos(t1);
            glTexCoord2f((float)j / (segs * 2), 1.0f - (float)i / segs);
            glNormal3f(x0, y0, z0); glVertex3f(x0 * R, y0 * R, z0 * R);
            glTexCoord2f((float)j / (segs * 2), 1.0f - (float)(i + 1) / segs);
            glNormal3f(x1, y1, z1); glVertex3f(x1 * R, y1 * R, z1 * R);
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    (void)s_noTex;
    glPopMatrix();
}

} // namespace rl
