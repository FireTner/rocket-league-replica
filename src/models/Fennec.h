// Fennec.h — CC-BY-4.0 community Fennec body mesh (Jako, sketchfab
// fairlight51; community recreation, NOT extracted game geometry). Loaded
// from assets/models/fennec.bin (see tools/convert_fennec.py + LICENSE).
// Ownership: A4 render. Drawn in car-local uu, z-up, +X forward, origin at
// the RocketSim car origin (rest z = 17).
#pragma once

namespace rl {

class Renderer;

// Load once (idempotent). Returns true when the mesh is available.
bool FennecLoad(const char* path = "assets/models/fennec.bin");

// Draw at car origin with yaw (z-up radians) in team paint. Falls back to
// drawing nothing if not loaded (caller keeps the box car as fallback).
void FennecDraw(Renderer& r, const float pos[3], float yaw, bool orange);

// Orientation-aware variant (W3): full pitch/roll from the Snapshot's
// forward/up unit vectors. Same mesh, same paint, same bake transform.
void FennecDraw(Renderer& r, const float pos[3], const float fwd[3],
                const float up[3], bool orange);

// Total triangles in the loaded mesh (0 when absent) — feeds the V2 budget
// report so the Fennec overage is a measured number, not an estimate.
int FennecTriCount();
int FennecVertCount();

bool FennecAvailable();

} // namespace rl
