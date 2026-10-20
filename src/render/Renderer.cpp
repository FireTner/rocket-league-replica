// Renderer.cpp — SDL2 + OpenGL 4.5 Compatibility fixed-function renderer.
// GRAPHICS45 §2.4: GL 4.5-compat hard-target (fixed-function keeps running,
// VBOs/FBOs/GLSL 4.50-compat first-class). No fallback, no core-only code.
#include "Renderer.h"
#include "Font.h"
#include "models/ArenaShell.h"
#include "render/PbrGl.h"
#include "settings/Settings.h"
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
