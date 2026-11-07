// Renderer.cpp — SDL2 + OpenGL 4.5 Compatibility fixed-function renderer.
// GRAPHICS45 §2.4: GL 4.5-compat hard-target (fixed-function keeps running,
// VBOs/FBOs/GLSL 4.50-compat first-class). No fallback, no core-only code.
#include "Renderer.h"
#include "Font.h"
#include "models/ArenaShell.h"
#include "models/ArenaRip.h"
#include "render/PbrGl.h"
#include "settings/Settings.h"
#include <GL/gl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
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
    // GRAPHICS45 G2: hard-target GL 4.5 Compatibility, no fallback.
    // Fixed-function (glBegin/glEnd, GL_NORMALIZE) keeps running under 4.5-compat;
    // VBOs/FBOs/GLSL 4.50-compat are first-class. No core-profile-only code.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    // [render] msaa/max_fps/pacing_log (schema v1 additive, defaults keep the
    // shipped behavior). msaa must be set before SDL_CreateWindow. msaa<=0
    // keeps gated captures pixel-stable.
    vsync_ = vsync;
    {
        int msaa = 0;
        Settings tmp = Settings::Defaults();
        std::string err;
        if (Settings::Load("config/settings.ini", tmp, &err)) {
            msaa = tmp.render.msaa;
            // G5: same tolerant load — missing keys keep shipped behavior
            // (limiter off, no CWD log spray). Clamped below / in Migrate.
            maxFps_ = tmp.render.maxFps;
            pacingLog_ = tmp.render.pacingLog;
            // G6a: PBR-lite opt-in (default OFF keeps gated captures stable).
            pbr_ = tmp.render.pbr;
        } else if (tmp.render.msaa != 0) {
            msaa = tmp.render.msaa;
        }
        if (msaa < 0) msaa = 0;
        if (msaa > 16) msaa = 16;
        if (maxFps_ < 0) maxFps_ = 0;
        if (maxFps_ > 0 && maxFps_ < 15) maxFps_ = 15;
        if (maxFps_ > 1000) maxFps_ = 1000;
        if (msaa > 0) {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa);
        } else {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        }
    }

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
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* rendererStr = (const char*)glGetString(GL_RENDERER);
    const char* versionStr = (const char*)glGetString(GL_VERSION);
    if (!vendor) vendor = "unknown";
    if (!rendererStr) rendererStr = "unknown";
    if (!versionStr) versionStr = "unknown";
    std::fprintf(stderr, "[render] GL4.5-compat ctx ok, vsync=%s (swapInterval rc=%d)\n",
        vsync ? "ON" : "OFF", ok);
    std::fprintf(stderr, "[render] GL_VENDOR=%s RENDERER=%s VERSION=%s\n",
        vendor, rendererStr, versionStr);
    // G6a: report the PBR-lite path so captures/soaks are attributable.
    std::fprintf(stderr, "[render] pbr=%s (PBR-lite car/ball material)\n",
        pbr_ ? "ON" : "OFF");
    // Hard-target gate: abort loudly when GL < 4.5. No 2.1/3.3 fallback,
    // no llvmpipe path. Parse leading "major.minor" from VERSION.
    {
        int major = 0, minor = 0;
        std::sscanf(versionStr, "%d.%d", &major, &minor);
        if (major < 4 || (major == 4 && minor < 5)) {
            std::fprintf(stderr,
                "[render] FATAL: GL 4.5 compatibility profile required, got VERSION=%s "
                "(VENDOR=%s RENDERER=%s) — aborting with no fallback\n",
                versionStr, vendor, rendererStr);
            Shutdown();
            return false;
        }
    }

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
    ArenaShell::ShutdownArenaBuckets(); // needs live GL context; no-op if unbuilt
    ShutdownArenaPack(); // A6b: no-op (no GL call) when no pack was built
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
    // G6a: PBR-lite branch. OFF = zero extra GL calls (byte-identical
    // shipped path); ON = fixed-function specular + roughness + IBL lift
    // (PbrGl, no shaders, no draws, idempotent per frame).
    if (pbr_) {
        double t0 = NowMs();
        pbr::ApplyFrameState();
        pbrApplyMs_ = NowMs() - t0;
    } else {
        pbrApplyMs_ = 0.0;
    }
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
    // G6a: fill the G5 forward-compat stub from the PBR-lite path. The path
    // is state-only (no extra draws/tris), so draws/tris stay 0 and ms is
    // the measured BeginFrame apply cost (SDL ms clock: usually 0.00).
    // Stays zero with pbr=false.
    counters_.flagCost.ms = pbr_ ? pbrApplyMs_ : 0.0;
    counters_.flagCost.draws = 0;
    counters_.flagCost.tris = 0;
    for (;;) {
        GLenum e = glGetError();
        if (e == GL_NO_ERROR) break;
        counters_.glErrors++;
    }
    SDL_GL_SwapWindow(window_); // vsync-paced present; never waits on physics
    double dt = NowMs() - frameStart_;
    counters_.frame_ms = dt; // G5: CPU wall-clock for the soak exit line
    hist_[histI_] = dt; histI_ = (histI_ + 1) % kHist;
    if (histN_ < kHist) histN_++;
    frames_++;
    LogPacingIfDue();
    // G5 max_fps limiter (bevy_framepace thinking): clamp + sleep, never tick
    // loss. Engages ONLY when vsync is off — vsync-on keeps the shipped
    // vsync-paced present untouched. The sleep only delays the present; the
    // sim accumulator absorbs it as ordinary wall dt (backlog kept, only a
    // >1 s backlog is dropped), so pacing never costs a physics tick.
    if (!vsync_ && maxFps_ > 0) {
        double target = 1000.0 / (double)maxFps_;
        double elapsed = NowMs() - frameStart_;
        if (elapsed < target)
            SDL_Delay((Uint32)(target - elapsed));
    }
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
    // V9: the CWD append below used to spray render_pacing.log on EVERY run
    // (play, capture, soak — every pacing print). Now gated on [render]
    // pacing_log (default OFF); opt in for soak analysis. stderr is unchanged.
    if (pacingLog_) {
        FILE* f = std::fopen("render_pacing.log", "a");
        if (f) { std::fputs(line, f); std::fclose(f); }
    }
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

// ---- arena-accurate texture mapping (A4) ---------------------------------
// A textured quad is a pure state sandwich: bind the texture, save whatever
// GL_TEXTURE_2D / blend state the caller had, draw one GL_QUADS, restore.
// Restoring matters because the frame state must stay a pure function of the
// current frame's draws (the bug class that once made every translucent
// surface opaque from frame 2 onward).
namespace {
struct TextureScope {
    TextureScope(unsigned tex, bool blend) {
        wasTex_ = glIsEnabled(GL_TEXTURE_2D) != 0;
        wasBlend_ = glIsEnabled(GL_BLEND) != 0;
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }
    ~TextureScope() {
        if (!wasBlend_) glDisable(GL_BLEND);
        if (!wasTex_) glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    bool wasTex_, wasBlend_;
};
} // namespace

void Renderer::DrawTexturedQuadXZ(float cx, float cy, float z, float sx, float sy,
                                  float yaw, unsigned tex, float uRep, float vRep,
                                  float r, float g, float b, float a, bool faceDown) {
    TextureScope ts(tex, a < 0.999f);
    glPushMatrix();
    glTranslatef(cx, cy, z);
    glRotatef(yaw * 180.0f / (float)M_PI, 0, 0, 1);
    // Modulate the sheet with the caller's color so one texture serves several
    // tints (the goal weave is shared by both team goals).
    glColor4f(r, g, b, a);
    const float hx = sx * 0.5f, hy = sy * 0.5f;
    // Normal sign matters: the key/fill model shades by the normal, so a
    // ceiling panel must face DOWN (normal -Z) to be lit from inside the arena.
    // A +Z normal would leave the visible underside ambient-only and the ceiling
    // would read near-black, which is the bug VISUAL_BUGS_TODO V5 fixed.
    const float nz = faceDown ? -1.0f : 1.0f;
    glNormal3f(0, 0, nz);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);     glVertex3f(-hx, -hy, 0);
    glTexCoord2f(uRep, 0);  glVertex3f( hx, -hy, 0);
    glTexCoord2f(uRep, vRep); glVertex3f( hx,  hy, 0);
    glTexCoord2f(0, vRep);  glVertex3f(-hx,  hy, 0);
    glEnd();
    glPopMatrix();
    CountTris(2);
}

void Renderer::DrawTexturedQuadWall(float cx, float cy, float cz, float sx, float sy,
                                    float yaw, unsigned tex, float uRep, float vRep,
                                    float r, float g, float b, float a,
                                    float uOff, float vOff) {
    TextureScope ts(tex, a < 0.999f);
    glPushMatrix();
    glTranslatef(cx, cy, cz);
    glRotatef(yaw * 180.0f / (float)M_PI, 0, 0, 1);
    glColor4f(r, g, b, a);
    const float hx = sx * 0.5f, hz = sy * 0.5f;
    // Local +x runs along the wall, +z up, so the quad's normal is local +y —
    // the arena's documented "local +y points OUTWARD" convention. Lighting
    // therefore shades the wall face the camera sees from inside the arena.
    glNormal3f(0, 1, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(uOff, vOff);            glVertex3f(-hx, 0, -hz);
    glTexCoord2f(uOff + uRep, vOff);     glVertex3f( hx, 0, -hz);
    glTexCoord2f(uOff + uRep, vOff + vRep); glVertex3f( hx, 0,  hz);
    glTexCoord2f(uOff, vOff + vRep);     glVertex3f(-hx, 0,  hz);
    glEnd();
    glPopMatrix();
    CountTris(2);
}

void Renderer::DrawTexturedOverlayXZ(float cx, float cy, float z, float sx, float sy,
                                     float yaw, unsigned tex, float uRep, float vRep,
                                     float r, float g, float b, float a) {
    // Overlay: never writes depth, so it composites onto whatever it covers
    // without ordering artifacts against the surface beneath it.
    GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    {
        TextureScope ts(tex, true);
        glPushMatrix();
        glTranslatef(cx, cy, z);
        glRotatef(yaw * 180.0f / (float)M_PI, 0, 0, 1);
        glColor4f(r, g, b, a);
        const float hx = sx * 0.5f, hy = sy * 0.5f;
        glNormal3f(0, 0, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);      glVertex3f(-hx, -hy, 0);
        glTexCoord2f(uRep, 0);   glVertex3f( hx, -hy, 0);
        glTexCoord2f(uRep, vRep); glVertex3f( hx,  hy, 0);
        glTexCoord2f(0, vRep);   glVertex3f(-hx,  hy, 0);
        glEnd();
        glPopMatrix();
    }
    if (depthWas) glEnable(GL_DEPTH_TEST);
    CountTris(2);
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

// ---- arena bucket VBOs (G3b; bake lives in src/models/ArenaShell.cpp) ----
void Renderer::DrawArenaBuckets(bool meshMode, bool noHexNet, float wallOpacity) {
    ArenaShell::DrawArenaBuckets(*this, meshMode, noHexNet, wallOpacity);
}

void Renderer::DrawArenaGoalInteriors(bool meshMode) {
    ArenaShell::DrawArenaGoalInteriors(*this, meshMode);
}

// ---- A6b: the ripped arena (the engine half of --arenadir) -----------------
//
// The pack itself is read by src/models/ArenaRip.h (A6a, read-only here). This
// block owns the GL side:
//
//   * ONE merged world-space VBO + EBO per bucket, uploaded ONCE with
//     GL_STATIC_DRAW (nothing is re-uploaded per frame).
//   * The bucket key is (texture/material id, spatial cell); the cell is the
//     8192 uu grid cell of the placement's world AABB centre. One draw call
//     per VISIBLE bucket per frame — the 119 meshes x 451 placements of the
//     real map never become a per-mesh glBegin storm.
//   * Per frame the buckets are frustum-culled by their world AABB (the
//     planes come from the matrix the camera just installed) and the
//     survivors are drawn with one fixed-function glDrawElements.
//   * Fixed-function only: GL_TEXTURE_2D + the repo's key+fill+ambient
//     lighting with GL_NORMALIZE (installed in Init) + the G4 mipmap
//     precedent. No GLSL, no VAO, no per-frame compile, no readback.
//
// The pack is a Psyonix-owned RUNTIME input (gitignored `arena_assets/`); it
// never enters the repository. With no pack the renderer creates NO GL
// objects, issues NO extra calls and the default path is byte-identical.
namespace {

// Spatial cell edge in uu (A6b contract).
constexpr int kArenaCell = 8192;
// Neutral fallback material for the sections that carry NO diffuse texture
// (392 of the 640 used section slots in the DFH Stadium day pack). A mid
// grey: it reads as "unpainted" without competing with the textured parts.
constexpr float kArenaFallback[3] = {0.55f, 0.55f, 0.55f};
// Vertex layout: pos(3) normal(3) uvDiff(2) uvLm(2) tangent(3) = 13 floats.
// A mesh that actually stores vertex colours appends rgb (16). This cook
// does not, so the colour slot stays off.
constexpr int kArenaStrideNoColor = 13;
constexpr int kArenaStrideColor = 16;

// Bucket key: (transparency, texture id, spatial cell). The pack's materials
// carry no colour, only a diffuse texture, so the TEXTURE is the material id
// for rendering purposes; -1 is the neutral fallback bucket. Transparency is
// part of the key so glass/force-field sections never merge with opaque ones
// (they need a blended, depth-write-off pass; opaque needs blend off to keep
// the DXT5 grass mask-alpha opaque).
struct ArenaKey {
    int tex = -1;
    int lm = -1;   // lightmap texture id (-1 = none)
    int nrm = -1;  // normal-map texture id (-1 = none)
    int nrmMode = 0;  // 1 = RGB, 2 = XY + reconstructed Z
    int tint = 0;  // fallback colour category when tex == -1 (0 = neutral)
    int cx = 0, cy = 0, cz = 0;
    bool transparent = false;
    bool unlit = false;
    bool decal = false;
    bool alphaTest = false;
    bool lmLuma = false;  // greyscale lightmap (StadiumBase only)
    bool color = false;   // vertex colours are stored on these verts
    float lmScale[3] = {1.f, 1.f, 1.f};
    bool operator<(const ArenaKey& o) const {
        if (transparent != o.transparent) return transparent < o.transparent;
        if (unlit != o.unlit) return unlit < o.unlit;
        if (decal != o.decal) return decal < o.decal;
        if (alphaTest != o.alphaTest) return alphaTest < o.alphaTest;
        if (lmLuma != o.lmLuma) return lmLuma < o.lmLuma;
        if (color != o.color) return color < o.color;
        if (tex != o.tex) return tex < o.tex;
        if (lm != o.lm) return lm < o.lm;
        if (nrm != o.nrm) return nrm < o.nrm;
        if (nrmMode != o.nrmMode) return nrmMode < o.nrmMode;
        if (tint != o.tint) return tint < o.tint;
        for (int i = 0; i < 3; ++i)
            if (lmScale[i] != o.lmScale[i]) return lmScale[i] < o.lmScale[i];
        if (cx != o.cx) return cx < o.cx;
        if (cy != o.cy) return cy < o.cy;
        return cz < o.cz;
    }
};

// Field-skin unwrap (legacy fall-back only). Prefer mesh UV2 when the pack
// stores it (rl-arena-pack/2 field floors). World unwrap is removed once the
// surface resolves to a real diffuse UV channel.
//   u = x/8192 + 1/2
//   v = 1 - |y|/8192
constexpr float kFieldUvScale = 1.0f / 8192.0f;
// Sky dome radius, inside the camera far plane (40000) and outside the bowl.
constexpr float kSkyRadius = 36000.0f;

// Force-field glass and the goal/side/roof nets, drawn in the blended pass.
//
// DFH Stadium day material rows have no blend, opacity, or masked flag.
// The keys are name, class, source, package, file, export, status, rule,
// parameter, diffuse_texture, texture_index, section_slots. These six
// materials are the whole glass/net set in that pack. The four mesh names
// are the DynamicMeshActor shells, so an empty slot on those shells still
// blends.
//
// The test is exact, not a substring. "glass" on the mesh name also covers
// Stadium_GlassSupports (DarkMetal_Mat / MetalBeam_SoftEdge_Mat) and the
// grate section of Goal_STD_Glass; those stay in the opaque pass.
//
// Beach_NetFence_MIC binds Hexagons_Pack_01_T. Upload converts that mask
// to an ice-blue net whose alpha peaks at G*80/255 (0.31). The blended
// pass keeps the lines and drops the holes. uAlphaTest at 0.5 would
// discard every texel of that mask, so the nets are not alpha-tested.
// Untextured glass (ForceField_HexGage, HexGlass_*, Glass_Projected_*)
// has no mask; upload stamps a 1x1 texel at kArenaGlassAlpha so the same
// blend path is see-through. The shader's untextured alpha is 1, and
// kArenaFs is left unchanged.
inline bool ArenaSectionTransparent(const std::string& meshName,
                                    const std::string& matName) {
    if (matName == "Beach_NetFence_MIC" ||
        matName == "ForceField_HexGage_MIC" ||
        matName == "Glass_Projected_V2_Mat" ||
        matName == "Glass_Projected_V2_Team2_MIC" ||
        matName == "HexGlass_WithArrows_Team1_MIC" ||
        matName == "HexGlass_WithArrows_Team2_MIC")
        return true;
    return meshName == "FF_Goal" || meshName == "FF_Side" ||
           meshName == "FF_Roof" || meshName == "FFCage_Full";
}

// Fog cards, light shafts, cloud planes and glow sprites. Drawn in the
// opaque pass they are slabs. They may join the existing transparent
// bucket only — never a new blend mode.
inline bool ArenaFxCardMesh(const std::string& meshName) {
    return meshName.rfind("Sprite", 0) == 0 || meshName == "Circle_Sprite" ||
           meshName.rfind("FogMesh", 0) == 0 ||
           meshName.rfind("FieldSpotLights", 0) == 0 ||
           meshName.rfind("CloudPlane", 0) == 0;
}

// The pack GLSL path blends with texel alpha. An opaque diffuse in the
// transparent bucket is still a slab, so a card needs real translucency.
bool ArenaDiffuseHasAlpha(const ArenaTexture& tex) {
    if (!tex.valid || tex.rgba.size() < 4) return false;
    const size_t n = tex.rgba.size() / 4u;
    for (size_t i = 0; i < n; ++i) {
        if (tex.rgba[i * 4u + 3u] < 250) return true;
    }
    return false;
}

// Pale ice-blue veil for untextured glass (matches the fallbackTint "glass"
// bucket 0.45/0.55/0.60) and its draw alpha: faint enough to see buildings
// through, strong enough to read as a wall. Drawn blended, depth-write off.
constexpr float kArenaGlass[3] = {0.45f, 0.55f, 0.60f};
constexpr float kArenaGlassAlpha = 0.18f;

struct ArenaCpuBucket {
    std::vector<float> v;       // stride floats per vertex
    std::vector<uint32_t> idx;  // 3 per triangle
    int stride = kArenaStrideNoColor;
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    float tintRgb[3] = {0.55f, 0.55f, 0.55f};  // fallback colour (tex == -1)
    std::string tag;  // first (mesh, material) baked here (diagnostics)
};

// V8U8 normals are stored with a constant B=128 (Z is not in the texture).
// RGB tangent normals (DXT1/DXT5 in this pack) have B near 230–250.
bool ArenaNormalIsXY(const ArenaTexture& td) {
    const size_t n = td.rgba.size() / 4u;
    if (n == 0 || !td.valid) return false;
    const size_t step = std::max<size_t>(n / 32u, 1u);
    for (size_t i = 0; i < n; i += step) {
        if (td.rgba[i * 4u + 2u] != 128) return false;
    }
    return true;
}

// Normal matrix = transpose(inverse(upper-left 3x3 of the column-major
// matrix)). Returns false for a singular matrix.
bool ArenaNormalMatrix(const float m[16], float out[9]) {
    const double a00 = m[0], a01 = m[4], a02 = m[8];
    const double a10 = m[1], a11 = m[5], a12 = m[9];
    const double a20 = m[2], a21 = m[6], a22 = m[10];
    const double c00 = a11 * a22 - a12 * a21;
    const double c01 = a12 * a20 - a10 * a22;
    const double c02 = a10 * a21 - a11 * a20;
    const double det = a00 * c00 + a01 * c01 + a02 * c02;
    if (std::fabs(det) < 1e-12) return false;
    const double id = 1.0 / det;
    out[0] = float(c00 * id);
    out[1] = float((a02 * a21 - a01 * a22) * id);
    out[2] = float((a01 * a12 - a02 * a11) * id);
    out[3] = float(c01 * id);
    out[4] = float((a00 * a22 - a02 * a20) * id);
    out[5] = float((a02 * a10 - a00 * a12) * id);
    out[6] = float(c02 * id);
    out[7] = float((a01 * a20 - a00 * a21) * id);
    out[8] = float((a00 * a11 - a01 * a10) * id);
    return true;
}

// The six frustum planes, extracted from the matrices the camera installed
// (client-side matrix state — no pipeline flush, no readback).
struct ArenaFrustum {
    double p[6][4];  // a*x+b*y+c*z+d >= 0 == inside
};

ArenaFrustum ExtractArenaFrustum() {
    double mv[16], pr[16];
    glGetDoublev(GL_MODELVIEW_MATRIX, mv);
    glGetDoublev(GL_PROJECTION_MATRIX, pr);
    // clip = P * V, read row-wise out of the column-major arrays.
    double row[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += pr[k * 4 + i] * mv[j * 4 + k];
            row[i][j] = s;
        }
    ArenaFrustum f;
    // left, right, bottom, top, near, far (Gribb-Hartmann).
    const int src[6] = {0, 0, 1, 1, 2, 2};
    const double sgn[6] = {1.0, -1.0, 1.0, -1.0, 1.0, -1.0};
    for (int i = 0; i < 6; ++i) {
        const int r = src[i];
        const double s = sgn[i];
        f.p[i][0] = row[3][0] + s * row[r][0];
        f.p[i][1] = row[3][1] + s * row[r][1];
        f.p[i][2] = row[3][2] + s * row[r][2];
        f.p[i][3] = row[3][3] + s * row[r][3];
    }
    return f;
}

// AABB vs frustum: the box is culled when it lies entirely behind any plane.
bool ArenaAabbVisible(const float lo[3], const float hi[3],
                      const ArenaFrustum& f) {
    for (int i = 0; i < 6; ++i) {
        const double a = f.p[i][0], b = f.p[i][1], c = f.p[i][2], d = f.p[i][3];
        const double x = a > 0.0 ? hi[0] : lo[0];
        const double y = b > 0.0 ? hi[1] : lo[1];
        const double z = c > 0.0 ? hi[2] : lo[2];
        if (a * x + b * y + c * z + d < 0.0) return false;
    }
    return true;
}

}  // namespace

// GL 1.5 / 3.0 entry points (VBO + mipmap). Their prototypes live in Mesa's
// glext.h behind GL_GLEXT_PROTOTYPES, which SDL's opengl header suppresses —
// so they are declared here (the same pattern as
// src/models/ArenaTextures.cpp). The context is 4.5-compat; libGL exports
// them; no loader is needed.
extern "C" void glGenBuffers(GLsizei n, GLuint* buffers);
extern "C" void glBindBuffer(GLenum target, GLuint buffer);
extern "C" void glBufferData(GLenum target, GLsizeiptr size, const void* data,
                             GLenum usage);
extern "C" void glDeleteBuffers(GLsizei n, const GLuint* buffers);
extern "C" void glGenerateMipmap(unsigned int target);
extern "C" GLuint glCreateShader(GLenum type);
extern "C" void glShaderSource(GLuint shader, GLsizei count,
                               const GLchar* const* string, const GLint* length);
extern "C" void glCompileShader(GLuint shader);
extern "C" void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
extern "C" void glGetShaderInfoLog(GLuint shader, GLsizei bufSize,
                                   GLsizei* length, GLchar* infoLog);
extern "C" void glDeleteShader(GLuint shader);
extern "C" GLuint glCreateProgram(void);
extern "C" void glAttachShader(GLuint program, GLuint shader);
extern "C" void glBindAttribLocation(GLuint program, GLuint index,
                                     const GLchar* name);
extern "C" void glLinkProgram(GLuint program);
extern "C" void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
extern "C" void glGetProgramInfoLog(GLuint program, GLsizei bufSize,
                                    GLsizei* length, GLchar* infoLog);
extern "C" void glDeleteProgram(GLuint program);
extern "C" void glUseProgram(GLuint program);
extern "C" GLint glGetUniformLocation(GLuint program, const GLchar* name);
extern "C" void glUniform1i(GLint location, GLint v0);
extern "C" void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
extern "C" void glActiveTexture(GLenum texture);
extern "C" void glEnableVertexAttribArray(GLuint index);
extern "C" void glDisableVertexAttribArray(GLuint index);
extern "C" void glVertexAttribPointer(GLuint index, GLint size, GLenum type,
                                      GLboolean normalized, GLsizei stride,
                                      const void* pointer);
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GLchar
typedef char GLchar;
#endif
// S3TC block formats (GL_EXT_texture_compression_s3tc). DXT1 uses the RGBA
// enum so a c0<=c1 block stays punch-through alpha, matching the CPU decoder.
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_TEXTURE_COMPRESSED
#define GL_TEXTURE_COMPRESSED 0x86A1
#endif

// Pack-only GLSL ≤ 4.50. Compiled once at upload. The no-pack path never
// builds this program.
//   colour = albedo * vertexColour? * lightmap * wrap + emissive
// Vertex colour is multiplied only when the mesh stored one (this cook does
// not). Lightmap hue is kept, except DFH_Day_StadiumBase2 (uLmLuma).
namespace {
constexpr const char* kArenaVs = R"(#version 450 compatibility
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv0;
layout(location = 3) in vec2 aUv1;
layout(location = 4) in vec3 aTan;
layout(location = 5) in vec3 aColor;
out vec2 vUv0;
out vec2 vUv1;
out vec3 vNrm;
out vec3 vTan;
out vec3 vColor;
void main() {
  vUv0 = aUv0;
  vUv1 = aUv1;
  vNrm = gl_NormalMatrix * aNrm;
  vTan = gl_NormalMatrix * aTan;
  vColor = aColor;
  gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);
}
)";
constexpr const char* kArenaFs = R"(#version 450 compatibility
uniform sampler2D uDiffuse;
uniform sampler2D uLightmap;
uniform sampler2D uNormal;
uniform vec3 uTint;
uniform vec3 uLmScale;
uniform vec3 uEmit;
uniform int uUseDiff;
uniform int uUseLm;
uniform int uUseNrm;
uniform int uNrmMode;
uniform int uLmLuma;
uniform int uAlphaTest;
uniform int uUseColor;
in vec2 vUv0;
in vec2 vUv1;
in vec3 vNrm;
in vec3 vTan;
in vec3 vColor;
void main() {
  vec3 albedo = uTint;
  float alpha = 1.0;
  if (uUseDiff != 0) {
    vec4 t = texture(uDiffuse, vUv0);
    albedo = t.rgb;
    alpha = t.a;
  }
  if (uUseColor != 0) albedo *= vColor;
  if (uAlphaTest != 0 && alpha < 0.5) discard;
  vec3 lm = vec3(1.0);
  if (uUseLm != 0) {
    // DirectionalMax * ScaleVectors. Hue is kept. DFH_Day_StadiumBase2
    // (uLmLuma) stays greyscale: its live chart is cyan (~0.67, 1.18, 1.18)
    // and that cast paints the grey concrete purple. Near-black atlas
    // padding stays the neutral 0.72, not a wash and not a blackout.
    vec3 raw = texture(uLightmap, vUv1).rgb * uLmScale;
    float lum = dot(raw, vec3(0.3333));
    float live = smoothstep(0.0, 0.06, lum);
    float gain = clamp(lum * 1.15, 0.28, 1.70);
    float sh = mix(0.72, gain, live);
    if (uLmLuma != 0 || lum <= 0.0001) {
      lm = vec3(sh);
    } else {
      lm = mix(vec3(0.72), raw * (gain / lum), live);
    }
  }
  vec3 n = normalize(vNrm);
  if (uUseNrm != 0) {
    vec3 t = vTan;
    float tl = length(t);
    if (tl > 0.00001) {
      t /= tl;
      t = t - n * dot(n, t);
      if (length(t) > 0.00001) {
        t = normalize(t);
        vec3 b = cross(n, t);
        vec3 s = texture(uNormal, vUv0).rgb;
        vec3 tn;
        if (uNrmMode == 2) {
          tn.xy = s.xy * 2.0 - 1.0;
          tn.z = sqrt(max(0.0, 1.0 - dot(tn.xy, tn.xy)));
        } else {
          tn = s * 2.0 - 1.0;
        }
        n = normalize(t * tn.x + b * tn.y + n * tn.z);
      }
    }
  }
  float ndl = max(dot(n, normalize(vec3(0.35, 0.2, 0.9))), 0.0);
  float shade = mix(0.92, 1.0, ndl);
  gl_FragColor = vec4(albedo * lm * shade + uEmit, alpha);
}
)";

GLuint ArenaCompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::fprintf(stderr, "[arena] shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint ArenaLinkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aNrm");
    glBindAttribLocation(p, 2, "aUv0");
    glBindAttribLocation(p, 3, "aUv1");
    glBindAttribLocation(p, 4, "aTan");
    glBindAttribLocation(p, 5, "aColor");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, 512, nullptr, log);
        std::fprintf(stderr, "[arena] program link failed: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool ArenaExtensionHas(const char* exts, const char* needle) {
    if (!exts || !needle || needle[0] == '\0') return false;
    const size_t n = std::strlen(needle);
    for (const char* p = exts; (p = std::strstr(p, needle)) != nullptr; p += n) {
        const bool left = (p == exts) || p[-1] == ' ';
        const bool right = p[n] == '\0' || p[n] == ' ';
        if (left && right) return true;
    }
    return false;
}

bool ArenaDriverHasS3tc() {
    const GLubyte* ext = glGetString(GL_EXTENSIONS);
    if (!ext) return false;
    return ArenaExtensionHas(reinterpret_cast<const char*>(ext),
                             "GL_EXT_texture_compression_s3tc");
}

const char* ArenaPackFormatName(ArenaTexFormat format) {
    if (format == ArenaTexFormat::kDxt1) return "PF_DXT1";
    if (format == ArenaTexFormat::kDxt5) return "PF_DXT5";
    return "RGBA8";
}

GLenum ArenaS3tcInternalFormat(ArenaTexFormat format) {
    if (format == ArenaTexFormat::kDxt1) return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    if (format == ArenaTexFormat::kDxt5) return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    return 0;
}

const char* ArenaS3tcEnumName(GLenum format) {
    if (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)
        return "GL_COMPRESSED_RGBA_S3TC_DXT1_EXT";
    if (format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)
        return "GL_COMPRESSED_RGBA_S3TC_DXT5_EXT";
    return nullptr;
}

// Level 0 of the bound texture becomes the S3TC payload. Mips are generated
// for diffuse textures (lightmaps stay a single level). False means the
// driver refused the format; `why` says which call, and the GL error is drained.
bool ArenaUploadCompressedLevel(const ArenaTexture& t, GLenum internalFormat,
                                bool withMips, char* why, size_t whyLen) {
    while (glGetError() != GL_NO_ERROR) {}
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalFormat, t.width, t.height,
                           0, static_cast<GLsizei>(t.blocks.size()),
                           t.blocks.data());
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        while (glGetError() != GL_NO_ERROR) {}
        std::snprintf(why, whyLen, "glCompressedTexImage2D error 0x%04x", err);
        return false;
    }
    if (withMips) {
        glGenerateMipmap(GL_TEXTURE_2D);
        err = glGetError();
        if (err != GL_NO_ERROR) {
            while (glGetError() != GL_NO_ERROR) {}
            std::snprintf(why, whyLen, "glGenerateMipmap error 0x%04x", err);
            return false;
        }
    }
    GLint isCompressed = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED,
                             &isCompressed);
    err = glGetError();
    if (err != GL_NO_ERROR || isCompressed != GL_TRUE) {
        while (glGetError() != GL_NO_ERROR) {}
        std::snprintf(why, whyLen, "driver did not keep the compressed format");
        return false;
    }
    return true;
}
}  // namespace

bool Renderer::BuildArenaPack(const ArenaPack& pack) {
    ShutdownArenaPack();
    arenaStats_ = ArenaPackStats();

    // ---- CPU bake: one merged bucket per (texture/material, cell) ----------
    std::map<ArenaKey, size_t> bucketIndex;
    std::vector<ArenaCpuBucket> cpu;
    std::vector<int32_t> remap;    // vertex -> local index in the bucket
    std::vector<uint32_t> stamp;   // vertex -> bucket/section stamp
    uint32_t stampCounter = 0;
    uint32_t skipped = 0;
    uint64_t tris = 0;
    uint32_t placements = 0, slots = 0;
    std::vector<std::string> skippedCards;
    std::vector<std::string> blendedCards;
    auto noteCard = [](std::vector<std::string>& rows, const std::string& mesh,
                       const std::string& mat) {
        std::string row = mesh;
        if (!mat.empty()) row += " / " + mat;
        if (std::find(rows.begin(), rows.end(), row) == rows.end())
            rows.push_back(std::move(row));
    };

    // A mesh NAME is not unique in a UE3 package: the Stadium_Day pack holds
    // EIGHT exports called "Grass" (distinct ground tiles). The pack's
    // instance rows name their mesh, not its export index, so the name join
    // is ambiguous for them. The loader keeps the FIRST mesh of a name; here
    // identical (name, matrix) placements are dealt round-robin across every
    // same-named mesh: 32 identical "Grass" placements at 4 mirror transforms
    // are 8 tiles x 4 mirrors, so placement k of a group takes tile k % 8.
    // Non-duplicated names hold exactly one mesh and behave as before.
    std::map<std::string, std::vector<const ArenaMesh*>> meshesByName;
    for (const ArenaMesh& m : pack.meshes) meshesByName[m.name].push_back(&m);
    // The day sky is an imported SkySphere01 (external mesh). The package's
    // own mesh table does not contain it.
    if (const ArenaMesh* sky = FindArenaMesh(pack, "SkySphere01"))
        meshesByName["SkySphere01"].push_back(sky);
    std::map<std::string, size_t> dupCounter;
    int fieldTexId = -1;
    for (size_t ti = 0; ti < pack.textures.size(); ++ti) {
        if (pack.textures[ti].name == "FieldSkin_FutureGrid_D")
            fieldTexId = int(ti);
    }

    // Untextured materials are NOT one grey: the pack's untextured majority
    // spans white field lines, green reflective floor panels, dark trim and
    // pale force-field glass. Tint the fallback bucket by material name so
    // the lines read as lines and the floor reads as turf.
    auto fallbackTint = [](const std::string& name, float rgb[3]) -> int {
        std::string n = name;
        for (char& c : n) c = char(std::tolower((unsigned char)c));
        auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
        // Measured LinearColors from the package's vector parameters:
        // Team1 / TrimLight / GoalGenerator Color1 = (1, 0.10, 0.03),
        // Team2 = (0, 0.26, 1.06). Unnamed glow keeps the orange default.
        const bool team2 = has("team2");
        const bool team1 = has("team1");
        // TrimLight_None is the off housing, not the team glow.
        if ((has("trimlight") && !has("none")) || has("glow") ||
            has("goalgenerator") || has("emiss")) {
            if (team2) {
                rgb[0] = 0.15f; rgb[1] = 0.45f; rgb[2] = 1.0f; return 9;
            }
            rgb[0] = 1.0f; rgb[1] = 0.12f; rgb[2] = 0.04f; return 8;
        }
        if (has("stadiumlight") || has("lightbulb") || has("lightarray") ||
            has("citylight")) {
            rgb[0] = 1.0f; rgb[1] = 0.97f; rgb[2] = 0.90f; return 5;
        }
        // Painted markings. Unlit white (tint 7) plus a decal offset.
        if (has("line") || has("paint") || has("stripe")) {
            rgb[0] = 1.0f; rgb[1] = 1.0f; rgb[2] = 1.0f; return 7;
        }
        if (has("bark")) {
            rgb[0] = 0.28f; rgb[1] = 0.20f; rgb[2] = 0.12f; return 12;
        }
        if (has("poplar") || has("tree") || has("foliage") || has("leaf")) {
            rgb[0] = 0.18f; rgb[1] = 0.46f; rgb[2] = 0.14f; return 10;
        }
        if (has("cloud") || has("sky")) {
            // DaytimeClouds LightColor-adjacent pale day sky (0.73, 0.82, 0.90).
            rgb[0] = 0.73f; rgb[1] = 0.82f; rgb[2] = 0.90f; return 11;
        }
        if (has("grass") || has("turf") || has("groomed") || has("pitch")) {
            rgb[0] = 0.42f; rgb[1] = 0.68f; rgb[2] = 0.24f; return 2;
        }
        if (has("seat")) {
            rgb[0] = 0.28f; rgb[1] = 0.36f; rgb[2] = 0.48f; return 13;
        }
        if (has("statue")) {
            rgb[0] = 0.64f; rgb[1] = 0.62f; rgb[2] = 0.56f; return 14;
        }
        // The bowl is light concrete. "frame" used to share the dark-metal
        // bucket, which painted the whole stadium charcoal.
        if (has("frame") || has("roof") || has("stair") || has("concrete") ||
            has("sidewalk") || has("handrail")) {
            rgb[0] = 0.80f; rgb[1] = 0.79f; rgb[2] = 0.76f; return 15;
        }
        if (has("metal") || has("grate") || has("beam") || has("steel") ||
            has("trim") || has("collision") || has("dark") || has("reflect")) {
            rgb[0] = 0.16f; rgb[1] = 0.16f; rgb[2] = 0.17f; return 3;
        }
        if (has("glass") || has("force") || has("cage") || has("hex") ||
            has("energy")) {
            if (team2) {
                rgb[0] = 0.35f; rgb[1] = 0.55f; rgb[2] = 0.90f; return 4;
            }
            if (team1) {
                rgb[0] = 0.90f; rgb[1] = 0.42f; rgb[2] = 0.22f; return 4;
            }
            rgb[0] = 0.55f; rgb[1] = 0.72f; rgb[2] = 0.84f; return 4;
        }
        if (has("advert")) {
            rgb[0] = 0.10f; rgb[1] = 0.12f; rgb[2] = 0.14f; return 3;
        }
        rgb[0] = kArenaFallback[0]; rgb[1] = kArenaFallback[1];
        rgb[2] = kArenaFallback[2];
        return 0;
    };

    // Package placements, plus the imported sky and the external cards.
    // A card is drawn later only when it can sit in the existing
    // transparent bucket. Anything else would be an opaque slab.
    std::vector<const ArenaInstance*> toDraw;
    toDraw.reserve(pack.instances.size() + pack.externalInstances.size());
    for (const ArenaInstance& inst : pack.instances) toDraw.push_back(&inst);
    for (const ArenaInstance& inst : pack.externalInstances) {
        if (inst.mesh == "SkySphere01" || ArenaFxCardMesh(inst.mesh))
            toDraw.push_back(&inst);
    }
    for (const ArenaInstance* instPtr : toDraw) {
        const ArenaInstance& inst = *instPtr;
        // Invisible gameplay helpers: decal blockers (full-field invisible
        // planes), particle/FX actors and warmup effects are never visible
        // geometry. Drawing them would bury the field under opaque planes.
        if (inst.owner == "DecalBlockingActor_TA" || inst.owner == "FXActor_TA" ||
            inst.owner == "BoostPill_WarmUp_FXActor") {
            ++skipped;
            continue;
        }
        // Field_Center_Trim has no safe albedo. Section 0 resolves
        // ENV_CarbonFiber_N (a normal; the non-albedo filter drops it) and
        // section 1 is untextured TrimLight_None. No lightmap, and it is not
        // a FieldSkin pitch mesh. Field_Center still draws: Hexagons_Pack_B
        // is rejected and the existing pitch rule binds FieldSkin. The hex
        // floor still draws: same rejection, then its baked lightmap.
        // FieldSkin on the main pitch meshes is unchanged.
        if (inst.mesh == "Field_Center_Trim") {
            ++skipped;
            continue;
        }
        const auto candIt = meshesByName.find(inst.mesh);
        const ArenaMesh* mesh = (candIt != meshesByName.end() && !candIt->second.empty())
            ? candIt->second[dupCounter[inst.mesh + "|" + std::string(
                reinterpret_cast<const char*>(inst.matrix),
                sizeof(inst.matrix))]++ % candIt->second.size()]
            : FindArenaMesh(pack, inst.mesh);
        if (!mesh || mesh->positions.size() < 3 || mesh->indices.empty() ||
            mesh->sections.empty()) {
            ++skipped;
            continue;
        }
        float nm[9];
        if (!ArenaNormalMatrix(inst.matrix, nm)) {
            ++skipped;
            continue;
        }
        // Mirror check: placements with a negative-determinant upper 3x3
        // (mirrored tiles: half the Grass placements) reverse the triangle
        // winding, so single-sided fixed-function lighting would shade them
        // as if their normals pointed down. Emitting those triangles with
        // swapped winding restores outward faces.
        const double m0 = inst.matrix[0], m1 = inst.matrix[1],
                     m2 = inst.matrix[2];
        const double m4 = inst.matrix[4], m5 = inst.matrix[5],
                     m6 = inst.matrix[6];
        const double m8 = inst.matrix[8], m9 = inst.matrix[9],
                     m10 = inst.matrix[10];
        const double det = m0 * (m5 * m10 - m6 * m9) -
                           m4 * (m1 * m10 - m2 * m9) +
                           m8 * (m1 * m6 - m2 * m5);
        const bool mirrored = (det < 0.0);
        const size_t nv = mesh->positions.size() / 3;
        const bool hasN = mesh->normals.size() >= nv * 3;
        const bool hasT = mesh->tangents.size() >= nv * 3;
        const bool hasColor = mesh->colors.size() >= nv * 3;
        const bool hasUv = mesh->uvs.size() >= nv * 2;
        const bool hasUvLm = mesh->uvsLm.size() >= nv * 2;
        const bool hasPackedLm = inst.lightmap.valid && inst.lightmap.texture >= 0;
        // Pack /2 stores field-skin layout on UV2 as the diffuse channel —
        // no world unwrap. Legacy packs (channel 0 only) keep the unwrap.
        const bool meshHasFieldUv = mesh->diffuseUvChannel >= 2 ||
            mesh->name == "Field_STD_Floor_Team1" ||
            mesh->name == "Field_STD_Floor_Team2" ||
            mesh->name == "Field_CenterField_Team1" ||
            mesh->name == "Field_CenterField_Team2";
        // Transform the mesh into world space once (positions, normals, tangents).
        std::vector<float> wp(nv * 3), wn(nv * 3), wt(nv * 3, 0.f);
        double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
        for (size_t v = 0; v < nv; ++v) {
            const double x = mesh->positions[v * 3 + 0];
            const double y = mesh->positions[v * 3 + 1];
            const double z = mesh->positions[v * 3 + 2];
            double px = inst.matrix[0] * x + inst.matrix[4] * y +
                        inst.matrix[8] * z + inst.matrix[12];
            double py = inst.matrix[1] * x + inst.matrix[5] * y +
                        inst.matrix[9] * z + inst.matrix[13];
            double pz = inst.matrix[2] * x + inst.matrix[6] * y +
                        inst.matrix[10] * z + inst.matrix[14];
            // The authored sky scale (64 × a 65536 uu sphere) lands millions
            // of units out, past the camera far plane. Pull the shell in to
            // kSkyRadius so the dome is visible and still outside the bowl.
            if (mesh->name == "SkySphere01") {
                const double len = std::sqrt(px * px + py * py + pz * pz);
                if (len > 1.0) {
                    const double s = double(kSkyRadius) / len;
                    px *= s; py *= s; pz *= s;
                }
            } else if (mesh->name == "OOBFloor") {
                // Sit under the pitch so the centre-circle cutout and the
                // turf win the depth test. The apron outside the bowl remains.
                pz -= 8.0;
            }
            wp[v * 3 + 0] = float(px);
            wp[v * 3 + 1] = float(py);
            wp[v * 3 + 2] = float(pz);
            for (int c = 0; c < 3; ++c) {
                const double p = c == 0 ? px : (c == 1 ? py : pz);
                if (p < lo[c]) lo[c] = p;
                if (p > hi[c]) hi[c] = p;
            }
            float nx = hasN ? mesh->normals[v * 3 + 0] : 0.0f;
            float ny = hasN ? mesh->normals[v * 3 + 1] : 0.0f;
            float nz = hasN ? mesh->normals[v * 3 + 2] : 1.0f;
            float tx = nm[0] * nx + nm[1] * ny + nm[2] * nz;
            float ty = nm[3] * nx + nm[4] * ny + nm[5] * nz;
            float tz = nm[6] * nx + nm[7] * ny + nm[8] * nz;
            const float len = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (len > 1e-8f) {
                tx /= len; ty /= len; tz /= len;
            } else {
                tx = 0.0f; ty = 0.0f; tz = 1.0f;
            }
            wn[v * 3 + 0] = tx;
            wn[v * 3 + 1] = ty;
            wn[v * 3 + 2] = tz;
            // Tangents use the instance 3x3, not the inverse-transpose.
            // A mirrored placement flips the bitangent; negate T so
            // cross(N, T) follows the swapped winding.
            float lx = hasT ? mesh->tangents[v * 3 + 0] : 0.f;
            float ly = hasT ? mesh->tangents[v * 3 + 1] : 0.f;
            float lz = hasT ? mesh->tangents[v * 3 + 2] : 0.f;
            float sx = inst.matrix[0] * lx + inst.matrix[4] * ly +
                       inst.matrix[8] * lz;
            float sy = inst.matrix[1] * lx + inst.matrix[5] * ly +
                       inst.matrix[9] * lz;
            float sz = inst.matrix[2] * lx + inst.matrix[6] * ly +
                       inst.matrix[10] * lz;
            if (mirrored) { sx = -sx; sy = -sy; sz = -sz; }
            const float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
            if (sl > 1e-8f) { sx /= sl; sy /= sl; sz /= sl; }
            else { sx = 0.f; sy = 0.f; sz = 0.f; }
            wt[v * 3 + 0] = sx;
            wt[v * 3 + 1] = sy;
            wt[v * 3 + 2] = sz;
        }
        // The spatial cell: the 8192 uu grid cell of the placement's world
        // AABB centre.
        int cell[3];
        for (int c = 0; c < 3; ++c)
            cell[c] = int(std::floor(((lo[c] + hi[c]) * 0.5) / double(kArenaCell)));

        remap.assign(nv, -1);
        stamp.assign(nv, 0u);
        for (const ArenaSection& sec : mesh->sections) {
            if (sec.numTriangles == 0) continue;
            const uint32_t first = sec.firstIndex;
            const size_t count = size_t(sec.numTriangles) * 3u;
            if (size_t(first) + count > mesh->indices.size()) continue;
            // The section's material: the instance's per-slot material, and
            // through it the diffuse texture (-1 = no diffuse -> the neutral
            // fallback material).
            int matIdx = -1;
            if (sec.materialSlot >= 0 &&
                size_t(sec.materialSlot) < inst.materialSlots.size())
                matIdx = inst.materialSlots[size_t(sec.materialSlot)];
            int texId = -1;
            int nrmId = -1;
            int nrmMode = 0;
            float tintRgb[3] = {kArenaFallback[0], kArenaFallback[1],
                                kArenaFallback[2]};
            int tint = 0;
            std::string secMatName;
            if (matIdx >= 0 && size_t(matIdx) < pack.materials.size()) {
                const std::string& matName =
                    pack.materials[size_t(matIdx)].name;
                secMatName = matName;
                // Invisible collision proxies: the pack binds Collision_Mat to
                // kill planes (Plane32768, one hung at z=2048 as a dark false
                // ceiling), goal/side/corner collision halves and the
                // full-field decal blockers. They carry no diffuse and are
                // never rendered in game; the dark fallback tint would draw
                // them as opaque slabs. Skip, counted.
                if (matName == "Collision_Mat") {
                    ++skipped;
                    continue;
                }
                texId = pack.materials[size_t(matIdx)].texture;
                // Reject non-albedo textures that slipped into the diffuse
                // slot (normal maps, 1x1 stubs). Keep the section; shade via
                // lightmap / tint instead of painting a normal as colour.
                if (texId >= 0 && size_t(texId) < pack.textures.size()) {
                    const ArenaTexture& td = pack.textures[size_t(texId)];
                    const std::string& tn = td.name;
                    // Real normal maps are ≥5×5 (ENV_CarbonFiber_N etc.).
                    // SeatBase's Step_N is a 1×512 ramp — keep it as albedo.
                    const bool bigEnough =
                        td.width > 4 && td.height > 4;
                    const bool looksNormal =
                        bigEnough &&
                        tn.size() >= 2 &&
                        (tn.compare(tn.size() - 2, 2, "_N") == 0 ||
                         tn.compare(tn.size() - 2, 2, "_n") == 0 ||
                         tn.find("_N_") != std::string::npos ||
                         tn.find("Normal") != std::string::npos);
                    // ShaderCache sometimes binds a curve/noise/mask into the
                    // diffuse slot (sky fog strip, grate smoke). Sampling
                    // those reads as purple moiré on frames/dome.
                    // Hexagons_Pack_B is a reflective/spec pack (MEASURED min
                    // B-heavy purple flecks) — not an albedo for goal floors.
                    const bool looksJunk =
                        tn.rfind("Curve_", 0) == 0 ||
                        tn.rfind("Noise_", 0) == 0 ||
                        tn == "FillFlare" ||
                        tn == "Hexagons_Pack_B";
                    const bool tinyStub = td.width <= 4 && td.height <= 4;
                    // A real normal map in the diffuse slot is not albedo.
                    // Sample it as a tangent-space normal instead.
                    if (looksNormal) {
                        nrmId = texId;
                        nrmMode = ArenaNormalIsXY(td) ? 2 : 1;
                    }
                    if (looksNormal || looksJunk || tinyStub || td.lightmap)
                        texId = -1;
                }
                if (texId < 0)
                    tint = fallbackTint(matName, tintRgb);
            }
            std::string matLower = secMatName;
            for (char& c : matLower)
                c = char(std::tolower((unsigned char)c));
            // FieldSkin only on the actual pitch meshes — never on stadium
            // "Grass_Base_*" sections (those stole the world unwrap and
            // painted the bowl with pitch swirls).
            const bool isPitchMesh =
                meshHasFieldUv || mesh->name == "Field_Center" ||
                mesh->name == "Field_STD_Floor_Team1" ||
                mesh->name == "Field_STD_Floor_Team2" ||
                mesh->name == "Field_STD_Floor_Thick" ||
                mesh->name == "Field_CenterField_Team1" ||
                mesh->name == "Field_CenterField_Team2";
            const bool fieldSkin =
                fieldTexId >= 0 && isPitchMesh &&
                (mesh->name == "Field_Center" || meshHasFieldUv ||
                 matLower.find("grass") != std::string::npos ||
                 matLower.find("groomed") != std::string::npos ||
                 matLower.find("field") != std::string::npos);
            if (fieldSkin) texId = fieldTexId;
            const bool useFieldUnwrap = fieldSkin && !meshHasFieldUv;
            const bool alphaTest =
                matLower.find("tree") != std::string::npos ||
                matLower.find("leaf") != std::string::npos ||
                matLower.find("foliage") != std::string::npos ||
                mesh->name.find("Leafy") != std::string::npos ||
                mesh->name.find("Tree") != std::string::npos;
            const bool skyDome = mesh->name == "SkySphere01";
            // Keep a resolved albedo. The pale day tint is only the fallback
            // when the pack and the imported mesh have no real diffuse
            // (curves, noise and flares are already rejected above).
            if (skyDome && texId < 0) {
                tint = 11;
                tintRgb[0] = 0.73f; tintRgb[1] = 0.82f; tintRgb[2] = 0.90f;
            }
            // The out-of-bounds floor is a Reflective_Floor overlay. Its only
            // resolved texture is Hexagons_Pack_B (a spec pack, not albedo),
            // and FieldSkin is clamped off the apron, so there is no safe
            // colour to replace this tint. The instance lightmap still
            // multiplies it. A flat mid ground reads as the apron.
            if (mesh->name == "OOBFloor") {
                texId = -1;
                tint = 16;
                tintRgb[0] = 0.34f; tintRgb[1] = 0.36f; tintRgb[2] = 0.33f;
            }
            bool boostPad = false;
            if (texId >= 0 && size_t(texId) < pack.textures.size())
                boostPad = pack.textures[size_t(texId)].name.rfind(
                               "BoostPad_", 0) == 0;
            // Force-field/glass transparency. The pack has no blend flag;
            // ArenaSectionTransparent is the exact material/mesh list.
            // Fog, shafts, clouds and sprites join that same bucket only
            // when their diffuse is actually translucent. Otherwise they
            // stay skipped: an opaque draw is a slab, and the pack shaders
            // are not given a new blend.
            const bool volumetric =
                ArenaFxCardMesh(mesh->name) ||
                secMatName.find("LightCone") != std::string::npos ||
                secMatName.find("Fog") != std::string::npos;
            bool secTransparent =
                ArenaSectionTransparent(mesh->name, secMatName);
            if (volumetric && !secTransparent && texId >= 0 &&
                size_t(texId) < pack.textures.size() &&
                ArenaDiffuseHasAlpha(pack.textures[size_t(texId)]))
                secTransparent = true;
            if (volumetric && !secTransparent) {
                ++skipped;
                noteCard(skippedCards, mesh->name, secMatName);
                continue;
            }
            if (volumetric)
                noteCard(blendedCards, mesh->name, secMatName);
            // Transparent sections get their own buckets (key.transparent).
            // Team/hex glass keeps fallback tint 4. Other untextured glass
            // takes the ice-blue veil. Alpha is stamped at upload.
            if (secTransparent && texId < 0 && tint != 4 && tint != 8 &&
                tint != 9) {
                tintRgb[0] = kArenaGlass[0];
                tintRgb[1] = kArenaGlass[1];
                tintRgb[2] = kArenaGlass[2];
                tint = 6;
            }
            const bool unlit = skyDome || fieldSkin || boostPad || tint == 5 ||
                               tint == 7 || tint == 8 || tint == 9 ||
                               tint == 11 || tint == 16;
            const bool decal = (tint == 7);

            // Lightmap: skip on sky / glass (atlas strips read as purple
            // moiré) and when the component's atlas rect is the known-empty
            // Floor_Team half (scale≈0.5 bias≈0/0.5 on Color1) — those
            // rects measure ~1.7% lit and raw UV1 wrongly samples neighbours.
            bool useLm = hasPackedLm && !skyDome && !secTransparent;
            if (useLm) {
                const float sx = inst.lightmap.coordScale[0];
                const float sy = inst.lightmap.coordScale[1];
                const float bx = inst.lightmap.coordBias[0];
                const float by = inst.lightmap.coordBias[1];
                // Tiny stamps (boost pads etc.) are fine; the empty Team
                // floor rects are ~0.5 with bias near 0 or 0.5 and belong
                // to pitch overlays that keep FieldSkin without LM.
                if (isPitchMesh && sx > 0.4f && sx < 0.55f &&
                    sy > 0.4f && sy < 0.55f &&
                    ((bx < 0.05f && by < 0.05f) ||
                     (bx > 0.45f && bx < 0.55f && by > 0.45f && by < 0.55f)))
                    useLm = false;
            }
            ArenaKey key;
            key.tex = texId;
            key.lm = useLm ? inst.lightmap.texture : -1;
            key.nrm = nrmId;
            key.nrmMode = nrmMode;
            key.tint = (texId < 0) ? tint : 0;
            key.transparent = secTransparent;
            key.unlit = unlit;
            key.decal = decal;
            key.alphaTest = alphaTest;
            // DFH_Day_StadiumBase2's live DirectionalMax chart is cyan
            // (~0.67, 1.18, 1.18). On the grey concrete that cast reads
            // purple, so this mesh keeps luminance. Every other chart
            // keeps its colour.
            key.lmLuma = useLm &&
                mesh->name.find("StadiumBase") != std::string::npos;
            key.color = hasColor;
            if (useLm) {
                key.lmScale[0] = inst.lightmap.colorScale[0];
                key.lmScale[1] = inst.lightmap.colorScale[1];
                key.lmScale[2] = inst.lightmap.colorScale[2];
            }
            key.cx = cell[0]; key.cy = cell[1]; key.cz = cell[2];
            size_t bi;
            auto it = bucketIndex.find(key);
            if (it == bucketIndex.end()) {
                bi = cpu.size();
                bucketIndex[key] = bi;
                cpu.emplace_back();
                cpu.back().stride = key.color ? kArenaStrideColor
                                              : kArenaStrideNoColor;
                cpu.back().tintRgb[0] = tintRgb[0];
                cpu.back().tintRgb[1] = tintRgb[1];
                cpu.back().tintRgb[2] = tintRgb[2];
                cpu.back().tag =
                    std::string(mesh->name) + " / " +
                    (matIdx >= 0 && size_t(matIdx) < pack.materials.size()
                         ? pack.materials[size_t(matIdx)].name
                         : std::string("(no material)"));
            } else {
                bi = it->second;
            }
            ArenaCpuBucket& b = cpu[bi];
            ++stampCounter;
            for (size_t k = 0; k < count; k += 3) {
                const uint32_t i0 = mesh->indices[first + k];
                const uint32_t i1 = mesh->indices[first + k + 1];
                const uint32_t i2 = mesh->indices[first + k + 2];
                if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
                // Mirrored placements reverse the winding: swap the last two
                // corners so front faces stay outward (see the det check).
                uint32_t corner[3];
                corner[0] = i0;
                corner[1] = mirrored ? i2 : i1;
                corner[2] = mirrored ? i1 : i2;
                for (int q = 0; q < 3; ++q) {
                    const uint32_t vi = corner[q];
                    if (stamp[vi] != stampCounter) {
                        stamp[vi] = stampCounter;
                        remap[vi] = int32_t(b.v.size() / size_t(b.stride));
                        b.v.push_back(wp[vi * 3 + 0]);
                        b.v.push_back(wp[vi * 3 + 1]);
                        b.v.push_back(wp[vi * 3 + 2]);
                        b.v.push_back(wn[vi * 3 + 0]);
                        b.v.push_back(wn[vi * 3 + 1]);
                        b.v.push_back(wn[vi * 3 + 2]);
                        float u = hasUv ? mesh->uvs[vi * 2 + 0] : 0.0f;
                        float v = hasUv ? mesh->uvs[vi * 2 + 1] : 0.0f;
                        if (useFieldUnwrap) {
                            u = wp[vi * 3 + 0] * kFieldUvScale + 0.5f;
                            v = 1.0f - std::fabs(wp[vi * 3 + 1]) * kFieldUvScale;
                        }
                        // UE3 FLightMap2D: LightmapUV = ChartUV * Scale + Bias.
                        // MEASURED: this beats raw UV on StadiumBase (71% vs
                        // 26%), Stadium_Top (95% vs 65%), Goal_Floor, Hex.
                        // Pitch Team floors with empty atlas rects disable LM
                        // above (useLm=false) so they are not blacked.
                        float lu = hasUvLm ? mesh->uvsLm[vi * 2 + 0] : 0.0f;
                        float lv = hasUvLm ? mesh->uvsLm[vi * 2 + 1] : 0.0f;
                        if (useLm) {
                            lu = lu * inst.lightmap.coordScale[0] +
                                 inst.lightmap.coordBias[0];
                            lv = lv * inst.lightmap.coordScale[1] +
                                 inst.lightmap.coordBias[1];
                        }
                        b.v.push_back(u);
                        b.v.push_back(v);
                        b.v.push_back(lu);
                        b.v.push_back(lv);
                        b.v.push_back(wt[vi * 3 + 0]);
                        b.v.push_back(wt[vi * 3 + 1]);
                        b.v.push_back(wt[vi * 3 + 2]);
                        if (key.color) {
                            b.v.push_back(mesh->colors[vi * 3 + 0]);
                            b.v.push_back(mesh->colors[vi * 3 + 1]);
                            b.v.push_back(mesh->colors[vi * 3 + 2]);
                        }
                        for (int c = 0; c < 3; ++c) {
                            const float p = b.v[b.v.size() - size_t(b.stride) + size_t(c)];
                            if (p < b.lo[c]) b.lo[c] = p;
                            if (p > b.hi[c]) b.hi[c] = p;
                        }
                    }
                    b.idx.push_back(uint32_t(remap[vi]));
                }
                ++tris;
            }
            ++slots;
        }
        ++placements;
    }

    // ---- GL upload: the textures first (the buckets reference them) ------
    // 1x1 white for missing albedo / lightmap slots.
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        GLuint wtex = 0;
        glGenTextures(1, &wtex);
        glBindTexture(GL_TEXTURE_2D, wtex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        arenaWhiteTex_ = wtex;
    }
    // The cook's static-mesh stream is packed tangent + packed normal + UVs
    // (stride 8+4*channels). This pack has no `colors` array, so the shader
    // does not multiply a fabricated vertex colour.
    {
        bool anyColor = false;
        for (const ArenaMesh& m : pack.meshes)
            if (!m.colors.empty()) anyColor = true;
        for (const ArenaMesh& m : pack.externalMeshes)
            if (!m.colors.empty()) anyColor = true;
        if (!anyColor) {
            std::fprintf(stderr,
                         "[arena] no vertex colours in this pack (cook stream "
                         "is packed tangent + normal + UVs; not inventing any)\n");
        }
    }
    // Pack-only GLSL program (compiled once; never per frame).
    {
        GLuint vs = ArenaCompileShader(GL_VERTEX_SHADER, kArenaVs);
        GLuint fs = ArenaCompileShader(GL_FRAGMENT_SHADER, kArenaFs);
        if (vs && fs) arenaProg_ = ArenaLinkProgram(vs, fs);
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (arenaProg_) {
            arenaUniDiffuse_ = glGetUniformLocation(arenaProg_, "uDiffuse");
            arenaUniLightmap_ = glGetUniformLocation(arenaProg_, "uLightmap");
            arenaUniNormal_ = glGetUniformLocation(arenaProg_, "uNormal");
            arenaUniTint_ = glGetUniformLocation(arenaProg_, "uTint");
            arenaUniLmScale_ = glGetUniformLocation(arenaProg_, "uLmScale");
            arenaUniEmit_ = glGetUniformLocation(arenaProg_, "uEmit");
            arenaUniUseDiff_ = glGetUniformLocation(arenaProg_, "uUseDiff");
            arenaUniUseLm_ = glGetUniformLocation(arenaProg_, "uUseLm");
            arenaUniUseNrm_ = glGetUniformLocation(arenaProg_, "uUseNrm");
            arenaUniNrmMode_ = glGetUniformLocation(arenaProg_, "uNrmMode");
            arenaUniLmLuma_ = glGetUniformLocation(arenaProg_, "uLmLuma");
            arenaUniAlphaTest_ = glGetUniformLocation(arenaProg_, "uAlphaTest");
            arenaUniUseColor_ = glGetUniformLocation(arenaProg_, "uUseColor");
            std::fprintf(stderr, "[arena] pack GLSL program ready\n");
        } else {
            std::fprintf(stderr,
                         "[arena] pack GLSL unavailable — fixed-function draw\n");
        }
    }
    std::vector<int> texGlId(pack.textures.size(), -1);
    const bool s3tcOk = ArenaDriverHasS3tc();
    bool loggedMissingS3tc = false;
    int compressedDxt1 = 0, compressedDxt5 = 0, rgbaUploads = 0;
    int rewrittenUploads = 0, refusedUploads = 0;
    for (size_t ti = 0; ti < pack.textures.size(); ++ti) {
        const ArenaTexture& t = pack.textures[ti];
        if (!t.valid || t.rgba.empty() || t.width <= 0 || t.height <= 0) continue;
        const uint8_t* px = t.rgba.data();
        std::vector<uint8_t> converted;
        GLenum wrap = t.lightmap ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        // The force-field hex texture is a packed UE3 shader mask, NOT albedo:
        // raw it is black centres + saturated green/blue/red rings (the rainbow
        // in the bug report). The green channel is the hex-line mask (27% of
        // texels at full strength + antialiased edges; blue-only glow and red
        // edges carry G=0). Convert once at upload to a faint ice-blue net on
        // transparent: RGB = veil, A = G scaled to ~31% max, so the blended
        // pass draws thin pale lines and shows the sky through elsewhere.
        // Mips are generated from the converted bytes, so distant minification
        // stays a stable faint mean (G4 precedent).
        if (t.lightmap) {
            // Baked lightmaps: upload as-is (no field-skin / pad remaps).
        } else if (t.name == "Hexagons_Pack_01_T") {
            converted.resize(t.rgba.size());
            for (size_t p = 0; p < t.rgba.size() / 4; ++p) {
                const uint8_t g = t.rgba[p * 4 + 1];
                converted[p * 4 + 0] = 175;
                converted[p * 4 + 1] = 205;
                converted[p * 4 + 2] = 235;
                converted[p * 4 + 3] =
                    (uint8_t)((unsigned)g * 80u / 255u);  // 0..80 (0..0.31)
            }
            px = converted.data();
        } else if (t.name == "FieldSkin_FutureGrid_D") {
            // The cooked field skin is a dark olive (mean G ~130) with the
            // mow checker and markings in the variation. A per-channel gain
            // lifts it to the bright day turf without flattening the checker.
            // Clamp so the layout does not repeat onto the apron.
            converted.resize(t.rgba.size());
            for (size_t p = 0; p < t.rgba.size() / 4; ++p) {
                auto lift = [](unsigned c, unsigned num, unsigned den) -> uint8_t {
                    const unsigned v = c * num / den;
                    return (uint8_t)(v > 255u ? 255u : v);
                };
                converted[p * 4 + 0] = lift(t.rgba[p * 4 + 0], 165, 100);
                converted[p * 4 + 1] = lift(t.rgba[p * 4 + 1], 140, 100);
                converted[p * 4 + 2] = lift(t.rgba[p * 4 + 2], 115, 100);
                converted[p * 4 + 3] = 255;
            }
            px = converted.data();
            wrap = GL_CLAMP_TO_EDGE;
        } else if (t.name.rfind("BoostPad_", 0) == 0) {
            // The cooked "diffuse" is a red/green/black mask. The measured
            // pad colours are gold (BoostPad_Color 1, 0.47, 0.05 and the
            // BoostPad_02 ColorA/B/C family). Red becomes the disc, green
            // the arrow highlight, black the centre.
            converted.resize(t.rgba.size());
            for (size_t p = 0; p < t.rgba.size() / 4; ++p) {
                const unsigned r = t.rgba[p * 4 + 0];
                const unsigned g = t.rgba[p * 4 + 1];
                const unsigned orr = r;
                const unsigned org = r * 45u / 100u + g * 35u / 100u;
                const unsigned orb = r * 8u / 100u;
                converted[p * 4 + 0] = (uint8_t)(orr > 255u ? 255u : orr);
                converted[p * 4 + 1] = (uint8_t)(org > 255u ? 255u : org);
                converted[p * 4 + 2] = (uint8_t)(orb > 255u ? 255u : orb);
                converted[p * 4 + 3] = 255;
            }
            px = converted.data();
        }
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Pixel rewrites (hex mask, field lift, boost-pad gold) are not the
        // stored blocks, so those stay RGBA8. Everything else with a DXT
        // payload is uploaded as S3TC when the driver accepts it.
        const bool rewritten = !converted.empty();
        const GLenum s3tcFormat = ArenaS3tcInternalFormat(t.format);
        bool compressed = false;
        if (rewritten) {
            std::fprintf(stderr,
                         "[arena] texture '%s' stays RGBA8 (pixels rewritten "
                         "at upload)\n",
                         t.name.c_str());
            ++rewrittenUploads;
        } else if (s3tcFormat != 0 && !t.blocks.empty()) {
            char why[96];
            why[0] = '\0';
            if (!s3tcOk) {
                if (!loggedMissingS3tc) {
                    std::fprintf(stderr,
                                 "[arena] GL_EXT_texture_compression_s3tc is "
                                 "absent — DXT textures stay RGBA8\n");
                    loggedMissingS3tc = true;
                }
                std::fprintf(stderr,
                             "[arena] texture '%s' %s refused (S3TC extension "
                             "absent) — RGBA8\n",
                             t.name.c_str(), ArenaPackFormatName(t.format));
                ++refusedUploads;
            } else if ((t.width % 4) != 0 || (t.height % 4) != 0) {
                std::fprintf(stderr,
                             "[arena] texture '%s' %s refused (size %dx%d is "
                             "not a multiple of 4) — RGBA8\n",
                             t.name.c_str(), ArenaPackFormatName(t.format),
                             t.width, t.height);
                ++refusedUploads;
            } else if (ArenaUploadCompressedLevel(t, s3tcFormat, !t.lightmap,
                                                  why, sizeof(why))) {
                compressed = true;
                if (t.format == ArenaTexFormat::kDxt1) ++compressedDxt1;
                else ++compressedDxt5;
            } else {
                std::fprintf(stderr,
                             "[arena] texture '%s' %s refused (%s) — RGBA8\n",
                             t.name.c_str(), ArenaPackFormatName(t.format), why);
                ++refusedUploads;
            }
        } else {
            ++rgbaUploads;
        }
        if (!compressed) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, px);
        }
        // Lightmaps are atlas-packed: mipmaps average across chart borders
        // and paint purple/blue padding onto seams (goal frames, dome edge).
        // Diffuse keeps G4 trilinear; lightmaps stay linear + clamp.
        // A compressed diffuse already has its mip chain.
        if (t.lightmap) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            if (!compressed) glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        glBindTexture(GL_TEXTURE_2D, 0);
        texGlId[ti] = int(tex);
        arenaTextures_.push_back(tex);
    }
    std::fprintf(stderr, "[arena] compressed upload:");
    if (compressedDxt1 > 0) {
        std::fprintf(stderr, " PF_DXT1 x%d as %s", compressedDxt1,
                     ArenaS3tcEnumName(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT));
    }
    if (compressedDxt5 > 0) {
        std::fprintf(stderr, " PF_DXT5 x%d as %s", compressedDxt5,
                     ArenaS3tcEnumName(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT));
    }
    if (compressedDxt1 == 0 && compressedDxt5 == 0)
        std::fprintf(stderr, " none");
    std::fprintf(stderr, " (rgba8=%d rewrite=%d refused=%d)\n", rgbaUploads,
                 rewrittenUploads, refusedUploads);

    // ---- GL upload: the buckets (GL_STATIC_DRAW, uploaded once) -----------
    // The map is ordered by the bucket key, so the draw order is deterministic.
    for (const auto& kv : bucketIndex) {
        const ArenaKey& key = kv.first;
        const ArenaCpuBucket& b = cpu[kv.second];
        if (b.idx.empty()) continue;
        ArenaBucket g;
        g.texId = (key.tex >= 0 && size_t(key.tex) < texGlId.size())
                      ? texGlId[size_t(key.tex)]
                      : -1;
        g.lmTexId = (key.lm >= 0 && size_t(key.lm) < texGlId.size())
                        ? texGlId[size_t(key.lm)]
                        : -1;
        g.nrmTexId = (key.nrm >= 0 && size_t(key.nrm) < texGlId.size())
                         ? texGlId[size_t(key.nrm)]
                         : -1;
        g.nrmMode = g.nrmTexId >= 0 ? key.nrmMode : 0;
        g.lmLuma = key.lmLuma;
        g.emit = (key.tint == 8 || key.tint == 9);
        g.useColor = key.color;
        g.floatsPerVert = b.stride;
        g.lmScale[0] = key.lmScale[0];
        g.lmScale[1] = key.lmScale[1];
        g.lmScale[2] = key.lmScale[2];
        g.tintRgb[0] = b.tintRgb[0];
        g.tintRgb[1] = b.tintRgb[1];
        g.tintRgb[2] = b.tintRgb[2];
        g.unlit = key.unlit;
        g.decal = key.decal;
        g.transparent = key.transparent;
        g.alphaTest = key.alphaTest;
        g.tag = b.tag;
        // Untextured glass has no mask and the pack shader writes alpha 1
        // when there is no diffuse. A 1x1 texel at kArenaGlassAlpha puts
        // that alpha on the existing blend path (kArenaFs unchanged).
        if (key.transparent && g.texId < 0) {
            const auto byte = [](float c) -> uint8_t {
                if (c <= 0.f) return 0;
                if (c >= 1.f) return 255;
                return (uint8_t)(c * 255.f + 0.5f);
            };
            const uint8_t px[4] = {
                byte(b.tintRgb[0]), byte(b.tintRgb[1]), byte(b.tintRgb[2]),
                byte(kArenaGlassAlpha)};
            GLuint veil = 0;
            glGenTextures(1, &veil);
            glBindTexture(GL_TEXTURE_2D, veil);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            arenaTextures_.push_back(veil);
            g.texId = int(veil);
        }
        g.cell[0] = key.cx; g.cell[1] = key.cy; g.cell[2] = key.cz;
        g.idxCount = uint32_t(b.idx.size());
        g.tris = b.idx.size() / 3;
        for (int c = 0; c < 3; ++c) { g.lo[c] = b.lo[c]; g.hi[c] = b.hi[c]; }
        glGenBuffers(1, &g.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, b.v.size() * sizeof(float), b.v.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glGenBuffers(1, &g.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, b.idx.size() * sizeof(uint32_t),
                     b.idx.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        arenaBuckets_.push_back(g);
    }

    arenaStats_.buckets = uint32_t(arenaBuckets_.size());
    arenaStats_.textures = uint32_t(arenaTextures_.size());
    arenaStats_.placements = placements;
    arenaStats_.skippedPlacements = skipped;
    arenaStats_.sectionSlots = slots;
    arenaStats_.tris = tris;
    for (const ArenaBucket& g : arenaBuckets_) {
        if (g.texId >= 0) ++arenaStats_.texturedBuckets;
        else ++arenaStats_.untexturedBuckets;
        if (g.transparent) ++arenaStats_.transparentBuckets;
    }
    arenaReady_ = !arenaBuckets_.empty();
    auto printCards = [](const char* label, std::vector<std::string>& rows) {
        std::sort(rows.begin(), rows.end());
        std::fprintf(stderr, "[arena] %s:", label);
        if (rows.empty()) std::fprintf(stderr, " (none)");
        for (const std::string& row : rows) std::fprintf(stderr, " [%s]", row.c_str());
        std::fprintf(stderr, "\n");
    };
    printCards("sky/cards blended", blendedCards);
    printCards("sky/cards skipped (no blend)", skippedCards);
    std::fprintf(stderr,
                 "[arena] pack uploaded: buckets=%u (textured=%u fallback=%u "
                 "transparent=%u) "
                 "textures=%u placements=%u skipped=%u slots=%u tris=%llu\n",
                 arenaStats_.buckets, arenaStats_.texturedBuckets,
                 arenaStats_.untexturedBuckets, arenaStats_.transparentBuckets,
                 arenaStats_.textures,
                 arenaStats_.placements, arenaStats_.skippedPlacements,
                 arenaStats_.sectionSlots, (unsigned long long)arenaStats_.tris);
    return arenaReady_;
}

void Renderer::DrawArenaPack() {
    // No pack -> no GL call at all (the default path stays byte-identical).
    if (!arenaReady_) return;
    // Day sky behind the bowl. The analytic path clears a night blue; that
    // clear is only replaced when a pack is actually drawn.
    glClearColor(0.73f, 0.82f, 0.90f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GLfloat prevAmb[4];
    glGetFloatv(GL_LIGHT_MODEL_AMBIENT, prevAmb);
    const GLfloat dayAmb[4] = {0.62f, 0.64f, 0.62f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, dayAmb);
    const ArenaFrustum f = ExtractArenaFrustum();
    const GLboolean texWas = glIsEnabled(GL_TEXTURE_2D);
    const GLboolean blendWas = glIsEnabled(GL_BLEND);
    const GLboolean lightWas = glIsEnabled(GL_LIGHTING);
    GLboolean maskWas = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &maskWas);
    const bool useGlsl = arenaProg_ != 0;
    if (useGlsl) {
        glUseProgram(arenaProg_);
        glUniform1i(arenaUniDiffuse_, 0);
        glUniform1i(arenaUniLightmap_, 1);
        glUniform1i(arenaUniNormal_, 2);
        glDisable(GL_LIGHTING);  // shade comes from the lightmap + soft fill
    }
    auto drawBucket = [&](const ArenaBucket& bk, bool transparentPass) {
        if (!ArenaAabbVisible(bk.lo, bk.hi, f)) return false;
        if (bk.decal) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.5f, -2.0f);
        }
        glBindBuffer(GL_ARRAY_BUFFER, bk.vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bk.ibo);
        const GLsizei stride = bk.floatsPerVert * int(sizeof(float));
        if (useGlsl) {
            const GLuint diff = bk.texId >= 0 ? GLuint(bk.texId) : arenaWhiteTex_;
            const GLuint lm =
                bk.lmTexId >= 0 ? GLuint(bk.lmTexId) : arenaWhiteTex_;
            const GLuint nrm =
                bk.nrmTexId >= 0 ? GLuint(bk.nrmTexId) : arenaWhiteTex_;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, diff);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, lm);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, nrm);
            glActiveTexture(GL_TEXTURE0);
            float tint[3] = {bk.tintRgb[0], bk.tintRgb[1], bk.tintRgb[2]};
            if (transparentPass && bk.texId < 0) {
                // keep glass veil colour; alpha handled in blend
            } else if (bk.texId >= 0) {
                tint[0] = tint[1] = tint[2] = 1.0f;
            }
            float emit[3] = {0.f, 0.f, 0.f};
            if (bk.emit) {
                emit[0] = bk.tintRgb[0];
                emit[1] = bk.tintRgb[1];
                emit[2] = bk.tintRgb[2];
            }
            glUniform3f(arenaUniTint_, tint[0], tint[1], tint[2]);
            glUniform3f(arenaUniLmScale_, bk.lmScale[0], bk.lmScale[1],
                        bk.lmScale[2]);
            glUniform3f(arenaUniEmit_, emit[0], emit[1], emit[2]);
            glUniform1i(arenaUniUseDiff_, bk.texId >= 0 ? 1 : 0);
            glUniform1i(arenaUniUseLm_, bk.lmTexId >= 0 ? 1 : 0);
            glUniform1i(arenaUniUseNrm_, bk.nrmTexId >= 0 ? 1 : 0);
            glUniform1i(arenaUniNrmMode_, bk.nrmMode);
            glUniform1i(arenaUniLmLuma_, bk.lmLuma ? 1 : 0);
            glUniform1i(arenaUniAlphaTest_, bk.alphaTest ? 1 : 0);
            glUniform1i(arenaUniUseColor_, bk.useColor ? 1 : 0);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            glEnableVertexAttribArray(3);
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                                  (const void*)(3 * sizeof(float)));
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                                  (const void*)(6 * sizeof(float)));
            glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
                                  (const void*)(8 * sizeof(float)));
            glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride,
                                  (const void*)(10 * sizeof(float)));
            if (bk.useColor) {
                glEnableVertexAttribArray(5);
                glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, stride,
                                      (const void*)(13 * sizeof(float)));
            } else {
                glDisableVertexAttribArray(5);
            }
        } else {
            if (bk.texId >= 0) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, GLuint(bk.texId));
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            } else {
                glDisable(GL_TEXTURE_2D);
                if (transparentPass)
                    glColor4f(bk.tintRgb[0], bk.tintRgb[1], bk.tintRgb[2],
                              kArenaGlassAlpha);
                else
                    glColor4f(bk.tintRgb[0], bk.tintRgb[1], bk.tintRgb[2], 1.0f);
            }
            if (bk.unlit) glDisable(GL_LIGHTING);
            else if (lightWas) glEnable(GL_LIGHTING);
            glVertexPointer(3, GL_FLOAT, stride, (const void*)0);
            glNormalPointer(GL_FLOAT, stride, (const void*)(3 * sizeof(float)));
            glTexCoordPointer(2, GL_FLOAT, stride,
                              (const void*)(6 * sizeof(float)));
        }
        glDrawElements(GL_TRIANGLES, GLsizei(bk.idxCount), GL_UNSIGNED_INT,
                       (const void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        if (bk.decal) glDisable(GL_POLYGON_OFFSET_FILL);
        NoteExternalDraw(bk.tris, 0);
        return true;
    };
    uint32_t visible = 0, culled = 0;
    if (!useGlsl) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    for (const ArenaBucket& bk : arenaBuckets_) {
        if (bk.transparent) continue;
        if (drawBucket(bk, false)) ++visible; else ++culled;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const ArenaBucket& bk : arenaBuckets_) {
        if (!bk.transparent) continue;
        if (drawBucket(bk, true)) ++visible; else ++culled;
    }
    if (useGlsl) {
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
        glDisableVertexAttribArray(3);
        glDisableVertexAttribArray(4);
        glDisableVertexAttribArray(5);
        glUseProgram(0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    } else {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }
    glDepthMask(maskWas ? GL_TRUE : GL_FALSE);
    if (texWas) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    if (blendWas) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (lightWas) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, prevAmb);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    arenaStats_.visible = visible;
    arenaStats_.culled = culled;
}

void Renderer::ShutdownArenaPack() {
    for (ArenaBucket& bk : arenaBuckets_) {
        if (bk.vbo) glDeleteBuffers(1, &bk.vbo);
        if (bk.ibo) glDeleteBuffers(1, &bk.ibo);
        bk.vbo = bk.ibo = 0;
    }
    arenaBuckets_.clear();
    for (unsigned& t : arenaTextures_)
        if (t) glDeleteTextures(1, &t);
    arenaTextures_.clear();
    if (arenaWhiteTex_) {
        glDeleteTextures(1, &arenaWhiteTex_);
        arenaWhiteTex_ = 0;
    }
    if (arenaProg_) {
        glDeleteProgram(arenaProg_);
        arenaProg_ = 0;
    }
    arenaUniDiffuse_ = arenaUniLightmap_ = arenaUniNormal_ = arenaUniTint_ = -1;
    arenaUniEmit_ = -1;
    arenaUniUseDiff_ = arenaUniUseLm_ = arenaUniUseNrm_ = arenaUniNrmMode_ = -1;
    arenaUniLmLuma_ = arenaUniAlphaTest_ = arenaUniUseColor_ = -1;
    arenaReady_ = false;
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
