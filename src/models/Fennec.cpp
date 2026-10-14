// Fennec.cpp — see Fennec.h. GL 2.1 vertex-array draw (VBO when available).
#include "Fennec.h"
#include "render/Renderer.h"
#define GL_GLEXT_PROTOTYPES 1 // VBO entry points (GL 1.5 core) in Mesa's gl.h
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace rl {

namespace {
struct Mesh {
    std::vector<float> data;   // interleaved 6f: pos3 + normal3
    std::vector<uint32_t> idx;
    std::vector<float> colO;   // baked paint, orange team (3f/vert)
    std::vector<float> colB;   // baked paint, blue team (3f/vert)
    GLuint vbo = 0, ibo = 0, cboO = 0, cboB = 0;
    bool ok = false;
};
Mesh g_mesh;

float Smooth01(float a, float b, float x) {
    float t = (x - a) / (b - a);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t * t * (3 - 2 * t);
}

// Procedural reference paint (license-clean approximation of the target
// look: orange body, pink/magenta roof sheen, black grille + lower + wheels).
// Baked ONCE at load into color VBOs — zero per-frame CPU. Masks are pure
// functions of car-local position + normal (bbox x[-65.9,74.5] nose+x,
// y +/-36, z[-17,34.7] per tools/verify/mesh_integrity.py).
void BakePaint(std::vector<float>& out, bool orange) {
    size_t nv = g_mesh.data.size() / 6;
    out.resize(nv * 3);
    for (size_t i = 0; i < nv; i++) {
        float x = g_mesh.data[i * 6 + 0];
        float y = g_mesh.data[i * 6 + 1];
        float z = g_mesh.data[i * 6 + 2];
        float nz = g_mesh.data[i * 6 + 5];
        float ay = std::fabs(y);
        // Base coat.
        float r = orange ? 1.0f : 0.15f;
        float g = orange ? 0.40f : 0.40f;
        float b = orange ? 0.07f : 0.95f;
        // Pink/magenta roof sheen: high + up-facing only, so flanks stay
        // body-coloured like the reference.
        float roof = Smooth01(6.0f, 26.0f, z);
        if (nz < 0) nz = 0;
        if (nz > 1) nz = 1;
        roof *= 0.25f + 0.75f * nz;
        float pr = orange ? 1.0f : 0.45f;
        float pg = orange ? 0.28f : 0.55f;
        float pb = orange ? 0.60f : 1.0f;
        r += (pr - r) * roof; g += (pg - g) * roof; b += (pb - b) * roof;
        // Black masks: front grille, lower valance, wheel arches, side vents.
        float grille = Smooth01(56.0f, 62.0f, x) * (1.0f - Smooth01(10.0f, 14.0f, z)) * (1.0f - Smooth01(18.0f, 24.0f, ay));
        float lower = 1.0f - Smooth01(-10.0f, -6.0f, z);
        // Wheels sit near x = +/-40, |y| ~ 30: arch-localised so the flanks
        // stay body-coloured instead of flooding black.
        float dw = std::fabs(x - 40.0f);
        float dw2 = std::fabs(x + 40.0f);
        if (dw2 < dw) dw = dw2;
        float wheel = (1.0f - Smooth01(-8.0f, -4.0f, z)) * Smooth01(25.0f, 29.0f, ay)
                    * (1.0f - Smooth01(14.0f, 22.0f, dw));
        float vent = Smooth01(31.5f, 33.5f, ay)
                   * Smooth01(-8.0f, -5.0f, z) * (1.0f - Smooth01(12.0f, 15.0f, z))
                   * Smooth01(-34.0f, -30.0f, x) * (1.0f - Smooth01(33.0f, 37.0f, x));
        float dark = grille;
        if (lower > dark) dark = lower;
        if (wheel > dark) dark = wheel;
        if (vent > dark) dark = vent;
        r += (0.03f - r) * dark; g += (0.03f - g) * dark; b += (0.04f - b) * dark;
        // Baked glass: cabin band (not the crown of the roof, which stays
        // body-coloured like the reference). Replaces the old glasshouse box,
        // which floated above the mesh windshield and read as a black slab.
        float glass = Smooth01(12.0f, 15.0f, z) * (1.0f - Smooth01(27.0f, 30.0f, z))
                    * Smooth01(-38.0f, -34.0f, x) * (1.0f - Smooth01(26.0f, 30.0f, x))
                    * (1.0f - Smooth01(24.0f, 27.0f, ay)) * (1.0f - Smooth01(0.55f, 0.8f, nz));
        if (glass > 0) {
            r += (0.02f - r) * glass; g += (0.03f - g) * glass; b += (0.05f - b) * glass;
        }
        // Metallic flake: deterministic per-vertex shimmer (+/-4%), baked once.
        // Catches the key light at glancing angles like the reference clearcoat.
        float h = std::sin(x * 12.9898f + y * 78.233f + z * 37.719f) * 43758.5453f;
        h -= (float)(int)h;
        if (h < 0) h += 1.0f;
        float fl = 0.96f + 0.08f * h;
        out[i * 3 + 0] = r * fl; out[i * 3 + 1] = g * fl; out[i * 3 + 2] = b * fl;
    }
}

void DrawTrim(bool orange, Renderer& r) {
    // License-clean "Fennec read": dark glasshouse, bumpers, lights, stripe.
    // Local space matches the converted mesh: +x fwd, +y left, +z up, origin
    // at the RocketSim car origin (hitbox ~120 x 87 x 39).
    float tr = orange ? 1.0f : 0.45f, tg = orange ? 0.62f : 0.75f,
          tb = orange ? 0.15f : 1.0f;
    (void)tr; (void)tg; (void)tb;
    // NOTE: no glasshouse / windshield-strip / roof-stripe boxes. The mesh
    // models its own cabin and the glass is baked per-vertex; boxes floated
    // above the windshield and read as slabs (playtest 2026-10-14).
    // Black front grille, flush with the nose (mesh max x = 74.5): thin dark
    // box + two horizontal slats. Previous bumper boxes stuck out past the
    // mesh and read as black slabs — this one lives inside it.
    r.DrawBox(70.0f, 0, 3.0f, 5.0f, 40.0f, 8.0f, 0, 0.03f, 0.03f, 0.04f);
    r.DrawBox(70.0f, 0, 1.5f, 5.2f, 38.0f, 0.9f, 0, 0.16f, 0.16f, 0.18f);
    r.DrawBox(70.0f, 0, 4.5f, 5.2f, 38.0f, 0.9f, 0, 0.16f, 0.16f, 0.18f);
    // Hood vents: two slim black slits each side of the center stripe, flush
    // on the hood like the reference (hood top sits ~z 12-14 near x 35-55).
    for (int s = -1; s <= 1; s += 2) {
        r.DrawBox(44.0f, s * 14.0f, 13.2f, 14.0f, 5.0f, 1.2f, 0,
                  0.02f, 0.02f, 0.03f);
    }
    // Flush side skirts (mesh half-width = 36.2): hug the flanks, dark.
    r.DrawBox(0, 34.8f, -8.0f, 80.0f, 3.0f, 8.0f, 0, 0.03f, 0.03f, 0.04f);
    r.DrawBox(0, -34.8f, -8.0f, 80.0f, 3.0f, 8.0f, 0, 0.03f, 0.03f, 0.04f);
    // Round headlights: emissive fans in the YZ plane at the nose.
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 0.97f, 0.88f, 1.0f);
    for (int s = -1; s <= 1; s += 2) {
        float cy = s * 21.0f, cz = 5.5f;
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(73.6f, cy, cz);
        for (int i = 0; i <= 12; i++) {
            float a = 2.0f * (float)M_PI * i / 12;
            glVertex3f(73.6f, cy + std::cos(a) * 6.0f, cz + std::sin(a) * 6.0f);
        }
        glEnd();
        // Dark bezel ring around each lamp.
        glColor4f(0.05f, 0.05f, 0.06f, 1.0f);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 12; i++) {
            float a = 2.0f * (float)M_PI * i / 12;
            glVertex3f(73.7f, cy + std::cos(a) * 6.8f, cz + std::sin(a) * 6.8f);
        }
        glEnd();
        glColor4f(1.0f, 0.97f, 0.88f, 1.0f);
    }
    // Taillight bar (mesh min x = -65.9).
    glColor4f(1.0f, orange ? 0.25f : 0.15f, orange ? 0.15f : 0.25f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-65.5f, -34.0f, 2.0f); glVertex3f(-65.5f, 34.0f, 2.0f);
    glVertex3f(-65.5f, 34.0f, 8.0f); glVertex3f(-65.5f, -34.0f, 8.0f);
    glEnd();
    if (r.LightingOn()) glEnable(GL_LIGHTING);
}

void DrawBody(float orange, Renderer& r) {
    // Baked procedural paint (color VBO / client array). The vertex colours
    // drive ambient+diffuse through GL_COLOR_MATERIAL — no per-frame cost.
    bool isO = orange > 0.5f;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)0);
    glNormalPointer(GL_FLOAT, 6 * sizeof(float),
                    (const void*)(3 * sizeof(float)));
    if (g_mesh.vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, g_mesh.vbo);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)0);
        glNormalPointer(GL_FLOAT, 6 * sizeof(float),
                        (const void*)(3 * sizeof(float)));
        glBindBuffer(GL_ARRAY_BUFFER, isO ? g_mesh.cboO : g_mesh.cboB);
        glColorPointer(3, GL_FLOAT, 0, (const void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_mesh.ibo);
        glDrawElements(GL_TRIANGLES, (GLsizei)g_mesh.idx.size(),
                       GL_UNSIGNED_INT, (const void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else {
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), g_mesh.data.data());
        glNormalPointer(GL_FLOAT, 6 * sizeof(float),
                        g_mesh.data.data() + 3);
        glColorPointer(3, GL_FLOAT, 0, (isO ? g_mesh.colO : g_mesh.colB).data());
        glDrawElements(GL_TRIANGLES, (GLsizei)g_mesh.idx.size(),
                       GL_UNSIGNED_INT, g_mesh.idx.data());
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    DrawTrim(orange > 0.5f, r);
}
} // namespace

bool FennecLoad(const char* path) {
    if (g_mesh.ok) return true;
    FILE* f = std::fopen(path ? path : "assets/models/fennec.bin", "rb");
    if (!f) return false;
    char magic[4];
    uint32_t nv = 0, ni = 0;
    if (std::fread(magic, 1, 4, f) != 4 || magic[0] != 'F' || magic[1] != 'E' ||
        magic[2] != 'N' || magic[3] != '1' ||
        std::fread(&nv, 4, 1, f) != 1 || nv == 0 || nv > 4u << 20) {
        std::fclose(f);
        return false;
    }
    g_mesh.data.resize(size_t(nv) * 6);
    if (std::fread(g_mesh.data.data(), 6 * sizeof(float), nv, f) != nv) {
        std::fclose(f);
        return false;
    }
    if (std::fread(&ni, 4, 1, f) != 1 || ni == 0 || ni > 8u << 20) {
        std::fclose(f);
        return false;
    }
    g_mesh.idx.resize(ni);
    if (std::fread(g_mesh.idx.data(), 4, ni, f) != ni) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    g_mesh.ok = true;
    // Procedural paint bake (CPU once, ~60k verts — negligible one-time).
    BakePaint(g_mesh.colO, true);
    BakePaint(g_mesh.colB, false);
    // VBO upload (optional; falls back to client arrays).
    glGenBuffers(1, &g_mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 g_mesh.data.size() * sizeof(float), g_mesh.data.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenBuffers(1, &g_mesh.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_mesh.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 g_mesh.idx.size() * sizeof(uint32_t), g_mesh.idx.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glGenBuffers(1, &g_mesh.cboO);
    glBindBuffer(GL_ARRAY_BUFFER, g_mesh.cboO);
    glBufferData(GL_ARRAY_BUFFER,
                 g_mesh.colO.size() * sizeof(float), g_mesh.colO.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenBuffers(1, &g_mesh.cboB);
    glBindBuffer(GL_ARRAY_BUFFER, g_mesh.cboB);
    glBufferData(GL_ARRAY_BUFFER,
                 g_mesh.colB.size() * sizeof(float), g_mesh.colB.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    std::fprintf(stderr, "[fennec] loaded %s: %u verts, %u tris (CC-BY-4.0 Jako/fairlight51)\n",
                 path, nv, ni / 3);
    return true;
}

bool FennecAvailable() { return g_mesh.ok; }

void FennecDraw(Renderer& r, const float pos[3], float yaw, bool orange) {
    if (!g_mesh.ok) return;
    glPushMatrix();
    glTranslatef(pos[0], pos[1], pos[2]);
    glRotatef(yaw * 180.0f / (float)M_PI, 0, 0, 1);
    DrawBody(orange ? 1.0f : 0.0f, r);
    glPopMatrix();
    r.NoteExternalDraw(uint64_t(g_mesh.idx.size() / 3));
}

void FennecDraw(Renderer& r, const float pos[3], const float fwd[3],
                const float up[3], bool orange) {
    if (!g_mesh.ok) return;
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
    float l[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2],
                  u[0] * f[1] - u[1] * f[0]};
    float m[16] = {
        f[0], f[1], f[2], 0,
        l[0], l[1], l[2], 0,
        u[0], u[1], u[2], 0,
        0, 0, 0, 1};
    glPushMatrix();
    glTranslatef(pos[0], pos[1], pos[2]);
    glMultMatrixf(m);
    DrawBody(orange ? 1.0f : 0.0f, r);
    glPopMatrix();
    r.NoteExternalDraw(uint64_t(g_mesh.idx.size() / 3));
}

int FennecTriCount() { return g_mesh.ok ? int(g_mesh.idx.size() / 3) : 0; }
int FennecVertCount() { return g_mesh.ok ? int(g_mesh.data.size() / 6) : 0; }

} // namespace rl
