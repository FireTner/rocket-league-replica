// Fennec.h — community Fennec body, car-local uu, +X forward, z-up.
// Mesh: CC-BY-4.0, Jako (https://sketchfab.com/fairlight51).
#pragma once

namespace rl {

class Renderer;

// Load once (idempotent). Returns true when the mesh is available.
bool FennecLoad(const char* path = "assets/models/fennec.bin");

// Draw at car origin with yaw (z-up radians) in team paint. Falls back to
// drawing nothing if not loaded (caller keeps the box car as fallback).
// steerRad / spinRad are the procedural wheel angles (Models.cpp): front
// hubs yaw about local +Z, every hub rolls about local +Y. Zero leaves the
// wheels in the baked rest pose.
void FennecDraw(Renderer& r, const float pos[3], float yaw, bool orange,
                float steerRad = 0, float spinRad = 0);

// Full pitch and roll from forward and up. Same mesh and paint.
void FennecDraw(Renderer& r, const float pos[3], const float fwd[3],
                const float up[3], bool orange, float steerRad = 0,
                float spinRad = 0);

// Triangles and vertices in the loaded mesh, or 0 when it is absent.
int FennecTriCount();
int FennecVertCount();

bool FennecAvailable();

} // namespace rl
