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
    GLuint vbo = 0, ibo = 0;
    bool ok = false;
};
Mesh g_mesh;

void DrawBody(float orange) {
    // Team paint: Fennec reads best with a saturated body + dark trim.
    if (orange)
        glColor4f(0.95f, 0.42f, 0.08f, 1.0f);
    else
        glColor4f(0.16f, 0.42f, 0.95f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)0);
    glNormalPointer(GL_FLOAT, 6 * sizeof(float),
                    (const void*)(3 * sizeof(float)));
    if (g_mesh.vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, g_mesh.vbo);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)0);
        glNormalPointer(GL_FLOAT, 6 * sizeof(float),
                        (const void*)(3 * sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_mesh.ibo);
        glDrawElements(GL_TRIANGLES, (GLsizei)g_mesh.idx.size(),
                       GL_UNSIGNED_INT, (const void*)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else {
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), g_mesh.data.data());
        glNormalPointer(GL_FLOAT, 6 * sizeof(float),
                        g_mesh.data.data() + 3);
        glDrawElements(GL_TRIANGLES, (GLsizei)g_mesh.idx.size(),
                       GL_UNSIGNED_INT, g_mesh.idx.data());
    }
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
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
    DrawBody(orange ? 1.0f : 0.0f);
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
    DrawBody(orange ? 1.0f : 0.0f);
    glPopMatrix();
    r.NoteExternalDraw(uint64_t(g_mesh.idx.size() / 3));
}

int FennecTriCount() { return g_mesh.ok ? int(g_mesh.idx.size() / 3) : 0; }
int FennecVertCount() { return g_mesh.ok ? int(g_mesh.data.size() / 6) : 0; }

} // namespace rl
