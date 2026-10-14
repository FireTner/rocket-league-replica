// Renderer.h — A4 RENDERER: SDL2 + OpenGL 2.1 minimal renderer.
// Ownership: A4. Others: consume via Snapshot + DrawBox/DrawSphere only.
//
// Physics-first policy: the renderer NEVER stalls the sim. If a frame
// overruns, we repeat (re-present) the previous frame / skip submission;
// physics keeps stepping at 120Hz regardless. See BUILD_NOTES.md.
#pragma once

#include <SDL2/SDL.h>
#include <cstdint>

namespace rl {

// Plain snapshot of sim state. Defined HERE (A4-owned) so we don't block
// on A1's sim headers. A1/A3 can fill it field-by-field each tick.
struct Snapshot {
    float carPos[3] = {0, 0, 40};  // uu, z-up (RL convention). Octane rest z~40.
    float carYaw = 0.0f;           // radians, 0 = +X (2D heading convenience)
    float carSpeed = 0.0f;         // uu/s, for trail/FX gating
    // Full car orientation (unit vectors, world space, z-up). Filled from the
    // sim's existing read-only GetCarForward/GetCarUp — no new sim coupling.
    // carYaw alone loses pitch/roll, which made car orientation invisible.
    float carForward[3] = {1, 0, 0};
    float carUp[3] = {0, 0, 1};
    float carVel[3] = {0, 0, 0};   // uu/s world velocity (wheel spin + steer)
    float ballPos[3] = {0, 0, 100};
    float boost = 50.0f;           // 0..100
    bool boosting = false;
    bool handbrake = false;  // powerslide/drift smoke gate (from input)
    bool airborne = false;   // no ground contact (z + vz heuristic, render-only)
    float angSpeed = 0.0f;   // rad/s of fwd/up frame change (flip/stall streaks)
    bool ballCam = true;
    bool demoed = false;           // demo respawn hook for FX
    bool goalScored = false;       // goal explosion hook
    uint32_t tick = 0;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Init a 1280x720 vsync GL2.1 window. Returns false on failure.
    bool Init(int w = 1280, int h = 720, bool vsync = true);
    void Shutdown();

    // Per-frame. BeginFrame clears; EndFrame presents (vsync) + pacing sample.
    // If sim has no fresh snapshot, callers may skip both (repeat last
    // presented frame) — renderer imposes no wait on physics.
    void BeginFrame(float r = 0.05f, float g = 0.06f, float b = 0.10f);
    void EndFrame();

    // Minimal immediate-mode API (fixed-function GL2.1, flat/unlit or
    // single directional light — see SetLight). All coords in uu, z-up.
    void DrawBox(float cx, float cy, float cz,
                 float sx, float sy, float sz, float yaw,
                 float r, float g, float b, float a = 1.0f);
    void DrawSphere(float cx, float cy, float cz, float radius,
                    int segs, float r, float g, float b, float a = 1.0f);
    // Flat ground grid (arena floor), extent in uu.
    void DrawGroundGrid(float extent = 9000.0f, float step = 500.0f);

    // Debug instrumentation (W6): unlit wireframe box / sphere / cylinder.
    // Used by --debug-colliders to make collision proxies legible. Wireframe
    // only — never writes depth-shaded geometry.
    void DrawWireBox(float cx, float cy, float cz, float sx, float sy, float sz,
                     float r, float g, float b, float a = 1.0f);
    void DrawWireSphere(float cx, float cy, float cz, float radius, int segs,
                        float r, float g, float b, float a = 1.0f);
    void DrawWireCylinder(float cx, float cy, float cz, float radius, float height,
                          int segs, float r, float g, float b, float a = 1.0f);

    // Floor-facing filled disc / annulus (z-up, world space). Used by the
    // ball ground indicator + pad halos. Disc z lifted by caller.
    void DrawDisc(float cx, float cy, float cz, float radius, int segs,
                  float r, float g, float b, float a = 1.0f);
    void DrawRing(float cx, float cy, float cz, float rIn, float rOut, int segs,
                  float r, float g, float b, float a = 1.0f);

    // Hexagon net (line grid) across a vertical wall panel: local frame
    // centered (cx,cy,cz), width along the yaw direction, height along +z.
    // Used for the glass wall pattern; alpha-blended lines.
    void DrawHexNet(float cx, float cy, float cz, float halfW, float halfH,
                    float yaw, float cell, float r, float g, float b, float a);

    // Screen-space 2D (ortho, like FillRect2D): filled disc / annular arc
    // segments. Angles in degrees, 0 = +x (right), CCW positive; y grows
    // DOWNWARD in screen space so pass angles accordingly. Used by the
    // RL-style circular boost gauge.
    void DrawDisc2D(float cx, float cy, float radius, int segs,
                    float r, float g, float b, float a = 1.0f);
    void DrawRing2D(float cx, float cy, float rIn, float rOut, int segs,
                    float r, float g, float b, float a = 1.0f);
    void DrawArc2D(float cx, float cy, float rIn, float rOut,
                   float a0Deg, float a1Deg, int segs,
                   float r, float g, float b, float a = 1.0f);
    void DrawLine2D(float x0, float y0, float x1, float y1, float thickness,
                    float r, float g, float b, float a = 1.0f);

    // Screen-space bitmap text (5x7 font, Font.h). px/py = top-left in
    // pixels, scale = pixel size multiplier. For menu/HUD overlay; call
    // between BeginFrame/EndFrame (saves/restores matrices + attribs).
    void DrawText2D(int px, int py, const char* text, int scale = 2,
                    float r = 1.0f, float g = 1.0f, float b = 1.0f);
    // Fill rect in screen space (menu panels/HUD bars).
    void FillRect2D(int px, int py, int w, int h,
                    float r, float g, float b, float a = 0.75f);
    // glReadPixels framebuffer dump (P6 PPM) for headless verification.
    bool SaveScreenshot(const char* path) const;

    // Single-light control (default: one dim directional light on).
    void SetLight(bool on) { lightOn_ = on; }
    bool LightingOn() const { return lightOn_; }

    bool IsOpen() const { return window_ != nullptr; }
    SDL_Window* Window() { return window_; }

    // Frame-pacing stats (ms) over trailing window. Logged every 5 s to
    // stderr + render_pacing.log. Never blocks.
    struct Pacing { double p50 = 0, p99 = 0, avg = 0; uint64_t frames = 0; };
    Pacing Stats() const;

    // ---- draw-budget counters (MASTER_PROMPT_VISUALS.md W1/V2) -------------
    // MEASURED geometry cost, not an on-paper estimate. tris/lines/drawCalls
    // are the LAST submitted frame; *Total are cumulative since Init();
    // glErrors counts every error ever returned by glGetError() (polled once
    // per frame in EndFrame — never per draw, so it cannot stall the loop).
    // Renderer-owned; read by the coordinator's HUD/soak log line and asserted
    // by the VG7 visual gate.
    struct Counters {
        uint64_t tris = 0;         // triangles submitted (last frame)
        uint64_t lines = 0;        // line primitives submitted (last frame)
        uint64_t drawCalls = 0;    // GL draw calls (last frame)
        uint64_t trisTotal = 0;    // cumulative
        uint64_t drawsTotal = 0;   // cumulative
        uint64_t glErrors = 0;     // cumulative GL errors observed
        uint64_t frames = 0;       // frames counted
    };
    Counters Stats3D() const { return counters_; }

    // Account for geometry submitted outside the Renderer's own draw calls
    // (the Fennec VBO path owns its glDrawElements). Without this the budget
    // would silently miss the single largest mesh in the scene.
    void NoteExternalDraw(uint64_t tris, uint64_t lines = 0) {
        CountTris(tris, lines);
    }

private:
    void LogPacingIfDue();
    double NowMs() const;
    void CountTris(uint64_t tris, uint64_t lines = 0) {
        frameTris_ += tris; frameLines_ += lines; frameDraws_ += 1;
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext ctx_ = nullptr;
    int w_ = 1280, h_ = 720;
    bool lightOn_ = true;

    static const int kHist = 600;
    double hist_[kHist] = {0};
    int histN_ = 0, histI_ = 0;
    double frameStart_ = 0;
    uint64_t frames_ = 0;
    uint64_t lastLogMs_ = 0;
    mutable Pacing cached_;

    // Draw-budget counters (see Stats3D). frame* reset each BeginFrame.
    uint64_t frameTris_ = 0, frameLines_ = 0, frameDraws_ = 0;
    Counters counters_;
};

} // namespace rl
