# Presentation stream — BUILD_NOTES (render / camera / fx / models / audio)

Owner: A4 (presentation). Peers: `src/sim` (read-only via `Snapshot`),
`src/settings` (INI), `src/game/main.cpp` (coordinator), `tools/verify`
(harness). Nothing here may alter a sim code path.

## Build / run

```sh
make all                 # headless_sim, render_stub, game, tests, arena_test
make build/game
make build/render_stub
python3 tools/verify/visual_gates.py     # VG1-VG12, offscreen, loud SKIP
```

## Renderer contract (GL 2.1 fixed-function, 720p60)

- Draw state is reset every frame in `BeginFrame` (blend SRC_ALPHA/ONE_MINUS,
  depth LEQUAL, lighting per `lightOn_`). The 2D overlay helpers save/restore
  blend+depth instead of clobbering them. **This fixed a real bug:** before the
  reset, the first `FillRect2D`/`OrthoScope` left `GL_BLEND` disabled, so from
  frame 2 onward every translucent surface (glass walls, ball ground indicator,
  pad halos, trails) rendered opaque. Caught by `VG4`/`VG6`.
- Lighting (`MASTER_PROMPT_VISUAL_FIXES.md`): **key + fill + global ambient**,
  `GL_SMOOTH`, colour material ambient+diffuse, **no specular**.
  - key `GL_LIGHT0` dir (0.25,−0.40,0.88) diffuse (0.72,0.74,0.80)
  - fill `GL_LIGHT1` dir (−0.30,0.55,−0.55) diffuse (0.30,0.32,0.36) so
    downward- and backward-facing surfaces are not ambient-only
  - `GL_LIGHT_MODEL_AMBIENT` (0.28,0.34,0.30)
  - **`GL_NORMALIZE`, not `GL_RESCALE_NORMAL`** — `DrawBox` applies a
    non-uniform `glScalef(sx,sy,sz)` to a unit cube; `GL_RESCALE_NORMAL` only
    divides by one factor and left the glass panels' side face with a normal of
    `(-1/10240,0,0)`, which every light rejected (the lit walls rendered flat
    grey regardless of tint). `GL_NORMALIZE` is exact for diffuse-only lighting.
  - specular is forced to 0: with `GL_COLOR_MATERIAL` it is additive over every
    surface and washes saturated tints toward white.
  The previous single directional light (dir 0.3,−0.5,0.8; diffuse 0.75; ambient
  0.45,0.45,0.50) + `GL_FLAT` left the ceiling's −Z face and the +x wall's inner
  face ambient-only (`docs/VISUAL_BUGS_TODO.md` V5). `--flat` turns lighting
  off; `Renderer::LightingOn()` lets FX respect that.
- Draw-budget counters (`Renderer::Stats3D`): triangles, lines, draw calls for
  the last frame plus cumulative totals and a cumulative `glGetError()` count,
  polled once per `EndFrame` (never inside a draw, so it cannot stall the
  loop). Reported in the `[hud]` and `[game] exit` lines and asserted by VG7.
  `NoteExternalDraw()` accounts for the Fennec VBO path, which draws with its
  own `glDrawElements`.
- `SaveScreenshot` writes P6 PPM (bottom-up GL readback flipped to top-down).

## Snapshot additions (visual legibility, no sim change)

- `carForward[3]` / `carUp[3]` — filled from the existing read-only
  `Sim::GetCarForward/GetCarUp`, so car pitch/roll are visible instead of being
  flattened to a 2D yaw.
- `carVel[3]` — the car's world velocity, used for wheel spin (forward speed
  integral) and steer (velocity slip angle). No new sim accessor, no renderer
  physics.

## Camera

Unchanged 1:1 defaults/ranges (`docs/CAMERA_NOTES.md`). Added: a
presentation-only shake overlay applied by the coordinator on the eye, disabled
for freecam/`--no-shake` and never touching `CameraParams`. `LookAt` is
degenerate for a straight-down view (up parallel to the view axis) — visual
gates avoid straight-down cameras for that reason.

## Models / FX

- `FennecDraw(pos, fwd, up, orange)` builds the modelview frame from the
  Snapshot orientation; the yaw-only overload is kept.
- `DrawCar` draws in car-local space (+x forward, +y left, +z up) with four
  procedural wheels: front pair steers, all four spin, and a bright spoke bar
  makes the spin legible.
- `DrawDebugColliders` (W6 `--debug-colliders`) draws oriented car hitboxes,
  the ball collision sphere (91.25), and — from the coordinator — pad pickup
  cylinders, the arena shell and goal boxes, x-ray (depth test off).
- FX advance on **sim time** (the coordinator feeds tick-delta seconds), so
  timers, pulses, the goal shockwave curve and the slow-mo beat are
  deterministic for capture and independent of frame pacing.

## Audio

Unchanged this pass; see `docs/AUDIO_NOTES.md`.

## Verification (this revision)

- `python3 tools/verify/visual_gates.py` → VG1–VG12 PASS (see
  `tools/verify/gates_visual.json`), including VG1 byte-identical captures on
  the NVIDIA 615.71.09 driver and VG7 `ticks=7200 drops=0 glerr=0
  tris_last=187790`.
- Physics regression re-proven on the same revision: `measured.py` 19/19,
  `mesh_gates.py` 13/13, `arena_test` 46/46, `make tests` PASS,
  `verify_vs_rocketsim.py` 110/110.
- 2026-10-14: FX car-feel pass — twin-cone boost flame (white/yellow/orange + smoke), powerslide tire smoke + slide marks + supersonic sparks (DrawDrift), flip streak arcs + stall/cancel freeze ring (DrawAir); Snapshot gained handbrake/airborne/angSpeed filled render-only in main.cpp; Fennec gained license-clean trim (glasshouse, bumpers, skirts, stripe, headlights, taillight bar, underglow). Physics gates 19/19; visual gates need GL re-baseline (VG1 byte-identical will differ).
- 2026-10-14: Fennec trim fix — removed bumper/skirt slabs (read as black boxes below the body) and underglow disc+ring (read as cyan ellipse/axis); kept glasshouse, stripe, headlights, taillight bar.
- 2026-10-14: Fennec paint-mask fix — grille center-only (`*(1-Smooth01(18,24,ay))`, wide nose stays orange) and glass sloped-only (`*(1-Smooth01(0.55,0.8,nz))`, roof crown stays pink). Measured on mesh verts: nose-wide orange 79%, nose-center black 86%, crown pink 100%/black 0%, flanks orange 73%. `measured.py` 19/19; `make tests` ROUNDTRIP FAIL pre-exists (test expects shipped `ballcam=true`, HEAD ships `false`, plus local keybind drift in worktree `config/settings.ini`) — untouched by this change.
