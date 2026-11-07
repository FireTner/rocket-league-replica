// ArenaShell.h — static VBOs for the arena shell and goal interiors.
#pragma once

namespace rl {

class Renderer;

namespace ArenaShell {

// Draws 1..7 (opaque buckets + B2 overlay + glass). meshMode selects the
// mesh vs fallback VBO set (built once on first use after GL context exists).
// noHexNet selects the PlainGlass bind for the glass draw (geometry+UVs are
// shared; only the bind swaps, as today).
// wallOpacity is the glass-bucket alpha (0..1). The baked default is 0.40;
// that value keeps the existing modulate path. Any other value replaces only
// the glass alpha for this draw.
void DrawArenaBuckets(Renderer& r, bool meshMode, bool noHexNet, float wallOpacity);

// Goal shells and weave, depth-write off. Call after the cars and the ball.
void DrawArenaGoalInteriors(Renderer& r, bool meshMode);

// Release bucket VBOs/IBOs (GL context must be live). Called from
// Renderer::Shutdown before the context is deleted.
void ShutdownArenaBuckets();

} // namespace ArenaShell

} // namespace rl
