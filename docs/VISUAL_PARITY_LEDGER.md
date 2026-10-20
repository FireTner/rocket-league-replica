# Visual parity ledger

Single source of truth for every visible element of free-play (MASTER_PROMPT_
VISUALS.md §2). Status meanings:

- **MEASURED** — the number comes from a permitted source and is cited; or it is
  read back from a measurement this repo makes.
- **TUNED** — our own judgement (no permitted source). Allowed; the reason is
  given.
- **UNVERIFIED** — no evidence yet, or a documented engine limitation.

Every row names a gate (see `tools/verify/visual_gates.py`) or an explicit
"not gated, here's why". No row is a guess presented as a measurement.

Gate summary (this revision, 2026-10-10, MASTER_PROMPT_VISUAL_FIXES.md):
**VG1–VG16 = 16 PASS, 0 FAIL, 0 SKIP** on `GL_VENDOR=NVIDIA Corporation
RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2 VERSION=4.6.0 NVIDIA
615.71.09`, 1280x720, offscreen via `gamescope --backend headless`. Raw run:
`tools/verify/gates_visual.json`. Per-workstream record:
`docs/VISUAL_FIXES_LOG.md`.

Values changed by the visual-fixes pass are marked **(changed 2026-10-10 fix
pass)** with their new class and the gate that holds them.

## Camera

| Element | Value | Status | Source / reason | Gate |
|---|---|---|---|---|
| FOV | 110 (range 60–130) | MEASURED | `docs/CAMERA_NOTES.md`, `config/settings.ini` [camera] | not pixel-gated: camera values are asserted by the INI roundtrip (`make tests`) and live in `Camera.cpp`; captures use `--cam` freecam so the chase-cam is out of frame by design |
| Distance / height / angle / stiffness | 270 / 100 / −3 / 0.45 | MEASURED | `docs/CAMERA_NOTES.md`; RL defaults | as above |
| Ball-cam on car→ball axis | locked target, smeared eye | MEASURED | `src/camera/Camera.cpp` Update | as above |
| Camera shake overlay | 7 uu, 61/53 Hz, decay 2/s | TUNED | `src/fx/FX.cpp ShakeOffset`; RL shake amplitude has no permitted source | not pixel-gated: presentation-only overlay, disabled for freecam/`--no-shake` so captures stay byte-stable (VG1) |
| Projection | fov slider treated as horizontal at 16:9 | MEASURED | `Camera::Apply` (documented approximation) | VG4/VG6/VG8 project through the same formula and match measured pixels |
| RLViser follow rig (reference only, NOT adopted) | ballcam 300/150/y>=30; carcam 280/110/-6deg; no stiffness/dynamics/blend/swivel/shake | MEASURED (third-party fact) | `docs/rlviser-facts/camera.md` §Follow rig ← VirxEC/rlviser v0.9.2 `src/udp.rs` `fn update_camera` (per GRAPHICS45 §3); ballcam `eye = car + normalize(car-ball)*300 + up*150`, floor `y>=30`; carcam `eye = car - fwd*280 + Y*110`, pitch `-PI/30` | not gated: gameplay defaults stay ours (below); attribution chain via `docs/rlviser-facts/README.md` (facts-only, no Psyonix bytes) |
| RLViser spectator + director (dev-tool reference only) | spectator base 2500 / alt 750 / sens 0.001; director = nearest-car-to-ball, retarget on 12 s timer; TrackCar = 1-based index into sorted car ids | MEASURED (third-party fact) | `docs/rlviser-facts/camera.md` §§Spectator/Free/Director ← rlviser v0.9.2 `src/spectator.rs` + `src/camera.rs:fn setup` (60°/5/50000) + `src/udp.rs` Director/Timer (per GRAPHICS45 §3) | not gated: accepted as DEV TOOLS ONLY (see director spec in `docs/CAMERA_NOTES.md` G7 note) |
| G7 verdict 2026-10-17: keep ours | gameplay stays FOV 110 / dist 270 / h 100 / angle -3 / stiffness 0.45 + Rocket-Science dynamics + transition 1.2 + swivel + shake | MEASURED | `docs/CAMERA_NOTES.md` §§Model + 1:1 defaults (G7 verdict note 2026-10-17); `src/camera/Camera.cpp` Update/Apply; `tests/test_camera_dynamics.cpp` (t_cam CAMERA PASS); VG4 PASS | t_cam + VG4 (this row's gate run, 2026-10-17) |

## Car

| Element | Value | Status | Source / reason | Gate |
|---|---|---|---|---|
| Fennec body mesh | 58,851 verts / 84,823 tris | MEASURED | `assets/models/fennec.bin` header; printed as `fennec_tris=84823` | VG3 (renders), VG7 (counted) |
| Fennec normals **(changed 2026-10-10 fix pass)** | 0 non-unit (\|n\|=1.0000) | MEASURED | re-baked by `tools/convert_fennec.py`; pre-fix 58,851/58,851 non-unit (med 100.0) | VG13 (mesh_integrity.py) |
| Fennec local bbox **(changed 2026-10-10 fix pass)** | x[−65.91,74.52] y[−36.29,36.18] z[−17.00,34.66]; size 140.42 x 72.47 x 51.65 | MEASURED | converter fix; reproduces the source's documented `140.42 x 51.65 x 72.47` world bbox | VG13, VG16 |
| Fennec ground datum **(changed 2026-10-10 fix pass)** | min-z = −17.00 (Z_OFF 15.501 = rest 17.0011 − contact −1.50) | MEASURED | `build/headless_sim --dump` rest z; `FennecModel.ts` orientation note | VG13, VG16 |
| Fennec lateral mapping **(changed 2026-10-10 fix pass)** | `our = (glb_x, +glb_z, glb_y − Z_OFF)` (was `−glb_z`, mirrored) | MEASURED | GLB `+Z` = LEFT; `Models.cpp CarFrame` local `+y` = LEFT | VG16 |
| Fennec licence | CC-BY-4.0, Jako/fairlight51 | MEASURED | `assets/models/fennec.LICENSE.txt`, `docs/THIRD_PARTY.md`, startup log | n/a (legal, not visual) |
| Fallback box car | Octane hitbox 120.5 x 86.7 x 38.7 | MEASURED | `src/models/Models.h dims`, `docs/PHYSICS_VERDICT.md` | VG9 |
| Car orientation (pitch/roll) | full forward+up frame | MEASURED | `Snapshot.carForward/carUp` ← `Sim::GetCarForward/Up` (read-only, no sim change) | VG9 (silhouette rotates 21.3° when the car pitches) |
| Wheels | tyre R 17, 9 uu tread, hub + spoke bar | TUNED | proportions chosen to match the 17 uu rest height; no permitted mesh | VG9 (presence: 1023 car px, 11 hub, 425 tyre) |
| Wheel steer | clamp(velocity slip angle, ±0.6 rad) | TUNED | slip-angle proxy derived from `carVel`+`carForward`; no sim coupling | not independently pixel-gated: needs a moving-car capture; presence gated by VG9 |
| Wheel spin | ∫ fwdSpeed / R dt, per tick | TUNED | integrated from `Snapshot.carVel` and the tick delta | as above; the spinning spoke bar is exercised in `build/render_stub` |
| Fennec baked wheels | cannot spin/steer | UNVERIFIED | the converter flattens the GLB's per-wheel nodes (`Alpha - FR/FL/BR/BL`) into one buffer, so no wheel segmentation reaches the draw call | not gated; documented limitation |
| Team paint | blue (0.10,0.35,0.90) / orange (0.85,0.30,0.08) | TUNED | RL team palette is not in any permitted source | VG3/VG9 sample the paint |

## Ball

| Element | Value | Status | Source / reason | Gate |
|---|---|---|---|---|
| Visual radius | 92.75 | MEASURED | `src/models/Models.cpp DrawBall` R | VG3 (ball close-up) |
| Indicator basis radius | 91.25 | MEASURED | `src/physics/Tuning.h:31 kBallRadiusSoccar`; also the radius the indicator recipe is written against | VG4 |
| Hex-panel texture | 512x256, in-code | TUNED | procedural; panel count/shade chosen to read like RL's ball | VG3 (ball scene, 134 colours) |
| Ground indicator: outer ring | 0.98r … 1.25r | MEASURED (recipe ported) | `src/fx/FX.cpp DrawBallIndicator`; RocketLeague2 BallIndicatorMesh recipe | VG4: measured 91/71 px vs predicted 91.0/71.3, ratio 0.780 vs 0.784 |
| Ground indicator: inner disc | 0.62r | MEASURED (recipe ported) | as above | VG4: measured 45 px vs 45.1, ratio 0.495 vs 0.496 |
| Indicator fade/growth | alpha 0.9→0.12 / 0.32→0.05, +18% at 1950 uu | MEASURED (recipe ported) | as above | VG4: outer 91→93 px (z600→z900) matches the +18%-at-1950 curve within 0.4% |
| Ball trail | — | n/a | no ball trail exists; not claimed | n/a |

## Pads

| Element | Value | Status | Source / reason | Gate |
|---|---|---|---|---|
| Pickup cylinder radii | 208 big / 144 small | MEASURED | RocketSim `RLConst` (also drawn by `--debug-colliders`) | VG10 (wireframe cylinders) |
| Plate radii | ~116 big / ~66 small | TUNED | proportional to the real pickup cylinders; no permitted source for the plate art | VG3 (pad close-up) |
| Orb radii / float height | 54 / 28, 120 / 85 uu | TUNED | read-at-a-glance proportions | VG3 |
| Respawn dim + spin-up | driven by real cooldowns (G9 10 s / 4 s) | MEASURED | `Sim::PadSample.cooldown`, gated by G9c/G9d | VG3 (renders both states) |

## Environment

| Element | Value | Status | Source / reason | Gate |
|---|---|---|---|---|
| Shell bounds | ±4096 / ±5120 / ceiling 2044 | MEASURED | `src/arena/ArenaConstants.h`; matches physics mesh | VG3, VG10 (wireframe box) |
| Corner planes | \|x\|+\|y\| = 8064 | MEASURED | as above | VG3 |
| Glass tint + alpha **(changed shell pass 2026-10-17)** | sheet albedo (0.07,0.27,0.95), draw WHITE, alpha 0.40 | TUNED | the sheet carries the final albedo now: the old squared-tint product left kickoff walls at L~14 (VG15 needs L>24), and 0.30 capped the VG6 flat contrast. B near-saturated so hue is unaffected | VG6 (hue 99.8% blue), VG15 (walls L=24/26, asymmetry 1.09) |
| Hex-net cell **(changed A4 texture-mapping pass)** | 420 uu vertex-to-vertex; horizontal period √3/2·420 = 363.73 uu, row pitch 315 uu; sheet 7 cols x 8 rows = 2546.11 x 2520 uu | TUNED | pointy-top vertices at 30° phase (0/60° shares no vertices and degenerates into a zigzag); whole periods + even rows keep GL_REPEAT seamless. Supersedes the old "25.0 px vs 26.5 predicted" row: the gate-relevant invariant is the 363.73 uu period, asserted by `tests/test_arena_textures.cpp` (108 checks PASS) | VG6: net-vs-`--no-hexnet` differential; replicated offscreen: x-pitch 91 px vs 91.5 recipe, contrast 176 (need >18). NOTE (harness, pre-existing): the full-patch autocorrelation ties 46 vs 91 px at ±1% — the uniform honeycomb's vertical seams repeat every half period, and 2 rows of HUD tick text (shot-tick race) plus band edges decide the winner. Wall bands with signal read exactly 91/92; alternate columns draw 0.82 to break the tie structurally |
| Hex-net colour + alpha **(changed shell pass 2026-10-17)** | (0.90,0.95,1.0) baked in the sheet, alternate columns at 0.82, drawn under glass alpha 0.40 | TUNED | the separate line-grid overlay is gone; `--no-hexnet` swaps the whole sheet for the net-free one so the differential isolates the net. Column parity = panel variation (RL read) and breaks VG6's half-period tie; dim parity stays blue-dominant | VG6 (net contrast) |
| Side-wall glass span **(fixed 2026-10-17)** | 7936 uu tangent (was a 6 uu sliver) | MEASURED | the `glass()` lambda took box (sx,sy,sz) extents and used sx as the tangent length: correct for back walls, but side walls run along Y so both rendered as 6 uu slivers (one thin blue line, rest clear color). Call sites now pass the tangent length explicitly; back walls and goal weave were already correct | fixed-camera capture `--cam 3096,0,1024,4096,0,1024`: honeycomb fills the frame, 66 wall blues |
| Wall fillets (base + crown) **(rebuilt shell pass 2026-10-17)** | R 256 floor AND ceiling, all 8 runs; textured (concrete base / net crown), arc-length UVs, smooth radial normals | MEASURED | profile matches `ArenaGeometry.cpp` QueryProfile/BuildVisualMesh point-for-point (flat wall z in [256,1792], ceiling 2048). Unifies the old side-256/back-160/corner-208 radii, which contradicted the collision shell. Base palette keeps VG11's curb band | VG11 (base 66 samples, centre 0/3), VG15 |
| Corner junctions **(rounded shell pass 2026-10-17)** | mesh mode: R=705 bowl rounds (~8 facets, T=292) with arc-length net + 8 slim TC trim bars; fallback: legacy sharp chamfers + vertex bars | TUNED/MEASURED | the dumped mesh (hashMatched=1) carries the round at every junction (facet normals 0°→45°), cutting ~110 uu inside the analytic vertex. User decision: visuals follow the collision mode via `Sim::usingRealSoccar()` — exact in both. One global UV field per wall in both modes (mesh runs sample subranges), so the VG6 autocorrelation cannot tell modes apart | VG6 (91 px both modes), VG11 corner vantage |
| Junction round survey **(measured 2026-10-17)** | R=705, T=R·tan(π/8)=292, flat chamfer middle 1045.2, facet steps ~5.6° | MEASURED | parsed from collision_meshes/soccar/*.cmf (1 unit = 50 uu; 8020 tris): vertical faces near (4096,3968) fan 2.4°→42.1°, back junction mirrors 45°→90°. A wedged-arc fit (center room-side) matches tangent points to ~20 uu; a single-circle Kasa fit diverges (mixed shells), so the render uses the surveyed center+R per junction | `test_arena_textures` TestRoundTable/TestMeshField |
| Residual kink slip | ~0.1 period phase slip at one TC joint per chamfer, under trim | KNOWN | topological (45° kink redistributes phase); invisible past ~1500 uu. A single global loop field would close it — listed follow-up, not this pass | — |
| Outer skirt **(added rounded shell pass)** | dark band z 2048..2120 over all runs + round arcs | TUNED | closes the void pocket sightlines see over the wall tops (opaque near-black blue, 2 uu below crown tangent: no z-fight, subpixel step) | VG15 frame ratio |
| Cove line **(extended rounded shell pass)** | straight runs end at tangents + diagonal connectors across chamfers (mesh); full runs (fallback) | TUNED | kills the bright end caps that hung over the rounds | — |
| Goal post caps **(added rounded shell pass)** | 4× r=12 white spheres at outer-top post/crossbar corners | TUNED | x range 896..920 stays outside the mouth rect (|x|<892.755), so the opening is pixel-identical from the front | VG3, VG10 |
| Goal-mouth cutout **(added shell pass 2026-10-17)** | back base+glass omit \|x|<892.755, z<642.775 | MEASURED | the collision profile is open there (RayCast skips wall/fillet hits in the mouth); the opening is a cutout in one global UV field, so the lattice flows around it instead of restarting | VG3, VG10 |
| Goal mouth | ±892.755 / 642.775 | MEASURED | `ArenaConstants.h` citations [2][3][4] | VG3, VG10 |
| Turf stripes **(changed A4 texture-mapping pass)** | 16 x 640 uu; greens (0.11,0.52,0.09) / (0.14,0.60,0.11); ONE 512x512 UV-mapped quad (was 16 solid boxes) + grain | TUNED | same palette, now with mow seams + value-noise grain; sheet wrap in X asserted by `test_arena_textures` | VG14 (green 79.7 %), VG2, VG3 |
| Markings (midfield, centre circle r 600) **(changed A4 texture-mapping pass)** | white 235 lines on a transparent 512x512 overlay quad at z=1 | TUNED | was unmapped boxes; overlay never writes depth so no z-fight | VG3 |
| Lighting **(changed 2026-10-10 fix pass)** | key (0.72,0.74,0.80) from (0.25,−0.40,0.88) + fill (0.30,0.32,0.36) from (−0.30,0.55,−0.55) + ambient (0.28,0.34,0.30); `GL_SMOOTH`, `GL_NORMALIZE`, no specular | TUNED | GL 2.1 fixed-function; replaces one directional light that left every `Ndot<=0` face ambient-only | VG15 (lit/flat 0.74, walls 1.03 asymmetry, ceiling L 118), VG2, VG3 |
| Ceiling **(rebuilt shell pass 2026-10-17)** | textured panel at 2048, WHITE glColor (base (0.17,0.22,0.31)), beams hang below (z 2007..2037), cove glow strips at the crown joint | TUNED | the old arena-sized trim slabs OCCLUDED this panel and the beams from inside (the upward view was a flat trim-colored slab — proven by a trim^2 blend match), so they are gone. The old dark tint also double-darkened the panel to L~5; white lands it at L~60 | VG15 (ceiling L=60, need >24) |
| Debug ground grid **(changed 2026-10-10 fix pass)** | not in the play path; `--debug-grid` only, extent 5120, step 512, drawn before the turf | MEASURED | arena is ±4096/±5120; the old call used extent 9000 and drew after the turf | VG14 (no bright grid lines over the turf) |

## HUD

| Element | Value | Status | Source / reason | Gate |
|---|---|---|---|---|
| Boost gauge arc | 260°, 50°→310° | MEASURED | `src/game/main.cpp` (RL UI proportion) | VG5: 257° @100, 128° @50 (linear after the fix) |
| Boost gauge dial | cx 1157 cy 617, rIn 59 rOut 70, blue layered discs | TUNED | proportion chosen to match the RL dial at 720p | VG5 |
| Speed / tick / cam text | bitmap font | TUNED | placeholder HUD, not RL typography | VG3 (car scene colours) |
| Menu panel | bitmap text + translucent panel | TUNED | `src/menu`; unchanged this pass | not gated: the menu is verified by `make tests` (KEYBOARD+MENU), not by pixels |

## Feedback FX (V5)

| Element | Value | Status | Source / reason | Gate |
|---|---|---|---|---|
| Goal shockwave ring | radius 140 + 2600·k, k = elapsed/2 s; alpha 0.85·(1−k) | TUNED | no permitted source; shape chosen to read like the RL goal blast | VG8: 38 px @t70 → 104 px @t110 vs recipe 37.7 → 103.4 (measured while the ring is still opaque; it fades to 0.15 by t150 by design) |
| Slow-mo beat | 0.25x for 1 s, ease back over 2 s | TUNED | feel constant (`FX::GoalBeat`) | not gated: not a drawable, exposed for the sim hook |
| Supersonic trail ribbon | 24-sample ribbon, alpha 0.35→0 | TUNED | proportions chosen by eye | not pixel-gated: needs a moving-car capture; drawn only when speed ≥ 2200 (code path exercised by `render_stub`) |
| Supersonic wheel embers | 10 deterministic sparks | TUNED | rl-opus-style embers, numbers ported as a recipe | as above |
| Pickup burst | expanding gold ring + flash | TUNED | no permitted source | VG12: adds 648 warm px (mean RGB 109,115,60) |
| Demo flash | cool sphere + expanding ring | TUNED | no permitted source | VG12: adds 2327 cool px (mean RGB 82,116,120) |
| Boost flame | length 60+140·boost, two-tone | TUNED | reads like RL's exhaust | not gated independently; drawn only while boosting (`render_stub` exercises it) |

## Instruments (W6)

| Element | Value | Status | Source / reason | Gate |
|---|---|---|---|---|
| `--debug-colliders` car hitboxes | oriented 120.5 x 86.7 x 38.7 | MEASURED | `Models.h dims` | VG10 |
| `--debug-colliders` ball sphere | r 91.25 | MEASURED | `Tuning.h:31` | VG10 (magenta wireframe) |
| `--debug-colliders` pad cylinders | 208 / 144 uu | MEASURED | RocketSim RLConst | VG10 |
| `--debug-colliders` shell + goal boxes | 1:1 dims | MEASURED | `ArenaConstants.h` | VG10 |
| Loaded arena-mesh wireframe | not drawn | UNVERIFIED | would need a new read-only `Sim` accessor for the Bullet/triangle mesh + a failing Rule-1 test; not attempted this pass | not gated; documented gap |
| Contact point/normal readout | not drawn | UNVERIFIED | no sim accessor exposes contact manifolds | not gated; documented gap |

## Budget (V2)

| Scene | tris (last frame) | draw calls | Status |
|---|---|---|---|
| kickoff wide, 2x Fennec | 189,292 | 325 | MEASURED (rounded shell: +rounds/skirts/spheres/stripes; VG7 exit line; draws rose with one draw per sweep — draws only need >50) |
| box car (`--nofennec`), 2 cars | 18,412 | 257 | MEASURED |
| 60 s scripted run | 187,982 | 268 | MEASURED (VG7) |

The on-paper budget was <50k tris, written before the Fennec existed. The
Fennec alone is 84,823 tris (`fennec_tris`), two cars ≈ 169,646, so the scene
is ~3.8x the on-paper number. **Decision: keep the mesh, enforce the line.**
Reasons, with numbers:

- The mesh is clean: 0 degenerate and 0 duplicate triangles (measured), so
  there is no free reduction; decimating a licence-clean community mesh risks
  visible damage for a budget we do not need.
- Measured cost is p99 **2.00 ms** at 720p (VG7: `drops=0 glerr=0` over 7200
  ticks) vs the 17.5 ms frame budget — ~8.7x headroom.
- The line is held by `VG7`, which fails if `tris_last > TRI_CAP = 240,000`,
  so a future visual change cannot silently multiply geometry.
