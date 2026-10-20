# Visual fixes log — MASTER_PROMPT_VISUAL_FIXES.md

Revision: working tree, 2026-10-10. Renderer: GL 2.1 fixed-function, 1280x720,
offscreen via `gamescope --backend headless` on
`NVIDIA GeForce RTX 4060 Laptop GPU` driver `615.71.09`.

Scope: fix every open bug in `docs/VISUAL_BUGS_TODO.md` (V1–V9) and harden
`tools/verify/visual_gates.py` so the same failures cannot pass again. Physics
untouched: `src/sim`, `src/arena`, `src/physics`, `tools/tas/tapes` and every
gate *value* are unchanged; all physics gates stay green (final section).

Workstreams were run in the master prompt's merge order: WS-D gates → WS-B
asset → WS-A renderer → WS-C FX → WS-E integration. Each section records the
failing check BEFORE the fix and the passing check AFTER, with raw output.

---

## WS-B — Fennec asset pipeline (V1, V2, V3)

**Round 1.**

changed: `tools/convert_fennec.py` — three separate defects:
1. `node_matrix()` returned glTF's **column-major** `matrix` array as
   `[c0,c1,c2,c3]` while `mul()`/`xform()` indexed it as if it were
   **row-major**, so the composition transposed the transform. Replaced the
   whole section with an explicit row-major representation (`to_rows()`
   transposes glTF on the way in).
2. `xform()` was applied to **normals**, adding the node translation to every
   normal, and nothing renormalised. Normals now go through the
   inverse-transpose 3x3 (`normal_matrix()` / `nrm()`) and are renormalised.
3. The lateral mapping `our_y = -glb_z` mirrored the car: GLB `+Z` is LEFT and
   the frozen `CarFrame` has local `+y = LEFT`, so the mapping is now
   `our = (glb_x, +glb_z, glb_y - Z_OFF)`.
   `Z_OFF` is pinned to the measured RocketSim rest height (`17.0011 uu`) minus
   the mesh's own wheel-contact plane (`glb y = -1.50`), i.e. **15.501** —
   exactly the datum the source documents.

failing-check (pre-fix): `python3 tools/verify/mesh_integrity.py`
```
V1/V2 mesh integrity: FAIL
  verts=58851 tris=84823
  normals non-unit=58851 (|n| 76.4238..100.0000)
  bbox min-z=-48.91 (target -17.00 +/- 1.0)  size=140.42 x 71.42 x 47.24
  FAIL normals: 58851/58851 non-unit (|n| min 76.4238 max 100.0000, tol 0.01)
  FAIL datum: bbox min-z -48.91, expected -17.00 +/- 1.00
  FAIL size width y: 71.42, expected 72.47 +/- 1.0 (source FennecModel.ts)
  FAIL size height z: 47.24, expected 51.65 +/- 1.0 (source FennecModel.ts)
exit=1
```

fix: corrected matrix representation + normal transform + lateral sign + datum;
re-ran the converter (source GLB is
`~/Projects/RocketLeague2/public/assets/models/fennec.glb`).

passing-check (post-fix): `python3 tools/convert_fennec.py` then
`python3 tools/verify/mesh_integrity.py`
```
vertices=58851 triangles=84823 degenerate=0
bbox x[-65.91,74.52] y[-36.29,36.18] z[-17.00,34.66] (uu, car-local)
normals: non-unit=0/58851  |n| min=1.0000 max=1.0000
V1/V2 mesh integrity: PASS
  normals non-unit=0 (|n| 1.0000..1.0000)
  bbox min-z=-17.00 (target -17.00 +/- 1.0)  size=140.42 x 72.47 x 51.65
  source delta L/W/H = 0.00 / 0.00 / 0.00 uu (tol 1.0)
  degenerate triangles=0
exit=0
```
The post-fix footprint `140.42 x 72.47 x 51.65` reproduces the source's
documented world bbox `140.42 x 51.65 x 72.47` exactly (same numbers, our
car-local x/y/z order); the pre-fix width/height were wrong because the
transposed transform sheared the mesh. Orientation was confirmed visually:
front/side captures show wheels at the bottom, nose along +x, no mirror, and
the mesh concentric with the cyan hitbox wireframe.

**New tool:** `tools/verify/mesh_integrity.py` (unit normals, datum, source
footprint, degenerate triangles; `--json` for the gate harness).

---

## WS-A — Lighting & arena composition (V4, V5, V8)

**Round 2.**

changed: `src/game/main.cpp`, `src/render/Renderer.cpp`.

1. V4 — removed the unconditional `renderer.DrawGroundGrid()` from the play
   path (it covered `±9000 uu`, ~2x past the walls, and was drawn *after* the
   turf). Added an instrument flag `--debug-grid` that draws it clipped to the
   arena (`extent 5120`, `step 512`) and **before** the turf. Default off.
2. V5 — replaced the single directional light + large global ambient with a
   **key + fill + ambient** model, switched `GL_FLAT` → `GL_SMOOTH`, and
   re-tuned the arena palette against the *lit* render.
3. V5 (root cause found while chasing the grey walls) — the renderer used
   **`GL_RESCALE_NORMAL`**, but `DrawBox` applies a **non-uniform**
   `glScalef(sx,sy,sz)` to a unit cube. `GL_RESCALE_NORMAL` divides by a single
   factor, leaving the glass panels' side face with a normal of
   `(-1/10240,0,0)`; every light rejected it and the walls rendered flat grey
   regardless of tint. Switched to **`GL_NORMALIZE`** (exact for the
   diffuse-only model). Specular was disabled too: with `GL_COLOR_MATERIAL` it
   adds a fixed additive term over every surface and washed saturated tints
   toward white.
4. V5 — ceiling is no longer a void: a brighter base panel plus cross and
   longitudinal beams, drawn before the emissive curb lip.
5. V8 — re-checked the 45° corner glass panels after the lighting fix. With
   the walls reading as lit blue glass of comparable brightness the corners no
   longer read as opaque slabs crossing the playfield; no geometry change.

failing-check (pre-fix, same poses):
```
VG15 FAIL  turf L=43 flatL=92 ratio=0.47; back wall L=33 flatL=52 ratio=0.63; left wall L=35 flatL=56 ratio=0.62; right wall L=37 flatL=63 ratio=0.58
VG14 FAIL  floor band green-dominant=0.0% (need >=60%), bright grid-line px=1.80% (need <=1%)
```
The wall tint was provably ignored by lighting: setting the glass to a strongly
blue tint still measured `blue 1.8%`, while `--flat` (no lighting) measured
`blue 99.8%` — the geometry was blue and the *lighting* was destroying it.

passing-check (post-fix):
```
VG14 PASS  floor band green-dominant=79.7%, bright grid-line px=0.52%, glerr=0
VG15 PASS  lit/flat frame luminance 0.74 (need 0.60..1.25); walls L=53/51 (asymmetry 1.03); ceiling L=118; turf L=62
VG6  PASS  100% blue glass, net contrast 360
VG7  PASS  ticks=7200 drops=0 glerr=0 tris_last=187982
```

Tuned values (all TUNED against captures; full table in the parity ledger):
key `(0.72,0.74,0.80)` from `(0.25,-0.40,0.88)`, fill `(0.30,0.32,0.36)` from
`(-0.30,0.55,-0.55)`, global ambient `(0.28,0.34,0.30)`, glass tint
`(0.06,0.20,0.95) @ alpha 0.30`, hex net `(0.90,0.95,1.0) @ alpha 0.42`, turf
stripes `(0.11,0.52,0.09)` / `(0.14,0.60,0.11)`, ceiling base `(0.15,0.19,0.27)`
plus beams `(0.28,0.34,0.45)`.

---

## WS-C — Pads & FX shading (V6)

**Round 3.**

changed: `src/fx/FX.cpp` — `FX::DrawPads` no longer disables lighting for the
whole function. The hexagonal plate (top face + skirt) and both orb spheres are
now **lit** and emit normals; the emissive trim ring, ground halo and pickup
burst stay unlit (they are glows). The hot orb core is drawn unlit so it stays
emissive. Pad radii/amounts/cooldowns are untouched (gated physics values).

failing-check (pre-fix): the `pad` capture showed a flat plate and disc-shaped
orbs — `glDisable(GL_LIGHTING)` spanned lines 102–143 of `FX.cpp`, covering the
plate and both spheres, re-enabling only at the end of the loop body.

passing-check (post-fix): the `pad` capture shows a lit solid plate and a
shaded orb; the pad gates stay green:
```
VG12 PASS  demo flash adds 2396 px ... pickup burst adds 589 px ...
G9a big pad refill 100: ck=47fc120c3b3c5640
G9b small pad +12: ck=486a65c66d8c5a30
G9c big respawn 1200±5 ticks: span 1201
G9d small respawn 480±5 ticks: small spans [480, 480, 480]
```

---

## WS-D — Gate hardening (V7)

**Round 4.** New gates, each first shown FAILING on the pre-fix revision
(final section), then PASSING after the fixes.

| Gate | What it asserts | Pre-fix | Post-fix |
|---|---|---|---|
| **VG13** | Fennec mesh integrity (unit normals, datum, source footprint, degenerate tris) via `mesh_integrity.py --json` | FAIL — 58851/58851 non-unit, min-z −48.91 | PASS — 0 non-unit, min-z −17.00 |
| **VG14** | Kickoff floor is green-dominant turf with no bright grid lines (HUD excluded) | FAIL — green 0.0%, bright 1.80% | PASS — green 79.7%, bright 0.52% |
| **VG15** | No ambient-only black; lit frame ≥60% of its unlit albedo; both walls within 2x | FAIL — turf ratio 0.47, walls 0.58–0.63 | PASS — frame ratio 0.74, asymmetry 1.03 |
| **VG16** | The Fennec render path itself (VG9 only ever tested `--nofennec`): level side pose, silhouette height | FAIL — 16 px (buried) | PASS — 22 px (grounded) |

Also added the `--no-hexnet` and `--debug-grid` instruments used by the gates.

**Existing-gate detector repairs (documented honestly).** The lighting change
altered what several *pre-existing* pixel detectors measure. In every case the
assertion's intent was preserved and the detector was made to measure it
robustly; no assertion was weakened to hide a regression, and no gate *value*
(frozen tape, physics constant) was touched.

- **VG4 disc** — used an absolute luminance threshold (`sum > 150`) that only
  worked while the turf was dim; with a correctly-lit turf the threshold fired
  everywhere and the disc read 69 px against a predicted 45.1. Now measured as
  a **contrast against the local turf** (the disc can be dark *or* bright).
  Post-fix: 45 px vs 45.1 predicted (0.3% error).
- **VG6 hex period** — counted threshold crossings on a horizontal scanline at
  a grazing close-up; the count depended on which edges happened to be
  foreshortened. Replaced with a straight-on camera plus a **net vs
  `--no-hexnet` differential** and a **spacing** measurement. Post-fix: 25.0 px
  vs 26.5 px predicted (0.25 × 420 uu cell).
- **VG8 shockwave** — sampled t90→t150, but the recipe fades the ring to
  alpha 0.15 by t150 **by design**, so the fixed colour threshold found nothing.
  Retargeted to t70→t110 where the ring is still opaque. Post-fix: 38→104 px vs
  38→103 predicted.
- **VG15** — the old lit/flat *per-patch* ratio was not a valid invariant:
  `--flat` draws the raw albedo, so a correctly lit surface legitimately sits
  below it. Replaced with the invariants that actually encode V5.
- **VG14** — the boost gauge sits inside the sampled band and its bright pixels
  counted as grid lines; the HUD is now excluded.

---

## WS-E — Integration & verdict

**Round 5.** Every gate re-run on the integrated revision plus the full physics
suite. `docs/VISUAL_VERDICT.md` supersedes the V6 pass and
`docs/VISUAL_PARITY_LEDGER.md` records every changed value with its class.

Remaining `UNVERIFIED` (unchanged by this pass): Fennec baked wheels cannot
spin/steer (the converter does not consume the GLB's per-wheel nodes), Fennec
paint is a single flat team colour, and `--debug-colliders` still does not draw
the loaded arena mesh or contact manifolds (needs a read-only `Sim` accessor +
a failing Rule-1 test). The mesh's wheel-contact plane sits ~3.6 uu above the
floor when the car rests at z = 17.0011 (the authored datum is honoured); this
is under one 4-px sample and is not separately gated.

---

## Final integrated verification (raw output)

```
$ python3 tools/verify/measured.py
# summary: 19 PASS, 0 FAIL
$ python3 tools/verify/mesh_gates.py
# summary: 13 PASS, 0 FAIL
$ /tmp/arena_test
RESULT PASS: 46 passed, 0 failed
$ make -B tests
ROUNDTRIP PASS
KEYBOARD+MENU PASS
GAMEPAD PASS
$ python3 src/physics/verify_vs_rocketsim.py
checked: 110 (presets=7 scalars=90 curves=13)
RESULT: ALL CHECKS PASSED
$ python3 tools/verify/visual_gates.py
VG1  PASS  capture determinism                two captures byte-identical md5=79a0137b9693396e404e1a4ccde0e25c
VG2  PASS  calibrate palette bands            max delta 1
VG3  PASS  scene render coverage
VG4  PASS  ball indicator radii (recipe)      z600 outer=91 inner=71 disc=45; ratios 0.780/0.495; growth 1.022
VG5  PASS  boost gauge 260-degree arc         measured 257 deg @100 (exp 260), 128 deg @50 (exp 130)
VG6  PASS  glass wall hue + hex period        100% blue glass; median hex-row spacing 25.0 px (recipe 26.5 px)
VG7  PASS  budget + GL errors (60s run)       ticks=7200 drops=0 glerr=0 tris_last=187982 draws_last=268
VG8  PASS  goal shockwave ring geometry       radius 38px@t70 -> 104px@t110 (recipe 38 -> 103 px)
VG9  PASS  car wheel + orientation visuals    pitch rotates silhouette 21.3 deg
VG10 PASS  debug-collider instrument          0.90% pixels change; car-cyan=10 bot-orange=7 ball-magenta=14
VG11 PASS  wall curb placement                136 curb samples; arena centre 10/3
VG12 PASS  demo + pickup feedback FX
VG13 PASS  Fennec mesh integrity              normals non-unit=0 | bbox min-z=-17.00 | size=140.4x72.5x51.7
VG14 PASS  arena turf occupancy               green 79.7%, bright grid-line 0.52%, glerr=0
VG15 PASS  arena lighting sanity              lit/flat frame luminance 0.74; walls L=53/51; ceiling L=118
VG16 PASS  Fennec render path                 Fennec silhouette 42x22 px (grounded); need >=18 px
# summary: 16 PASS, 0 FAIL, 0 SKIP
```

Determinism is unchanged by this pass (physics byte-identical):

```
$ ./build/headless_sim --tape tools/tas/tapes/straight_boost_600.csv \
    --ticks 10000 --seed 7 --dump /tmp/md1.csv --meshdir collision_meshes
CHECKSUM 7dd64cccedbb9a1f   (run twice; cmp /tmp/md1.csv /tmp/md2.csv byte-identical)
$ ./build/headless_sim --tape tools/tas/tapes/straight_boost_600.csv --ticks 100 --seed 1 --dump /tmp/fb.csv
CHECKSUM 73db10410bd8a8ad
```
Both match `docs/PHYSICS_VERDICT.md` / `docs/PHYSICS_1TO1_LOG.md` exactly.

## Carried / not done

- **V9** (`render_pacing.log` written to the CWD on every pacing print) is the
  one bug from `docs/VISUAL_BUGS_TODO.md` not addressed in this pass.
- The Fennec's baked wheels still cannot spin/steer: the converter flattens the
  GLB's `Alpha - FR/FL/BR/BL` nodes into a single vertex buffer, so no wheel
  segmentation reaches the draw call. Splitting them is future work.
- `--debug-colliders` still does not draw the arena collision mesh or contact
  manifolds (needs a read-only `Sim` accessor + a failing Rule-1 test).
- The mesh's wheel-contact plane sits ~3.6 uu above the floor at rest; the
  authored datum is honoured, and the offset is under one 4-px sample at the
  gated poses, so it is recorded rather than chased.

---

# Follow-up pass — V10 arena shell geometry (2026-10-12)

Owner report: "the arena is a cluttered mess full of non-collidable walls and
similar." Cause and full evidence are in `docs/VISUAL_BUGS_TODO.md` §V10.
Four visual-only defects in `src/game/main.cpp`; physics untouched (no `src/sim`,
`src/arena`, `src/physics` or gate value changed).

changed: `src/game/main.cpp`
1. `wallCurb` rotation: `glRotatef(yaw * 180/π, …)` → `glRotatef(yaw, …)`. The
   call sites pass DEGREES; the conversion turned `-90°` into `243.4°` and
   `180°` into `233.2°`, so six of the eight coves swept the floor diagonally.
2. Corner coves + glass centred on the real chamfer midpoint `(3520,4544)`
   (was `(2944,2944)`, i.e. plane `|x|+|y| = 5888`), with local `+y` outward.
3. Straight side/back panels shortened to the chamfer junctions
   (`y ∈ ±3968`, `x ∈ ±2944`; were `±5120` / `±4096`).
4. `glass()` now converts its degree yaw before calling `DrawHexNet`, which
   takes radians — side-wall nets were 26.6° out of the wall plane.

failing-check (pre-fix, `build/game` swapped for a locally rebuilt pre-fix
binary; raw `--only VG6,VG11 --diag`):

```
VG11 FAIL  wall curb placement   wall-base cove 136 samples, centre 10/3;
                                  corner floor 21131 vs stray cove 41215
VG6  FAIL  glass wall hue + hex period   100% blue glass, net contrast 360;
                                  hex x-pitch 41 px (recipe 92.0 px)
```

passing-check (post-fix):

```
VG11 PASS  wall curb placement   wall-base cove 112 samples, centre 0/3;
                                  corner floor 33392 vs stray cove 19915
VG6  PASS  glass wall hue + hex period   96% blue glass, net contrast 360;
                                  hex x-pitch 92 px (recipe 92.0 px
                                  = sqrt(3)/2 x 420 uu cell)
```

Gate hardening (same pass, with reasons):

- VG11 now also renders a corner vantage and requires the floor to dominate the
  stray cove colour (`floor > 1.2 x cove`). The old single `+Y` view sampled the
  one cove whose yaw (0°) was already correct, so it could never see this class
  of bug.
- VG6's hex-period recipe was wrong: a flat-top hexagon tiling has an exact
  horizontal period of `sqrt(3)/2 * cell`, not the `0.25*cell` vertical
  run-spacing the old code measured (which only matched while the net was
  rotated out of the wall plane). It now autocorrelates the net-only x-profile.
- VG15's whole-frame lit/flat ratio was calibrated when the buggy shell covered
  the floor with dark glass; the corrected shell shows more full-albedo floor,
  so the unlit reference legitimately brightens (lit frame mean is unchanged:
  54.0 → 53.3). The band is now `0.45..1.25`, and a per-patch
  `lit >= 0.35 x albedo` check was added so a surface crushed by lighting is
  still caught independently of frame composition.

Full re-verification on the rebuilt binaries:

```
$ python3 tools/verify/visual_gates.py
# summary: 16 PASS, 0 FAIL, 0 SKIP
$ python3 tools/verify/measured.py
# summary: 19 PASS, 0 FAIL
$ /tmp/arena_test
RESULT PASS: 46 passed, 0 failed
$ make tests                # ROUNDTRIP + KEYBOARD+MENU + GAMEPAD PASS
```
## 2026-10-17 — corner-round shell work, step 1 (infra; user: curves missing at wall/chamfer junctions)

Measurement first: the dumped collision mesh (hashMatched=1) carries an ~8-facet
round at every straight<->chamfer junction (inward normals 45° -> ~2°), cutting
~110 uu inside the analytic sharp vertex (4096,3968). Back junctions symmetric.
So the sharp octagon is the approximation, not the round.

- `planRound` sweep added (main.cpp arena block): plan-view faceted strip,
  one flat inward normal per surveyed facet, arc-length U, V over z0..z1.
- `TestPlanRoundUV` added: `ARENA_TEXTURES PASS (88 checks, 0 failed)`.
- `make game` clean. Metrology (tangent points, all 8 junctions, z-extent,
  goal joints) delegated to a subagent; softening design (goal spheres,
  bevel/stripe package, +624 tris) received and fact-checked.

## 2026-10-17 — rounded shell pass, steps 2-4 (user: curves missing at wall/chamfer junctions)

Survey first: parsed collision_meshes/soccar/*.cmf (1 unit = 50 uu; dumped mesh,
hashMatched=1). Every straight<->chamfer junction carries an R=705 bowl round
wedged in the 135° corner: vertical faces fan 0°->45° in ~5.6° steps (side) and
45°->90° (back), tangent length T=R*tan(pi/8)=292, flat chamfer middle 1045.2.
The sharp octagon vertex sits ~110 uu outside the real surface. (A single-circle
Kasa fit over all verts diverges — mixed inner/outer shells; the wedged-arc
construction verifies tangent distances to exactly R on both planes.)

- Mode branch per user decision: `roundCorners = sim->usingRealSoccar()` —
  mesh mode draws tangent-shortened runs + 8 planRound junctions (all 3 courses)
  + relocated TC trim; fallback keeps the legacy sharp shell.
- One global UV field per wall in BOTH modes (mesh runs sample subranges):
  a mesh-anchored re-zeroing once shifted the lattice 292 uu and flipped the
  VG6 full-patch autocorrelation 91->46; the unified field restored 91
  deterministically (band analysis: rows with signal read exactly 91/92).
- Column parity (alternate hex columns x0.82): panel variation + breaks VG6's
  half-period tie structurally. Dim parity stays blue-dominant.
- Softening package (subagent design, fact-checked, bevel-box idea dropped —
  a 10x10 diamond sits entirely inside the 28-wide bar footprint): 4 goal
  post-cap spheres (mouth pixel-identical), 16 beam + 4 cove shadow stripes,
  4 diagonal cove connectors, outer skirt band z 2048..2120 (closes the void
  pocket over wall tops), TC trim sunk to 24 wide darker steel.
- Ceiling: full footprint at z=2046 (2 uu below crown tangent — no z-fight,
  covers round-top slivers that read as black wedges).
- `ARENA_TEXTURES PASS (108 checks, 0 failed)` (new: TestPlanRoundUV,
  TestRoundTable, TestMeshField, column parity).
- Gate-metric replication offscreen, mesh mode: VG6 pitch 91 (exp 91.5),
  blue 0.998; VG15 all patches L>24 (min 24.1), asymmetry 1.09, frame ratio
  0.54; VG11 base 66; ceiling L=60; fallback probes all OK.
- KNOWN (harness, pre-existing, documented in ledger): VG6 full-patch AC ties
  46 vs 91 px at ±1% — uniform-honeycomb vertical seams repeat every half
  period, and 2 rows of HUD tick text (shot-tick race: screenshots land on
  tick 120-129) decide the winner nondeterministically. Wall bands with
  signal read exactly 91/92 on every build; reference exposure identical.
- Full bar on rebuilt binaries: make all (ROUNDTRIP/GAMEPAD/CAMERA/
  KEYBOARD+MENU/ARENA_TEXTURES 108/ARENA 55) PASS, measured 19/19, mesh 13/13,
  verify_vs_rocketsim ALL PASSED, visual 1 PASS/0 FAIL/15 SKIP (no gamescope;
  VG1 re-baseline on the reference machine still pending from the A4 line).

## 2026-10-17 — playtest defects: mouth base slit + car hidden in net (surgical)

Two defects from live playtest screenshots (CARCAM, real match):
1. A black slit in the base curb at the goal-mouth cutout edge.
2. A blue car inside the goal rendering embedded in the dark net box.
Both reproduced offscreen on gate cameras before touching code.

1. Slit: the mouth cutout leaves the base sweep as an open tube; the hollow
   shows as a black slit at grazing angles. Fix (narrowest): opt-in end caps
   on arcSweep (triangle fan from the arc centre over the rim, same texture/
   state machine), enabled only on the 4 mouth-facing cut ends. Probe: 0
   void-dark pixels in the cut band (was a visible slit); capture shows solid
   concrete ends.
2. Hidden car: the goal interior box + weave drew BEFORE cars/ball WITH depth
   write, culling anything inside/behind the net (ball at (0,5213,91.5): 0
   visible pixels). Fix (narrowest): opaque trim/posts stay early; the two
   translucent pieces moved after DrawBall with depth test on + depth write
   off (mask saved/restored around the pass). Ball now renders veiled through
   the interior (359 vs 78 neutral-bright mouth pixels; image confirms).
   Side benefit: FX after the pass no longer get culled by net depth.
   Step detail: --ballpos (0,5500,100) scores+resets (empty goallog check lies
   unless placed at (0,5213,91.5), inside the mouth but short of the 5215.5
   threshold); veiled-ball pixels are warm-neutral, not blue-dominant.
