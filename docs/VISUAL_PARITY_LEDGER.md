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
| Glass tint + alpha **(changed 2026-10-10 fix pass)** | colour (0.06,0.20,0.95), alpha 0.30 | TUNED | re-derived against the lit render: the old (0.22,0.44,0.90)@0.16 washed to neutral grey under the key/fill model | VG6 (hue ≥ 90% blue) |
| Hex-net cell | 420 uu | TUNED | chosen to read like RL's net | VG6: median row spacing 25.0 px vs 26.5 predicted (0.25 x 420 uu) via a net-vs-`--no-hexnet` differential |
| Hex-net colour + alpha **(changed 2026-10-10 fix pass)** | (0.90,0.95,1.0), alpha 0.42 | TUNED | raised so the net keeps contrast over the now-lit wall | VG6 (net contrast 360) |
| Wall fillets (curbs) | side 256, back 160, corner 208 | MEASURED | mesh-measured (see `docs/VISUAL_NOTES.md`); orientation fixed this pass | VG11 (at wall base, not across the arena centre) |
| Goal mouth | ±892.755 / 642.775 | MEASURED | `ArenaConstants.h` citations [2][3][4] | VG3, VG10 |
| Turf stripes **(palette changed 2026-10-10 fix pass)** | 16 x 640 uu; greens (0.11,0.52,0.09) / (0.14,0.60,0.11) | TUNED | re-tuned so the floor stays green-dominant under the new key/fill model | VG14 (green 79.7 %), VG2, VG3 |
| Markings (midfield, centre circle r 600) | white lines | TUNED | approximation of RL soccar markings | VG3 |
| Lighting **(changed 2026-10-10 fix pass)** | key (0.72,0.74,0.80) from (0.25,−0.40,0.88) + fill (0.30,0.32,0.36) from (−0.30,0.55,−0.55) + ambient (0.28,0.34,0.30); `GL_SMOOTH`, `GL_NORMALIZE`, no specular | TUNED | GL 2.1 fixed-function; replaces one directional light that left every `Ndot<=0` face ambient-only | VG15 (lit/flat 0.74, walls 1.03 asymmetry, ceiling L 118), VG2, VG3 |
| Ceiling **(changed 2026-10-10 fix pass)** | base (0.15,0.19,0.27) + cross/longitudinal beams (0.28,0.34,0.45) + emissive lip | TUNED | the old (0.02,0.04,0.07) base rendered as a void once the single light left the −Z face unlit | VG15 (ceiling not near-black) |
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
| kickoff wide, 2x Fennec | 187,982 | 268 | MEASURED (VG7/exit line; draws rose with the added ceiling beams) |
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
