# Visual pass — 1:1 direction (Round 35)

Goal: match the real game's look as closely as possible while staying
license-clean. References: the user's RL screenshot + RocketLeague2 (BrowserRL)
render recipes (measured values, ported as numbers — no assets copied).

## Landed

- **Fennec car body** — `assets/models/fennec.bin` converted from the CC-BY-4.0
  community GLB (Jako/fairlight51, NOT extracted game geometry; full provenance
  in `assets/models/fennec.LICENSE.txt` + `THIRD_PARTY.md`). Loader
  `src/models/Fennec.{h,cpp}` (VBO vertex arrays, 84.8k tris); bake transform
  mirrors the reference port's verified orientation: GLB already RL units,
  +X nose; our frame = (x, −z, y−15.5) so wheels touch ground at the RocketSim
  origin's rest height (17). Fallback box car stays if the asset is missing.
- **Ball** — procedural equirect hex-panel texture (512×256, in-code, no
  assets): white panels, gray seams. Visual radius 92.75 (collision 91.25).
  NOTE: an `(unsigned char)` cast overflow made mid rows yellow — fixed with
  clamps (debug hooks: RL_TEXDBG dumps the uploaded texture, RL_NOTEX renders
  untextured).
- **Ball ground indicator** — the RL highlight under the ball with BOTH
  circles: outer ring 0.98r..1.25r + inner disc 0.62r, light cyan-white,
  fading (0.9→0.12 / 0.32→0.05) and growing (+18%) with height to ~1950 uu
  (recipe ported from the reference's BallIndicatorMesh constants).
- **Boost pads/orbs** — dark hexagonal plate tied to the real pickup cylinder
  radii (big ≈116 / small ≈66 plate), emissive gold trim ring, ground halo,
  floating orb (54/28) with hot core; respawn = dim + spin-up growth (G9
  cooldowns drive it).
- **Glass walls** — 1:1 shell dims (±4096 / ±5120 / ceiling 2044, corner
  planes |x|+|y|=8064) as low-alpha glass + white hex net (420 uu cells) on
  the inner surface; corner panels rotated 45°.
- **Curbs** — the physics wall fillet made visible: quarter-round sweeps
  (side 256, back 160, corners 208) in cool blue-gray; ceiling lip band +
  emissive trim.
- **Goals** — dark interior + glowing team trim frame (orange/blue) + white
  posts/crossbar at the exact mouth dims (±892.755, 642.775).
- **Boost gauge** — RL-style circular gauge bottom-right: blue layered dial,
  orange segmented 260° arc (proportional to boost), white tick zone, number
  + BOOST label (replaces the old text "BST" readout).

## Verification

`--cam x,y,z,tx,ty,tz` freecam hook + `--shot-at-tick` screenshots (PPM):
kickoff wide, ball close-up, pad close-up, wall close-up, Fennec close-up.
All greens elsewhere: measured.py 16/16, arena 46/46, unit tests PASS.

## Next candidates

- Fennec paint shader (team-agnostic decal look), wheel spin + steer visuals.
- Ball decal (team-agnostic); goal explosion geometry (shockwave ring).
- Supersonic wheel embers (rl-opus style) instead of/augmenting the trail.
- Real arena palette pass (per-map turf/markings) once meshes load.

---

# Round 36 — visual pass V1–V6 (MASTER_PROMPT_VISUALS.md)

The visual work order ran end-to-end: evidence harness first, then budget,
car/ball, environment, FX and close-out. All numbers below are MEASURED on
`GL_VENDOR=NVIDIA Corporation RENDERER=NVIDIA GeForce RTX 4060 Laptop
GPU/PCIe/SSE2 VERSION=4.6.0 NVIDIA 615.71.09`, 1280x720, offscreen via
`gamescope --backend headless` (no display takeover). Raw: 
`tools/verify/gates_visual.json`; element-by-element status:
`docs/VISUAL_PARITY_LEDGER.md`.

## Fixed rendering bugs (found by the new pixel gates)

Four real defects, each caught by a gate rather than by eye:

1. **`GL_BLEND` was left disabled after the first 2D overlay draw**, so from
   frame 2 on every translucent surface (glass walls, ball ground indicator,
   pad halos, trails) rendered **opaque**. `Renderer::BeginFrame` now resets the
   draw state every frame, and `OrthoScope`/`FillRect2D` save/restore blend and
   depth. Caught by VG4 (the indicator read as a solid disc, not a ring+disc)
   and VG6 (the glass measured opaque). (`src/render/Renderer.cpp`)
2. **Wall curbs and hex nets were rotated 90° off**, sweeping the fillets along
   `x=0`/`y=0` through the arena centre instead of along the wall base, and
   standing the nets edge-on. Fixed with an explicit local-frame convention
   (`+x` along the wall, `+y` outward). Evidence: a wall-facing capture moved
   the curb band from the arena-centre floor (41–54 samples) up to the wall
   base (155 samples, centre rows 15/3) → VG11. (`src/game/main.cpp`)
3. **The hex nets were fully depth-occluded by their own glass panel** (net and
   glass face were coplanar), so the walls were blank sheets. The net is now
   offset 5 uu inward → VG6 measures 14.2 hex-edge crossings per scanline vs
   13.9 predicted for the 420 uu cell. (`src/game/main.cpp`)
4. **The boost gauge arc grew quadratically, not linearly** (65° at boost=50
   instead of 130°) because the segment count *and* each segment's angular
   width were both scaled by the fill fraction. → VG5 now measures 257° @100
   and 128° @50. (`src/game/main.cpp`)

## V1 — evidence harness

`tools/verify/visual_gates.py` runs `build/game` offscreen with a fixed
seed/tick/`--cam` pose and asserts on framebuffer pixels. It polls for the
game's own completion line and then kills the process group, because
gamescope's headless teardown can hang after the game has already exited. Loud
`SKIP:` (never a fake PASS) when gamescope/GL is unavailable.

- **VG1** determinism: two captures **byte-identical** (md5
  `f334befd5c5956c0e33c16428cd61f91`); a documented per-channel tolerance
  fallback exists for drivers that forbid byte-equality.
- **VG2** `--calibrate`: 5 bands at the documented x positions, all within 1/255
  (turf 36,112,48 / wall 64,127,255 / carblue 25,89,229 / goalorange
  255,140,51 / white 229,229,229).
- **VG3** coverage: kickoff / ball / pad / wall / ceiling / car are all
  non-empty, ≥12 quantised colours and ≥20% non-background.
- **VG7** budget + GL errors over a 60 s scripted run: `ticks=7200 drops=0
  glerr=0 tris_last=187790 draws_last=253`.

## V2 — budget, measured

Real counters replace the on-paper estimate: `tris_last`, `draws_last`,
`tris_total`, `draws_total`, `glerr` are printed in the `[hud]`/exit lines and
asserted by VG7. Per-scene: 187,986 tris / 253 draws with two Fennecs, 18,412
tris with the procedural box car (`--nofennec`). The Fennec is 84,823 tris
(`fennec_tris`), measured clean (0 degenerate, 0 duplicate triangles), so the
overage is **justified in writing rather than decimated**, and the line is held
mechanically: VG7 fails above `TRI_CAP = 240,000`.

## V3 — car and ball fidelity

- The `Snapshot` now carries the car's forward/up frame and velocity, filled
  from the existing read-only `Sim::GetCarForward/GetCarUp` (no sim change), so
  **pitch/roll are visible** instead of being flattened to a yaw. VG9 measures
  the box car's silhouette rotating **21.3°** between two frames of a held jump
  + pitch.
- The box car has four procedural wheels (tyre R 17, hub, bright spoke bar);
  the front pair steers from the velocity slip angle and all four spin from the
  integrated forward speed. VG9 counts 1023 car px, 11 hub, 425 tyre.
- The Fennec's baked wheels cannot spin without mesh segmentation — recorded as
  a documented limitation, not hidden.

## V4 — environment, palette, lighting

Deliberate lighting model (single directional + model ambient, `GL_FLAT`,
documented in `src/render/BUILD_NOTES.md`). With blending actually working, the
glass read too faint at alpha 0.10, so it is now 0.16 with the hex net at 0.13
and a 420 uu cell; VG6 measures the blue hue (100% of the sampled wall) and the
net period. Turf, markings, curbs, goals and the ceiling lip are unchanged
apart from the orientation fix.

## INI schema

No new visual knobs were needed: every visual number this pass added (glass
alpha, hex cell/alpha, wheel radii, shake amplitude, embers) is a code constant
cited in `docs/VISUAL_PARITY_LEDGER.md` rather than user-facing config, so
`config/settings.ini` stays **schema v1 unchanged** (`docs/INI_SCHEMA.md`) and
the roundtrip test stays green (`make tests` → ROUNDTRIP PASS). Had a knob been
warranted it would have extended the schema per constraint 7 rather than hiding
in an env var.

## V5 — feedback FX

- Goal **shockwave ring**: azimuthal floor rings plus two upright rings around
  the ball, radius `140 + 2600·k` over the 2 s blast, fading. VG8 measures
  71 px @t90 and 169 px @t150 against a projection-model prediction of 70.5 and
  169.1.
- Demo flash and pickup burst: gated differentially by VG12 (the probe frame
  must add a body of pixels with the effect's hue: +2327 cool px for the demo,
  +648 warm px for the pickup).
- Supersonic trail ribbon + deterministic wheel embers, boost flame, and a
  camera-shake overlay that never touches `CameraParams` and is disabled for
  freecam/`--no-shake` so captures stay byte-identical.
- FX now advance on **sim time** (tick delta), so every effect is
  capture-deterministic.

## V6 — close-out

`docs/VISUAL_PARITY_LEDGER.md` (every element classified, every row gated or
explicitly not) and `docs/VISUAL_VERDICT.md`. Physics regression re-proven on
this revision (see the verdict). New harness hooks (all coordinator-owned,
debug-only): `--ballpos`, `--boost`, `--nofennec`, `--debug-colliders`,
`--no-shake`, `--drive`, `--fx-probe`, `--fx-probe-tick`.

## Verification

`python3 tools/verify/visual_gates.py` → **VG1–VG12 = 12 PASS, 0 FAIL, 0
SKIP**. Physics gates unchanged and green: `measured.py` 19/19,
`mesh_gates.py` 13/13, `/tmp/arena_test` 46/46, `make tests` PASS,
`verify_vs_rocketsim.py` 110/110. Soak (mesh, offscreen, `--soak 120 --bot
slow-chase`): `ticks=14396 drops=0 wall=120.0s p50=2.00ms p99=2.00ms
glerr=0`, plus the VG7 60 s scripted run `ticks=7200 drops=0 glerr=0`.

## Next candidates (unchanged, not attempted this pass)

- Fennec paint/decal treatment (the mesh is single-colour per team).
- Loaded arena-mesh and contact-manifold wireframes (`--debug-colliders` gap;
  needs a read-only `Sim` accessor + Rule-1 test).
- Per-map turf palettes once a mesh is present.
- Audio: doppler/distance/crowd (see `docs/AUDIO_NOTES.md`).
