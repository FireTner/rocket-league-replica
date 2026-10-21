# Presentation stream — BUILD_NOTES (render / camera / fx / models / audio)

Owner: A4 (presentation). Peers: `src/sim` (read-only via `Snapshot`),
`src/settings` (INI), `src/game/main.cpp` (coordinator), `tools/verify`
(harness). Nothing here may alter a sim code path.

## Build / run

```sh
make all                 # headless_sim, render_stub, game, tests, arena_test
make build/game
make build/render_stub
python3 tools/verify/visual_gates.py     # VG1-VG16, offscreen, loud SKIP
```

## Renderer contract (GL 4.5 Compatibility hard target, fixed-function default, programmable permitted, 720p60)

- **GL baseline (GRAPHICS45 G2, owner decision):** the context requests **4.5
  Compatibility** and `Init` **aborts loudly** below it — no 2.1/3.3 fallback,
  no llvmpipe, no core-profile *context*. Fixed-function draws
  (`glBegin/glEnd`, `glPushMatrix`, `GL_NORMALIZE`) keep running under 4.5
  compat; VBOs/FBOs/GLSL 4.50-compat are first-class. Supported machines are
  only A = AMD Radeon R4 / Mesa 26.2.4 (GL 4.5 compat, the performance floor —
  60 FPS there is the open question) and B = RTX 4060 / NVIDIA 615.71.09
  (GL 4.6). See `docs/GATE_POLICY.md`.
- **Fixed-function is the default, not the ceiling (rev 2026-10-20).** A
  programmable VS+FS path (GLSL ≤ 4.50, VAOs/VBOs) is allowed *inside the same
  compat context* under `docs/RENDER_POLICY.md`: entry points via the existing
  `GL_GLEXT_PROTOTYPES` pattern, static uploads, compile/link at init only, and
  the **PG1** bar (A ≥60 FPS, p99 ≤ 16.67 ms, ≤1.15× the default `avg`; B p99 ≤
  8.33 ms; `drops=0`, `glerr=0`) green on A *and* B before it becomes default or
  enters a gated capture. The default path stays byte-identical (`VG1`) until
  that baseline is deliberately re-recorded with a full suite run per machine.
- **Visual evidence is currently partial:** `tools/verify/gates_visual.json`
  holds only the VG1 row (recorded on A, tolerance path) after the GRAPHICS45
  pull; the VG2–VG16 rows were dropped there. The checker itself is unchanged
  (VG1–VG16), so re-recording is the debt, not a re-implementation. Note the
  writer **replaces** the file on every run, so an `--only VGx` pass erases the
  other rows. Post-pull spot checks on B (2026-10-20): `VG1` PASS byte-identical
  (`md5=0a2bbb529ea559daaf5c922efc3ba4b8`) and `VG7` PASS
  `ticks=7200 drops=0 glerr=0 tris_last=189144 draws_last=189` — draws
  268 → 189 from the merged buckets, tris +0.6%.
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

## G1 baseline (2026-10-17) — GRAPHICS45 §5/G1, record-only (no code touched)

Two-machine table (A probed live this round; B cited from the repo record):

| Machine | GPU / driver | GL strings | VG1 / VG7 this round |
|---|---|---|---|
| A (this box, live probe) | AMD Radeon R4 (radeonsi, stoney, ACO, DRM 3.64), Mesa 26.2.4-arch1.1, SDL 2.32.74 | `GL_VENDOR=AMD RENDERER=AMD Radeon R4 Graphics (radeonsi, stoney, ACO, DRM 3.64, 7.2.9-arch1-1) VERSION=4.5 (Compatibility Profile) Mesa 26.2.4-arch1.1` | VG1 PASS, VG7 PASS |
| B (not this box — UNREACHABLE/SKIP) | RTX 4060 Laptop, NVIDIA 615.71.09, GL 4.6 (recorded in `docs/VISUAL_VERDICT.md:14`, `docs/PHYSICS_1TO1_LOG.md:607`) | `GL_VENDOR=NVIDIA Corporation RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2 VERSION=4.6.0 NVIDIA 615.71.09` (cited, not re-probed) | SKIP |

Exact gate lines (`timeout 120 python3 tools/verify/visual_gates.py --only VG1,VG7` → 2 PASS, 0 FAIL, 0 SKIP):

- `VG1  PASS  capture determinism                byte-compare differs; 512 probes max|d|=2 (tol 8) driver=GL_VENDOR=AMD RENDERER=AMD Radeon R4 Graphics (radeonsi, stoney, ACO, DRM 3.64, 7.2.9-arch1-1) VERSION=4.5 (Compatibility Profile) Mesa 26.2.4-arch1.1`
- `VG7  PASS  budget + GL errors (60s run)       ticks=7201 drops=0 glerr=0 tris_last=189126 (cap 240000) draws_last=325 fennec_tris=84823`

Build: `build/game` sha256 `d759826a8bb5401811830f6e839fca6ac87cdb9cba7463e757ec081453631e64`
(built via `source tools/dev-local.sh && make build/game`). `check_touched.py`
maps the dirty tree to advisory [15 files incl. `src/render/Renderer.cpp/.h`],
harness [Makefile, 4 tests, `tools/dev-local.sh`, `tools/verify/check_touched.py`],
docs [12 files].

Counter evidence: `Renderer::Stats3D` → `Counters` (last-frame `tris`/`drawCalls`,
`src/render/Renderer.h:164-180`; `CountTris` `:192-194`; publish
`src/render/Renderer.cpp:140-144`); VG7 asserts `1000 < tris <= TRI_CAP =
240000` and `draws > 50` (`tools/verify/visual_gates.py:77,623-626`).
Reference budget rows: kickoff-wide `189,292 / 325`, 60 s scripted `187,982 /
268` (`docs/VISUAL_PARITY_LEDGER.md:138-140`; VG7 line `docs/VISUAL_FIXES_LOG.md:241`).
This round's A-box `189126 / 325` matches the kickoff-wide row, inside the cap.
Cited-not-rerun: `measured.py` 19/19, `arena_test` 46/46 (`docs/VISUAL_VERDICT.md`
§Physics; `docs/PHYSICS_1TO1_LOG.md:578`). No code files changed.

## G2 4.5-compat hard-target (2026-10-17) — GRAPHICS45 §5/G2, builder

Request GL 4.5 Compatibility hard-fail (no fallback), log GL strings, `[render] msaa`.

Build: `source tools/dev-local.sh && make build/game` → exit 0 (warnings only:
misleading-indentation in Camera/FX, unused `DrawBall r`). Binary sha256
`ebc47202f789af809a183ea37e86964754a978c6cbab479a9273e79e8bdeb2dd`.

Startup GL (A-box, offscreen `gamescope --backend headless`, `--shot-at-tick 120`):

- `[render] GL4.5-compat ctx ok, vsync=OFF (swapInterval rc=0)`
- `[render] GL_VENDOR=AMD RENDERER=AMD Radeon R4 Graphics (radeonsi, stoney, ACO, DRM 3.64, 7.2.9-arch1-1) VERSION=4.5 (Compatibility Profile) Mesa 26.2.4-arch1.1`
- B (RTX 4060/NVIDIA 615.71.09): unreachable from this box — never stalled; SKIP per brief.

Capture: `/tmp/g2_shot.ppm` (2764816 bytes, P6 1280x720) via
`gamescope --backend headless -W 1280 -H 720 -r 60 -- ./build/game --no-vsync --no-audio --meshdir collision_meshes --seed 7 --ticks 160 --no-shake --shot-at-tick 120 --shot-path /tmp/g2_shot.ppm --cam 3000,-3000,1200,0,0,200`
→ `[game] exit ticks=162 drops=0 frames=34 wall=1.7s tris_last=189330 draws_last=325 glerr=0 fennec_tris=84823`.

Gates (`timeout 120 python3 tools/verify/visual_gates.py --only VGX`):

- `VG1  PASS  capture determinism  two captures byte-identical (GL_VENDOR=AMD RENDERER=AMD Radeon R4 Graphics (radeonsi, stoney, ACO, DRM 3.64, 7.2.9-arch1-1) VERSION=4.5 (Compatibility Profile) Mesa 26.2.4-arch1.1) md5=b872257e82a7deaa219906e0519b4be5`
- `VG7  PASS  budget + GL errors (60s run)  ticks=7201 drops=0 glerr=0 tris_last=189126 (cap 240000) draws_last=325 fennec_tris=84823`

Delta vs G1 baseline (same A-box, `BUILD_NOTES.md` G1 entry): tris 189126→189126
(+0), draws 325→325 (+0), glerr 0→0. Expected: msaa default 0 keeps the
single-sample path; context-version bump adds no geometry. No `Renderer.h` decl
change needed (`Init(w,h,vsync)` unchanged; msaa read from INI inside
`Renderer::Init` + pre-set in `main.cpp` before `Init`).

Files: `src/render/Renderer.cpp` (4.5 attrs + msaa + version hard-fail + log),
`src/game/main.cpp` (pre-Init 4.5/msaa attrs + FATAL abort line),
`src/settings/Settings.{h,cpp}` + `config/settings.ini` + `docs/INI_SCHEMA.md`
(`[render] msaa`, schema v1 additive, default 0). Roundtrip:
`/tmp/t_rt` → `ROUNDTRIP PASS`; msaa probe `/tmp/t_msaa` → `MSAA ROUNDTRIP PASS`.
`make check-touched` → advisory only, no `[hard]` (sim-owned untouched).
No sim accessor needed.

## G3a bucket-plan sweep (2026-10-17) — GRAPHICS45 §5/G3 step 1, read-only (no code touched)

Investigator sweep: every arena draw in the per-frame path inventoried and
classified into the 4 G3 buckets. Full executable plan (bucket table with
file:line call sites, tri/draw estimates, V5/V6-sensitive list, VBO strategy,
VG1-risk note) lives in `docs/ARENA_BUCKETS.md` (presentation-owned planning
doc, new this step).

Headline numbers (mesh mode = VG7 baseline, `--meshdir`, `roundCorners=true`):
arena today **145 draws / 2130 tris** → post-merge **9 GL draws**
((a+d) 1 untextured + (b) 5, one per texture + (c) 3; texture + depth-state
splits force 9 draws from 4 VBO buckets — fixed-function binds one texture
per draw; B2 overlay keeps depth-test OFF, C5+C6 keep depth-write OFF).
Fallback arena ≈ 101 draws (no rounds/cove-diagonals/round-skirts); VBOs are
built per mode at startup, never mixed. Bucket (d) merges into (a) — `DrawBox`
never disables lighting, so state is identical; (d) stays a color label only.

FLAG carried to G3b/parent: frame-total `<60` is unreachable by arena merging
alone (pads alone are ~136 counted draws via `src/fx/FX.cpp:132-152`; dynamic
rest ≈ 170; projected total ≈ 185). Recommend G3 success = arena 145→9 with
frame total ~185, plus a pad-batching G3c follow-up. No re-scope inside G3b.

V5/V6 preserved by construction: ceiling `faceDown` (−Z), inward glass
`quadYaw` + corner π-rotation, crown radial normals, per-goal weave yaw,
`GL_NORMALIZE` stays ON; arc-length UVs with per-axis spans
(`kHexSpanX/kHexSpanY`, `kHexCell=420`), wall-global `uOff/vOff`, negative
corner `uRep`; game UVs never enter bucket (b). `--no-hexnet` swaps texture,
never geometry. VG1 byte-stable expected; risk list (float32 bake chain,
quad diagonal, depth-state boundaries, mode/VBO selection) in the plan doc
§6. G3b proves with VG1 byte-compare + VG7 `tris_last` exact-match.

Docs-only step: this section appended + new `docs/ARENA_BUCKETS.md`; `make
check-touched` shows docs-only (quoted in the G3a report).

## 2026-10-17 — G4 texture chains (GRAPHICS45 §5/G4, models-owner)

Full mip chains generated ONCE at upload (never in frame loop) in all three
upload paths: `UploadRGB` (turf/hex/plain-glass/goal/ceiling/wall-base),
`BuildTurfMarkingsTexture` (RGBA overlay, CLAMP kept), `BuildBallTexture`.
`glGenerateMipmap` (core since 3.0; context is 4.5-compat, linked from libGL,
no loader) + `GL_LINEAR_MIPMAP_LINEAR` min (mag stays `GL_LINEAR`) + aniso
when `GL_EXT_texture_filter_anisotropic` is present. Wrap modes untouched
(`GL_REPEAT` everywhere except markings `GL_CLAMP` + ball T `GL_CLAMP`);
`RL_TEXDBG` level-0 dumps and `RL_NOTEX` draw-time skip preserved. Zero
geometry change (sampler state only). No header decl changes (helpers are
file-local statics; no Renderer/sim touch).

Build: `make build/game` → clean (only pre-existing `-Wunused-parameter`
`DrawBall r`), sha256
`a06d94cc9fc9b14b983fcb51be4aafdca47d9f15747cfe9c5d47af2d80d15c17`
(G2 baseline was `ebc47202…`).

Runtime probe (first upload logs once):
`[tex] G4 chains: trilinear mips, aniso on (max=16.0, chosen=4.0)`
→ aniso cap 4x (R4-class incl. llvmpipe fallback), driver max 16x queried.

Gates: `timeout 120 python3 tools/verify/visual_gates.py --only VG3` →
`VG3 PASS scene render coverage (6 scenes non-empty…)`; summary
`1 PASS, 0 FAIL, 0 SKIP`. `--only VG6` → FAIL both before and after G4
(`hex x-pitch 46 px vs recipe 92.0 px`, 100% blue, G4 contrast 175 vs
pre-G4 646): pre-existing half-period lock, NOT a G4 regression — proven by
rebuilding with chains env-disabled (`G4_NOCHAIN`, since removed) and
re-running VG6 → identical 46 px. Flagged to parent as detector/texture
half-period issue for the hex-net owner.

Shimmer evidence (same-tick captures, final binary):
far `/tmp/g4shots/g4_far.ppm` (`--cam 3000,-3000,1200,0,0,200`)
md5 `5265ff5adec2b40a0a0ed1e57bc7b738`;
near `/tmp/g4shots/g4_near.ppm` (`--cam 500,-600,300,0,0,120`)
md5 `13a23d8e41a971d83638959defd63e98`.
Minified-net signal (VG6 net-only contrast, same cam/recipe) drops 646 →
175 (3.7×): mip averaging replaces single-level aliasing sparkle with a
stable mean; near-field (magnified) pixels still use level 0 + GL_LINEAR,
so close detail is unchanged.

Soak exit line (capture run): `ticks=161 drops=0 … tris_last=189322
draws_last=325 … glerr=0`. Delta vs G1 baseline (189126/325): tris +196
(scene-state jitter at shot tick — G4 adds zero geometry), draws +0,
glerr +0.

`check-touched`: models files map to `[advisory]` tier only (no sim tier).

## G3b bucket-VBO build (2026-10-17) — GRAPHICS45 §5/G3 step 2, builder

New bucket-VBO path, NOT yet wired (old immediate path still live — G3c wires
+ deletes the old calls, then runs VG1/VG7). Owns: `src/render/Renderer.*`
(API) + new `src/models/ArenaShell.*` (bake + buffers, preferred over
bloating Renderer per brief).

New API (Renderer.h decl — allowed, G3b owns Renderer.*; cited here):
`void DrawArenaBuckets(bool meshMode, bool noHexNet)` (draws 1..7:
(a+d)->turf->wallbase->ceiling->skirt->B2->glass) and
`void DrawArenaGoalInteriors(bool meshMode)` (draws 8..9: C5+C6, stays AFTER
cars/ball — C5/C6 blend OVER cars today via depth-write-off, so a single
call would lose the net veil). G3c: `renderer.DrawArenaBuckets(
sim->usingRealSoccar(), noHexNet)` at the arena block +
`renderer.DrawArenaGoalInteriors(sim->usingRealSoccar())` at main.cpp:1568.
No sim accessor, no main.cpp edit, no ArenaTextures/Models.cpp touch.

Bake replicates every main.cpp call-site literal (frames, UVs, colors,
float32 chain, arcSweep sinf/cosf + planRound std::cos/sin narrowed, quad
diagonal (v0,v1,v2)+(v0,v2,v3), true strip triangulation for the 4 s6
spheres): face-down ceiling, glass quadYaw inward, corner pi-rotation +
neg-U REPEAT, crown radial normals, weave yaw per goal, unit baked normals
(GL_NORMALIZE idempotent). Box normals are yaw-rotated face axes (== what
GL_NORMALIZE yields after the immediate non-uniform scale — exact). Binds
(hexnet/plain, all sheets) resolve at draw time — geometry/UVs shared.
Build-once per mode (GL_STATIC_DRAW, Fennec pattern) after ctx; VBOs freed
in Renderer::Shutdown before ctx delete. Counters use BAKED tris per draw,
so VG7 cannot shift by construction; DrawBall/wheel gap untouched.

DEVIATION (accepted, baked): C1 is 8 glassSub quads = **16** tris, but the
G3a table prints 24/8 (impossible: 8x2=16; call sites main.cpp:1237-1254).
Baked totals: mesh **2122** (plan 2130), fallback **1562**; glass draw 472/216
(plan 480/224); draws exactly 9 as planned. The binary prints baked-vs-plan
per draw (`[buckets] built ...`) so G3c cites it, not the table.

Drive-by: `build/render_stub` failed to link since G2 (Renderer::Init uses
Settings::{Defaults,Load}, stub line lacked Settings.cpp — pre-existing,
verified via `git show HEAD`). Fixed by adding Settings.cpp (+ArenaShell/
ArenaTextures, needed by the new Renderer refs) to the stub line only.

Build: `source tools/dev-local.sh && make build/game` → exit 0 (only
pre-existing warnings: misleading-indentation Camera/FX/Renderer, unused
`DrawBall r`; zero new warnings from ArenaShell.*). sha256
`4b65f991e84f755d4744225bd4a055a6f12e2f89514d12fac15657d015fc0880`
(G4 was `a06d94cc…`). Old-path smoke (same cam as G4 far shot):
`tris_last=189322 draws_last=325 glerr=0` — identical to G4's capture line,
zero drift (new code uncalled). `check-touched` → advisory only, no sim.

## G3c bucket wire-up (2026-10-17) — GRAPHICS45 §5/G3 step 3, builder

Cutover: old immediate arena path deleted, bucket VBOs live. Owns
`src/game/main.cpp` arena section ONLY (read-only everything else).

Hunks (`src/game/main.cpp`, 1891 → 1226 lines, net −665):
- :883-888 KEPT (debug-grid instrument `DrawGroundGrid`, `--debug-grid` only).
- :889-1533 (old) → :889-896 (new): turf B1 + markings B2 overlay + all
  glass/crown/round walls C1-C4 + trim/beams/cove/skirt/goal-trim A/D +
  ceiling B4 + bases B3/B7 + skirts B5/B6 + opaque goal boxes A5/A6 (incl.
  `arcSweep`/`runFrame`/`planRound`/`glassSub`/`cornerGlass`/`skirtQuad`/
  `roundJ`/`roundSkirt` lambdas) DELETED, replaced with
  `renderer.DrawArenaBuckets(sim->usingRealSoccar(), noHexNet)` (:896).
  Order inside buckets (opaque → B2 overlay → glass) preserves the old
  V5/V6 state sandwiches; B2 keeps depth-test OFF inside its draw.
- :1534-1553 (old) pads block KEPT verbatim (FX-owned, pad batching OUT of
  scope). Cars + `DrawBall` untouched.
- :1565-1593 (old) goal interiors → :928-930 (new):
  `renderer.DrawArenaGoalInteriors(sim->usingRealSoccar())`, still AFTER
  cars/ball (C5/C6 blend OVER cars today; depth-write OFF inside).
- :1594-1621 (old) `--debug-colliders` wire + text KEPT verbatim.

Build: `source tools/dev-local.sh && make build/game` → exit 0 (only
pre-existing warnings: misleading-indentation Camera/FX, unused `DrawBall
r`; zero new warnings from the wire-up). sha256
`031789fd3c7f9306f83b955dcba51ce27e1e7cdcab7d5b7d0625cef0ad918a73`
(G3b was `4b65f991…`).

Gates (`timeout 120 python3 tools/verify/visual_gates.py --only VGX`):
- `VG1  PASS  capture determinism  byte-compare differs; 512 probes max|d|=0 (tol 8) driver=GL_VENDOR=AMD RENDERER=AMD Radeon R4 Graphics (radeonsi, stoney, ACO, DRM 3.64, 7.2.9-arch1-1) VERSION=4.5 (Compatibility Profile) Mesa 26.2.4-arch1.1`
- `VG7  PASS  budget + GL errors (60s run)  ticks=7200 drops=0 glerr=0 tris_last=189144 (cap 240000) draws_last=189 fennec_tris=84823`
- `VG3  PASS  scene render coverage  6 scenes non-empty, >=12 colours each, >=20% non-background`
- Each summary: `1 PASS, 0 FAIL, 0 SKIP`.

Draws/tris before → after (VG7 60 s run, mesh mode): draws_last 325 → 189
(Δ −136 = exactly the planned arena 145 → 9 consolidation; no other path
changed count); tris_last 189126 → 189144 (+18, +0.0095%, within ±1%);
glerr 0 → 0, drops 0 → 0. Shot-cam (3000,-3000,1200,0,0,200, tick-120)
exit line: `tris_last=189322 draws_last=189` vs G4 `189322/325` — tris
EXACT at fixed camera, draws −136. Frame total 189 ≈ 185 projected;
pads (~136 draws, `src/fx/FX.cpp:132-152`) stay unmerged — pad batching is
a follow-up, different owner (DoD correction accepted: arena 145→9, frame
≈185, NOT <60).

VG1-shift report (NO silent re-baseline — owner sign-off required):
same-tick far-capture old-vs-new (`/tmp/g4shots/g4_far.ppm` vs rebuilt
`/tmp/g3c_far.ppm`, same recipe `--meshdir collision_meshes --seed 7
--ticks 160 --no-shake --shot-at-tick 120`): sizes equal (2764816 B),
headers equal, 93251/2764800 bytes differ (3.37%), max|d|=243. Near-cam
(`--cam 500,-600,300,0,0,120`): 76855 bytes (2.78%), max|d|=243. New-vs-new
repeat of the far recipe is byte-identical (0 diffs), so the delta is
100% bucket-bake, not run noise. Signature: ±1 floor dominates (56k of
93k far bytes at |d|=1; cell means 0-2 everywhere, no region systematically
off) = float32 bake rounding + a=0.9/0.95 blend off-by-one (plan §6
predicted both); ~1px edge shifts on diagonal/trim edges (e.g. far-cam
diagonal x≈260-288 y≈261-348 dark→bright swap; near-cam vertical edge at
x≈1270-1279) = float32 vertex rounding; 359 far / 170 near pixels with
|d|≥50 cluster at car/wall occlusion silhouettes (center screen) — knock-on
of the sub-pixel edge shifts, not missing geometry (whole-region means
unchanged; wrong-VBO/missing-draw would flip regions, not edges).
`check-touched` → advisory only, no sim tier (main.cpp listed under
[advisory]).

## G5 pacing + counters (2026-10-17) — GRAPHICS45 §5/G5, builder

Owns: `src/render/Renderer.*` (Stats3D + EndFrame/pacing only) + ONE
coordinator log line in `src/game/main.cpp` (soak exit print only).
Forbidden respected: no sim/arena/physics/Tuning/tapes/sim-flag touch, no
`tools/verify/gates*` values, no bucket-draw logic, no ArenaShell/G4 files,
no gameplay-cam numbers, no per-draw glGetError, no blocking wait / sync
readback / shader compile in the frame loop. No sim accessor needed.

Stats3D (`src/render/Renderer.h:184-202`): `Counters` gains `double
frame_ms` (:195 — last-frame CPU ms, wall-clock BeginFrame→EndFrame incl.
the vsync-paced present, no GL sync anywhere; stored in EndFrame
`src/render/Renderer.cpp:209`) plus a G6 forward-compat stub (:200-201:
`struct FlagCost { ms, draws, tris }` + `flagCost` member, default zero —
the eye-candy flags fill it later; no G6 code reads/writes it today).
Once-per-frame `glGetError` drain in EndFrame is untouched (the pacing
budget the VG gates assert).

`[render] max_fps` (schema v1 additive, default 0 = shipped behavior):
`Settings.{h,cpp}` (`RenderSettings::maxFps`, load `max_fps`, save,
`Migrate` clamp 0..1000 with 1..14 floored to 15) + `config/settings.ini`
(`max_fps = 0`) + `docs/INI_SCHEMA.md` table row. Semantics (bevy_framepace
thinking): read once in `Renderer::Init` from the same tolerant INI load
the msaa knob uses (`Renderer.cpp:40-58`, clamped 15..1000); `EndFrame`
(`:214-224`) sleeps the remainder via `SDL_Delay` ONLY when vsync is off
(`!vsync_`, mirrored from the `Init` request) — vsync-on keeps the shipped
present untouched, and the sleep only delays the present (the sim
accumulator absorbs it as ordinary wall dt; backlog kept, only >1 s
dropped), so pacing never costs a physics tick. No main.cpp wiring.

V9 fix (`docs/VISUAL_BUGS_TODO.md` V9): BEFORE — `LogPacingIfDue` did
`fopen("render_pacing.log","a")` on every pacing print (every 5 s, every
run: play/capture/soak), so the CWD accumulated a 163890-byte log. AFTER —
the append is gated on `[render] pacing_log` (default OFF/false;
`Renderer.cpp:253`, member `Renderer.h:229`); the stderr `[pacing]` print
is unchanged. Proof: CWD `render_pacing.log` measured 163890 bytes before
AND after the 60 s VG7 run below (0 bytes appended; 12 `[pacing]` stderr
lines still printed).

Coordinator log line (`src/game/main.cpp`, exit `fprintf` only): exit line
gains `frame_ms=%.2f` (`(double)rc.frame_ms`, last-frame CPU ms). The token
is float-formatted so `counter_of` int parsing for the gated keys
(ticks/drops/glerr/tris_last/draws_last) is unaffected.

Build: `source tools/dev-local.sh && make build/game` → exit 0 (only
pre-existing warnings: misleading-indentation in Renderer draw helpers /
Camera / FX, unused `DrawBall r`; zero warnings in the G5 hunks).
sha256 `260c9af8ae1a90c63d741295e003894842edc5f6b85773e1b6ed4c6701427750`
(stable across the touch-rebuild).

Gates (`timeout 120 python3 tools/verify/visual_gates.py --only VG7`):

- `VG7  PASS  budget + GL errors (60s run)       ticks=7200 drops=0 glerr=0 tris_last=189144 (cap 240000) draws_last=189 fennec_tris=84823`
- summary `1 PASS, 0 FAIL, 0 SKIP`. Delta vs G3c (same A-box): tris
  189144→189144 (+0), draws 189→189 (+0), glerr 0→0, drops 0→0.

Soak exit line (`/tmp/visual_gates/budget60.log`):
`[game] exit ticks=7200 drops=0 frames=4718 wall=60.2s p50=11.00ms
p99=29.00ms avg=12.34ms frame_ms=13.00 unlimited=0 bot=stationary
audio_cb=0 audio_frames=0 tris_last=189144 draws_last=189
tris_total=892313312 draws_total=891716 glerr=0 fennec_tris=84823
fennec_verts=58851`
— drops=0 glerr=0. frame_ms distribution (Pacing hist of the same
BeginFrame→EndFrame dt) on A: p50=11.00ms p99=29.00ms (render-frame pacing
under gamescope headless `--no-vsync`, uncapped ≈78 fps; not the physics
step cost).

Limiter probe (temp `max_fps=30`, `--ticks 1200 --no-vsync`, settings.ini
restored + sha-verified after): `[game] exit ticks=1203 drops=0
frames=295 wall=10.3s … frame_ms=11.00 … glerr=0` → 295/10.3 = 28.6 fps ≈
30 cap (vs 78 uncapped), drops=0 — clamp+sleep engages, never tick loss.

Roundtrip: `/tmp/t_rt` → `ROUNDTRIP PASS` (test file untouched);
`/tmp/t_maxfps` (G5 probe, not committed) → `MAXFPS ROUNDTRIP PASS`
(shipped max_fps=0/pacing_log=false, mutations survive, clamps 5→15,
5000→1000, 0 stays off). `check-touched` → advisory/harness/docs only, no
`[hard]`/sim tier (G5 paths all under `[advisory]`/docs).

## 2026-10-20 — V11 arena shell is one-sided (cull pass in the two arena draws)

Owner report: "when a camera goes through a wall (intentionally) it is too
opaque and stops rendering things inside of the arena". Reproduced with fixed
captures, not by reasoning (`tools/verify/capture.sh`):

```sh
bash tools/verify/capture.sh /tmp/v11 outside --cam 4300,0,400,0,0,400
bash tools/verify/capture.sh /tmp/v11 inside  --cam 3000,0,400,0,0,400
```

`4300,0,400` is 204 uu outside the +X wall plane (4096); the chase rig only
clamps the eye's Z (`main.cpp:840-851`, `cz` 40..1928), and freecam clamps
nothing, so an eye outside a wall is reachable in the shipped build.

Two defects, one root (culling was never enabled for the shell, which is
authored as an interior surface — wall/crown/skirt/weave normals all point INTO
the room):

1. Outer faces rasterise, so from outside you look at an opaque backside.
2. The glass is blended (`kGlassA = 0.40`) but still writes depth, and the
   buckets draw BEFORE pads/cars/ball: the wall's depth lands first, so every
   interior object drawn afterwards was depth-rejected. Turf (drawn earlier)
   survived tinted; cars and ball vanished. That is the "stops rendering"
   half.

Fix, `src/models/ArenaShell.cpp` only:

- `fixWinding()` — after `bake()`, re-wind every triangle so its CCW front face
  agrees with its own vertex normals. Measured flips: opaque 0/1056, turf 0/2,
  wallbase 64/416, ceiling 2/2, skirt 80/144, markings 0/2, glass 152/472,
  shell 0/24, weave 4/4 (the emitted winding was only self-consistent for the
  `DrawBox`/sphere emitters). No vertex, UV, colour, normal or tri count
  changes, so VG7 counters are untouched by construction.
- `CullScope` — `GL_CULL_FACE` + `GL_BACK` + `GL_CCW` enabled for
  `DrawArenaBuckets` and `DrawArenaGoalInteriors` only, with the previous
  enable/mode/front-face saved and restored (same discipline as the existing
  depth-test/depth-mask sandwiches). Culling stays OFF for cars/ball/pads/FX.
- `RL_NOCULL=1` — debug lever (like `RL_NOTEX`) that restores the old
  double-sided arena, so A/B captures can separate the re-wind from the cull
  pass.

Measured, pre-fix → post-fix (seed 7, tick 120, `--no-shake`, 1280x720):

```
eye 4300,0,400 outside +X wall : full green 0.079 -> 0.327
                                 centre green 0.021 -> 0.429  (inside ref 0.432)
                                 car crop (x480-800,y370-400) var 236 -> 1632,
                                 max channel 122 -> 249 (Fennec detail visible)
eye 4300,0,400 --nofennec vs Fennec: 66 px differ in rows 380-383 (cols
                                 500-779) -> the cars ARE drawn now
eye 3000,0,400 inside control  : 76309 px move, 64686 by 1-4/255, 2208 by >8
                                 (max 87, the 1 px wide bright cove lines)
RL_NOCULL=1 vs pre-fix build   : BYTE-IDENTICAL in both views (md5
                                 9027bf5b… chase cam, b5f8a7bb… inside eye),
                                 i.e. the re-wind alone is pixel-neutral and
                                 every delta above is the cull pass alone
default chase cam (in-arena)   : 53954 px move, 52662 by 1-4/255, 850 by >8
                                 (max 118, isolated 1 px depth-tie seams)
```

The inside-frame deltas are the `a = 0.9` trim/beam/cove boxes: their front and
back faces project onto the same pixels, and `GL_LEQUAL` let the far face
blend a second time (visible as the double-bright thin glow lines). Culling
removes that second layer — measured on the inside control, 76309 px move by
1-4/255, 1396 px by more than 12 (the 1-px bright cove lines, max 87). No
surface loses coverage: a culled *whole* surface would show up as a large area
at 50-120, and the histogram has none.

Verification:

- `python3 tools/verify/visual_gates.py` → **15 PASS, 1 FAIL, 0 SKIP**.
  `VG1` PASS byte-identical `md5=5326135d54b86d53532d32d78670030d` (NVIDIA
  615.78.08); `VG7` PASS `ticks=7200 drops=0 glerr=0 tris_last=189144
  draws_last=189 fennec_tris=84823` — tris/draws identical to the G3c record,
  so the cull pass costs no geometry and no draw call;
  `VG11` 66 wall-base samples / 0 centre, `VG14` 98.8% green,
  `VG15` lit/flat 0.54 (need 0.45..1.25), ceiling L=57, walls 31/26.
  `VG6` **FAIL** (hex x-pitch 46 px vs 92) — PRE-EXISTING and unrelated:
  identical numbers on the stashed pre-fix binary and with `RL_NOCULL=1`.
- `make all` exit 0 (only the pre-existing warnings); `/tmp/t_rt` ROUNDTRIP,
  `/tmp/t_kb` KEYBOARD+MENU, `/tmp/t_gp` GAMEPAD, `/tmp/t_cam` CAMERA,
  `/tmp/t_atex` ARENA_TEXTURES (108) all PASS; `/tmp/arena_test` 55/55;
  `measured.py` **19 PASS, 0 FAIL**.
- `check-touched`: `src/models/ArenaShell.cpp` → `[advisory]` tier only, no
  sim tier.
- `sha256 build/game = 0ca5fe7f94e711051b963d756aaba3cba2ab0f5f152dc7a44898bb1e17637a8a`.

Behaviour note for the next visual pass: VG1–VG16 all capture an INSIDE eye, so
they can only catch a cull regression indirectly. The outside-wall recipe above
(`--cam 4300,0,400,0,0,400` → centre green ≈ 0.43) is the direct check until a
gate is added for it.

## A6b (2026-10-20) — the ripped-arena draw path (`--arenadir`)

The engine half of the arena 1:1 order: a ripped arena pack
(`rl-arena-pack/1`, read by `src/models/ArenaRip.h`, A6a) is uploaded ONCE
into merged world-space buckets and drawn with ONE fixed-function
`glDrawElements` per visible bucket per frame.

* **Bucket key** = (texture/material id, 8192 uu spatial cell of the
  placement's world AABB centre). The pack's materials carry no colour, only
  a diffuse, so the texture IS the material id; `-1` is the neutral fallback
  bucket.
* **Upload**: one VBO + one EBO per bucket, `GL_STATIC_DRAW`, at load time
  (`Renderer::BuildArenaPack`, called once after `Init`). Nothing is
  re-uploaded per frame; no shader is compiled; nothing is read back.
* **Draw** (`Renderer::DrawArenaPack`): the six frustum planes are extracted
  from the matrices the camera just installed (`glGetDoublev` of the
  modelview/projection — client-side matrix state, no pipeline flush); a
  bucket whose world AABB lies behind any plane is culled, the survivors are
  drawn one per call. Fixed-function only: `GL_TEXTURE_2D` + the repo's
  key+fill+ambient lighting with `GL_NORMALIZE` + the G4 mipmap chain
  (`glGenerateMipmap`, trilinear, aniso 4x). No GLSL, no VAO.
* **Neutral fallback**: the sections with no diffuse (392 of the 640 used
  section slots in the DFH Stadium day pack) get a mid grey
  `(0.55, 0.55, 0.55)` in their own buckets; the value is logged at startup
  (`fallback_material=neutral-grey(0.55,0.55,0.55)`) and lives in
  `kArenaFallback` (`src/render/Renderer.cpp`).
* **Default path untouched**: with no pack the renderer creates NO GL
  object, issues NO extra call and the frame is byte-identical — MEASURED:
  the VG1 capture md5 is `5326135d54b86d53532d32d78670030d` with AND without
  the A6b change (identical binary-level A/B, same command line).
* The 26 IMPORTED placements (`externalInstances`: the sky sphere and the
  billboard sprites of the sibling package) are NOT drawn: none of them
  carries a diffuse, and the neutral fallback would render the sky sphere as
  a grey dome around the camera. They are counted and reported, never
  silently dropped.

## A6b-center (2026-10-21) — midfield trench, trim-light extrusion, collision planes

Root-caused the three midfield artifacts at triangle level against the pack
bytes (transforming indexed verts with the placement's column-major doubles,
never by reasoning about the math):
* **Dark N–S/E–W cross bands are authored, kept.** The grass slab
  `Field_STD_Floor_Thick` (top z=10) has exact slots where it contributes 0
  triangles (measured: none at x=0, covered again from x≈233); the slots are
  floored by `Field_CenterField_Team1/2` (|x|<233) and `Field_Mid_A`
  (|y|<269) `Frame_01` strips at z=0 — a 10 uu recessed metal inlay, not a
  mapping bug. The `Field_Mid_A` debug skip was reverted (proven innocent:
  no field-collector triangle sits above z=10 at x=0).
* **"Extrusion" around the ball: trim-light rings drawn shaded-dark.**
  `Field_Center_Trim` section 1 (`TrimLight_None_Mat`, param-mask, 350 tris)
  is two concentric rings (r≈272 and r≈640–725) rising to z=64; in game they
  are emissive. They now get fallback tint 5 (near-white `(1.0,0.97,0.90)`)
  and per-bucket `unlit` (lighting disabled for those draws, state
  saved/restored like blend). Same rule covers `TrimLight_Glow_*`,
  `CityLights`, `StadiumLight`, `LightArray_Arrow` — all untextured lights
  that the old "trim→dark metal" order caught via the "trim" substring.
  Textured materials bypass the tint entirely, so e.g. the FakeLight grass
  MICs are unaffected.
* **`Collision_Mat` sections are skipped (counted).** Collision proxies are
  invisible in game; the dark fallback drew them as slabs — most visibly
  six `Plane32768` kill planes, one hung at z=2048 as a dark false ceiling
  over the day stadium, plus the goal/side/corner collision halves.
* **Kept as-is:** the `Sprite`/`FogMesh`/`FieldSpotLights` mesh skips and the
  `LightCone`/`StadiumFog` material skip (translucent/additive FX, already
  validated — the X bands are gone); double-sided rendering (no
  `GL_CULL_FACE`: the pack's CW fronts were measured on the slab top, but
  the skybox shells are viewed from inside and the pack carries no
  per-material cull flag, so global culling would erase the sky).
* MEASURED (`--arenadir arena_assets/stadium_day`, seed 7, tick 120):
  buckets=123 (48 textured / 75 fallback), tris=569941, draws=278,
  glerr=0; fixed-camera captures low/top/elevated show grass wall to wall,
  the glowing center ring, orange pads and white goal frames.

## A6b-glass (2026-10-21) — force-field/glass transparency (opaque rainbow fix)

Bug (playtest capture with `--arenadir arena_assets/stadium_day`): the whole
dome (walls + ceiling) rendered as an opaque black/green/purple honeycomb.
Two roots, both in the pack path (`src/render/Renderer.cpp`, default path
untouched):

1. `DrawArenaPack` drew every bucket with `glDisable(GL_BLEND)` (the DXT5
   grass mask-alpha fix). Glass/force-field sections therefore drew opaque,
   and their vertex alpha was ignored.
2. `Beach_NetFence_MIC`'s diffuse `Hexagons_Pack_01_T` (DXT1 512x512) is a
   packed UE3 shader mask, not albedo — measured 499 distinct RGB values:
   black centres (29%), saturated green rings (27%), blue glow (13%),
   red edges (<1%). Drawn raw as albedo it is the rainbow in the report.

Fix (pack path only, fixed-function, upload-time cost, zero per-frame
handles):

* `ArenaSectionTransparent(mesh, mat)` name join: mesh has `glass`/`cage`/
  `fence`/`force`/`FF_*`, or material has `glass`/`force`/`cage`/`fence`/
  `netfence`/`hexglass`. Verified against the DFH manifest: exactly the 6
  glass/cage materials and the 11 `FF_*`/`*Glass*` meshes match, nothing
  opaque does. Null-slot glass placements (no material) are caught by the
  mesh side.
* Transparency is part of `ArenaKey`, so glass never merges with opaque
  buckets; untextured glass shares one veil bucket (`tint 6`,
  `0.45/0.55/0.60`, the existing glass fallback colour).
* The hex mask is converted ONCE at upload to a faint ice-blue net on
  transparent (`RGB 175/205/235`, `A = G*80/255`, i.e. 0..0.31; green is the
  line mask, black/blue-only/red go fully transparent). Mips generate from
  the converted bytes (G4 precedent).
* `DrawArenaPack` is two passes: opaque buckets blend-off/depth-write-on
  (grass mask-alpha still ignored), then transparent buckets blend-on
  (`SRC_ALPHA/ONE_MINUS_SRC_ALPHA`) with depth-write OFF and depth-test ON —
  the goal-interior C5/C6 sandwich rule, so cars/ball behind the veil still
  draw over it. Restores blend + depth-mask exactly (frame stays a pure
  function of its draws). `ArenaPackStats` gains `transparentBuckets`,
  printed in the `[arena] pack uploaded` line.
* No sky is added: behind the veil is the dark clear colour, while the RL
  reference shows bright sky + buildings — the pack's sky sphere and
  billboards are the deliberately skipped `externalInstances` (grey-dome
  rationale, A6b). Sky scenery is a separate visual pass, not this bug.

MEASURED (machine B, `GL_VENDOR=NVIDIA Corporation`
`RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2`
`VERSION=4.6.0 NVIDIA 615.78.08`; same seed-7 tick-120 recipe
`--cam 0,-4000,800,0,0,200`):

* `[arena] pack uploaded: buckets=119 (textured=48 fallback=71
  transparent=14) textures=13 placements=405 skipped=74 slots=532
  tris=569941` (bucket count moves 123 -> 119: glass splits out of the
  opaque buckets into 14 veil/net buckets).
* Capture `/tmp/fix_glass2.ppm`: rainbow gone; dome is a dark transparent
  veil with a faint pale hex net (converted texture, alpha <= 80), field/
  stands/pads unchanged (grass still opaque, no grey floor regression).
* Default-path proof (fix touches only pack code): `--only VG1` PASS
  byte-identical `md5=5326135d54b86d53532d32d78670030d` (== V11 record).
