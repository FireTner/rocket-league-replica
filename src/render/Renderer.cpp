// Renderer.cpp — SDL2 + OpenGL 2.1 fixed-function minimal renderer.
#include "Renderer.h"
#include "Font.h"
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace rl {

Renderer::Renderer() = default;
Renderer::~Renderer() { Shutdown(); }

double Renderer::NowMs() const { return (double)SDL_GetTicks(); }

bool Renderer::Init(int w, int h, bool vsync) {
    w_ = w; h_ = h;
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
            std::fprintf(stderr, "[render] SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0); // compat / fixed fn
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    window_ = SDL_CreateWindow("RL Free-Play Clone (A4 render stub)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w_, h_,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window_) {
        std::fprintf(stderr, "[render] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    ctx_ = SDL_GL_CreateContext(window_);
    if (!ctx_) {
        std::fprintf(stderr, "[render] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_); window_ = nullptr;
        return false;
    }
    int ok = SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    std::fprintf(stderr, "[render] GL2.1 ctx ok, vsync=%s (swapInterval rc=%d)\n",
        vsync ? "ON" : "OFF", ok);
    std::fprintf(stderr, "[render] GL_VENDOR=%s RENDERER=%s VERSION=%s\n",
        (const char*)glGetString(GL_VENDOR),
        (const char*)glGetString(GL_RENDERER),
        (const char*)glGetString(GL_VERSION));

    glViewport(0, 0, w_, h_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glShadeModel(GL_SMOOTH);
    // ---- Lighting: key + fill + hemisphere-ish ambient ----------------------
    // The previous model was ONE directional light with a large global ambient.
    // Any surface whose normal had Ndot(dir) <= 0 was ambient-only, so the
    // ceiling under-face (normal -Z) and the +x wall's inner face (normal -X)
    // both rendered near-black (docs/VISUAL_BUGS_TODO.md V5).
    //
    // Now: a key light from above/front, a dim fill from below/behind so every
    // downward- and backward-facing surface still receives some diffuse, and a
    // small global ambient. GL_NORMALIZE renormalises the (now unit-length)
    // authored normals under the modelview scale (VISUAL_BUGS_TODO.md V1's
    // defence-in-depth requirement); the procedural box car's normals are
    // already unit but rescaling is idempotent.
    //
    // Values are TUNED against the captured frames; WCAG-style relative
    // luminance targets are in tools/verify/visual_gates.py (VG15).
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0); // key
    glEnable(GL_LIGHT1); // fill
    {
        GLfloat keyDir[] = {0.25f, -0.40f, 0.88f, 0.0f}; // direction TO key
        GLfloat keyDif[] = {0.72f, 0.74f, 0.80f, 1.0f};
        GLfloat keyAmb[] = {0.00f, 0.00f, 0.00f, 1.0f};
        GLfloat fillDir[] = {-0.30f, 0.55f, -0.55f, 0.0f}; // from below/behind
        GLfloat fillDif[] = {0.30f, 0.32f, 0.36f, 1.0f};
        GLfloat fillAmb[] = {0.00f, 0.00f, 0.00f, 1.0f};
        GLfloat amb[] = {0.28f, 0.34f, 0.30f, 1.0f}; // global (cool)
        glLightfv(GL_LIGHT0, GL_POSITION, keyDir);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, keyDif);
        glLightfv(GL_LIGHT0, GL_AMBIENT, keyAmb);
        glLightfv(GL_LIGHT1, GL_POSITION, fillDir);
        glLightfv(GL_LIGHT1, GL_DIFFUSE, fillDif);
        glLightfv(GL_LIGHT1, GL_AMBIENT, fillAmb);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
        // No specular: with GL_COLOR_MATERIAL the material colour drives
        // ambient+diffuse only, so a fixed specular term is added on top of
        // every surface and washes saturated tints toward white (it turned the
        // blue glass panels grey). Keep it off and tune albedo instead.
        GLfloat spec[] = {0.0f, 0.0f, 0.0f, 1.0f};
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    }
    // GL_NORMALIZE (not GL_RESCALE_NORMAL): DrawBox applies a NON-UNIFORM scale
    // (glScalef(sx,sy,sz)) to a unit cube, so the box-face normals come out
    // badly skewed. GL_RESCALE_NORMAL only divides by a single factor and left
    // the glass panels' side face with a normal of (-1/10240,0,0), which every
    // light then rejected -- the walls rendered flat grey. GL_NORMALIZE
    // renormalises each normal, which is exact for diffuse-only lighting.
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    lastLogMs_ = SDL_GetTicks();
    return true;
}

void Renderer::Shutdown() {
    if (ctx_) { SDL_GL_DeleteContext(ctx_); ctx_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
}

void Renderer::BeginFrame(float r, float g, float b) {
    frameStart_ = NowMs();
    frameTris_ = frameLines_ = frameDraws_ = 0;
    glViewport(0, 0, w_, h_);
    // Reset the whole draw state every frame. The 2D overlay helpers
    // (OrthoScope / FillRect2D) toggle blending + depth, so leaving the state
    // to whatever the previous frame's last helper did made every translucent
    // surface (glass walls, ball indicator, pads, trails) render OPAQUE from
    // frame 2 onward — a real bug the VG4/VG6 gates caught. State is now a
    // pure function of this frame's draws.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_TEXTURE_2D);
    if (!lightOn_) glDisable(GL_LIGHTING); else glEnable(GL_LIGHTING);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::EndFrame() {
    // Commit this frame's measured geometry cost + drain any GL errors.
    // glGetError() is polled ONCE here (never inside a draw), so the budget
    // instrumentation cannot stall the frame loop.
    counters_.tris = frameTris_;
    counters_.lines = frameLines_;
    counters_.drawCalls = frameDraws_;
    counters_.trisTotal += frameTris_;
    counters_.drawsTotal += frameDraws_;
    counters_.frames++;
    for (;;) {
        GLenum e = glGetError();
        if (e == GL_NO_ERROR) break;
        counters_.glErrors++;
    }
    SDL_GL_SwapWindow(window_); // vsync-paced present; never waits on physics
    double dt = NowMs() - frameStart_;
    hist_[histI_] = dt; histI_ = (histI_ + 1) % kHist;
    if (histN_ < kHist) histN_++;
    frames_++;
    LogPacingIfDue();
}

Renderer::Pacing Renderer::Stats() const {
    Pacing p; p.frames = frames_;
    if (histN_ == 0) return p;
    static double tmp[600];
    for (int i = 0; i < histN_; i++) tmp[i] = hist_[i];
    std::sort(tmp, tmp + histN_);
    p.p50 = tmp[histN_ / 2];
    p.p99 = tmp[(histN_ * 99) / 100 >= histN_ ? histN_-1 : (histN_*99)/100];
    double s = 0; for (int i = 0; i < histN_; i++) s += tmp[i];
    p.avg = s / histN_;
    return p;
}

void Renderer::LogPacingIfDue() {
    uint64_t now = SDL_GetTicks();
    if (now - lastLogMs_ < 5000) return;
    lastLogMs_ = now;
    cached_ = Stats();
    char line[256];
    std::snprintf(line, sizeof(line),
        "[pacing] frames=%llu avg=%.2fms p50=%.2fms p99=%.2fms (vsync 720p60 target 16.67ms)\n",
        (unsigned long long)cached_.frames, cached_.avg, cached_.p50, cached_.p99);
    std::fputs(line, stderr);
    FILE* f = std::fopen("render_pacing.log", "a");
    if (f) { std::fputs(line, f); std::fclose(f); }
}

void Renderer::DrawBox(float cx, float cy, float cz,
                       float sx, float sy, float sz, float yaw,
                       float r, float g, float b, float a) {
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glRotatef(yaw * 180.0f / (float)M_PI, 0, 0, 1); // z-up yaw
    glScalef(sx, sy, sz);
    glColor4f(r, g, b, a);
    const float h = 0.5f;
    glBegin(GL_QUADS);
    // +z / -z
    glNormal3f(0,0,1);
    glVertex3f(-h,-h, h); glVertex3f( h,-h, h); glVertex3f( h, h, h); glVertex3f(-h, h, h);
    glNormal3f(0,0,-1);
    glVertex3f(-h,-h,-h); glVertex3f(-h, h,-h); glVertex3f( h, h,-h); glVertex3f( h,-h,-h);
    // +x / -x
    glNormal3f(1,0,0);
    glVertex3f( h,-h,-h); glVertex3f( h, h,-h); glVertex3f( h, h, h); glVertex3f( h,-h, h);
    glNormal3f(-1,0,0);
    glVertex3f(-h,-h,-h); glVertex3f(-h,-h, h); glVertex3f(-h, h, h); glVertex3f(-h, h,-h);
    // +y / -y
    glNormal3f(0,1,0);
    glVertex3f(-h, h,-h); glVertex3f(-h, h, h); glVertex3f( h, h, h); glVertex3f( h, h,-h);
    glNormal3f(0,-1,0);
    glVertex3f(-h,-h,-h); glVertex3f( h,-h,-h); glVertex3f( h,-h, h); glVertex3f(-h,-h, h);
    glEnd();
    glPopMatrix();
    CountTris(12); // 6 quads
}

void Renderer::DrawSphere(float cx, float cy, float cz, float radius,
                          int segs, float r, float g, float b, float a) {
    if (segs < 6) segs = 6; if (segs > 24) segs = 24; // tri budget guard
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glColor4f(r, g, b, a);
    for (int i = 0; i < segs; i++) {
        float t0 = (float)i / segs * (float)M_PI;
        float t1 = (float)(i + 1) / segs * (float)M_PI;
        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= segs; j++) {
            float p = (float)j / segs * 2.0f * (float)M_PI;
            float x0 = std::sin(t0)*std::cos(p), y0 = std::sin(t0)*std::sin(p), z0 = std::cos(t0);
            float x1 = std::sin(t1)*std::cos(p), y1 = std::sin(t1)*std::sin(p), z1 = std::cos(t1);
            glNormal3f(x0, y0, z0); glVertex3f(x0*radius, y0*radius, z0*radius);
            glNormal3f(x1, y1, z1); glVertex3f(x1*radius, y1*radius, z1*radius);
        }
        glEnd();
    }
    glPopMatrix();
    CountTris(uint64_t(2 * segs * segs)); // segs strips x 2*segs tris
}

void Renderer::DrawGroundGrid(float extent, float step) {
    glDisable(GL_LIGHTING);
    glColor4f(0.35f, 0.45f, 0.55f, 1.0f);
    uint64_t segs = 0;
    glBegin(GL_LINES);
    for (float v = -extent; v <= extent + 0.01f; v += step) {
        glVertex3f(v, -extent, 0); glVertex3f(v, extent, 0);
        glVertex3f(-extent, v, 0); glVertex3f(extent, v, 0);
        segs += 2;
    }
    glEnd();
    CountTris(0, segs);
    if (lightOn_) glEnable(GL_LIGHTING);
}

void Renderer::DrawWireBox(float cx, float cy, float cz, float sx, float sy,
                           float sz, float r, float g, float b, float a) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    float vx[8] = {-hx, hx, hx, -hx, -hx, hx, hx, -hx};
    float vy[8] = {-hy, -hy, hy, hy, -hy, -hy, hy, hy};
    float vz[8] = {-hz, -hz, -hz, -hz, hz, hz, hz, hz};
    static const int e[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                                 {0,4},{1,5},{2,6},{3,7}};
    glDisable(GL_LIGHTING);
    glColor4f(r, g, b, a);
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glBegin(GL_LINES);
    for (int i = 0; i < 12; i++) {
        glVertex3f(vx[e[i][0]], vy[e[i][0]], vz[e[i][0]]);
        glVertex3f(vx[e[i][1]], vy[e[i][1]], vz[e[i][1]]);
    }
    glEnd();
    glPopMatrix();
    CountTris(0, 12);
    if (lightOn_) glEnable(GL_LIGHTING);
}

void Renderer::DrawWireSphere(float cx, float cy, float cz, float radius, int segs,
                              float r, float g, float b, float a) {
    if (segs < 8) segs = 8;
    if (segs > 32) segs = 32;
    glDisable(GL_LIGHTING);
    glColor4f(r, g, b, a);
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    for (int axis = 0; axis < 3; axis++) {
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < segs; i++) {
            float t = 2.0f * (float)M_PI * i / segs;
            float c = std::cos(t) * radius, s = std::sin(t) * radius;
            if (axis == 0) glVertex3f(0, c, s);
            else if (axis == 1) glVertex3f(c, 0, s);
            else glVertex3f(c, s, 0);
        }
        glEnd();
    }
    glPopMatrix();
    CountTris(0, uint64_t(3 * segs));
    if (lightOn_) glEnable(GL_LIGHTING);
}

void Renderer::DrawWireCylinder(float cx, float cy, float cz, float radius,
                                float height, int segs, float r, float g,
                                float b, float a) {
    if (segs < 6) segs = 6;
    if (segs > 32) segs = 32;
    glDisable(GL_LIGHTING);
    glColor4f(r, g, b, a);
    glPushMatrix();
    glTranslatef(cx, cy, cz + height * 0.5f);
    for (int k = 0; k < 2; k++) {
        float z = k ? height * 0.5f : -height * 0.5f;
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < segs; i++) {
            float t = 2.0f * (float)M_PI * i / segs;
            glVertex3f(std::cos(t) * radius, std::sin(t) * radius, z);
        }
        glEnd();
    }
    glBegin(GL_LINES);
    int step = segs / 4 > 0 ? segs / 4 : 1;
    for (int i = 0; i < segs; i += step) {
        float t = 2.0f * (float)M_PI * i / segs;
        float c = std::cos(t) * radius, s = std::sin(t) * radius;
        glVertex3f(c, s, -height * 0.5f);
        glVertex3f(c, s, height * 0.5f);
    }
    glEnd();
    glPopMatrix();
    CountTris(0, uint64_t(2 * segs + segs / 4 + 1));
    if (lightOn_) glEnable(GL_LIGHTING);
}

void Renderer::DrawDisc(float cx, float cy, float cz, float radius, int segs,
                        float r, float g, float b, float a) {
    if (segs < 8) segs = 8; if (segs > 64) segs = 64;
    glDisable(GL_LIGHTING);
    glColor4f(r, g, b, a);
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= segs; i++) {
        float p = (float)i / segs * 2.0f * (float)M_PI;
        glVertex3f(std::cos(p) * radius, std::sin(p) * radius, 0);
    }
    glEnd();
    glPopMatrix();
    CountTris(uint64_t(segs)); // triangle fan
    if (lightOn_) glEnable(GL_LIGHTING);
}

void Renderer::DrawRing(float cx, float cy, float cz, float rIn, float rOut,
                        int segs, float r, float g, float b, float a) {
    if (segs < 8) segs = 8; if (segs > 64) segs = 64;
    glDisable(GL_LIGHTING);
    glColor4f(r, g, b, a);
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= segs; i++) {
        float p = (float)i / segs * 2.0f * (float)M_PI;
        float c = std::cos(p), s = std::sin(p);
        glVertex3f(c * rIn, s * rIn, 0);
        glVertex3f(c * rOut, s * rOut, 0);
    }
    glEnd();
    glPopMatrix();
    CountTris(uint64_t(2 * segs)); // strip: 2 tris per segment
    if (lightOn_) glEnable(GL_LIGHTING);
}

void Renderer::DrawHexNet(float cx, float cy, float cz, float halfW, float halfH,
                          float yaw, float cell, float r, float g, float b, float a) {
    // Grid of complete flat-top hexagon outlines spanning the panel rect,
    // drawn as line segments in the wall's local frame (x = along wall,
    // z = up, y = wall normal). cell = hexagon height (point-to-point).
    if (cell < 40) cell = 40;
    float R = cell * 0.5f;          // circumradius
    float hw = 0.8660254f * R;      // horizontal half-extent of a hexagon
    glDisable(GL_LIGHTING);
    glColor4f(r, g, b, a);
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glRotatef(yaw * 180.0f / (float)M_PI, 0, 0, 1);
    glBegin(GL_LINES);
    // Flat-top hexagon centers spaced 2*hw horizontally, 0.75*cell
    // vertically with every other row offset by hw (honeycomb tiling).
    float dy = 0.75f * cell;
    uint64_t nLines = 0;
    int rows = (int)((halfH * 2.0f) / dy) + 2;
    int cols = (int)((halfW * 2.0f) / (2.0f * hw)) + 2;
    for (int ri = 0; ri < rows; ri++) {
        float cyh = -halfH + ri * dy;
        for (int ci = 0; ci < cols; ci++) {
            float cxh = -halfW + ci * 2.0f * hw + ((ri & 1) ? hw : 0.0f);
            // All six edges of this hexagon, clipped to the panel.
            for (int e = 0; e < 6; e++) {
                float a0 = (float)M_PI / 3.0f * e, a1 = (float)M_PI / 3.0f * (e + 1);
                float x0 = cxh + std::cos(a0) * R, z0 = cyh + std::sin(a0) * R;
                float x1 = cxh + std::cos(a1) * R, z1 = cyh + std::sin(a1) * R;
                if (x0 < -halfW && x1 < -halfW) continue;
                if (x0 > halfW && x1 > halfW) continue;
                if (z0 < -halfH && z1 < -halfH) continue;
                if (z0 > halfH && z1 > halfH) continue;
                glVertex3f(x0, 0, z0);
                glVertex3f(x1, 0, z1);
                nLines++;
            }
        }
    }
    glEnd();
    glPopMatrix();
    CountTris(0, nLines);
    if (lightOn_) glEnable(GL_LIGHTING);
}

// ---- screen-space 2D helpers (ortho like FillRect2D; y down) -------------
namespace {
struct OrthoScope {
    OrthoScope(int w, int h)
        : lit_(glIsEnabled(GL_LIGHTING)), blend_(glIsEnabled(GL_BLEND)),
          depth_(glIsEnabled(GL_DEPTH_TEST)) {
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glOrtho(0, w, h, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    ~OrthoScope() {
        // Restore, don't clobber: the caller may be mid-frame 3D pass.
        if (blend_) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (depth_) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (lit_) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }
    bool lit_, blend_, depth_;
};
} // namespace

void Renderer::DrawDisc2D(float cx, float cy, float radius, int segs,
                          float r, float g, float b, float a) {
    if (segs < 8) segs = 8; if (segs > 96) segs = 96;
    OrthoScope sc(w_, h_);
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segs; i++) {
        float p = (float)i / segs * 2.0f * (float)M_PI;
        glVertex2f(cx + std::cos(p) * radius, cy + std::sin(p) * radius);
    }
    glEnd();
    CountTris(uint64_t(segs));
}

void Renderer::DrawRing2D(float cx, float cy, float rIn, float rOut, int segs,
                          float r, float g, float b, float a) {
    if (segs < 8) segs = 8; if (segs > 128) segs = 128;
    OrthoScope sc(w_, h_);
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= segs; i++) {
        float p = (float)i / segs * 2.0f * (float)M_PI;
        float c = std::cos(p), s = std::sin(p);
        glVertex2f(cx + c * rIn, cy + s * rIn);
        glVertex2f(cx + c * rOut, cy + s * rOut);
    }
    glEnd();
    CountTris(uint64_t(2 * segs));
}

void Renderer::DrawArc2D(float cx, float cy, float rIn, float rOut,
                         float a0Deg, float a1Deg, int segs,
                         float r, float g, float b, float a) {
    if (segs < 2) segs = 2; if (segs > 128) segs = 128;
    float rad0 = a0Deg * (float)M_PI / 180.0f, rad1 = a1Deg * (float)M_PI / 180.0f;
    OrthoScope sc(w_, h_);
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= segs; i++) {
        float t = (float)i / segs;
        float p = rad0 + (rad1 - rad0) * t;
        float c = std::cos(p), s = std::sin(p);
        glVertex2f(cx + c * rIn, cy + s * rIn);
        glVertex2f(cx + c * rOut, cy + s * rOut);
    }
    glEnd();
    CountTris(uint64_t(2 * segs));
}

void Renderer::DrawLine2D(float x0, float y0, float x1, float y1, float thickness,
                          float r, float g, float b, float a) {
    OrthoScope sc(w_, h_);
    glColor4f(r, g, b, a);
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
    glBegin(GL_QUADS);
    glVertex2f(x0 + nx, y0 + ny); glVertex2f(x1 + nx, y1 + ny);
    glVertex2f(x1 - nx, y1 - ny); glVertex2f(x0 - nx, y0 - ny);
    glEnd();
    CountTris(2); // quad
}

void Renderer::FillRect2D(int px, int py, int w, int h, float r, float g, float b, float a) {
    bool prevBlend = glIsEnabled(GL_BLEND) != 0;
    bool prevDepth = glIsEnabled(GL_DEPTH_TEST) != 0;
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w_, h_, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2i(px, py);
    glVertex2i(px + w, py);
    glVertex2i(px + w, py + h);
    glVertex2i(px, py + h);
    glEnd();
    CountTris(2); // quad
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (lightOn_) glEnable(GL_LIGHTING);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void Renderer::DrawText2D(int px, int py, const char* text, int scale, float r, float g, float b) {
    if (!text || scale < 1) return;
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w_, h_, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3f(r, g, b);
    int cx = px;
    uint64_t glyphQuads = 0;
    for (const char* p = text; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\n') {
            cx = px;
            py += 9 * scale;
            continue;
        }
        if (ch < 32 || ch > 126) {
            cx += 6 * scale;
            continue;
        }
        const uint8_t* gl5 = kFont57[ch - 32];
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = gl5[col];
            for (int row = 0; row < 7; ++row) {
                if (bits & (1u << row)) {
                    int x0 = cx + col * scale, y0 = py + row * scale;
                    glRecti(x0, y0, x0 + scale, y0 + scale);
                    glyphQuads++;
                }
            }
        }
        cx += 6 * scale;
    }
    if (lightOn_) glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    CountTris(uint64_t(2 * glyphQuads));
}

bool Renderer::SaveScreenshot(const char* path) const {
    if (!path) return false;
    std::vector<unsigned char> px(size_t(w_) * size_t(h_) * 3);
    glReadPixels(0, 0, w_, h_, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w_, h_);
    // PPM rows are top-down; glReadPixels is bottom-up: flip.
    for (int y = h_ - 1; y >= 0; --y)
        std::fwrite(px.data() + size_t(y) * size_t(w_) * 3, 3, size_t(w_), f);
    std::fclose(f);
    return true;
}

} // namespace rl
