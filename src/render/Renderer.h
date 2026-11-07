// Renderer.h — SDL2 window and fixed-function GL draws (uu, z-up).
#pragma once

#include <SDL2/SDL.h>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "models/ArenaRip.h"

namespace rl {

// ---- tolerant INI reader (A6b) ---------------------------------------------
// Reads ONE key out of ONE section of an INI file. Startup still resolves
// `[arena] arena_dir` here (flag > ini). `ArenaSettings::arenaDir` stores the
// same key so `Settings::Save` writes it back.
//
// Rules (a subset of docs/INI_SCHEMA.md's grammar): `;` or `#` starts a
// comment, `[section]` headers, `key = value` pairs; section and key names
// are case-insensitive; the value is trimmed; the FIRST matching key wins.
// Returns false (and leaves `*out` untouched) when the file, the section or
// the key is missing — never a silent default.
inline bool ReadIniKey(const std::string& path, const std::string& section,
                       const std::string& key, std::string* out) {
    std::ifstream f(path);
    if (!f) return false;
    auto trim = [](std::string s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    };
    auto lower = [](std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        return s;
    };
    const std::string wantSection = lower(section);
    const std::string wantKey = lower(key);
    std::string line;
    bool inSection = false;
    while (std::getline(f, line)) {
        // Strip a trailing comment (";" or "#" after whitespace).
        size_t cut = line.find_first_of(";#");
        if (cut != std::string::npos) line = line.substr(0, cut);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[') {
            size_t close = line.find(']');
            if (close == std::string::npos) continue;
            inSection = (lower(trim(line.substr(1, close - 1))) == wantSection);
            continue;
        }
        if (!inSection) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (lower(trim(line.substr(0, eq))) != wantKey) continue;
        if (out) *out = trim(line.substr(eq + 1));
        return true;
    }
    return false;
}

// One frame of sim state for drawing. Units are uu, z-up.
struct Snapshot {
    float carPos[3] = {0, 0, 40};  // uu, z-up (RL convention). Octane rest z~40.
    float carYaw = 0.0f;           // radians, 0 = +X (2D heading convenience)
    float carSpeed = 0.0f;         // uu/s, for trail/FX gating
    // Full car orientation (unit vectors, world, z-up). carYaw alone drops pitch and roll.
    float carForward[3] = {1, 0, 0};
    float carUp[3] = {0, 0, 1};
    float carVel[3] = {0, 0, 0};   // uu/s world velocity (wheel spin + steer)
    float ballPos[3] = {0, 0, 100};
    // Ball spin. angVel is physics rad/s (world). ballRot is that rate
    // integrated in sim time (column-major 3x3, local +X/+Y/+Z in world).
    // Identity until the ball has spun. Filled from Sim::GetBallSpin.
    // The panel texture is a separate draw and is not this matrix.
    float ballAngVel[3] = {0, 0, 0};
    float ballRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float ballSpeed = 0.0f;        // uu/s, length of GetBall velocity
    bool ballTouched = false;      // Sim::GetLastTouch valid
    int ballTeam = 0;              // Sim::kBlue / kOrange when ballTouched
    float boost = 50.0f;           // 0..100
    bool boosting = false;
    // RocketSim's flag, not a local speed check. On at 2200 uu/s, held to 2100.
    bool supersonic = false;
    bool handbrake = false;  // powerslide; drift smoke
    bool airborne = false;   // no ground contact (z + vz heuristic, render-only)
    // RocketSim CarState::HasFlipReset, filled from Sim::HasFlipReset.
    // True only for an unused wheel-granted flip. A jump dodge window is false.
    bool flipReset = false;
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

    // 1280x720 window. Returns false on failure.
    bool Init(int w = 1280, int h = 720, bool vsync = true);
    void Shutdown();

    // Per-frame. BeginFrame clears; EndFrame presents (vsync) + pacing sample.
    // If sim has no fresh snapshot, callers may skip both (repeat last
    // presented frame) — renderer imposes no wait on physics.
    void BeginFrame(float r = 0.05f, float g = 0.06f, float b = 0.10f);
    void EndFrame();

    // Immediate-mode draws. Coordinates are uu, z-up.
    void DrawBox(float cx, float cy, float cz,
                 float sx, float sy, float sz, float yaw,
                 float r, float g, float b, float a = 1.0f);
    void DrawSphere(float cx, float cy, float cz, float radius,
                    int segs, float r, float g, float b, float a = 1.0f);
    // Flat ground grid (arena floor), extent in uu.
    void DrawGroundGrid(float extent = 9000.0f, float step = 500.0f);

    // Unlit wireframe for --debug-colliders. Does not write shaded geometry.
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

    // uRep/vRep are repeats across the quad (surface_uu / sheet_span_uu), GL_REPEAT.

    // Floor-facing textured quad in the XY plane at height z, centred (cx,cy),
    // size sx by sy, rotated by yaw (radians, z-up). Normal is +Z by default;
    // pass faceDown=true for a ceiling panel so its LIT face points down into
    // the arena (the side the camera actually sees from below).
    void DrawTexturedQuadXZ(float cx, float cy, float z, float sx, float sy,
                            float yaw, unsigned tex,
                            float uRep, float vRep,
                            float r = 1, float g = 1, float b = 1,
                            float a = 1.0f, bool faceDown = false);

    // Vertical textured quad in the wall's local frame: centred (cx,cy,cz),
    // `sx` wide along the yaw direction and `sy` tall along +Z. Normal is the
    // quad's outward face (local +Y rotated by yaw), matching the arena's wall
    // convention (local +x along the wall, local +y outward).
    // `uOff`/`vOff` shift the lattice phase (in whole repeats): a chamfer
    // panel continues its neighbour's hex phase across the junction by
    // starting at the neighbour's end phase instead of 0.
    void DrawTexturedQuadWall(float cx, float cy, float cz, float sx, float sy,
                              float yaw, unsigned tex,
                              float uRep, float vRep,
                              float r = 1, float g = 1, float b = 1,
                              float a = 1.0f, float uOff = 0.0f,
                              float vOff = 0.0f);

    // Same as DrawTexturedQuadXZ but alpha-blended as an overlay sheet (used
    // for the turf markings layer, which must not occlude what it sits on).
    void DrawTexturedOverlayXZ(float cx, float cy, float z, float sx, float sy,
                               float yaw, unsigned tex,
                               float uRep, float vRep,
                               float r = 1, float g = 1, float b = 1,
                               float a = 1.0f);

    // ---- arena bucket VBOs (G3b, GRAPHICS45 §5/G3 step 2) ------------------
    // Build-once static buckets (src/models/ArenaShell.*) replacing the
    // per-frame arena immediate calls. NOT called from the frame yet — G3c
    // owns the wire-up + deletion of the old immediate calls in main.cpp.
    // meshMode selects the mesh vs fallback VBO set (G3c passes
    // sim->usingRealSoccar()); noHexNet swaps the glass bind only.
    // DrawArenaBuckets emits draws 1..7 ((a+d)->turf->wallbase->ceiling->
    // skirt->B2->glass); DrawArenaGoalInteriors emits draws 8..9 (C5+C6,
    // depth-write OFF) and must stay AFTER cars/ball in the frame.
    void DrawArenaBuckets(bool meshMode, bool noHexNet, float wallOpacity);
    void DrawArenaGoalInteriors(bool meshMode);

    // Ripped arena, uploaded once: one VBO+EBO per texture and 8192 uu cell.
    // Frustum-culled. No pack means no extra GL objects.
    struct ArenaPackStats {
        uint32_t buckets = 0;            // buckets built
        uint32_t texturedBuckets = 0;    // buckets bound to a diffuse texture
        uint32_t untexturedBuckets = 0;  // buckets on the neutral fallback
        uint32_t transparentBuckets = 0; // force-field/glass buckets (blended pass)
        uint32_t textures = 0;           // GL textures uploaded
        uint32_t placements = 0;         // placements baked into buckets
        uint32_t skippedPlacements = 0;  // placements dropped (see the log)
        uint32_t sectionSlots = 0;       // section slots baked
        uint64_t tris = 0;               // triangles in the whole scene
        uint32_t visible = 0;            // buckets drawn in the last frame
        uint32_t culled = 0;             // buckets culled in the last frame
    };

    // Upload the pack's geometry + textures into GL objects. Needs a live GL
    // context (call it after Init). Returns false when the pack holds no
    // drawable geometry. Nothing is uploaded again afterwards.
    // `packDir` is the pack directory (--arenadir). A missing
    // material_overlays.json there changes nothing.
    bool BuildArenaPack(const ArenaPack& pack, const std::string& packDir = {});
    // Draw the visible buckets of the ripped arena (one draw call each).
    // No-op — and no GL call at all — when no pack was built.
    void DrawArenaPack();
    bool HasArenaPack() const { return arenaReady_; }
    const ArenaPackStats& ArenaPackInfo() const { return arenaStats_; }
    // Release the pack's GL objects (the GL context must still be live).
    void ShutdownArenaPack();

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
    // PPM framebuffer dump.
    bool SaveScreenshot(const char* path) const;

    // Single-light control (default: one dim directional light on).
    void SetLight(bool on) { lightOn_ = on; }
    bool LightingOn() const { return lightOn_; }

    bool IsOpen() const { return window_ != nullptr; }
    SDL_Window* Window() { return window_; }

    // Trailing-window frame times, ms. Stderr every 5 s.
    // render_pacing.log is appended only when pacingLog_ is set.
    struct Pacing { double p50 = 0, p99 = 0, avg = 0; uint64_t frames = 0; };
    Pacing Stats() const;

    // Last-frame tris, lines, and draws, plus totals since Init.
    // glErrors is polled once per EndFrame.
    struct Counters {
        uint64_t tris = 0;         // triangles submitted (last frame)
        uint64_t lines = 0;        // line primitives submitted (last frame)
        uint64_t drawCalls = 0;    // GL draw calls (last frame)
        uint64_t trisTotal = 0;    // cumulative
        uint64_t drawsTotal = 0;   // cumulative
        uint64_t glErrors = 0;     // cumulative GL errors observed
        uint64_t frames = 0;       // frames counted
        // Last-frame CPU ms, BeginFrame through EndFrame, including the swap.
        double frame_ms = 0;
        // Eye-candy cost. PBR fills ms; draws and tris stay 0.
        struct FlagCost { double ms = 0.0; uint64_t draws = 0; uint64_t tris = 0; };
        FlagCost flagCost;
    };
    Counters Stats3D() const { return counters_; }

    // Geometry submitted outside Renderer draw calls (Fennec glDrawElements).
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
    // vsync_ is the Init request. maxFps_ 0 means uncapped.
    bool vsync_ = true;
    int maxFps_ = 0;       // 0 = off (shipped behavior); else clamped 15..1000
    bool pacingLog_ = false; // append render_pacing.log only when set
    // [render] pbr, read once at Init. Off: BeginFrame adds no GL calls.
    bool pbr_ = false;
    bool packSpec_ = false;
    double pbrApplyMs_ = 0.0;

    static const int kHist = 600;
    double hist_[kHist] = {0};
    int histN_ = 0, histI_ = 0;
    double frameStart_ = 0;
    uint64_t frames_ = 0;
    uint64_t lastLogMs_ = 0;
    mutable Pacing cached_;

    // frame* reset each BeginFrame.
    uint64_t frameTris_ = 0, frameLines_ = 0, frameDraws_ = 0;
    Counters counters_;

    struct ArenaBucket {
        unsigned vbo = 0, ibo = 0;
        uint32_t idxCount = 0;
        uint64_t tris = 0;
        int texId = -1;      // -1 = a tinted fallback material (see tintRgb)
        int lmTexId = -1;    // -1 = no baked lightmap (shader uses white)
        int nrmTexId = -1;   // -1 = no normal map (geometric normal only)
        int nrmMode = 0;     // 1 = RGB tangent normal, 2 = XY + reconstructed Z
        float lmScale[3] = {1.f, 1.f, 1.f};  // DirectionalMax ScaleVectors
        float tintRgb[3] = {0.55f, 0.55f, 0.55f};
        bool unlit = false;  // emissive / fake-lit sections ignore scene lighting
        bool decal = false;  // field markings: polygon offset so they sit on the turf
        bool transparent = false;  // force-field/glass: blended pass, no depth write
        bool alphaTest = false;    // tree cards / masked foliage
        bool lmLuma = false;       // this bucket's lightmap stays greyscale
        bool emit = false;         // add tintRgb as an emissive term
        bool nearVeil = false;     // untextured glass: thin the alpha up close
        bool nearEdge = false;     // goal trim: fade the lightmap shell up close
        bool useColor = false;     // mesh stored vertex colours
        int floatsPerVert = 14;    // +3 when useColor
        bool clipThrough = false;  // field rim: pitch-facing side only
        int maskTexId = -1;         // alpha mask; -1 = off
        float emissive[3] = {0.f, 0.f, 0.f};
        int blend = 0;              // 0 off, 1 veil alpha, 2 mask alpha
        bool tintTex = false;       // multiply a bound albedo by tintRgb
        bool emissiveOn = false;
        std::string tag;   // first (mesh, material) baked here (diagnostics)
        int cell[3] = {0, 0, 0};
        float lo[3] = {0, 0, 0};  // world AABB (culling)
        float hi[3] = {0, 0, 0};
    };
    std::vector<ArenaBucket> arenaBuckets_;
    std::vector<unsigned> arenaTextures_;
    unsigned arenaWhiteTex_ = 0;   // 1x1 white for missing albedo/lightmap
    unsigned arenaProg_ = 0;       // pack-only GLSL ≤ 4.50 program
    int arenaUniDiffuse_ = -1;
    int arenaUniLightmap_ = -1;
    int arenaUniNormal_ = -1;
    int arenaUniTint_ = -1;
    int arenaUniLmScale_ = -1;
    int arenaUniEmit_ = -1;
    int arenaUniUseDiff_ = -1;
    int arenaUniUseLm_ = -1;
    int arenaUniUseNrm_ = -1;
    int arenaUniNrmMode_ = -1;
    int arenaUniLmLuma_ = -1;
    int arenaUniAlphaTest_ = -1;
    int arenaUniUseColor_ = -1;
    int arenaUniNearVeil_ = -1;
    int arenaUniGlowFade_ = -1;
    int arenaUniMask_ = -1;
    int arenaUniEmissive_ = -1;
    int arenaUniUseNormal_ = -1;
    int arenaUniUseMask_ = -1;
    int arenaUniUseEmissive_ = -1;
    int arenaUniUseSpec_ = -1;
    int arenaUniBlend_ = -1;
    int arenaUniTintTex_ = -1;
    ArenaPackStats arenaStats_;
    bool arenaReady_ = false;
};

} // namespace rl
