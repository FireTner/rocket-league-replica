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
void Renderer::DrawArenaBuckets(bool meshMode, bool noHexNet) {
    ArenaShell::DrawArenaBuckets(*this, meshMode, noHexNet);
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
// Vertex layout: pos(3) normal(3) uv(2) = 8 floats.
constexpr int kArenaStride = 8;

// Bucket key: (texture id, spatial cell). The pack's materials carry no
// colour, only a diffuse texture, so the TEXTURE is the material id for
// rendering purposes; -1 is the neutral fallback bucket.
struct ArenaKey {
    int tex = -1;
    int tint = 0;  // fallback colour category when tex == -1 (0 = neutral)
    int cx = 0, cy = 0, cz = 0;
    bool operator<(const ArenaKey& o) const {
        if (tex != o.tex) return tex < o.tex;
        if (tint != o.tint) return tint < o.tint;
        if (cx != o.cx) return cx < o.cx;
        if (cy != o.cy) return cy < o.cy;
        return cz < o.cz;
    }
};

struct ArenaCpuBucket {
    std::vector<float> v;       // kArenaStride floats per vertex
    std::vector<uint32_t> idx;  // 3 per triangle
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    float tintRgb[3] = {0.55f, 0.55f, 0.55f};  // fallback colour (tex == -1)
    std::string tag;  // first (mesh, material) baked here (diagnostics)
};

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
    std::map<std::string, size_t> dupCounter;

    // Untextured materials are NOT one grey: the pack's untextured majority
    // spans white field lines, green reflective floor panels, dark trim and
    // pale force-field glass. Tint the fallback bucket by material name so
    // the lines read as lines and the floor reads as turf.
    auto fallbackTint = [](const std::string& name, float rgb[3]) -> int {
        std::string n = name;
        for (char& c : n) c = char(std::tolower((unsigned char)c));
        auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
        // Emissive fixtures first: TrimLight/glow names also contain "trim",
        // which would otherwise land them in the dark-metal bucket. In game
        // these are additive glow strips (center rings, side trim, goal and
        // stadium lights); drawn shaded-dark they read as extrusion ridges.
        // Tint 5 renders unlit near-white (see DrawArenaPack).
        if (has("trimlight") || has("glow") || has("emiss") || has("light")) {
            rgb[0] = 1.0f; rgb[1] = 0.97f; rgb[2] = 0.90f; return 5;
        }
        if (has("line") || has("paint") || has("white") || has("stripe")) {
            rgb[0] = 0.85f; rgb[1] = 0.85f; rgb[2] = 0.85f; return 1;
        }
        if (has("grass") || has("turf") || has("field") || has("floor") ||
            has("reflect") || has("pitch") || has("groomed")) {
            rgb[0] = 0.28f; rgb[1] = 0.36f; rgb[2] = 0.26f; return 2;
        }
        if (has("frame") || has("trim") || has("grate") || has("dark") ||
            has("cross") || has("beam") || has("steel") || has("metal") ||
            has("collision")) {
            rgb[0] = 0.18f; rgb[1] = 0.18f; rgb[2] = 0.20f; return 3;
        }
        if (has("glass") || has("force") || has("cage") || has("hex") ||
            has("energy")) {
            rgb[0] = 0.45f; rgb[1] = 0.55f; rgb[2] = 0.60f; return 4;
        }
        rgb[0] = kArenaFallback[0]; rgb[1] = kArenaFallback[1];
        rgb[2] = kArenaFallback[2];
        return 0;
    };

    // The 451 placements of the package itself. The 26 IMPORTED placements
    // (externalInstances) are deliberately NOT drawn: none of them carries a
    // diffuse (the pack records 0 resolved diffuses for them) and they are
    // the sky sphere / billboard sprites of the sibling package, which the
    // neutral fallback material would render as a grey dome around the
    // camera. They are counted and reported, never silently dropped.
    for (const ArenaInstance& inst : pack.instances) {
        // Invisible gameplay helpers: decal blockers (full-field invisible
        // planes), particle/FX actors and warmup effects are never visible
        // geometry. Drawing them would bury the field under opaque planes.
        if (inst.owner == "DecalBlockingActor_TA" || inst.owner == "FXActor_TA" ||
            inst.owner == "BoostPill_WarmUp_FXActor") {
            ++skipped;
            continue;
        }
        // Volumetric light shafts, fog cards and glow sprites (light-cone
        // meshes, fog materials, sprite cards) are translucent/additive FX in
        // game; drawn opaque they become giant slabs crossing the field (the
        // dark X bands over midfield). Skip them, never silently: counted.
        if (inst.mesh.rfind("Sprite", 0) == 0 ||
            inst.mesh.rfind("FogMesh", 0) == 0 ||
            inst.mesh.rfind("FieldSpotLights", 0) == 0) {
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
        const bool hasUv = mesh->uvs.size() >= nv * 2;
        // Transform the mesh into world space once (positions + normals).
        std::vector<float> wp(nv * 3), wn(nv * 3);
        double lo[3] = {1e30, 1e30, 1e30}, hi[3] = {-1e30, -1e30, -1e30};
        for (size_t v = 0; v < nv; ++v) {
            const double x = mesh->positions[v * 3 + 0];
            const double y = mesh->positions[v * 3 + 1];
            const double z = mesh->positions[v * 3 + 2];
            const double px = inst.matrix[0] * x + inst.matrix[4] * y +
                              inst.matrix[8] * z + inst.matrix[12];
            const double py = inst.matrix[1] * x + inst.matrix[5] * y +
                              inst.matrix[9] * z + inst.matrix[13];
            const double pz = inst.matrix[2] * x + inst.matrix[6] * y +
                              inst.matrix[10] * z + inst.matrix[14];
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
            float tintRgb[3] = {kArenaFallback[0], kArenaFallback[1],
                                kArenaFallback[2]};
            int tint = 0;
            if (matIdx >= 0 && size_t(matIdx) < pack.materials.size()) {
                const std::string& matName =
                    pack.materials[size_t(matIdx)].name;
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
                // Light-shaft / fog sections that survive the placement
                // filter (shared meshes): same skip, same counting.
                if (matName.find("LightCone") != std::string::npos ||
                    matName.find("StadiumFog") != std::string::npos) {
                    ++skipped;
                    continue;
                }
                texId = pack.materials[size_t(matIdx)].texture;
                if (texId < 0)
                    tint = fallbackTint(matName, tintRgb);
            }

            ArenaKey key;
            key.tex = texId;
            key.tint = (texId < 0) ? tint : 0;
            key.cx = cell[0]; key.cy = cell[1]; key.cz = cell[2];
            size_t bi;
            auto it = bucketIndex.find(key);
            if (it == bucketIndex.end()) {
                bi = cpu.size();
                bucketIndex[key] = bi;
                cpu.emplace_back();
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
                        remap[vi] = int32_t(b.v.size() / kArenaStride);
                        b.v.push_back(wp[vi * 3 + 0]);
                        b.v.push_back(wp[vi * 3 + 1]);
                        b.v.push_back(wp[vi * 3 + 2]);
                        b.v.push_back(wn[vi * 3 + 0]);
                        b.v.push_back(wn[vi * 3 + 1]);
                        b.v.push_back(wn[vi * 3 + 2]);
                        b.v.push_back(hasUv ? mesh->uvs[vi * 2 + 0] : 0.0f);
                        b.v.push_back(hasUv ? mesh->uvs[vi * 2 + 1] : 0.0f);
                        for (int c = 0; c < 3; ++c) {
                            const float p = b.v[b.v.size() - kArenaStride + size_t(c)];
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
    std::vector<int> texGlId(pack.textures.size(), -1);
    for (size_t ti = 0; ti < pack.textures.size(); ++ti) {
        const ArenaTexture& t = pack.textures[ti];
        if (!t.valid || t.rgba.empty() || t.width <= 0 || t.height <= 0) continue;
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, t.rgba.data());
        // G4 precedent: build the mip chain ONCE at upload, trilinear min.
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
        texGlId[ti] = int(tex);
        arenaTextures_.push_back(tex);
    }

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
        g.tintRgb[0] = b.tintRgb[0];
        g.tintRgb[1] = b.tintRgb[1];
        g.tintRgb[2] = b.tintRgb[2];
        g.unlit = (key.tint == 5);  // trim-light glow ignores scene lighting
        g.tag = b.tag;
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
    }
    arenaReady_ = !arenaBuckets_.empty();
    std::fprintf(stderr,
                 "[arena] pack uploaded: buckets=%u (textured=%u fallback=%u) "
                 "textures=%u placements=%u skipped=%u slots=%u tris=%llu\n",
                 arenaStats_.buckets, arenaStats_.texturedBuckets,
                 arenaStats_.untexturedBuckets, arenaStats_.textures,
                 arenaStats_.placements, arenaStats_.skippedPlacements,
                 arenaStats_.sectionSlots, (unsigned long long)arenaStats_.tris);
    return arenaReady_;
}

void Renderer::DrawArenaPack() {
    // No pack -> no GL call at all (the default path stays byte-identical).
    if (!arenaReady_) return;
    const ArenaFrustum f = ExtractArenaFrustum();
    const GLsizei stride = kArenaStride * int(sizeof(float));
    const GLboolean texWas = glIsEnabled(GL_TEXTURE_2D);
    const GLboolean blendWas = glIsEnabled(GL_BLEND);
    const GLboolean lightWas = glIsEnabled(GL_LIGHTING);
    // The pack's DXT5 diffuses (grass) carry a near-zero alpha channel that
    // is NOT opacity (UE3 mask); with the frame's default SRC_ALPHA blending
    // the floor went fully transparent and only the grey behind showed.
    // The arena is opaque: draw it unblended so texture alpha is ignored.
    glDisable(GL_BLEND);
    uint32_t visible = 0, culled = 0;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    for (const ArenaBucket& bk : arenaBuckets_) {
        if (!ArenaAabbVisible(bk.lo, bk.hi, f)) { ++culled; continue; }
        if (bk.texId >= 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, GLuint(bk.texId));
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(bk.tintRgb[0], bk.tintRgb[1], bk.tintRgb[2], 1.0f);
        }
        // Emissive trim-light buckets render unlit (they ARE the lights);
        // shaded they would go dark on their shadow side and read as ridges.
        if (bk.unlit) glDisable(GL_LIGHTING);
        else if (lightWas) glEnable(GL_LIGHTING);
        glBindBuffer(GL_ARRAY_BUFFER, bk.vbo);
        glVertexPointer(3, GL_FLOAT, stride, (const void*)0);
        glNormalPointer(GL_FLOAT, stride, (const void*)(3 * sizeof(float)));
        glTexCoordPointer(2, GL_FLOAT, stride, (const void*)(6 * sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bk.ibo);
        glDrawElements(GL_TRIANGLES, GLsizei(bk.idxCount), GL_UNSIGNED_INT,
                       (const void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        NoteExternalDraw(bk.tris, 0);
        ++visible;
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (texWas) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    if (blendWas) glEnable(GL_BLEND);
    if (lightWas) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
    glBindTexture(GL_TEXTURE_2D, 0);
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
