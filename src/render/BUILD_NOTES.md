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
