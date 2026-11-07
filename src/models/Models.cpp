// Models.cpp — procedural car + RL-style ball.
// Ball: truncated-icosahedron equirect generated in code (no assets):
// white hexagons, black pentagons. Shading comes from the scene lights.
// Car: hitbox-proportioned stylized body (replaced by the Fennec GLB mesh
// when assets/models/fennec.bin is present — see Fennec.cpp).
#include "Models.h"
#include "BallOrient.h"
#include <GL/gl.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>

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
} // namespace

WheelMotion UpdateWheelMotion(const Snapshot& s, int slot) {
    if (slot < 0) slot = 0;
    if (slot > 1) slot = 1;
    WheelVis& wv = g_wheel[slot];
    float dt = 0.0f;
    if (wv.init && s.tick > wv.lastTick)
        dt = float(s.tick - wv.lastTick) * (1.0f / 120.0f);
    if (dt > 0.25f) dt = 0.25f;
    wv.init = true;
    wv.lastTick = s.tick;
    float f[3] = {s.carForward[0], s.carForward[1], s.carForward[2]};
    float u[3] = {s.carUp[0], s.carUp[1], s.carUp[2]};
    float l[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2],
                  u[0] * f[1] - u[1] * f[0]};
    float fwdSp = s.carVel[0] * f[0] + s.carVel[1] * f[1] + s.carVel[2] * f[2];
    float latSp = s.carVel[0] * l[0] + s.carVel[1] * l[1] + s.carVel[2] * l[2];
    float target = -std::atan2(latSp, std::fabs(fwdSp) + 200.0f);
    if (target > 0.6f) target = 0.6f;
    if (target < -0.6f) target = -0.6f;
    float k = dt > 0 ? (dt * 14.0f > 1.0f ? 1.0f : dt * 14.0f) : 0.0f;
    wv.steer += (target - wv.steer) * k;
    const float R = 17.0f;
    wv.spin += (fwdSp / R) * dt;
    if (wv.spin > 1000.0f) wv.spin -= 1000.0f;
    if (wv.spin < -1000.0f) wv.spin += 1000.0f;
    return {wv.steer, wv.spin};
}

namespace {

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
    WheelMotion wv = UpdateWheelMotion(s, slot);
    const float R = 17.0f;

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

// ---- RL ball: truncated-icosahedron print + textured sphere ----------------

static const int kBtw = 512, kBth = 256; // equirect texture size

// Object-space direction. Same frame as DrawBall's parametric sphere:
// +Z is the v=1 pole, phi=0 lies on +X and increases toward +Y.
struct BallV {
    float x, y, z;
};

static BallV BAdd(BallV a, BallV b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static BallV BSub(BallV a, BallV b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static BallV BMul(BallV a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float BDot(BallV a, BallV b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static BallV BCross(BallV a, BallV b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static float BLen(BallV a) { return std::sqrt(BDot(a, a)); }

// Archimedean truncated icosahedron: 12 pentagons + 20 hexagons.
// Regular icosahedron vertices are the cyclic (0, ±1, ±φ) set,
// φ = (1+√5)/2. Truncation at 1/3 of each edge gives equal edges,
// because consecutive neighbors of a vertex are themselves an edge.
// Each texel is the face hit by its equirect direction. The print is
// fixed in this object frame; DrawBall's modelview spins it.
//
// Panel bytes are UNVERIFIED swatches of the work-order white/black.
// They sit a little inside 0 and 255 so the existing key+ambient still
// shade the panel and a separate specular lobe has headroom.
static void PaintBallPanels(unsigned char* img) {
    const unsigned char kHexR = 242, kHexG = 244, kHexB = 247;
    const unsigned char kPentR = 18, kPentG = 18, kPentB = 20;
    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
    BallV ico[12];
    int ni = 0;
    for (int sy = -1; sy <= 1; sy += 2)
        for (int sp = -1; sp <= 1; sp += 2)
            ico[ni++] = {0.0f, (float)sy, sp * phi};
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sp = -1; sp <= 1; sp += 2)
            ico[ni++] = {(float)sx, sp * phi, 0.0f};
    for (int sp = -1; sp <= 1; sp += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            ico[ni++] = {sp * phi, 0.0f, (float)sz};
    float minD = 1.0e30f;
    for (int i = 0; i < 12; i++) {
        for (int j = i + 1; j < 12; j++) {
            float d2 = BDot(BSub(ico[i], ico[j]), BSub(ico[i], ico[j]));
            if (d2 > 1.0e-6f && d2 < minD)
                minD = d2;
        }
    }
    int edges[30][2];
    int ne = 0;
    int edgeOf[12][12];
    for (int i = 0; i < 12; i++)
        for (int j = 0; j < 12; j++)
            edgeOf[i][j] = -1;
    for (int i = 0; i < 12; i++) {
        for (int j = i + 1; j < 12; j++) {
            float d2 = BDot(BSub(ico[i], ico[j]), BSub(ico[i], ico[j]));
            if (std::fabs(d2 - minD) > 0.05f)
                continue;
            if (ne >= 30)
                break;
            edges[ne][0] = i;
            edges[ne][1] = j;
            edgeOf[i][j] = edgeOf[j][i] = ne;
            ne++;
        }
    }
    int nbr[12][5];
    int deg[12] = {};
    for (int e = 0; e < ne; e++) {
        int a = edges[e][0], b = edges[e][1];
        if (deg[a] < 5)
            nbr[a][deg[a]++] = b;
        if (deg[b] < 5)
            nbr[b][deg[b]++] = a;
    }
    for (int v = 0; v < 12; v++) {
        BallV outward = ico[v];
        float ol = BLen(outward);
        BallV tmp = std::fabs(outward.z) < 0.9f * ol ? BallV{0, 0, 1} : BallV{0, 1, 0};
        BallV ref = BCross(tmp, outward);
        BallV bitan = BCross(outward, ref);
        float ang[5];
        int ord[5];
        int n = deg[v];
        for (int k = 0; k < n; k++) {
            ord[k] = nbr[v][k];
            BallV d = BSub(ico[ord[k]], outward);
            ang[k] = std::atan2(BDot(d, bitan), BDot(d, ref));
        }
        for (int i = 1; i < n; i++) {
            int id = ord[i];
            float a = ang[i];
            int j = i;
            while (j > 0 && ang[j - 1] > a) {
                ang[j] = ang[j - 1];
                ord[j] = ord[j - 1];
                j--;
            }
            ang[j] = a;
            ord[j] = id;
        }
        for (int k = 0; k < n; k++)
            nbr[v][k] = ord[k];
    }
    int tri[20][3];
    int ntri = 0;
    bool seen[12][12][12] = {};
    for (int v = 0; v < 12; v++) {
        int n = deg[v];
        for (int k = 0; k < n; k++) {
            int a = v, b = nbr[v][k], c = nbr[v][(k + 1) % n];
            int s0 = a, s1 = b, s2 = c;
            if (s0 > s1) { int t = s0; s0 = s1; s1 = t; }
            if (s1 > s2) { int t = s1; s1 = s2; s2 = t; }
            if (s0 > s1) { int t = s0; s0 = s1; s1 = t; }
            if (seen[s0][s1][s2] || ntri >= 20)
                continue;
            seen[s0][s1][s2] = true;
            tri[ntri][0] = a;
            tri[ntri][1] = b;
            tri[ntri][2] = c;
            ntri++;
        }
    }
    BallV vert[60];
    for (int e = 0; e < ne; e++) {
        BallV a = ico[edges[e][0]], b = ico[edges[e][1]];
        vert[e * 2] = BAdd(BMul(a, 2.0f / 3.0f), BMul(b, 1.0f / 3.0f));
        vert[e * 2 + 1] = BAdd(BMul(a, 1.0f / 3.0f), BMul(b, 2.0f / 3.0f));
    }
    auto nearEnd = [&](int edge, int endpoint) {
        return edges[edge][0] == endpoint ? edge * 2 : edge * 2 + 1;
    };
    struct Face {
        int n;
        bool pent;
        BallV v[6];
        BallV normal;
        float plane;
    };
    Face faces[32];
    int nf = 0;
    auto pushFace = [&](BallV* pts, int n, bool pent) {
        if (nf >= 32)
            return;
        Face& f = faces[nf];
        f.n = n;
        f.pent = pent;
        for (int i = 0; i < n; i++)
            f.v[i] = pts[i];
        BallV nrm = BCross(BSub(pts[1], pts[0]), BSub(pts[2], pts[0]));
        BallV cen = {0, 0, 0};
        for (int i = 0; i < n; i++)
            cen = BAdd(cen, pts[i]);
        cen = BMul(cen, 1.0f / (float)n);
        if (BDot(nrm, cen) < 0.0f)
            nrm = BMul(nrm, -1.0f);
        float nl = BLen(nrm);
        if (nl < 1.0e-8f)
            return;
        f.normal = BMul(nrm, 1.0f / nl);
        f.plane = BDot(f.normal, pts[0]);
        nf++;
    };
    for (int v = 0; v < 12; v++) {
        BallV pts[5];
        int n = deg[v];
        if (n != 5)
            continue;
        bool bad = false;
        for (int k = 0; k < 5; k++) {
            int e = edgeOf[v][nbr[v][k]];
            if (e < 0) {
                bad = true;
                break;
            }
            pts[k] = vert[nearEnd(e, v)];
        }
        if (bad) {
            ne = -1;
            break;
        }
        pushFace(pts, 5, true);
    }
    for (int t = 0; t < ntri; t++) {
        int seq[3] = {tri[t][0], tri[t][1], tri[t][2]};
        BallV pts[6];
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            int fr = seq[k], to = seq[(k + 1) % 3];
            int e = edgeOf[fr][to];
            if (e < 0) {
                ok = false;
                break;
            }
            pts[k * 2] = vert[nearEnd(e, fr)];
            pts[k * 2 + 1] = vert[nearEnd(e, to)];
        }
        if (ok)
            pushFace(pts, 6, false);
    }
    if (ne != 30 || ntri != 20 || nf != 32) {
        std::fprintf(stderr, "[ball] panel build failed edges=%d tris=%d faces=%d\n",
                     ne, ntri, nf);
        for (int i = 0; i < kBtw * kBth * 3; i += 3) {
            img[i] = kHexR;
            img[i + 1] = kHexG;
            img[i + 2] = kHexB;
        }
        return;
    }
    int misses = 0;
    for (int y = 0; y < kBth; y++) {
        float v = ((float)y + 0.5f) / (float)kBth;
        float theta = (float)M_PI * (1.0f - v);
        float st = std::sin(theta), ct = std::cos(theta);
        for (int x = 0; x < kBtw; x++) {
            float u = ((float)x + 0.5f) / (float)kBtw;
            float phiA = 2.0f * (float)M_PI * u;
            BallV d = {st * std::cos(phiA), st * std::sin(phiA), ct};
            int hit = -1;
            for (int fi = 0; fi < nf; fi++) {
                const Face& f = faces[fi];
                float nd = BDot(f.normal, d);
                if (nd <= 1.0e-8f)
                    continue;
                float t = f.plane / nd;
                if (t <= 0.0f)
                    continue;
                BallV p = BMul(d, t);
                bool inside = true;
                for (int i = 0; i < f.n; i++) {
                    BallV e = BSub(f.v[(i + 1) % f.n], f.v[i]);
                    BallV c = BCross(e, BSub(p, f.v[i]));
                    if (BDot(c, f.normal) < -1.0e-4f) {
                        inside = false;
                        break;
                    }
                }
                if (inside) {
                    hit = fi;
                    break;
                }
            }
            if (hit < 0) {
                misses++;
                float best = -1.0e30f;
                for (int fi = 0; fi < nf; fi++) {
                    float nd = BDot(faces[fi].normal, d);
                    if (nd > best) {
                        best = nd;
                        hit = fi;
                    }
                }
            }
            unsigned char* p = img + (y * kBtw + x) * 3;
            if (hit >= 0 && faces[hit].pent) {
                p[0] = kPentR;
                p[1] = kPentG;
                p[2] = kPentB;
            } else {
                p[0] = kHexR;
                p[1] = kHexG;
                p[2] = kHexB;
            }
        }
    }
    if (misses)
        std::fprintf(stderr, "[ball] panel rays missed %d texels\n", misses);
}

// G4 texture chains (GRAPHICS45 §5/G4): full mip chain generated ONCE at
// upload, trilinear minification, aniso when the driver offers it. Same
// treatment as ArenaTextures.cpp; kept file-local so no header decl changes.
// glGenerateMipmap is GL 3.0+ core, our context is 4.5-compat — no loader.
extern "C" void glGenerateMipmap(unsigned int target);
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

static void FinishBallUpload() {
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const char* exts = (const char*)glGetString(GL_EXTENSIONS);
    if (exts && std::strstr(exts, "GL_EXT_texture_filter_anisotropic")) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        const GLfloat kAnisoCap = 4.0f;  // G4: R4-class cap
        GLfloat chosen = maxAniso < kAnisoCap ? maxAniso : kAnisoCap;
        if (chosen < 1.0f) chosen = 1.0f;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, chosen);
    }
}

static unsigned BuildBallTexture() {
    static unsigned tex = 0;
    if (tex) return tex;
    static unsigned char img[kBtw * kBth * 3];
    PaintBallPanels(img);
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
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    FinishBallUpload();  // G4: wrap preserved, filters upgraded to chained
    return tex;
}

void DrawBall(Renderer& r, const Snapshot& s) {
    // Truncated icosahedron print. Visual radius 92.75 (collision 91.25).
    // Textured sphere, ~1.1k tris. Panels stay in object space.
    unsigned tex = BuildBallTexture();
    const int segs = 16;
    const float R = 92.75f;
    static bool s_noTex = std::getenv("RL_NOTEX") != nullptr; // debug
    glPushMatrix();
    glTranslatef(s.ballPos[0], s.ballPos[1], s.ballPos[2]);
    // Physics spin, integrated in sim time. The panel UVs stay on the mesh
    // so a later pattern change keeps this orientation.
    if (!BallRotIsIdentity(s.ballRot)) {
        float orient[16];
        BallOrientationMatrix(s.ballRot, orient);
        glMultMatrixf(orient);
    }
    // Gloss under the existing key and fill. Local to this draw: the arena
    // keeps the zero specular from Renderer::Init. Shininess is UNVERIFIED
    // (no published fixed-function exponent). Separate specular is added
    // after the texture multiply, so the lobe still reads on a black pentagon.
    // Key/fill specular colours match those lights' diffuse in Renderer::Init.
    GLfloat prevSpec[4] = {0, 0, 0, 1};
    GLfloat prevShine = 0.0f;
    GLint prevCtl = GL_SINGLE_COLOR;
    GLfloat prevKey[4] = {1, 1, 1, 1};
    GLfloat prevFill[4] = {0, 0, 0, 1};
    glGetMaterialfv(GL_FRONT, GL_SPECULAR, prevSpec);
    glGetMaterialfv(GL_FRONT, GL_SHININESS, &prevShine);
    glGetIntegerv(GL_LIGHT_MODEL_COLOR_CONTROL, &prevCtl);
    glGetLightfv(GL_LIGHT0, GL_SPECULAR, prevKey);
    glGetLightfv(GL_LIGHT1, GL_SPECULAR, prevFill);
    const GLfloat ballSpec[] = {0.55f, 0.58f, 0.65f, 1.0f};
    const GLfloat keySpec[] = {0.72f, 0.74f, 0.80f, 1.0f};
    const GLfloat fillSpec[] = {0.30f, 0.32f, 0.36f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, ballSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);
    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
    glLightfv(GL_LIGHT0, GL_SPECULAR, keySpec);
    glLightfv(GL_LIGHT1, GL_SPECULAR, fillSpec);
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
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, prevSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, prevShine);
    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, prevCtl);
    glLightfv(GL_LIGHT0, GL_SPECULAR, prevKey);
    glLightfv(GL_LIGHT1, GL_SPECULAR, prevFill);
    (void)s_noTex;
    glPopMatrix();
}

} // namespace rl
