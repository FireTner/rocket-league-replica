# RENDER_POLICY — backend + programmable-pipeline policy (rev 1, 2026-10-20)

**Status: normative.** This file is the owner decision on the render backend.
It **supersedes** the "fixed-function only / no shaders, no VAO, no GL 3.x"
clause in `MASTER_PROMPT_VISUALS.md` §3 and in the
`raw-cpp-opengl-compatibility-builder` skill. Everything else about the
backend (GL 4.5 Compatibility as a hard target, no fallback, two machines,
gates, determinism) is **unchanged**.

Summary: **fixed-function is the default, not the ceiling.** A programmable
(vertex+fragment) path is now allowed inside the *same* GL 4.5 Compatibility
context, provided it clears the measured performance bar in §3.

## 1. Context rule (unchanged, still hard)

- The context stays **GL 4.5 Compatibility**, requested at startup with a loud
  abort below it. No 2.1/3.3 path, no llvmpipe, no Vulkan, **no core-profile
  context** (`SDL_GL_CONTEXT_PROFILE_CORE` stays banned).
- Why the programmable path is legal here: a 4.5 *compatibility* context is a
  superset of 4.5 core — shaders, VAO/VBO, FBO and GLSL ≤ 4.50 are all core in
  4.5, while the compat profile keeps every legacy entry point. "Allow
  programmable" is therefore **not** "migrate to core", and it needs no
  fallback: both supported machines expose 4.5 compat
  (A = `AMD Radeon R4 (radeonsi, stoney)`, Mesa 26.2.4; B = `RTX 4060 Laptop`,
  NVIDIA 615.71.09). **Availability is not the constraint — measured cost is.**
- Still banned: a 4.5 check that falls back instead of aborting; a shader-only
  build that would not run without 4.5; `#version 330 core` used as the only
  compile target; llvmpipe as a supported path.

## 2. What a programmable path may do

- **API:** vertex + fragment shaders, VAOs, VBOs/IBOs, FBOs, GLSL up to
  `#version 450` (or the `compatibility` profile). Keep the floor machine to
  vertex+fragment: do not *require* geometry/tessellation shaders, and use
  compute only for init-time uploads when it measurably pays.
- **Entry points:** reuse the repo's existing `GL_GLEXT_PROTOTYPES` pattern
  (`src/models/Fennec.cpp:4`, `src/models/ArenaShell.cpp:45`). No third-party
  loader (glad/GLEW/epoxy) without a `docs/THIRD_PARTY.md` row — a loader is a
  dependency; Mesa's `glext.h` prototypes are not.
- **Programs:** compile + link at init (or load a binary cache). **Never**
  compile/link/map-check inside the frame loop.
- **Geometry:** static VAOs/VBOs uploaded once, per the Fennec precedent
  (`src/models/Fennec.cpp:84-95`). No per-frame buffer rebuilds, no per-frame
  allocation, no `glMapBuffer` stalls, no sync readbacks.
- **State:** draws must stay a pure function of the current frame; the
  `BeginFrame` reset contract (`src/render/BUILD_NOTES.md`) applies to
  programmable state too.
- **Reachability:** a programmable path may ship default-ON or behind a
  `[render]` flag. Either is fine **only if §3 passes**; until then it stays
  behind a default-OFF flag so gated captures remain byte-stable.

## 3. The performance bar ("as long as it's ensured")

Any programmable path must show, **on both A and B**, `1280x720`, `--no-vsync`,
a 60 s scripted run (the `--soak`/VG7 recipe), against the *default*
fixed-function run of the same scenario:

| Metric | Source field | Bar |
|---|---|---|
| sustained framerate | `[game] exit ... fps~` | **A: ≥ 60** (avg ≤ 16.67 ms) |
| hitch bound | `p99=` | **A: ≤ 16.67 ms**; **B: ≤ 8.33 ms** |
| dropped ticks | `drops=` | **0** on A and B |
| GL errors | `glerr=` | **0** on A and B |
| geometry budget | `tris_last` / `draws_last` | VG7 `TRI_CAP=240000` still holds |
| relative cost | `avg=` vs the default run | **≤ 1.15×** the default path's `avg` |
| determinism | VG1 | two runs on the same machine → identical md5 |
| frame-loop rules | diff review | no compile/link, no `glFinish`/sync readback, no per-draw `glGetError` |

Every row is quoted with the machine's
`GL_VENDOR/RENDERER/VERSION` strings. **A must be measured on A**; "A not
measured" blocks any 60 FPS claim (this dev box is B).

**Gate:** **PG1 — programmable-path perf** (pending: not yet implemented in
`tools/verify/visual_gates.py`). Until PG1 exists, read the same fields off the
`[game] exit` line and record both runs (default + programmable) in
`docs/VISUAL_NOTES.md`. PG1 is required before a programmable path (a) becomes
the default, (b) appears in any gated capture, or (c) is quoted in a verdict.

## 4. Interaction with the existing gates

- **VG1** stays the determinism baseline. The default path must remain
  byte-identical (`md5=0a2bbb529ea559daaf5c922efc3ba4b8`) until PG1 is green on
  A and B **and** the baseline is deliberately re-recorded with a full
  `visual_gates.py` run per machine.
- **VG7** stays counters + GL errors + 60 s soak; unchanged.
- **Precedent:** G6a PBR-lite (`src/render/PbrGl.*`) is already a default-OFF
  eye-candy path that keeps captures byte-stable — it happens to need no
  program at all. G6b (cascades) / G6c (atmosphere) may now be real GLSL paths
  under §2–§3 instead of being recorded as `UNVERIFIED (GL2.1 limit)`.

## 5. Ownership + evidence duties

- Presentation-owned (`src/render`, `src/models`, `src/fx`); never a sim code
  path, never a `src/sim|arena|physics` diff (AGENTS.md Rule 2).
- Every landed programmable path adds: a `src/render/BUILD_NOTES.md` entry, the
  A+B perf rows from §3, its PG1/VG7 line, and — if user-visible — a
  `docs/INI_SCHEMA.md` key with the roundtrip test green.
- Programmable paths never touch gate *values* (AGENTS.md Rule 5); they only
  add evidence rows.
