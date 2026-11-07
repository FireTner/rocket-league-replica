// Fennec.cpp — FEN2 wheel parts (steer/spin) or FEN1 plus a TEX1 albedo block.
// Mesh and sheets: CC-BY-4.0, Jako (https://sketchfab.com/fairlight51).
#include "Fennec.h"
#include "render/Renderer.h"
#define GL_GLEXT_PROTOTYPES 1 // VBO entry points (GL 1.5 core) in Mesa's gl.h
#include <GL/gl.h>
#include "stb_image.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rl {

namespace {
struct Part {
    std::vector<float> data;   // interleaved 6f: pos3 + normal3, hub-local for wheels
    std::vector<uint32_t> idx;
    std::vector<float> colO;   // baked paint, orange team (3f/vert)
    std::vector<float> colB;   // baked paint, blue team (3f/vert)
    float pivot[3] = {0, 0, 0};
    bool steers = false;
    GLuint vbo = 0, ibo = 0, cboO = 0, cboB = 0;
};
std::vector<Part> g_parts;
bool g_ok = false;

int PartTris() {
    int n = 0;
    for (const Part& p : g_parts) n += int(p.idx.size() / 3);
    return n;
}

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
void BakePaint(Part& part, std::vector<float>& out, bool orange) {
    size_t nv = part.data.size() / 6;
    out.resize(nv * 3);
    for (size_t i = 0; i < nv; i++) {
        float x = part.data[i * 6 + 0] + part.pivot[0];
        float y = part.data[i * 6 + 1] + part.pivot[1];
        float z = part.data[i * 6 + 2] + part.pivot[2];
        float nz = part.data[i * 6 + 5];
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

void DrawPart(const Part& part, bool orange) {
    if (part.idx.empty()) return;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (part.vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)0);
        glNormalPointer(GL_FLOAT, 6 * sizeof(float),
                        (const void*)(3 * sizeof(float)));
        glBindBuffer(GL_ARRAY_BUFFER, orange ? part.cboO : part.cboB);
        glColorPointer(3, GL_FLOAT, 0, (const void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ibo);
        glDrawElements(GL_TRIANGLES, (GLsizei)part.idx.size(),
                       GL_UNSIGNED_INT, (const void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else {
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), part.data.data());
        glNormalPointer(GL_FLOAT, 6 * sizeof(float), part.data.data() + 3);
        glColorPointer(3, GL_FLOAT, 0, (orange ? part.colO : part.colB).data());
        glDrawElements(GL_TRIANGLES, (GLsizei)part.idx.size(),
                       GL_UNSIGNED_INT, part.idx.data());
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void DrawBody(float orange, Renderer& r, float steerRad, float spinRad) {
    // Baked procedural paint (color VBO / client array). The vertex colours
    // drive ambient+diffuse through GL_COLOR_MATERIAL — no per-frame cost.
    bool isO = orange > 0.5f;
    if (!g_parts.empty()) DrawPart(g_parts[0], isO);
    // Same angles as the procedural box wheels: steer about +Z, spin about +Y.
    float steerDeg = steerRad * 180.0f / (float)M_PI;
    float spinDeg = spinRad * 180.0f / (float)M_PI;
    for (size_t i = 1; i < g_parts.size(); i++) {
        const Part& w = g_parts[i];
        glPushMatrix();
        glTranslatef(w.pivot[0], w.pivot[1], w.pivot[2]);
        if (w.steers) glRotatef(steerDeg, 0, 0, 1);
        glRotatef(spinDeg, 0, 1, 0);
        DrawPart(w, isO);
        glPopMatrix();
    }
    DrawTrim(isO, r);
}
constexpr uint32_t kTeam = 1;
constexpr uint32_t kEmissive = 2;
constexpr uint32_t kGlass = 4;
constexpr uint32_t kMetal = 8;

struct Image {
    std::string name;
    std::vector<unsigned char> png;
    int w = 0, h = 0;
    GLuint id = 0;
};

struct Mat {
    float r = 1, g = 1, b = 1;
    int tex = -1;
    uint32_t flags = 0;
    std::vector<uint32_t> idx;
    GLuint ibo = 0;
};

struct Mesh {
    std::vector<float> data;  // 6 floats: pos3 + normal3
    std::vector<float> uv;    // 2 floats per vertex
    std::vector<uint32_t> idx;
    std::vector<Image> images;
    std::vector<Mat> mats;
    GLuint vbo = 0;
    bool ok = false;
    bool gpu = false;
};
Mesh g_mesh;

void SpecFor(uint32_t flags, bool textured, float& sr, float& sg, float& sb, float& shine) {
    if (flags & kGlass) {
        sr = 0.55f; sg = 0.58f; sb = 0.62f; shine = 96.0f;
    } else if (flags & kMetal) {
        sr = 0.50f; sg = 0.46f; sb = 0.32f; shine = 64.0f;
    } else if (flags & kTeam) {
        sr = 0.35f; sg = 0.35f; sb = 0.40f; shine = 48.0f;
    } else if (textured) {
        sr = 0.14f; sg = 0.14f; sb = 0.14f; shine = 24.0f;
    } else {
        sr = 0.05f; sg = 0.05f; sb = 0.05f; shine = 12.0f;
    }
}

bool ReadTex1(FILE* f, uint32_t nv) {
    char magic[4];
    if (std::fread(magic, 1, 4, f) != 4) return false;
    if (magic[0] != 'T' || magic[1] != 'E' || magic[2] != 'X' || magic[3] != '1')
        return false;
    uint32_t nuv = 0;
    if (std::fread(&nuv, 4, 1, f) != 1 || nuv != nv) return false;
    g_mesh.uv.resize(size_t(nv) * 2);
    if (std::fread(g_mesh.uv.data(), sizeof(float), g_mesh.uv.size(), f) != g_mesh.uv.size())
        return false;
    uint32_t nimg = 0;
    if (std::fread(&nimg, 4, 1, f) != 1 || nimg > 16) return false;
    g_mesh.images.resize(nimg);
    for (uint32_t i = 0; i < nimg; i++) {
        char name[16];
        uint32_t len = 0;
        if (std::fread(name, 1, 16, f) != 16 || std::fread(&len, 4, 1, f) != 1 || len > 32u << 20)
            return false;
        name[15] = 0;
        g_mesh.images[i].name = name;
        g_mesh.images[i].png.resize(len);
        if (len && std::fread(g_mesh.images[i].png.data(), 1, len, f) != len) return false;
    }
    uint32_t nmat = 0;
    if (std::fread(&nmat, 4, 1, f) != 1 || nmat > 32) return false;
    g_mesh.mats.resize(nmat);
    for (uint32_t i = 0; i < nmat; i++) {
        Mat& m = g_mesh.mats[i];
        int32_t tex = -1;
        uint32_t flags = 0, nidx = 0;
        if (std::fread(&m.r, 4, 1, f) != 1 || std::fread(&m.g, 4, 1, f) != 1 ||
            std::fread(&m.b, 4, 1, f) != 1 || std::fread(&tex, 4, 1, f) != 1 ||
            std::fread(&flags, 4, 1, f) != 1 || std::fread(&nidx, 4, 1, f) != 1 ||
            nidx > 8u << 20)
            return false;
        m.tex = tex;
        m.flags = flags;
        m.idx.resize(nidx);
        if (nidx && std::fread(m.idx.data(), 4, nidx, f) != nidx) return false;
    }
    return true;
}

void LoadOverride(Image& im) {
    if (im.name.empty()) return;
    std::string path = "car_assets/fennec/" + im.name + ".png";
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) return;
    im.w = w;
    im.h = h;
    im.png.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    std::fprintf(stderr, "[fennec] local sheet %s (%dx%d, not shipped)\n",
                 path.c_str(), w, h);
}

bool EnsureGpu() {
    if (g_mesh.gpu) return g_mesh.vbo != 0;
    if (!glGetString(GL_VERSION)) return false;
    g_mesh.gpu = true;
    stbi_set_flip_vertically_on_load(1);
    std::vector<float> inter;
    inter.resize(g_mesh.data.size() / 6 * 8);
    size_t nv = g_mesh.data.size() / 6;
    for (size_t i = 0; i < nv; i++) {
        for (int k = 0; k < 6; k++) inter[i * 8 + k] = g_mesh.data[i * 6 + k];
        float u = 0, v = 0;
        if (i * 2 + 1 < g_mesh.uv.size()) {
            u = g_mesh.uv[i * 2];
            v = g_mesh.uv[i * 2 + 1];
        }
        inter[i * 8 + 6] = u;
        inter[i * 8 + 7] = v;
    }
    glGenBuffers(1, &g_mesh.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, inter.size() * sizeof(float), inter.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    for (Image& im : g_mesh.images) {
        int w = 0, h = 0, n = 0;
        unsigned char* px = nullptr;
        if (!im.png.empty() && im.w == 0) {
            px = stbi_load_from_memory(im.png.data(), (int)im.png.size(), &w, &h, &n, 4);
        }
        im.png.clear();
        if (px) {
            im.w = w;
            im.h = h;
            im.png.assign(px, px + size_t(w) * size_t(h) * 4);
            stbi_image_free(px);
        }
        LoadOverride(im);
        if (im.w <= 0 || im.png.size() < size_t(im.w) * size_t(im.h) * 4) {
            im.png.clear();
            continue;
        }
        glGenTextures(1, &im.id);
        glBindTexture(GL_TEXTURE_2D, im.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.w, im.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im.png.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
        im.png.clear();
        std::fprintf(stderr, "[fennec] sheet %s %dx%d\n", im.name.c_str(), im.w, im.h);
    }
    for (Mat& m : g_mesh.mats) {
        if (m.idx.empty()) continue;
        glGenBuffers(1, &m.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.idx.size() * sizeof(uint32_t), m.idx.data(), GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return g_mesh.vbo != 0;
}

void DrawTexBody(bool orange, Renderer& r) {
    if (!EnsureGpu()) return;
    const GLboolean texWas = glIsEnabled(GL_TEXTURE_2D);
    const GLboolean lightWas = glIsEnabled(GL_LIGHTING);
    GLint twoSide = 0;
    glGetIntegerv(GL_LIGHT_MODEL_TWO_SIDE, &twoSide);
    GLfloat prevSpec[4] = {0, 0, 0, 1};
    GLfloat prevShine = 0;
    glGetMaterialfv(GL_FRONT, GL_SPECULAR, prevSpec);
    glGetMaterialfv(GL_FRONT, GL_SHININESS, &prevShine);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, g_mesh.vbo);
    glVertexPointer(3, GL_FLOAT, 8 * sizeof(float), (const void*)0);
    glNormalPointer(GL_FLOAT, 8 * sizeof(float), (const void*)(3 * sizeof(float)));
    glTexCoordPointer(2, GL_FLOAT, 8 * sizeof(float), (const void*)(6 * sizeof(float)));
    for (const Mat& m : g_mesh.mats) {
        if (m.idx.empty() || !m.ibo) continue;
        const bool unlit = (m.flags & kEmissive) != 0;
        if (unlit || !r.LightingOn()) glDisable(GL_LIGHTING);
        else glEnable(GL_LIGHTING);
        if (!unlit) {
            float sr, sg, sb, shine;
            SpecFor(m.flags, m.tex >= 0, sr, sg, sb, shine);
            const GLfloat spec[4] = {sr, sg, sb, 1};
            glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
            glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shine);
        }
        const bool textured = m.tex >= 0 && m.tex < (int)g_mesh.images.size() && g_mesh.images[m.tex].id;
        if (textured) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, g_mesh.images[m.tex].id);
            glColor3f(1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            float cr = m.r, cg = m.g, cb = m.b;
            if (m.flags & kTeam) {
                if (orange) { cr = 1.0f; cg = 0.42f; cb = 0.06f; }
            }
            glColor3f(cr, cg, cb);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glDrawElements(GL_TRIANGLES, (GLsizei)m.idx.size(), GL_UNSIGNED_INT, (const void*)0);
        r.NoteExternalDraw(uint64_t(m.idx.size() / 3));
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, prevSpec);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, prevShine);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, twoSide);
    if (texWas) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (lightWas) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
    glColor3f(1, 1, 1);
}

} // namespace

bool ReadPart(FILE* f, Part& part) {
    uint32_t flags = 0, nv = 0, ni = 0;
    if (std::fread(&flags, 4, 1, f) != 1 ||
        std::fread(part.pivot, 4, 3, f) != 3)
        return false;
    part.steers = (flags & 1u) != 0;
    if (std::fread(&nv, 4, 1, f) != 1 || nv == 0 || nv > 4u << 20)
        return false;
    part.data.resize(size_t(nv) * 6);
    if (std::fread(part.data.data(), 6 * sizeof(float), nv, f) != nv)
        return false;
    if (std::fread(&ni, 4, 1, f) != 1 || ni == 0 || ni > 8u << 20)
        return false;
    part.idx.resize(ni);
    return std::fread(part.idx.data(), 4, ni, f) == ni;
}

void UploadPart(Part& part) {
    BakePaint(part, part.colO, true);
    BakePaint(part, part.colB, false);
    glGenBuffers(1, &part.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
    glBufferData(GL_ARRAY_BUFFER, part.data.size() * sizeof(float),
                 part.data.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenBuffers(1, &part.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, part.idx.size() * sizeof(uint32_t),
                 part.idx.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glGenBuffers(1, &part.cboO);
    glBindBuffer(GL_ARRAY_BUFFER, part.cboO);
    glBufferData(GL_ARRAY_BUFFER, part.colO.size() * sizeof(float),
                 part.colO.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenBuffers(1, &part.cboB);
    glBindBuffer(GL_ARRAY_BUFFER, part.cboB);
    glBufferData(GL_ARRAY_BUFFER, part.colB.size() * sizeof(float),
                 part.colB.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool LoadFen2(FILE* f, const char* path) {
    uint32_t nparts = 0;
    if (std::fread(&nparts, 4, 1, f) != 1 || nparts < 5 || nparts > 8)
        return false;
    g_parts.resize(nparts);
    for (uint32_t i = 0; i < nparts; i++) {
        if (!ReadPart(f, g_parts[i])) {
            g_parts.clear();
            return false;
        }
    }
    for (Part& part : g_parts) UploadPart(part);
    g_ok = true;
    std::fprintf(stderr,
                 "[fennec] loaded %s: %d verts, %d tris, %u parts (CC-BY-4.0 Jako/fairlight51)\n",
                 path, FennecVertCount(), PartTris(), nparts);
    return true;
}

bool LoadFen1(FILE* f, const char* path) {
    uint32_t nv = 0, ni = 0;
    if (std::fread(&nv, 4, 1, f) != 1 || nv == 0 || nv > 4u << 20)
        return false;
    g_mesh.data.resize(size_t(nv) * 6);
    if (std::fread(g_mesh.data.data(), sizeof(float), g_mesh.data.size(), f) != g_mesh.data.size())
        return false;
    if (std::fread(&ni, 4, 1, f) != 1 || ni == 0 || ni > 8u << 20)
        return false;
    g_mesh.idx.resize(ni);
    if (std::fread(g_mesh.idx.data(), 4, ni, f) != ni)
        return false;
    const bool tex = ReadTex1(f, nv);
    if (!tex) {
        Mat m;
        m.r = 0.0f; m.g = 0.13f; m.b = 0.80f;
        m.flags = kTeam;
        m.idx = g_mesh.idx;
        g_mesh.mats.push_back(std::move(m));
        std::fprintf(stderr, "[fennec] %s has no TEX1 block; team colour only\n", path);
    }
    g_mesh.ok = true;
    std::fprintf(stderr,
                 "[fennec] loaded %s: %u verts, %u tris, %zu materials (CC-BY-4.0 Jako/fairlight51)\n",
                 path, nv, ni / 3, g_mesh.mats.size());
    return true;
}

bool FennecLoad(const char* path) {
    if (g_ok || g_mesh.ok) return true;
    const char* file = path ? path : "assets/models/fennec.bin";
    FILE* f = std::fopen(file, "rb");
    if (!f) return false;
    char magic[4];
    if (std::fread(magic, 1, 4, f) != 4 || magic[0] != 'F' || magic[1] != 'E' ||
        magic[2] != 'N') {
        std::fclose(f);
        return false;
    }
    bool ok = false;
    if (magic[3] == '2') ok = LoadFen2(f, file);
    else if (magic[3] == '1') ok = LoadFen1(f, file);
    std::fclose(f);
    if (!ok) {
        g_parts.clear();
        g_mesh = Mesh{};
    }
    return ok;
}

bool FennecAvailable() { return g_ok || g_mesh.ok; }

void FennecDraw(Renderer& r, const float pos[3], float yaw, bool orange,
                float steerRad, float spinRad) {
    if (!g_ok && !g_mesh.ok) return;
    glPushMatrix();
    glTranslatef(pos[0], pos[1], pos[2]);
    glRotatef(yaw * 180.0f / (float)M_PI, 0, 0, 1);
    if (g_ok) {
        DrawBody(orange ? 1.0f : 0.0f, r, steerRad, spinRad);
        r.NoteExternalDraw(uint64_t(PartTris()));
    } else {
        DrawTexBody(orange, r);
    }
    glPopMatrix();
}

void FennecDraw(Renderer& r, const float pos[3], const float fwd[3],
                const float up[3], bool orange, float steerRad, float spinRad) {
    if (!g_ok && !g_mesh.ok) return;
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
    if (g_ok) {
        DrawBody(orange ? 1.0f : 0.0f, r, steerRad, spinRad);
        r.NoteExternalDraw(uint64_t(PartTris()));
    } else {
        DrawTexBody(orange, r);
    }
    glPopMatrix();
}

int FennecTriCount() {
    if (g_ok) return PartTris();
    return g_mesh.ok ? int(g_mesh.idx.size() / 3) : 0;
}
int FennecVertCount() {
    if (g_ok) {
        int n = 0;
        for (const Part& p : g_parts) n += int(p.data.size() / 6);
        return n;
    }
    return g_mesh.ok ? int(g_mesh.data.size() / 6) : 0;
}

} // namespace rl
