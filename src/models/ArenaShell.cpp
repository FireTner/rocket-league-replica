// ArenaShell.cpp — G3b arena draw-bucket VBOs (GRAPHICS45 §5/G3 step 2).
//
// Bake of the per-frame arena immediate calls in src/game/main.cpp (READ-ONLY
// this step; main.cpp is G3c's to rewire) into build-once GL_STATIC_DRAW
// buckets. Draw pattern is the Fennec precedent (Fennec.cpp:152-186,221-229):
// fixed-function client arrays + glDrawElements, no VAOs/shaders.
//
// Bit-for-bit rules (docs/ARENA_BUCKETS.md §§4-6):
// - Same vertex ORDER, same UVs/colors/normals, same state sandwiches as the
//   immediate path: face-down ceiling normal -Z (Renderer.cpp:499-504), glass
//   quadYaw inward + corner pi-rotation, crown radial normals, weave yaw per
//   goal, GL_NORMALIZE stays ON (baked normals are unit-length, so it is
//   idempotent), arc-length UVs (spanX=2546.1147/spanY=2520/kHexCell=420),
//   negative-U corner REPEAT, float32 bake chain, quad diagonal
//   (v0,v1,v2)+(v0,v2,v3). DrawSphere strips are converted with the TRUE strip
//   triangulation (same vertex sets the driver rasterizes).
// - Box normals: the immediate path scales the unit cube non-uniformly, then
//   GL_NORMALIZE renormalizes — baked yaw-rotated face normals equal that
//   result exactly (rotation of a unit axis normal is unit).
// - DrawBox/textured helpers never touch blend/lighting/texture-enable beyond
//   TextureScope; the buckets replicate those sandwiches and nothing more.
// - Counters: NoteExternalDraw(bakedTris) per merged draw. The count used is
//   what was BAKED (== what is drawn), so VG7 tris_last cannot shift by
//   construction. The DrawBall/wheel uncounted-tris gap is NOT touched here.
// - Mode: mesh vs fallback selected by meshMode (G3c passes
//   sim->usingRealSoccar()); buffer sets are separate, never mixed.
//
// DEVIATION vs the G3a plan (docs/ARENA_BUCKETS.md §1), found while baking:
// C1 is 8 glassSub quads = 16 tris, but the plan table says 24 tris / 8 draws
// (impossible: 8 quads x 2 tris = 16; verified by counting the 8 glassSub
// call sites at main.cpp:1237-1254). So baked mesh arena = 2122 tris (plan:
// 2130), fallback = 1562 (plan-implied: 1570); the glass draw holds 472 mesh
// / 216 fallback tris (plan: 480/224). Draw counts (9 GL draws) are exactly
// as planned. The build log below prints baked-vs-plan so G3c/VG7 can cite it.
#include "models/ArenaShell.h"

#include "models/ArenaTextures.h"
#include "render/Renderer.h"

#define GL_GLEXT_PROTOTYPES 1 // VBO entry points (GL 1.5 core) in Mesa's gl.h
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace rl {
namespace ArenaShell {
namespace {

// ---- CPU bucket -----------------------------------------------------------
// Interleaved vertex: pos(3) normal(3) color(4) uv(2) = 12 floats.
constexpr int kStrideFloats = 12;

struct Bucket {
    std::vector<float> v;      // kStrideFloats per vertex
    std::vector<uint32_t> idx; // 3 per triangle
    uint32_t quad(float x0, float y0, float z0, float nx0, float ny0, float nz0,
                  float r0, float g0, float b0, float a0, float u0, float v0,
                  float x1, float y1, float z1, float nx1, float ny1, float nz1,
                  float r1, float g1, float b1, float a1, float u1, float v1,
                  float x2, float y2, float z2, float nx2, float ny2, float nz2,
                  float r2, float g2, float b2, float a2, float u2, float v2,
                  float x3, float y3, float z3, float nx3, float ny3, float nz3,
                  float r3, float g3, float b3, float a3, float u3, float v3) {
        uint32_t b = (uint32_t)(v.size() / kStrideFloats);
        const float vs[4][12] = {
            {x0, y0, z0, nx0, ny0, nz0, r0, g0, b0, a0, u0, v0},
            {x1, y1, z1, nx1, ny1, nz1, r1, g1, b1, a1, u1, v1},
            {x2, y2, z2, nx2, ny2, nz2, r2, g2, b2, a2, u2, v2},
            {x3, y3, z3, nx3, ny3, nz3, r3, g3, b3, a3, u3, v3},
        };
        for (int k = 0; k < 4; k++)
            v.insert(v.end(), vs[k], vs[k] + kStrideFloats);
        // Plan §5 diagonal: (v0,v1,v2)+(v0,v2,v3) — the GL_QUADS convention.
        idx.push_back(b); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b); idx.push_back(b + 2); idx.push_back(b + 3);
        return b;
    }
    void tri(uint32_t a, uint32_t b, uint32_t c) {
        idx.push_back(a); idx.push_back(b); idx.push_back(c);
    }
    uint64_t tris() const { return idx.size() / 3; }
};

// Rigid frame bake: world = T(cx,cy,cz) * Rz(yawRad) * local. Matches the
// glTranslatef + glRotatef(yawDeg) sandwiches (runFrame, cornerGlass outer,
// skirtChamfer outer, and the yaw-radians helpers in Renderer).
struct Frame {
    float cx = 0, cy = 0, cz = 0, c = 1, s = 0;
    static Frame at(float x, float y, float z, float yawRad) {
        Frame f;
        f.cx = x; f.cy = y; f.cz = z;
        f.c = cosf(yawRad); f.s = sinf(yawRad);
        return f;
    }
    static Frame deg(float x, float y, float z, float yawDeg) {
        return at(x, y, z, yawDeg * (float)M_PI / 180.0f);
    }
    void pos(float lx, float ly, float lz, float& wx, float& wy, float& wz) const {
        wx = cx + c * lx - s * ly;
        wy = cy + s * lx + c * ly;
        wz = cz + lz;
    }
    void nrm(float lx, float ly, float lz, float& wx, float& wy, float& wz) const {
        wx = c * lx - s * ly;
        wy = s * lx + c * ly;
        wz = lz;
    }
};

// DrawBox bake (Renderer.cpp:230-258): unit cube scaled by (sx,sy,sz), yaw in
// RADIANS (Renderer converts to degrees for glRotatef), flat color, uv 0.
// Normals are the yaw-rotated face axes (== what GL_NORMALIZE yields after
// the immediate path's non-uniform scale — exact, see file header).
void emitBox(Bucket& bk, const Frame& f, float sx, float sy, float sz,
             float r, float g, float b, float a) {
    const float h = 0.5f;
    // Faces in Renderer::DrawBox order: +z -z +x -x +y -y.
    const float fv[6][4][3] = {
        {{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}},
        {{-h,-h,-h},{-h,h,-h},{h,h,-h},{h,-h,-h}},
        {{h,-h,-h},{h,h,-h},{h,h,h},{h,-h,h}},
        {{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}},
        {{-h,h,-h},{-h,h,h},{h,h,h},{h,h,-h}},
        {{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}},
    };
    const float fn[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    for (int q = 0; q < 6; q++) {
        float w[4][3], n[3];
        f.nrm(fn[q][0], fn[q][1], fn[q][2], n[0], n[1], n[2]);
        for (int k = 0; k < 4; k++)
            f.pos(fv[q][k][0] * sx, fv[q][k][1] * sy, fv[q][k][2] * sz,
                  w[k][0], w[k][1], w[k][2]);
        bk.quad(w[0][0], w[0][1], w[0][2], n[0], n[1], n[2], r, g, b, a, 0, 0,
                w[1][0], w[1][1], w[1][2], n[0], n[1], n[2], r, g, b, a, 0, 0,
                w[2][0], w[2][1], w[2][2], n[0], n[1], n[2], r, g, b, a, 0, 0,
                w[3][0], w[3][1], w[3][2], n[0], n[1], n[2], r, g, b, a, 0, 0);
    }
}

// DrawTexturedQuadXZ bake (Renderer.cpp:488-513). yaw in RADIANS.
void emitQuadXZ(Bucket& bk, const Frame& f, float sx, float sy,
                float uRep, float vRep, float r, float g, float b, float a,
                bool faceDown) {
    const float hx = sx * 0.5f, hy = sy * 0.5f;
    const float nz = faceDown ? -1.0f : 1.0f;
    float w[4][3], n[3];
    f.nrm(0, 0, nz, n[0], n[1], n[2]);
    f.pos(-hx, -hy, 0, w[0][0], w[0][1], w[0][2]);
    f.pos( hx, -hy, 0, w[1][0], w[1][1], w[1][2]);
    f.pos( hx,  hy, 0, w[2][0], w[2][1], w[2][2]);
    f.pos(-hx,  hy, 0, w[3][0], w[3][1], w[3][2]);
    bk.quad(w[0][0], w[0][1], w[0][2], n[0], n[1], n[2], r, g, b, a, 0, 0,
            w[1][0], w[1][1], w[1][2], n[0], n[1], n[2], r, g, b, a, uRep, 0,
            w[2][0], w[2][1], w[2][2], n[0], n[1], n[2], r, g, b, a, uRep, vRep,
            w[3][0], w[3][1], w[3][2], n[0], n[1], n[2], r, g, b, a, 0, vRep);
}

// DrawTexturedQuadWall bake (Renderer.cpp:515-537). yaw in RADIANS.
void emitQuadWall(Bucket& bk, const Frame& f, float sx, float sy,
                  float uRep, float vRep, float r, float g, float b, float a,
                  float uOff, float vOff) {
    const float hx = sx * 0.5f, hz = sy * 0.5f;
    float w[4][3], n[3];
    f.nrm(0, 1, 0, n[0], n[1], n[2]);
    f.pos(-hx, 0, -hz, w[0][0], w[0][1], w[0][2]);
    f.pos( hx, 0, -hz, w[1][0], w[1][1], w[1][2]);
    f.pos( hx, 0,  hz, w[2][0], w[2][1], w[2][2]);
    f.pos(-hx, 0,  hz, w[3][0], w[3][1], w[3][2]);
    bk.quad(w[0][0], w[0][1], w[0][2], n[0], n[1], n[2], r, g, b, a, uOff, vOff,
            w[1][0], w[1][1], w[1][2], n[0], n[1], n[2], r, g, b, a,
            uOff + uRep, vOff,
            w[2][0], w[2][1], w[2][2], n[0], n[1], n[2], r, g, b, a,
            uOff + uRep, vOff + vRep,
            w[3][0], w[3][1], w[3][2], n[0], n[1], n[2], r, g, b, a, uOff,
            vOff + vRep);
}

// arcSweep bake (main.cpp:917-1006): quarter-round strip in the runFrame-local
// Y-Z plane, AS=12 quads + optional 12-tri cap fans, sinf/cosf float32 chain.
void emitArcSweep(Bucket& bk, const Frame& f, float tangentLen, float radius,
                  bool ceilingCourse, float u0, float u1, float v0, float v1,
                  float r, float g, float b, float a,
                  bool capStart, bool capEnd) {
    const int AS = 12;
    const float kHalfPi = (float)M_PI / 2.0f;
    const float hx = tangentLen * 0.5f;
    for (int k = 0; k < AS; k++) {
        float t0 = (float)k / AS * kHalfPi;
        float t1 = (float)(k + 1) / AS * kHalfPi;
        float y0, z0, ny0, nz0, y1, z1, ny1, nz1;
        if (!ceilingCourse) {
            y0 = -radius + radius * sinf(t0);
            z0 = radius - radius * cosf(t0);
            ny0 = -sinf(t0); nz0 = cosf(t0);
            y1 = -radius + radius * sinf(t1);
            z1 = radius - radius * cosf(t1);
            ny1 = -sinf(t1); nz1 = cosf(t1);
        } else {
            y0 = -radius + radius * cosf(t0);
            z0 = -radius + radius * sinf(t0);
            ny0 = -cosf(t0); nz0 = -sinf(t0);
            y1 = -radius + radius * cosf(t1);
            z1 = -radius + radius * sinf(t1);
            ny1 = -cosf(t1); nz1 = -sinf(t1);
        }
        float vv0 = v0 + (v1 - v0) * t0 / kHalfPi;
        float vv1 = v0 + (v1 - v0) * t1 / kHalfPi;
        float w[4][3], n0[3], n1[3];
        f.nrm(0, ny0, nz0, n0[0], n0[1], n0[2]);
        f.nrm(0, ny1, nz1, n1[0], n1[1], n1[2]);
        f.pos(-hx, y0, z0, w[0][0], w[0][1], w[0][2]);
        f.pos( hx, y0, z0, w[1][0], w[1][1], w[1][2]);
        f.pos( hx, y1, z1, w[2][0], w[2][1], w[2][2]);
        f.pos(-hx, y1, z1, w[3][0], w[3][1], w[3][2]);
        bk.quad(w[0][0], w[0][1], w[0][2], n0[0], n0[1], n0[2], r, g, b, a, u0, vv0,
                w[1][0], w[1][1], w[1][2], n0[0], n0[1], n0[2], r, g, b, a, u1, vv0,
                w[2][0], w[2][1], w[2][2], n1[0], n1[1], n1[2], r, g, b, a, u1, vv1,
                w[3][0], w[3][1], w[3][2], n1[0], n1[1], n1[2], r, g, b, a, u0, vv1);
    }
    if (capStart || capEnd) {
        float acy = -radius, acz = ceilingCourse ? -radius : radius;
        for (int k = 0; k < AS; k++) {
            float t0 = (float)k / AS * kHalfPi;
            float t1 = (float)(k + 1) / AS * kHalfPi;
            float y0, z0, y1, z1;
            if (!ceilingCourse) {
                y0 = -radius + radius * sinf(t0);
                z0 = radius - radius * cosf(t0);
                y1 = -radius + radius * sinf(t1);
                z1 = radius - radius * cosf(t1);
            } else {
                y0 = -radius + radius * cosf(t0);
                z0 = -radius + radius * sinf(t0);
                y1 = -radius + radius * cosf(t1);
                z1 = -radius + radius * sinf(t1);
            }
            float vv0 = v0 + (v1 - v0) * t0 / kHalfPi;
            float vv1 = v0 + (v1 - v0) * t1 / kHalfPi;
            if (capStart) {
                float wc[3], w0[3], w1[3], n[3];
                f.nrm(-1, 0, 0, n[0], n[1], n[2]);
                f.pos(-hx, acy, acz, wc[0], wc[1], wc[2]);
                f.pos(-hx, y1, z1, w1[0], w1[1], w1[2]);
                f.pos(-hx, y0, z0, w0[0], w0[1], w0[2]);
                uint32_t vb = (uint32_t)(bk.v.size() / kStrideFloats);
                const float vs[3][12] = {
                    {wc[0], wc[1], wc[2], n[0], n[1], n[2], r, g, b, a, 0.5f, 0.5f},
                    {w1[0], w1[1], w1[2], n[0], n[1], n[2], r, g, b, a, u0, vv1},
                    {w0[0], w0[1], w0[2], n[0], n[1], n[2], r, g, b, a, u0, vv0},
                };
                for (int q = 0; q < 3; q++)
                    bk.v.insert(bk.v.end(), vs[q], vs[q] + kStrideFloats);
                bk.tri(vb, vb + 1, vb + 2);
            }
            if (capEnd) {
                float wc[3], w0[3], w1[3], n[3];
                f.nrm(1, 0, 0, n[0], n[1], n[2]);
                f.pos(hx, acy, acz, wc[0], wc[1], wc[2]);
                f.pos(hx, y0, z0, w0[0], w0[1], w0[2]);
                f.pos(hx, y1, z1, w1[0], w1[1], w1[2]);
                uint32_t vb = (uint32_t)(bk.v.size() / kStrideFloats);
                const float vs[3][12] = {
                    {wc[0], wc[1], wc[2], n[0], n[1], n[2], r, g, b, a, 0.5f, 0.5f},
                    {w0[0], w0[1], w0[2], n[0], n[1], n[2], r, g, b, a, u1, vv0},
                    {w1[0], w1[1], w1[2], n[0], n[1], n[2], r, g, b, a, u1, vv1},
                };
                for (int q = 0; q < 3; q++)
                    bk.v.insert(bk.v.end(), vs[q], vs[q] + kStrideFloats);
                bk.tri(vb, vb + 1, vb + 2);
            }
        }
    }
}

// planRound bake (main.cpp:1029-1064): faceted vertical strip around a corner
// bend, already in WORLD frame (roundJ/roundSkirt call it directly — no
// runFrame). Flat mid-angle normals INTO the room, arc-length U. Replicates
// the float chain literally, including std::cos/std::sin narrowed to float.
void emitPlanRound(Bucket& bk, float ccx, float ccy, float R, float a0, float a1,
                   int steps, float z0, float z1, float u0, float spanX,
                   float v0, float v1, float r, float g, float b, float a) {
    for (int k = 0; k < steps; k++) {
        float t0 = a0 + (a1 - a0) * (float)k / steps;
        float t1 = a0 + (a1 - a0) * (float)(k + 1) / steps;
        float tm = (t0 + t1) * 0.5f;
        float nx = -std::cos(tm), ny = -std::sin(tm);
        float x0 = ccx + R * std::cos(t0), y0 = ccy + R * std::sin(t0);
        float x1 = ccx + R * std::cos(t1), y1 = ccy + R * std::sin(t1);
        float s0 = R * (t0 - a0), s1 = R * (t1 - a0);
        float uu0 = u0 + s0 / spanX, uu1 = u0 + s1 / spanX;
        bk.quad((float)x0, (float)y0, z0, (float)nx, (float)ny, 0, r, g, b, a, uu0, v0,
                (float)x1, (float)y1, z0, (float)nx, (float)ny, 0, r, g, b, a, uu1, v0,
                (float)x1, (float)y1, z1, (float)nx, (float)ny, 0, r, g, b, a, uu1, v1,
                (float)x0, (float)y0, z1, (float)nx, (float)ny, 0, r, g, b, a, uu0, v1);
    }
}

// DrawSphere bake (Renderer.cpp:260-281): segs strips of (segs+1) vertex
// pairs, std::sin/std::cos narrowed to float, then the TRUE
// TRIANGLE_STRIP triangulation (same vertex SETS the driver rasterizes:
// even tri {A,B,A'} odd tri {B,B',A'} — winding swaps don't change pixels
// with culling off, and the sets match exactly).
void emitSphere(Bucket& bk, float cx, float cy, float cz, float radius,
                int segs, float r, float g, float b, float a) {
    if (segs < 6) segs = 6;
    if (segs > 24) segs = 24;
    for (int i = 0; i < segs; i++) {
        float t0 = (float)i / segs * (float)M_PI;
        float t1 = (float)(i + 1) / segs * (float)M_PI;
        uint32_t base = (uint32_t)(bk.v.size() / kStrideFloats);
        for (int j = 0; j <= segs; j++) {
            float p = (float)j / segs * 2.0f * (float)M_PI;
            float x0 = std::sin(t0) * std::cos(p), y0 = std::sin(t0) * std::sin(p),
                  z0 = std::cos(t0);
            float x1 = std::sin(t1) * std::cos(p), y1 = std::sin(t1) * std::sin(p),
                  z1 = std::cos(t1);
            const float vs[2][12] = {
                {cx + (float)x0 * radius, cy + (float)y0 * radius, cz + (float)z0 * radius,
                 (float)x0, (float)y0, (float)z0, r, g, b, a, 0, 0},
                {cx + (float)x1 * radius, cy + (float)y1 * radius, cz + (float)z1 * radius,
                 (float)x1, (float)y1, (float)z1, r, g, b, a, 0, 0},
            };
            bk.v.insert(bk.v.end(), vs[0], vs[0] + kStrideFloats);
            bk.v.insert(bk.v.end(), vs[1], vs[1] + kStrideFloats);
        }
        for (int j = 0; j < segs; j++) {
            uint32_t A = base + 2 * j, B = A + 1, A2 = A + 2, B2 = A + 3;
            bk.tri(A, B, A2);
            bk.tri(B, B2, A2);
        }
    }
}

// ---- draw indices ----------------------------------------------------------
enum DrawId {
    D_OPAQUE = 0, // (a+d) untextured: trim bars, beams+reveals, shadows, posts,
                  // spheres, cove glow, goal trim
    D_TURF,       // B1 turf sheet
    D_WALLBASE,   // B3 floor-fillet bases + B7 round bases (mesh)
    D_CEILING,    // B4 ceiling panel (face-down)
    D_SKIRT,      // B5 straights+chamfers + B6 round skirts (mesh)
    D_MARKINGS,   // B2 markings overlay (depth-TEST OFF)
    D_GLASS,      // C1 flat + C2 corners + C3 crowns + C4 rounds (mesh)
    D_SHELL,      // C5 goal interior shells (depth-WRITE OFF)
    D_WEAVE,      // C6 goal weave backs (depth-WRITE OFF)
    kDraws,
};

// Plan §1 sums as CLAIMED (used only for the build-log comparison). NOTE the
// accepted deviation: C1 bakes to 16 tris (8 quads), not the 24 the plan
// table prints — so the TRUE glass/mesh/fallback sums below differ by -8.
const uint64_t kPlanMesh[kDraws] = {1056, 2, 416, 2, 144, 2, 480, 24, 4};
const uint64_t kPlanFallback[kDraws] = {1008, 2, 288, 2, 16, 2, 224, 24, 4};

// Bake all 9 buckets for one mode. Every literal below is copied from the
// cited main.cpp call site (read-only this step); frame/UV/color rules match
// the immediate path exactly.
void bake(bool meshMode, Bucket out[kDraws]) {
    using namespace texturegen;
    const bool roundCorners = meshMode;
    const float spanX = kHexSpanX;
    const float spanY = kHexSpanY;
    const float filR = kFilletR;
    const float glassV = (kGlassZ1 - kGlassZ0) / spanY;
    const float arcUu = (float)M_PI / 2.0f * filR;
    const float crownV0 = glassV, crownV1 = glassV + arcUu / spanY;
    const float sideU = 7936.0f / spanX;
    const float backU = 5888.0f / spanX;
    const float roundR = 705.0f;
    const float roundT = roundR * std::tan((float)M_PI / 8.0f);
    const float sideEnd = roundCorners ? 3968.0f - roundT : 3968.0f;
    const float backEnd = roundCorners ? 2944.0f - roundT : 2944.0f;
    const float chamLen = roundCorners ? 1629.174f - 2.0f * roundT : 1629.174f;
    const float mArcU = (roundR * (float)M_PI / 4.0f) / spanX;
    const float mChamU = chamLen / spanX;
    const float glassCz = (kGlassZ0 + kGlassZ1) * 0.5f;
    const float glassH = kGlassZ1 - kGlassZ0;
    const float segLen = backEnd - 892.755f;
    const float segCtr = (backEnd + 892.755f) * 0.5f;
    const float topCz = (642.775f + kGlassZ1) * 0.5f;
    const float topH = kGlassZ1 - 642.775f;
    const float topV = (642.775f - kGlassZ0) / spanY;
    const float kGlassA = 0.40f;
    const float c1u1 = roundCorners ? (sideEnd + 3968.0f) / spanX + mArcU : sideU;
    const float c2u1 = roundCorners ? (2944.0f + backEnd) / spanX + mArcU : backU;
    const float c4u1 = roundCorners ? (3968.0f + sideEnd) / spanX + mArcU : sideU;

    Bucket& opq = out[D_OPAQUE];
    // A1 trim bars (main.cpp:1298-1328): fallback sharp-vertex bars vs mesh
    // round/chamfer tangent-joint bars. Same 8-draw / 96-tri cost either way.
    if (!roundCorners) {
        for (int jx = -1; jx <= 1; jx += 2)
            for (int jy = -1; jy <= 1; jy += 2) {
                emitBox(opq, Frame::at(4096.0f + 6.0f * jx, 3968.0f + 6.0f * jy,
                                       1024.0f, 0),
                        28.0f, 28.0f, 2048.0f, 0.42f, 0.55f, 0.75f, 1.0f);
                emitBox(opq, Frame::at(2944.0f + 6.0f * jx, 5120.0f + 6.0f * jy,
                                       1024.0f, 0),
                        28.0f, 28.0f, 2048.0f, 0.42f, 0.55f, 0.75f, 1.0f);
            }
    } else {
        const float tk = roundT * 0.70711f, to = 2.0f;
        const float tj[8][4] = {
            {4096.0f - tk, 3968.0f + tk, 0.70711f, 0.70711f},
            {2944.0f + tk, 5120.0f - tk, 0.70711f, 0.70711f},
            {-2944.0f - tk, 5120.0f - tk, -0.70711f, 0.70711f},
            {-4096.0f + tk, 3968.0f + tk, -0.70711f, 0.70711f},
            {4096.0f - tk, -3968.0f - tk, 0.70711f, -0.70711f},
            {2944.0f + tk, -5120.0f + tk, 0.70711f, -0.70711f},
            {-2944.0f - tk, -5120.0f + tk, -0.70711f, -0.70711f},
            {-4096.0f + tk, -3968.0f - tk, -0.70711f, -0.70711f},
        };
        for (int j = 0; j < 8; j++)
            emitBox(opq, Frame::at(tj[j][0] + to * tj[j][2], tj[j][1] + to * tj[j][3],
                                   1024.0f, 0),
                    24.0f, 24.0f, 2048.0f, 0.32f, 0.42f, 0.60f, 1.0f);
    }
    // A2 cross beams + shadow reveals (main.cpp:1408-1416): 9 x 2 draws.
    for (int i = 0; i < 9; i++) {
        float y = -5120.0f + 640.0f + i * 1120.0f;
        emitBox(opq, Frame::at(0, y, 2022.0f, 0), 7632.0f, 90.0f, 30.0f,
                0.28f, 0.34f, 0.45f, 0.9f);
        emitBox(opq, Frame::at(0, y, 2005.0f, 0), 7632.0f, 20.0f, 6.0f,
                0.20f, 0.25f, 0.35f, 0.9f);
    }
    // A3 longitudinal beams + reveals (main.cpp:1417-1423): 7 x 2 draws.
    for (int i = 0; i < 7; i++) {
        float x = -4096.0f + 585.0f + i * 1170.0f;
        emitBox(opq, Frame::at(x, 0, 2022.0f, 0), 90.0f, 9680.0f, 30.0f,
                0.28f, 0.34f, 0.45f, 0.9f);
        emitBox(opq, Frame::at(x, 0, 2005.0f, 0), 20.0f, 9680.0f, 6.0f,
                0.20f, 0.25f, 0.35f, 0.9f);
    }
    // A4 cove shadow lines (main.cpp:1499-1506): fixed 7936/5888 both modes.
    emitBox(opq, Frame::at(4096.0f - 262.0f, 0.0f, 2030.0f, 0), 4.0f, 7936.0f, 4.0f,
            0.28f, 0.50f, 0.85f, 0.9f);
    emitBox(opq, Frame::at(-4096.0f + 262.0f, 0.0f, 2030.0f, 0), 4.0f, 7936.0f, 4.0f,
            0.28f, 0.50f, 0.85f, 0.9f);
    emitBox(opq, Frame::at(0.0f, 5120.0f - 262.0f, 2030.0f, 0), 5888.0f, 4.0f, 4.0f,
            0.28f, 0.50f, 0.85f, 0.9f);
    emitBox(opq, Frame::at(0.0f, -5120.0f + 262.0f, 2030.0f, 0), 5888.0f, 4.0f, 4.0f,
            0.28f, 0.50f, 0.85f, 0.9f);
    // D1 cove glow strips (main.cpp:1431-1448): 4 straights + 4 mesh diagonals.
    const float coveSide = roundCorners ? 7352.0f : 7936.0f;
    const float coveBack = roundCorners ? 5304.0f : 5888.0f;
    emitBox(opq, Frame::at(4096.0f - 262.0f, 0.0f, 2036.0f, 0), 12.0f, coveSide, 10.0f,
            0.45f, 0.72f, 1.0f, 0.9f);
    emitBox(opq, Frame::at(-4096.0f + 262.0f, 0.0f, 2036.0f, 0), 12.0f, coveSide, 10.0f,
            0.45f, 0.72f, 1.0f, 0.9f);
    emitBox(opq, Frame::at(0.0f, 5120.0f - 262.0f, 2036.0f, 0), coveBack, 12.0f, 10.0f,
            0.45f, 0.72f, 1.0f, 0.9f);
    emitBox(opq, Frame::at(0.0f, -5120.0f + 262.0f, 2036.0f, 0), coveBack, 12.0f, 10.0f,
            0.45f, 0.72f, 1.0f, 0.9f);
    if (roundCorners) {
        const float cq = (float)M_PI / 4.0f;
        emitBox(opq, Frame::at(3243.0f, 4267.0f, 2036.0f, -cq), 1671.6f, 12.0f, 10.0f,
                0.45f, 0.72f, 1.0f, 0.9f);
        emitBox(opq, Frame::at(-3243.0f, 4267.0f, 2036.0f, cq), 1671.6f, 12.0f, 10.0f,
                0.45f, 0.72f, 1.0f, 0.9f);
        emitBox(opq, Frame::at(3243.0f, -4267.0f, 2036.0f, cq), 1671.6f, 12.0f, 10.0f,
                0.45f, 0.72f, 1.0f, 0.9f);
        emitBox(opq, Frame::at(-3243.0f, -4267.0f, 2036.0f, -cq), 1671.6f, 12.0f, 10.0f,
                0.45f, 0.72f, 1.0f, 0.9f);
    }
    // A5/A6/D2 goals (main.cpp:1513-1533): trim (team), posts+spheres (white).
    for (int gg = 0; gg < 2; gg++) {
        float ys = gg == 0 ? 5120.0f : -5120.0f;
        float tr = gg == 0 ? 1.0f : 0.16f, tg = gg == 0 ? 0.48f : 0.55f,
              tb = gg == 0 ? 0.10f : 1.0f;
        emitBox(opq, Frame::at(0, ys, 642.775f + 8.0f, 0), 1875.0f, 26.0f, 14.0f,
                tr, tg, tb, 0.95f);
        emitBox(opq, Frame::at(-906.0f, ys, 321.0f, 0), 26.0f, 26.0f, 642.775f + 8.0f,
                tr, tg, tb, 0.95f);
        emitBox(opq, Frame::at(906.0f, ys, 321.0f, 0), 26.0f, 26.0f, 642.775f + 8.0f,
                tr, tg, tb, 0.95f);
        emitBox(opq, Frame::at(-892.755f, ys, 321.4f, 0), 24.0f, 24.0f, 642.775f,
                0.95f, 0.95f, 0.95f, 1.0f);
        emitBox(opq, Frame::at(892.755f, ys, 321.4f, 0), 24.0f, 24.0f, 642.775f,
                0.95f, 0.95f, 0.95f, 1.0f);
        emitBox(opq, Frame::at(0, ys, 642.775f, 0), 1815.51f, 24.0f, 24.0f,
                0.95f, 0.95f, 0.95f, 1.0f);
        emitSphere(opq, 908.0f, ys, 643.0f, 12.0f, 6, 0.95f, 0.95f, 0.95f, 1.0f);
        emitSphere(opq, -908.0f, ys, 643.0f, 12.0f, 6, 0.95f, 0.95f, 0.95f, 1.0f);
    }

    // B1 turf (main.cpp:895-896): one quad, full footprint.
    emitQuadXZ(out[D_TURF], Frame::at(0, 0, -1, 0), 8192.0f, 10240.0f,
               1.0f, 1.0f, 1, 1, 1, 1, false);

    // B3 floor-fillet bases (main.cpp:1118-1176): 2 side + 4 mouth-split
    // (one cap fan each) + 4 chamfer arcSweeps.
    Bucket& wb = out[D_WALLBASE];
    emitArcSweep(wb, Frame::deg(4096.0f, 0.0f, 0.0f, -90.0f), 2.0f * sideEnd, filR,
                 false, (sideEnd + 3968.0f) / spanX, (3968.0f - sideEnd) / spanX,
                 0.0f, 1.0f, 1, 1, 1, 1, false, false);
    emitArcSweep(wb, Frame::deg(-4096.0f, 0.0f, 0.0f, 90.0f), 2.0f * sideEnd, filR,
                 false, (3968.0f + sideEnd) / spanX, (3968.0f - sideEnd) / spanX,
                 0.0f, 1.0f, 1, 1, 1, 1, false, false);
    emitArcSweep(wb, Frame::deg(-segCtr, 5120.0f, 0.0f, 0.0f), segLen, filR, false,
                 (2944.0f + backEnd) / spanX, (2944.0f + 892.755f) / spanX,
                 0.0f, 1.0f, 1, 1, 1, 1, false, true);
    emitArcSweep(wb, Frame::deg(segCtr, 5120.0f, 0.0f, 0.0f), segLen, filR, false,
                 (2944.0f - 892.755f) / spanX, (2944.0f - backEnd) / spanX,
                 0.0f, 1.0f, 1, 1, 1, 1, true, false);
    emitArcSweep(wb, Frame::deg(-segCtr, -5120.0f, 0.0f, 180.0f), segLen, filR, false,
                 (2944.0f - 892.755f) / spanX, (2944.0f - backEnd) / spanX,
                 0.0f, 1.0f, 1, 1, 1, 1, true, false);
    emitArcSweep(wb, Frame::deg(segCtr, -5120.0f, 0.0f, 180.0f), segLen, filR, false,
                 (backEnd + 2944.0f) / spanX, (892.755f + 2944.0f) / spanX,
                 0.0f, 1.0f, 1, 1, 1, 1, false, true);
    emitArcSweep(wb, Frame::deg(3520.0f, 4544.0f, 0.0f, -45.0f), chamLen, filR, false,
                 c1u1 - mChamU, c1u1, 0.0f, 1.0f, 1, 1, 1, 1, false, false);
    emitArcSweep(wb, Frame::deg(-3520.0f, 4544.0f, 0.0f, 45.0f), chamLen, filR, false,
                 c2u1 - mChamU, c2u1, 0.0f, 1.0f, 1, 1, 1, 1, false, false);
    emitArcSweep(wb, Frame::deg(3520.0f, -4544.0f, 0.0f, -135.0f), chamLen, filR, false,
                 c2u1 - mChamU, c2u1, 0.0f, 1.0f, 1, 1, 1, 1, false, false);
    emitArcSweep(wb, Frame::deg(-3520.0f, -4544.0f, 0.0f, 135.0f), chamLen, filR, false,
                 c4u1 - mChamU, c4u1, 0.0f, 1.0f, 1, 1, 1, 1, false, false);

    // Junction rounds (main.cpp:1370-1394, mesh only): base course here (B7);
    // glass + crown courses join the glass bucket (C4) below.
    const float rj[8][5] = {
        {4096.0f - roundR, 3968.0f - roundT, 0.0f, 45.0f, (sideEnd + 3968.0f) / spanX},
        {-4096.0f + roundR, 3968.0f - roundT, 180.0f, 135.0f, (3968.0f - sideEnd) / spanX},
        {4096.0f - roundR, -3968.0f + roundT, 0.0f, -45.0f, (3968.0f - sideEnd) / spanX},
        {-4096.0f + roundR, -3968.0f + roundT, 180.0f, 225.0f, (3968.0f + sideEnd) / spanX},
        {2944.0f - roundT, 5120.0f - roundR, 90.0f, 45.0f, (2944.0f - backEnd) / spanX},
        {-2944.0f + roundT, 5120.0f - roundR, 90.0f, 135.0f, (2944.0f + backEnd) / spanX},
        {2944.0f - roundT, -5120.0f + roundR, -90.0f, -45.0f, (backEnd + 2944.0f) / spanX},
        {-2944.0f + roundT, -5120.0f + roundR, -90.0f, -135.0f, (2944.0f - backEnd) / spanX},
    };
    const float d2r = (float)M_PI / 180.0f;
    Bucket& gl = out[D_GLASS];
    if (roundCorners) {
        for (int j = 0; j < 8; j++) {
            float a0 = rj[j][2] * d2r, a1 = rj[j][3] * d2r, u0 = rj[j][4];
            emitPlanRound(wb, rj[j][0], rj[j][1], roundR, a0, a1, 8,
                          0.0f, filR, u0, spanX, 0.0f, 1.0f, 1, 1, 1, 1);
            emitPlanRound(gl, rj[j][0], rj[j][1], roundR, a0, a1, 8,
                          kGlassZ0, kGlassZ1, u0, spanX, 0.0f, glassV,
                          1, 1, 1, kGlassA);
            emitPlanRound(gl, rj[j][0], rj[j][1], roundR, a0, a1, 8,
                          kGlassZ1, 2048.0f, u0, spanX, crownV0, crownV1,
                          1, 1, 1, kGlassA);
        }
    }

    // B4 ceiling (main.cpp:1405-1407): face-down lit underside.
    emitQuadXZ(out[D_CEILING], Frame::at(0, 0, 2046.0f, 0), 8192.0f, 10240.0f,
               8192.0f / 2048.0f, 10240.0f / 2048.0f, 1, 1, 1, 1, true);

    // B5 outer skirt (main.cpp:1453-1478): 4 straights + 4 chamfers (the inner
    // pi rotation faces the quad inward; composed with the outer yaw here —
    // Z-rotations commute, so one frame with the summed angle is exact).
    Bucket& sk = out[D_SKIRT];
    const float pi = (float)M_PI;
    auto skirtStraight = [&](float cx, float cy, float tanLen, float yawDeg) {
        emitQuadWall(sk, Frame::deg(cx, cy, 2084.0f, yawDeg), tanLen, 72.0f,
                     tanLen / spanX, 72.0f / spanY,
                     0.20f, 0.25f, 0.35f, 1.0f, 0.0f, 0.9f);
    };
    skirtStraight(4096.0f + 2.0f, 0.0f, 2.0f * sideEnd, 90.0f);
    skirtStraight(-4096.0f - 2.0f, 0.0f, 2.0f * sideEnd, 270.0f);
    skirtStraight(0.0f, 5120.0f + 2.0f, 2.0f * backEnd, 180.0f);
    skirtStraight(0.0f, -5120.0f - 2.0f, 2.0f * backEnd, 0.0f);
    auto skirtChamfer = [&](float cx, float cy, float yawDeg) {
        emitQuadWall(sk, Frame::at(cx, cy, 2084.0f, yawDeg * pi / 180.0f + pi),
                     chamLen, 72.0f, chamLen / spanX, 72.0f / spanY,
                     0.20f, 0.25f, 0.35f, 1.0f, 0.0f, 0.9f);
    };
    skirtChamfer(3520.0f, 4544.0f, -45.0f);
    skirtChamfer(-3520.0f, 4544.0f, 45.0f);
    skirtChamfer(3520.0f, -4544.0f, -135.0f);
    skirtChamfer(-3520.0f, -4544.0f, 135.0f);
    // B6 round skirts (main.cpp:1479-1496, mesh only).
    if (roundCorners) {
        for (int j = 0; j < 8; j++)
            emitPlanRound(sk, rj[j][0], rj[j][1], roundR, rj[j][2] * d2r,
                          rj[j][3] * d2r, 8, 2048.0f, 2120.0f, rj[j][4], spanX,
                          0.9f, 0.93f, 0.20f, 0.25f, 0.35f, 1.0f);
    }

    // B2 markings overlay (main.cpp:899-900): same footprint, lifted 2 uu.
    // Depth-test OFF is a draw-time sandwich (see DrawArenaBuckets), not bake.
    emitQuadXZ(out[D_MARKINGS], Frame::at(0, 0, 1.0f, 0), 8192.0f, 10240.0f,
               1.0f, 1.0f, 1, 1, 1, 1, false);

    // C1 flat glass (main.cpp:1237-1254): 8 quads = 16 tris (see file header
    // re the plan's 24-tri slip). quadYaw puts local +y INTO the arena.
    auto glassSub = [&](float cx, float cy, float cz, float tanLen, float height,
                        float quadYawDeg, float uOff, float vOff) {
        emitQuadWall(gl, Frame::at(cx, cy, cz, quadYawDeg * pi / 180.0f),
                     tanLen, height, tanLen / spanX, height / spanY,
                     1.0f, 1.0f, 1.0f, kGlassA, uOff, vOff);
    };
    glassSub(4096.0f + 2.0f, 0.0f, glassCz, 2.0f * sideEnd, glassH, 90.0f,
             (3968.0f - sideEnd) / spanX, 0);
    glassSub(-4096.0f - 2.0f, 0.0f, glassCz, 2.0f * sideEnd, glassH, 270.0f,
             (3968.0f - sideEnd) / spanX, 0);
    glassSub(-segCtr, 5120.0f + 2.0f, glassCz, segLen, glassH, 180.0f,
             (2944.0f + 892.755f) / spanX, 0);
    glassSub(segCtr, 5120.0f + 2.0f, glassCz, segLen, glassH, 180.0f,
             (2944.0f - backEnd) / spanX, 0);
    glassSub(0, 5120.0f + 2.0f, topCz, 1785.51f, topH, 180.0f,
             (2944.0f - 892.755f) / spanX, topV);
    glassSub(-segCtr, -5120.0f - 2.0f, glassCz, segLen, glassH, 0.0f,
             (2944.0f - backEnd) / spanX, 0);
    glassSub(segCtr, -5120.0f - 2.0f, glassCz, segLen, glassH, 0.0f,
             (892.755f + 2944.0f) / spanX, 0);
    glassSub(0, -5120.0f - 2.0f, topCz, 1785.51f, topH, 0.0f,
             (2944.0f - 892.755f) / spanX, topV);
    // C2 corner glass (main.cpp:1263-1292): NEGATIVE uRep under GL_REPEAT +
    // pi rotation (quad-local +x points away from the field growth after it).
    auto cornerGlass = [&](float cx, float cy, float yawDeg, float uOff) {
        emitQuadWall(gl, Frame::at(cx, cy, glassCz, yawDeg * pi / 180.0f + pi),
                     chamLen, glassH, -mChamU, glassH / spanY,
                     1.0f, 1.0f, 1.0f, kGlassA, uOff, 0);
    };
    float cu1 = roundCorners ? (sideEnd + 3968.0f) / spanX + mArcU : sideU;
    float cu2 = roundCorners ? (2944.0f + backEnd) / spanX + mArcU : backU;
    cornerGlass(3520.0f, 4544.0f, -45.0f, cu1);
    cornerGlass(-3520.0f, 4544.0f, 45.0f, cu2);
    cornerGlass(3520.0f, -4544.0f, -135.0f, cu2);
    cornerGlass(-3520.0f, -4544.0f, 135.0f, cu1);
    // C3 crown courses (main.cpp:1333-1364): ceiling-course arcSweeps, same u
    // edges as the base below, v continuing the glass field.
    emitArcSweep(gl, Frame::deg(4096.0f, 0.0f, 2048.0f, -90.0f), 2.0f * sideEnd, filR,
                 true, (sideEnd + 3968.0f) / spanX, (3968.0f - sideEnd) / spanX,
                 crownV0, crownV1, 1, 1, 1, kGlassA, false, false);
    emitArcSweep(gl, Frame::deg(-4096.0f, 0.0f, 2048.0f, 90.0f), 2.0f * sideEnd, filR,
                 true, (sideEnd + 3968.0f) / spanX, (3968.0f - sideEnd) / spanX,
                 crownV0, crownV1, 1, 1, 1, kGlassA, false, false);
    emitArcSweep(gl, Frame::deg(0.0f, 5120.0f, 2048.0f, 0.0f), 2.0f * backEnd, filR,
                 true, (2944.0f + backEnd) / spanX, (2944.0f - backEnd) / spanX,
                 crownV0, crownV1, 1, 1, 1, kGlassA, false, false);
    emitArcSweep(gl, Frame::deg(0.0f, -5120.0f, 2048.0f, 180.0f), 2.0f * backEnd, filR,
                 true, (2944.0f + backEnd) / spanX, (2944.0f - backEnd) / spanX,
                 crownV0, crownV1, 1, 1, 1, kGlassA, false, false);
    emitArcSweep(gl, Frame::deg(3520.0f, 4544.0f, 2048.0f, -45.0f), chamLen, filR,
                 true, c1u1 - mChamU, c1u1, crownV0, crownV1, 1, 1, 1, kGlassA,
                 false, false);
    emitArcSweep(gl, Frame::deg(-3520.0f, 4544.0f, 2048.0f, 45.0f), chamLen, filR,
                 true, c2u1 - mChamU, c2u1, crownV0, crownV1, 1, 1, 1, kGlassA,
                 false, false);
    emitArcSweep(gl, Frame::deg(3520.0f, -4544.0f, 2048.0f, -135.0f), chamLen, filR,
                 true, c2u1 - mChamU, c2u1, crownV0, crownV1, 1, 1, 1, kGlassA,
                 false, false);
    emitArcSweep(gl, Frame::deg(-3520.0f, -4544.0f, 2048.0f, 135.0f), chamLen, filR,
                 true, c4u1 - mChamU, c4u1, crownV0, crownV1, 1, 1, 1, kGlassA,
                 false, false);
    // (C4 round glass+crown were emitted with the B7 loop above, mesh only.)

    // C5/C6 goal interiors (main.cpp:1571-1591): drawn after cars/ball with
    // depth-test ON + depth-write OFF (draw-time sandwich, not bake).
    Bucket& sh = out[D_SHELL];
    Bucket& wv = out[D_WEAVE];
    for (int gg = 0; gg < 2; gg++) {
        float ys = gg == 0 ? 5120.0f : -5120.0f;
        float tr = gg == 0 ? 1.0f : 0.16f, tg = gg == 0 ? 0.48f : 0.55f,
              tb = gg == 0 ? 0.10f : 1.0f;
        emitBox(sh, Frame::at(0, ys + (gg == 0 ? 440.0f : -440.0f), 321.0f, 0),
                1785.51f, 880.0f, 642.775f,
                tr * 0.35f, tg * 0.35f, tb * 0.35f, 0.30f);
        // Weave normal points back INTO the arena: -Y for the +Y goal (pi),
        // +Y for the -Y goal (0) — else the weave is ambient-only black.
        float weaveYaw = gg == 0 ? pi : 0.0f;
        emitQuadWall(wv, Frame::at(0.0f, ys + (gg == 0 ? 880.0f : -880.0f), 321.0f,
                                   weaveYaw),
                     1785.51f, 642.775f, 1785.51f / 880.0f, 642.775f / 880.0f,
                     tr * 0.55f, tg * 0.55f, tb * 0.55f, 0.55f, 0.0f, 0.0f);
    }
}

// ---- GL buckets ------------------------------------------------------------
struct GLBucket {
    unsigned vbo = 0, ibo = 0;
    uint32_t idxCount = 0;
    uint64_t tris = 0;
};

struct ModeSet {
    bool built = false;
    GLBucket d[kDraws];
};

ModeSet g_mesh, g_fallback;

void buildMode(ModeSet& ms, bool meshMode) {
    Bucket cpu[kDraws];
    bake(meshMode, cpu);
    const uint64_t* plan = meshMode ? kPlanMesh : kPlanFallback;
    uint64_t bakedTotal = 0, planTotal = 0;
    for (int d = 0; d < kDraws; d++) {
        GLBucket& g = ms.d[d];
        glGenBuffers(1, &g.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
        glBufferData(GL_ARRAY_BUFFER, cpu[d].v.size() * sizeof(float),
                     cpu[d].v.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glGenBuffers(1, &g.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     cpu[d].idx.size() * sizeof(uint32_t), cpu[d].idx.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        g.idxCount = (uint32_t)cpu[d].idx.size();
        g.tris = cpu[d].tris();
        bakedTotal += g.tris;
        planTotal += plan[d];
    }
    // Proof line for G3c/VG7: baked (== drawn, == counted) vs plan-claimed.
    std::fprintf(stderr,
                 "[buckets] built %s: 9 draws tris=%llu (plan %llu%s); "
                 "per-draw baked/plan: %llu/%llu %llu/%llu %llu/%llu %llu/%llu "
                 "%llu/%llu %llu/%llu %llu/%llu %llu/%llu %llu/%llu\n",
                 meshMode ? "mesh" : "fallback",
                 (unsigned long long)bakedTotal, (unsigned long long)planTotal,
                 bakedTotal == planTotal ? "" : " DEVIATION (C1 16-vs-24, G3b notes)",
                 (unsigned long long)ms.d[0].tris, (unsigned long long)plan[0],
                 (unsigned long long)ms.d[1].tris, (unsigned long long)plan[1],
                 (unsigned long long)ms.d[2].tris, (unsigned long long)plan[2],
                 (unsigned long long)ms.d[3].tris, (unsigned long long)plan[3],
                 (unsigned long long)ms.d[4].tris, (unsigned long long)plan[4],
                 (unsigned long long)ms.d[5].tris, (unsigned long long)plan[5],
                 (unsigned long long)ms.d[6].tris, (unsigned long long)plan[6],
                 (unsigned long long)ms.d[7].tris, (unsigned long long)plan[7],
                 (unsigned long long)ms.d[8].tris, (unsigned long long)plan[8]);
    ms.built = true;
}

inline ModeSet& forMode(bool meshMode) {
    return meshMode ? g_mesh : g_fallback;
}

// One bucket draw, Fennec pattern (Fennec.cpp:152-186): client arrays over a
// bound VBO + glDrawElements. Textured draws replicate the TextureScope
// sandwich (bind, draw, unbind, restore enable) and touch nothing else —
// blend/lighting/depth-mask/test stay exactly as the caller left them.
void drawOne(Renderer& r, const GLBucket& g, unsigned tex, bool textured) {
    GLboolean texWas = glIsEnabled(GL_TEXTURE_2D);
    if (textured) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (textured)
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    const GLsizei stride = kStrideFloats * sizeof(float);
    glVertexPointer(3, GL_FLOAT, stride, (const void*)0);
    glNormalPointer(GL_FLOAT, stride, (const void*)(3 * sizeof(float)));
    glColorPointer(4, GL_FLOAT, stride, (const void*)(6 * sizeof(float)));
    if (textured)
        glTexCoordPointer(2, GL_FLOAT, stride, (const void*)(10 * sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
    glDrawElements(GL_TRIANGLES, (GLsizei)g.idxCount, GL_UNSIGNED_INT,
                   (const void*)0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (textured)
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (textured) {
        glBindTexture(GL_TEXTURE_2D, 0);
        if (!texWas)
            glDisable(GL_TEXTURE_2D);
    }
    r.NoteExternalDraw(g.tris, 0);
}

} // namespace

void DrawArenaBuckets(Renderer& r, bool meshMode, bool noHexNet) {
    ModeSet& ms = forMode(meshMode);
    if (!ms.built)
        buildMode(ms, meshMode);
    unsigned glassTex = noHexNet ? PlainGlassTexture() : HexNetTexture();
    // Emission order per plan §5: (a+d) -> turf -> wallbase -> ceiling ->
    // skirt -> B2 overlay -> glass. Untextured draws touch no GL state beyond
    // the arrays (exactly like DrawBox); textured draws add the bind sandwich.
    drawOne(r, ms.d[D_OPAQUE], 0, false);
    drawOne(r, ms.d[D_TURF], TurfTexture(), true);
    drawOne(r, ms.d[D_WALLBASE], WallBaseTexture(), true);
    drawOne(r, ms.d[D_CEILING], CeilingTexture(), true);
    drawOne(r, ms.d[D_SKIRT], PlainGlassTexture(), true);
    // B2 overlay (DrawTexturedOverlayXZ, Renderer.cpp:539-564): depth-TEST
    // OFF sandwich, depth writes still on (mask untouched) — replicated.
    {
        GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        drawOne(r, ms.d[D_MARKINGS], TurfMarkingsTexture(), true);
        if (depthWas)
            glEnable(GL_DEPTH_TEST);
    }
    drawOne(r, ms.d[D_GLASS], glassTex, true);
}

void DrawArenaGoalInteriors(Renderer& r, bool meshMode) {
    ModeSet& ms = forMode(meshMode);
    if (!ms.built)
        buildMode(ms, meshMode);
    // C5+C6 (main.cpp:1568-1592): depth-TEST on, depth-WRITE off, restored
    // after — so cars/balls inside the net show through veiled.
    GLboolean maskWas = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &maskWas);
    glDepthMask(GL_FALSE);
    drawOne(r, ms.d[D_SHELL], 0, false);
    drawOne(r, ms.d[D_WEAVE], GoalInteriorTexture(), true);
    glDepthMask(maskWas ? GL_TRUE : GL_FALSE);
}

void ShutdownArenaBuckets() {
    for (int m = 0; m < 2; m++) {
        ModeSet& ms = m ? g_fallback : g_mesh;
        if (!ms.built)
            continue;
        for (int d = 0; d < kDraws; d++) {
            glDeleteBuffers(1, &ms.d[d].vbo);
            glDeleteBuffers(1, &ms.d[d].ibo);
            ms.d[d].vbo = ms.d[d].ibo = 0;
        }
        ms.built = false;
    }
}

} // namespace ArenaShell
} // namespace rl
