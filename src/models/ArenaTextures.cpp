// ArenaTextures.cpp — procedural arena surface textures.
#include "ArenaTextures.h"

#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rl {

// Sheet constants live in the header's texturegen namespace so tests and draw
// sites share one definition. Pull them in unqualified for the code below.
using namespace texturegen;

namespace {

// ---------------------------------------------------------------------------
// Small helpers. The ball texture (Models.cpp) established this style: build a
// byte buffer, wrap-safe index it, upload once.
// ---------------------------------------------------------------------------

inline int Wrap(int v, int n) { return ((v % n) + n) % n; }

// Set pixel (x,y) to an RGB triple, wrapping both axes (tileable sheets).
inline void Put(unsigned char* img, int w, int h, int x, int y,
                float r, float g, float b) {
    unsigned char* p = img + (Wrap(y, h) * w + Wrap(x, w)) * 3;
    p[0] = (unsigned char)((r < 0 ? 0 : r > 1 ? 1 : r) * 255.0f);
    p[1] = (unsigned char)((g < 0 ? 0 : g > 1 ? 1 : g) * 255.0f);
    p[2] = (unsigned char)((b < 0 ? 0 : b > 1 ? 1 : b) * 255.0f);
}

// Deterministic value noise in [0,1). No rand(), no time — a hash of the
// integer lattice, so the texture is bit-identical on every machine and every
// run (the same discipline the sim follows).
inline float Hash2(int x, int y) {
    unsigned h = (unsigned)(x * 374761393) + (unsigned)(y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFF) / 65536.0f;
}

// Bilinear value noise with smoothstep interpolation. Two octaves are layered
// by callers; this is the single-octave primitive.
inline float ValueNoise(float x, float y) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - (float)xi, yf = y - (float)yi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);
    float a = Hash2(xi, yi), b = Hash2(xi + 1, yi);
    float c = Hash2(xi, yi + 1), d = Hash2(xi + 1, yi + 1);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

// Cached GL_TEXTURE_2D. The mip chain is built once here, not in the frame loop.
extern "C" void glGenerateMipmap(unsigned int target); // resolved from libGL
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

// Bound texture: mip chain, trilinear min, bilinear mag. Anisotropy is capped at 4.
void FinishUploadChains() {
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const char* exts = (const char*)glGetString(GL_EXTENSIONS);
    bool hasAniso =
        exts && std::strstr(exts, "GL_EXT_texture_filter_anisotropic");
    float chosen = 1.0f, maxAniso = 1.0f;
    if (hasAniso) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        const float kAnisoCap = 4.0f;
        chosen = maxAniso < kAnisoCap ? maxAniso : kAnisoCap;
        if (chosen < 1.0f) chosen = 1.0f;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, chosen);
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        std::fprintf(stderr, "[tex] trilinear mips, aniso %s "
                             "(max=%.1f, chosen=%.1f)\n",
                     hasAniso ? "on" : "off", maxAniso, chosen);
    }
}

unsigned UploadRGB(const unsigned char* img, int w, int h, bool repeat) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 img);
    GLint wrap = repeat ? GL_REPEAT : GL_CLAMP;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    FinishUploadChains();  // wrap preserved; filters upgraded to chained
    return tex;
}

// RL_TEXDBG dumps what GL actually holds, so a blank/garbled upload is visible
// without guessing. Same lever as the ball texture; writes to /tmp.
void DumpIfRequested(unsigned tex, const char* path, int w, int h) {
    if (!std::getenv("RL_TEXDBG")) return;
    static unsigned char back[1024 * 1024 * 3];
    if ((size_t)w * h * 3 > sizeof(back)) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, back);
    FILE* df = std::fopen(path, "wb");
    if (!df) return;
    std::fprintf(df, "P6\n%d %d\n255\n", w, h);
    std::fwrite(back, 1, (size_t)w * h * 3, df);
    std::fclose(df);
}

// ---------------------------------------------------------------------------
// Turf sheet.
//
// One sheet = one full arena floor footprint (8192 x 10240 uu) mapped 1:1 in
// UV, so the mowed bands land at the real 640 uu pitch (16 bands across the
// 10240 uu Y extent) and the grain is scaled to read as grass, not as noise.
// ---------------------------------------------------------------------------

unsigned g_turf = 0;

void BuildTurfTexture() {
    static unsigned char img[kTurfW * kTurfH * 3];
    texturegen::FillTurf(img);
    g_turf = UploadRGB(img, kTurfW, kTurfH, true);
    DumpIfRequested(g_turf, "/tmp/turftex_dump.ppm", kTurfW, kTurfH);
}

// ---------------------------------------------------------------------------
// Turf markings overlay.
//
// RGBA sheet, transparent everywhere except the painted lines: the midfield
// line across Y=0 and the centre circle r=600 uu. Drawn as a single quad on
// the floor, lifted a hair in Z so it never z-fights the turf quad.
// ---------------------------------------------------------------------------

unsigned g_mark = 0;

void BuildTurfMarkingsTexture() {
    static unsigned char img[kMarkW * kMarkH * 4];
    texturegen::FillTurfMarkings(img);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kMarkW, kMarkH, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    FinishUploadChains(); // clamp wrap stays; this adds the mip chain
    g_mark = tex;
}

// ---------------------------------------------------------------------------
// Hex-net glass sheet.
//
// 420 uu cell. UV repeat is span/2520 so the net lands on that pitch.
// ---------------------------------------------------------------------------

unsigned g_hex = 0;
unsigned g_glassPlain = 0;

void BuildHexNetTexture() {
    static unsigned char img[kHexW * kHexH * 3];
    texturegen::FillHexNet(img);
    g_hex = UploadRGB(img, kHexW, kHexH, true);
    DumpIfRequested(g_hex, "/tmp/hexnettex_dump.ppm", kHexW, kHexH);
}

void BuildPlainGlassTexture() {
    static unsigned char img[kHexW * kHexH * 3];
    texturegen::FillPlainGlass(img);
    g_glassPlain = UploadRGB(img, kHexW, kHexH, true);
}

// ---------------------------------------------------------------------------
// Goal interior sheet: dark net weave. Kept neutral-dark; the team tint is
// applied by the draw site's glColor so the same sheet serves both goals.
// ---------------------------------------------------------------------------

unsigned g_goal = 0;

void BuildGoalInteriorTexture() {
    static unsigned char img[kGoalW * kGoalH * 3];
    texturegen::FillGoalInterior(img);
    g_goal = UploadRGB(img, kGoalW, kGoalH, true);
}

// ---------------------------------------------------------------------------
// Ceiling sheet: base panel with seam lines matching the beam lattice.
// ---------------------------------------------------------------------------

unsigned g_ceil = 0;
unsigned g_base = 0;

void BuildCeilingTexture() {
    static unsigned char img[kCeilW * kCeilH * 3];
    texturegen::FillCeiling(img);
    g_ceil = UploadRGB(img, kCeilW, kCeilH, true);
}

void BuildWallBaseTexture() {
    static unsigned char img[kBaseW * kBaseH * 3];
    texturegen::FillWallBase(img);
    g_base = UploadRGB(img, kBaseW, kBaseH, true);
    DumpIfRequested(g_base, "/tmp/wallbasetex_dump.ppm", kBaseW, kBaseH);
}

} // namespace

// ---------------------------------------------------------------------------
// Pure generation layer. GL-free so a headless test can assert the content.
// The GL builders above call exactly these functions, so asserting here is
// asserting on the uploaded bytes.
// ---------------------------------------------------------------------------
namespace texturegen {

void FillTurf(unsigned char* img) {
    // Two greens for the 16 mowed stripes.
    const float darkR = 0.11f, darkG = 0.52f, darkB = 0.09f;
    const float liteR = 0.14f, liteG = 0.60f, liteB = 0.11f;
    for (int y = 0; y < kTurfH; y++) {
        // 16 mowed bands down V. The band boundary is a hard edge in RL, so no
        // smoothstep here — only the grain modulates within a band.
        int band = y * 16 / kTurfH;
        bool dark = (band % 2) == 0;
        float br = dark ? darkR : liteR;
        float bg = dark ? darkG : liteG;
        float bb = dark ? darkB : liteB;
        // Side-of-field wash. v=0 is world -Y (blue goal), v=1 is +Y (orange),
        // matching FillTurfMarkings. UNVERIFIED: no measured DFH bytes. A few
        // percent keeps VG14 green dominance and the mow-band luma window.
        const float side = ((float)y + 0.5f) / (float)kTurfH * 2.0f - 1.0f;
        br *= 1.0f + 0.06f * side;
        bb *= 1.0f - 0.08f * side;
        for (int x = 0; x < kTurfW; x++) {
            // Two octaves of value noise, low amplitude.
            float n = ValueNoise(x * 0.28f, y * 0.28f) * 0.6f +
                      ValueNoise(x * 0.95f, y * 0.95f) * 0.4f;
            float k = (n - 0.5f) * 0.16f;  // +/-0.08 relative brightness
            // A gentle hue shift as well: grass reads greener in the lit parts.
            Put(img, kTurfW, kTurfH, x, y,
                br * (1.0f + k), bg * (1.0f + k * 0.55f), bb * (1.0f + k * 0.75f));
        }
    }
    // Mow-line seams: a faint darker line at each band boundary makes the 640 uu
    // pitch legible even where the two greens are close in luma.
    for (int band = 0; band <= 16; band++) {
        int y = band * kTurfH / 16;
        for (int x = 0; x < kTurfW; x++)
            Put(img, kTurfW, kTurfH, x, y % kTurfH, 0.085f, 0.40f, 0.075f);
    }
}

void FillTurfMarkings(unsigned char* img) {
    for (int i = 0; i < kMarkW * kMarkH; i++) {
        img[i * 4 + 0] = 235; img[i * 4 + 1] = 235; img[i * 4 + 2] = 235;
        img[i * 4 + 3] = 0;  // fully transparent by default
    }
    // The sheet maps the same 8192 x 10240 uu footprint as the turf: u along X
    // (+/-4096), v along Y (+/-5120).
    const float px = kTurfSpanX / kMarkW;   // uu per texel in X
    const float py = kTurfSpanY / kMarkH;   // uu per texel in Y
    // Midfield line: a 24 uu band around Y = 0.
    {
        int y0 = (int)std::floor((kTurfSpanY * 0.5f - 12.0f) / py);
        int y1 = (int)std::ceil((kTurfSpanY * 0.5f + 12.0f) / py);
        for (int y = y0; y <= y1; y++) {
            if (y < 0 || y >= kMarkH) continue;
            for (int x = 0; x < kMarkW; x++) {
                unsigned char* p = img + (y * kMarkW + x) * 4;
                p[0] = 235; p[1] = 235; p[2] = 235; p[3] = 235;
            }
        }
    }
    // Centre circle, r = 600 uu, stroke ~26 uu.
    {
        const float rIn = 600.0f - 13.0f, rOut = 600.0f + 13.0f;
        for (int y = 0; y < kMarkH; y++) {
            float wy = (float)y * py - kTurfSpanY * 0.5f;
            for (int x = 0; x < kMarkW; x++) {
                float wx = (float)x * px - kTurfSpanX * 0.5f;
                float d = std::sqrt(wx * wx + wy * wy);
                if (d < rIn || d > rOut) continue;
                unsigned char* p = img + (y * kMarkW + x) * 4;
                p[0] = 235; p[1] = 235; p[2] = 235; p[3] = 235;
            }
        }
    }
}

void FillPlainGlass(unsigned char* img) {
    // Same base as the net sheet, with no net.
    const float baseR = 0.07f, baseG = 0.27f, baseB = 0.95f;
    for (int y = 0; y < kHexH; y++) {
        for (int x = 0; x < kHexW; x++) {
            float t = (float)y / kHexH;
            float k = 1.0f - 0.16f * t;
            Put(img, kHexW, kHexH, x, y, baseR * k, baseG * k, baseB * k);
        }
    }
}

void FillHexNet(unsigned char* img) {
    // ---- geometry --------------------------------------------------------
    // Pointy-top honeycomb (the standard hex tiling):
    //   vertices at 30, 90, ... deg   <-- the 30 deg phase is REQUIRED
    //   width  (horizontal, flat-to-flat) = sqrt(3)*R
    //   height (vertical,   vertex-to-vertex) = 2*R
    // With vertices at 0/60/... instead, neighbouring hexagons share no
    // vertices at all and the "net" degenerates into a zigzag of stray bars.
    // Rows repeat every 1.5*R and every other row is shifted half a period.
    //
    // The sheet spans exactly kHexCols horizontal periods by kHexRows row
    // pitches, so both edges meet the opposite edge in the SAME phase and the
    // GL_REPEAT wrap is seamless. Texels-per-uu is derived from that span, not
    // assumed, so the net lands at the true cell size on every wall.
    // Periods in TEXELS are taken straight from the sheet dimensions rather
    // than recomputed through uu. Those divisions are exact by construction
    // (kHexSpanX/Y were derived from the same counts), which is what makes the
    // GL_REPEAT seam land perfectly; going via uu reintroduces rounding and
    // leaves a sub-texel gap that shows up as a broken lattice at the seam.
    const float hPeriodX = (float)kHexW / (float)kHexCols;
    const float rowPitchY = (float)kHexH / (float)kHexRows;
    // Circumradius in texels. Derived from the horizontal period so the
    // hexagons keep their true proportions: hPeriod = sqrt(3)*R.
    const float Rx = hPeriodX / std::sqrt(3.0f);
    const float kPhase = (float)M_PI / 6.0f;            // 30 deg (see above)

    // Same base as the net-free sheet. Draw sites multiply white, so this is the albedo.
    const float baseR = 0.07f, baseG = 0.27f, baseB = 0.95f;
    for (int y = 0; y < kHexH; y++) {
        for (int x = 0; x < kHexW; x++) {
            // Subtle vertical falloff so the sheet is not perfectly flat —
            // glass reads slightly lighter near the top of a panel.
            float t = (float)y / kHexH;
            float k = 1.0f - 0.16f * t;
            Put(img, kHexW, kHexH, x, y, baseR * k, baseG * k, baseB * k);
        }
    }

    // lineW is the half-width in texels. Alternate columns at 0.82 so a half-period seam
    // does not match the full 363.73 uu period.
    const float lineW = 0.7f;
    const float seamR = 0.90f, seamG = 0.95f, seamB = 1.00f;
    const float dimF = 0.82f;

    // Draw one hexagon outline centred at (cx,cy). Seams are clipped to the
    // sheet by Put()'s wrap, and because the sheet holds whole periods the
    // wrapped pieces land exactly where the tiled neighbour's copy would.
    auto hexOutline = [&](float cx, float cy) {
        // Parity from the drawn centre so wrapped copies keep their own
        // (deterministic) brightness; the 7-period sheet flips parity across
        // repeats, which reads as larger-scale panel variation, not a seam.
        int parity = ((int)std::floor(cx / hPeriodX + 0.5f)) & 1;
        float f = (parity == 0) ? 1.0f : dimF;
        float sr = seamR * f, sg = seamG * f, sb = seamB * f;
        for (int e = 0; e < 6; e++) {
            float a0 = kPhase + (float)M_PI / 3.0f * e;
            float a1 = kPhase + (float)M_PI / 3.0f * (e + 1);
            float x0 = cx + std::cos(a0) * Rx, y0 = cy + std::sin(a0) * Rx;
            float x1 = cx + std::cos(a1) * Rx, y1 = cy + std::sin(a1) * Rx;
            int steps = (int)Rx + 2;
            for (int s = 0; s <= steps; s++) {
                float tt = (float)s / steps;
                float pxx = x0 + (x1 - x0) * tt;
                float pyy = y0 + (y1 - y0) * tt;
                for (int dy = -(int)lineW - 1; dy <= (int)lineW + 1; dy++)
                    for (int dx = -(int)lineW - 1; dx <= (int)lineW + 1; dx++) {
                        if ((float)(dx * dx + dy * dy) > lineW * lineW) continue;
                        Put(img, kHexW, kHexH, (int)(pxx + 0.5f) + dx,
                            (int)(pyy + 0.5f) + dy, sr, sg, sb);
                    }
            }
        }
    };

    // One hexagon per row, kHexRows rows, each row offset half a period from the
    // last. Draw one ring of extra centres around the sheet so outlines that
    // overhang an edge are painted on the opposite edge too (their wrapped part
    // is the tiled neighbour's seam, so the lattice stays continuous).
    for (int row = -1; row <= kHexRows; row++) {
        float cy = row * rowPitchY;
        float cxOff = (row & 1) ? hPeriodX * 0.5f : 0.0f;
        for (int col = -1; col <= kHexCols; col++) {
            float cx = col * hPeriodX + cxOff;
            hexOutline(cx, cy);
        }
    }
}

void FillGoalInterior(unsigned char* img) {
    // Dark backdrop: keep the base well under full brightness so the interior
    // reads as a shadowed net box, not a lit panel. The team tint is multiplied
    // on top at draw time, so the base must leave headroom.
    for (int y = 0; y < kGoalH; y++)
        for (int x = 0; x < kGoalW; x++) {
            float n = ValueNoise(x * 0.11f, y * 0.11f);
            float k = 0.42f + 0.20f * n;   // 0.42 .. 0.62
            Put(img, kGoalW, kGoalH, x, y, k, k, k);
        }
    // Net weave: a fine lattice every 16 texels. The weave is LIGHTER than the
    // base (so the net reads as lit cords) but still dark overall — this sheet
    // is modulated by the team tint at draw time, so a near-white weave would
    // blow the goal interior out to full brightness. 0.80 sits just under the
    // "stays dark" bound while giving the lattice real contrast.
    for (int y = 0; y < kGoalH; y++)
        for (int x = 0; x < kGoalW; x++) {
            if ((x % 16) != 0 && (y % 16) != 0) continue;
            Put(img, kGoalW, kGoalH, x, y, 0.80f, 0.80f, 0.80f);
        }
}

void FillCeiling(unsigned char* img) {
    // Draw sites multiply this sheet by white.
    const float baseR = 0.17f, baseG = 0.22f, baseB = 0.31f;
    for (int y = 0; y < kCeilH; y++)
        for (int x = 0; x < kCeilW; x++) {
            float n = ValueNoise(x * 0.06f, y * 0.06f);
            float k = 0.88f + 0.24f * n;
            Put(img, kCeilW, kCeilH, x, y, baseR * k, baseG * k, baseB * k);
        }
    // Panel seams every 64 texels: a brighter line so the panel grid reads.
    for (int y = 0; y < kCeilH; y++)
        for (int x = 0; x < kCeilW; x++) {
            if ((x % 64) != 0 && (y % 64) != 0) continue;
            Put(img, kCeilW, kCeilH, x, y, 0.30f, 0.37f, 0.49f);
        }
}

void FillWallBase(unsigned char* img) {
    // v=0 is the floor contact, v=1 meets the glass. One sheet is one quarter arc.
    for (int y = 0; y < kBaseH; y++) {
        float t = (float)y / (kBaseH - 1);   // 0 floor contact .. 1 glass line
        float br = 0.155f + 0.085f * t;
        float bg = 0.225f + 0.095f * t;
        float bb = 0.305f + 0.105f * t;
        for (int x = 0; x < kBaseW; x++) {
            float n = ValueNoise(x * 0.09f, y * 0.09f) * 0.65f +
                      ValueNoise(x * 0.41f, y * 0.41f) * 0.35f;
            float k = 1.0f + (n - 0.5f) * 0.22f;
            // Scuff darkening near the floor contact where the ball/car wear
            // the paint: strongest at v=0, gone by a third of the arc.
            float scuff = 1.0f - 0.16f * (1.0f - t) * (1.0f - t) * (1.0f - t);
            Put(img, kBaseW, kBaseH, x, y,
                br * k * scuff, bg * k * scuff, bb * k * scuff);
        }
    }
    // Formwork lines: two faint horizontal seams across the arc (constant-v
    // rows), slightly darker than the local gradient.
    for (int r = 1; r <= 2; r++) {
        int y = r * kBaseH / 3;
        for (int x = 0; x < kBaseW; x++) {
            const unsigned char* p = img + (y * kBaseW + x) * 3;
            float rr = p[0] / 255.0f * 0.82f;
            float gg = p[1] / 255.0f * 0.82f;
            float bb = p[2] / 255.0f * 0.82f;
            Put(img, kBaseW, kBaseH, x, y, rr, gg, bb);
        }
    }
}

} // namespace texturegen

unsigned TurfTexture() {
    if (!g_turf) BuildTurfTexture();
    return g_turf;
}

unsigned TurfMarkingsTexture() {
    if (!g_mark) BuildTurfMarkingsTexture();
    return g_mark;
}

unsigned HexNetTexture() {
    if (!g_hex) BuildHexNetTexture();
    return g_hex;
}

unsigned PlainGlassTexture() {
    if (!g_glassPlain) BuildPlainGlassTexture();
    return g_glassPlain;
}

unsigned GoalInteriorTexture() {
    if (!g_goal) BuildGoalInteriorTexture();
    return g_goal;
}

unsigned CeilingTexture() {
    if (!g_ceil) BuildCeilingTexture();
    return g_ceil;
}

unsigned WallBaseTexture() {
    if (!g_base) BuildWallBaseTexture();
    return g_base;
}

void FreeArenaTextures() {
    GLuint ids[7] = {g_turf, g_mark, g_hex, g_glassPlain, g_goal, g_ceil,
                     g_base};
    for (unsigned id : ids)
        if (id) {
            GLuint t = id;
            glDeleteTextures(1, &t);
        }
    g_turf = g_mark = g_hex = g_glassPlain = g_goal = g_ceil = g_base = 0;
}

} // namespace rl
