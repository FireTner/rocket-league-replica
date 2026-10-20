// ArenaShell.h — G3b arena draw-bucket VBOs (GRAPHICS45 §5/G3 step 2).
//
// Presentation-owned geometry bake: replicates the per-frame arena immediate
// calls in src/game/main.cpp (read-only this step — G3c owns the wire-up)
// into build-once GL_STATIC_DRAW buckets drawn with fixed-function arrays
// (glVertex/Normal/Color/TexCoordPointer + glDrawElements, the Fennec
// precedent in src/models/Fennec.cpp:152-186,221-229). No shaders, no VAOs.
//
// Bucket plan: docs/ARENA_BUCKETS.md §§1-6. Draw order when live:
//   DrawArenaBuckets:      (a+d) -> turf -> wallbase -> ceiling -> skirt ->
//                          B2 overlay -> glass          (7 GL draws)
//   DrawArenaGoalInteriors: C5 shells -> C6 weave        (2 GL draws, after
//                          cars/ball, depth-write OFF — same split as today)
// Texture binds (hexnet on/off) stay dynamic per draw; team tints are baked
// per-vertex (per-goal colors differ, so no dynamic tint is needed).
// Buffers are built per mode via meshMode (== sim->usingRealSoccar(), chosen
// by G3c at the call site) and never mixed.
#pragma once

namespace rl {

class Renderer;

namespace ArenaShell {

// Draws 1..7 (opaque buckets + B2 overlay + glass). meshMode selects the
// mesh vs fallback VBO set (built once on first use after GL context exists).
// noHexNet selects the PlainGlass bind for the glass draw (geometry+UVs are
// shared; only the bind swaps, as today).
void DrawArenaBuckets(Renderer& r, bool meshMode, bool noHexNet);

// Draws 8..9 (C5 interior shells + C6 weave backs) with the depth-write-off
// sandwich. Call after cars/ball, exactly where main.cpp:1568-1592 draws
// them today. Same meshMode-selected buffer set as DrawArenaBuckets.
void DrawArenaGoalInteriors(Renderer& r, bool meshMode);

// Release bucket VBOs/IBOs (GL context must be live). Called from
// Renderer::Shutdown before the context is deleted.
void ShutdownArenaBuckets();

} // namespace ArenaShell

} // namespace rl
