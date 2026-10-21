---
name: raw-cpp-opengl-compatibility-builder
description: Build and debug raw C++20 + OpenGL 4.5 compatibility-profile game code in RocketLeagueLaptop without breaking determinism, the fixed-function default, or the physics/visual gates. Fixed-function and programmable (GLSL <= 4.50, VAO/VBO) paths are both allowed; the perf bar in docs/RENDER_POLICY.md decides. Use for renderer, camera, model, FX, HUD, arena-draw or screenshot work in this repo.
---

# Raw C++20 + GL 4.5 compatibility builder (RocketLeagueLaptop)

Project-local skill. It encodes this repo's real constraints, not generic C++/GL advice.

## Hard constraints (do not violate)

- **C++20, g++ only, no cmake.** Build through the repo Makefile targets
  (`make all`, `make game`, `make render_stub`), never a hand-rolled `g++` line —
  the Makefile carries the determinism flags.
- **GL 4.5 compatibility profile; fixed-function is the default, not the
  ceiling.** `MASTER_PROMPT_VISUALS.md` §3's "no shaders, no VAO, no GL 3.x"
  freeze is **superseded** by `docs/RENDER_POLICY.md` (rev 2026-10-20): a
  programmable VS+FS path (GLSL ≤ 4.50, VAOs/VBOs) is allowed in the *same*
  4.5-compat context, subject to the **PG1** perf bar (A ≥60 FPS, p99 ≤ 16.67 ms,
  ≤1.15× the default `avg`; B p99 ≤ 8.33 ms; `drops=0`, `glerr=0`) on A *and* B
  plus a stable `VG1` md5. Still banned: a core-profile *context*, a 2.1/3.3 or
  llvmpipe fallback, compile/link or sync readback inside the frame loop.
  Both supported machines expose 4.5 compat, so availability is not the issue —
  measured cost is.
- **Sim-path flags stay** (`-ffp-contract=off -fno-fast-math -fno-strict-aliasing`).
  Presentation code never sits on a sim code path.
- **Sim and render are Z-up** (RL convention, `src/render/Renderer.h:16`). The
  Fennec asset is authored **Y-up** and goes through the converter — never
  hand-roll a matrix swap. Assuming "right-handed Y-up" generates wrong code.
- Visual changes never touch `tools/verify/gates*`; feel/juice changes go through
  `docs/PLAYTEST_CHECKLIST.md` / `docs/blind_feel.md`.
- Every landed change updates the owning `BUILD_NOTES.md` (repo Rule 2).

## Verify, do not guess

- **Visual proof already exists — use it.** `build/game --shot-at-tick N
  --shot-path P`, `--shot-every N --shot-dir D`, `--calibrate`,
  `tools/verify/capture.sh`, `tools/verify/visual_gates.py` (VG1–VG16).
  Do **not** invent an ad-hoc `stb_image_write` / `glReadPixels` dumper;
  `Renderer::SaveScreenshot` already does the P6 PPM + row-flip.
- **A visual bug reported without a screenshot:** reproduce it under a fixed
  `--cam eye...,target...` capture, then inspect the dumped pixels. Do not reason
  about matrix math from the description alone.
- Full bar before claiming done: `measured.py` 19/19, `mesh_gates.py` 13/13,
  `visual_gates.py` VG1–VG16, `make tests`, `/tmp/arena_test` 55/55,
  `verify_vs_rocketsim.py` 110/110.
- Docs-only changes run **no** build and **no** gates (`docs/GATE_POLICY.md`);
  `make check-touched` prints the tier.

## Debug levers that already exist

- `--debug-colliders` — oriented car hitboxes, ball sphere, pads, arena shell and
  goal boxes, x-ray (depth test off).
- `--flat` unlit path (weak-GPU / isolate lighting); `RL_TEXDBG` / `RL_NOTEX`
  texture dumps; `F` freecam; `F12` screenshot; `build/render_stub`
  renderer-only harness.
- **`glGetError()` is polled exactly once per frame** in `Renderer::EndFrame`
  (accumulated into `glErrors`). Never add per-draw error checks — that is the
  pacing budget the VG gates assert. `glDebugMessageCallback` *does* exist at
  4.3+ (`KHR_debug`) and therefore in a 4.5-compat context, but it is not the
  gate's error source (VG7 reads `glErrors`); do not silently swap the poll for
  it without extending the gate.
- Frame GL state is reset every `BeginFrame` and must stay a pure function of the
  current frame's draws; the 2D overlay helpers save/restore blend + depth.

## The two bug classes that actually bite here

1. **Matrix / coordinate composition** — column-vs-row major, translation landing
   in normals, Y-up→Z-up. This is what broke the Fennec (buried + sheared car).
   Confirm with a fixed-camera capture, never by grooming the math in your head.
2. **GL state leakage between frames** — a helper leaving blend or depth set. This
   once made every translucent surface opaque from frame 2 onward.

## Resource style

- Draw with immediate mode (`glBegin`/`glEnd`) or the existing model helpers. The
  render path has no owning raw pointers, no `malloc`/`free`, and allocates no
  per-frame GL handles.
- A programmable path is legal but must stay boring: one program, static
  VAO/VBOs uploaded once, `GL_GLEXT_PROTOTYPES` entry points, compile at init,
  no per-frame handle churn, no `glMapBuffer`. Read `docs/RENDER_POLICY.md` §2
  before writing one, and measure it against PG1 (§3) before claiming anything.
- The single static Fennec VBO/IBO is uploaded **once** and reused
  (`src/models/Fennec.cpp:84-95`). Do not add dynamic VBO rebuilds, and do not
  churn the working fixed-function path into RAII wrappers for their own sake —
  the visual gates require byte-identical captures (VG1), so gratuitous refactors
  are a regression risk, not a cleanup.
