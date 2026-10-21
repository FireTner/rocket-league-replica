# Visual bugs TODO — free-play looks broken (2026-10-10)

Owner: coordinator. Status: **V1–V8 RESOLVED (2026-10-10 fixes pass); V10 (arena
shell geometry) RESOLVED 2026-10-12; V11 (arena back faces) RESOLVED
2026-10-20; V9 OPEN.**
This file is the evidence-backed bug list for the visual regressions a human
sees in `build/game`. Every claim has a measured number, a `file:line`, or a
reproducible capture command. Nothing here is a guess presented as a fact —
where a mechanism is inferred rather than proven, it says so.

Resolution record: `docs/VISUAL_FIXES_LOG.md` (per-workstream failing check →
fix → passing check, with raw output) and the top of `docs/VISUAL_VERDICT.md`.

| Bug | Status | Held by |
|---|---|---|
| V1 corrupt Fennec normals | fixed — 0 non-unit (`\|\|n\|\|`=1.0000) | VG13 |
| V2 wrong vertical datum (car buried) | fixed — bbox min-z −17.00 | VG13, VG16 |
| V3 mirrored lateral axis | fixed — `our_y = +glb_z` | VG16 |
| V4 debug grid in the play path | fixed — `--debug-grid` instrument only | VG14 |
| V5 single-light model (black ceiling/walls) | fixed — key+fill+ambient, `GL_NORMALIZE` | VG15, VG6 |
| V6 pads render flat | fixed — plate + orbs are lit | VG10, VG12 |
| V7 gates cannot see any of this | fixed — VG13–VG16 added, each shown failing pre-fix | VG13–VG16 |
| V8 corner glass reads as walls | resolved by the V5 lighting fix; no geometry change | VG6 |
| V9 `render_pacing.log` in the CWD | **OPEN** | — |
| V10 shell geometry vs collision octagon | fixed — shell now matches the octagon | VG6, VG11 |
| V11 arena shell is double-sided | fixed — one-sided shell, cull pass in the 2 arena draws | VG1–VG16, cull captures |

The per-bug detail below is kept verbatim as the pre-fix evidence pack.

Fix order: **V1–V5 first** (they are the "arena is a mess / car is wrong"
cluster), then V6, then V7 (gates) as the regression bar. V8/V9 are follow-ups.

---

## How to reproduce the evidence

```sh
cd ~/Projects/RocketLeagueLaptop
# offscreen captures (gamescope hangs in teardown; poll for the PPM then killpg)
cat > /tmp/cap.sh <<'EOF'
#!/bin/bash
cd ~/Projects/RocketLeagueLaptop || exit 1
out=/tmp/rlcap; mkdir -p "$out"
cap() { local n=$1; shift; rm -f "$out/$n.ppm"
  setsid gamescope --backend headless -W 1280 -H 720 -r 60 -- ./build/game \
    --no-vsync --no-audio --meshdir collision_meshes --seed 7 --ticks 160 \
    --no-shake --shot-at-tick 120 --shot-path "$out/$n.ppm" "$@" >"$out/$n.log" 2>&1 &
  local p=$!; for _ in $(seq 1 250); do
    [ -f "$out/$n.ppm" ] && grep -q "\[game\] exit" "$out/$n.log" 2>/dev/null && break; sleep 0.2; done
  kill -- "-$p" 2>/dev/null; wait "$p" 2>/dev/null; echo "$n done"; }
cap kickoff  --cam 3000,-3000,1200,0,0,200
cap flat_kickoff --flat --cam 3000,-3000,1200,0,0,200
cap car_tight --cam 120,-2450,70,0,-2300,25
cap car_flat --flat --cam 120,-2450,70,0,-2300,25
cap car_debug --debug-colliders --cam 120,-2450,70,0,-2300,25
cap pad --cam -3072,-2900,320,-3072,-3600,0
cap up --cam 0,-300,200,0,0,2044
cap flat_up --flat --cam 0,-300,200,0,0,2044
EOF
bash /tmp/cap.sh
for f in /tmp/rlcap/*.ppm; do magick "$f" "${f%.ppm}.png"; done
```

---

## V1 — P0 — Fennec normals are corrupt (the car's "weird texture")

**Symptom.** The car body renders as a harsh white/navy speckled patchwork.

**Measured evidence.**
- `assets/models/fennec.bin`: **58,851 / 58,851 (100%) of normals are non-unit**.
  Length min 76.42, median 100.00, max 100.00. A correct mesh has |n| = 1.
- `src/render/Renderer.cpp` — grep for `NORMALIZE|RESCALE` returns **none**;
  fixed-function lighting consumes the authored normal directly.

**Root cause (proven).** `tools/convert_fennec.py:90`

```python
wn = xform(m, (nn[0], nn[1], nn[2]))   # rotation only matters  <- comment is wrong
```

`xform` (same file, line 72-73) is a **point** transform:

```python
def xform(m, p):
    return tuple(m[c][3] + sum(m[c][k] * p[k] for k in range(3)) for c in range(3))
```

It adds the node translation `m[c][3]` to the vector. Feeding a normal through
it adds a large constant (order 10–100 uu) to every normal, and there is no
renormalisation afterwards. With `GL_NORMALIZE` off, the lighting dot product
is scaled by |n| ≈ 100, so faces saturate to white or fall to black per
triangle (`glShadeModel(GL_FLAT)` at `Renderer.cpp:57` makes it per-face).

**Fix.**
1. Transform normals with the rotation-only 3×3 part of `m` and renormalise;
   re-run the converter (source GLB is present at
   `~/Projects/RocketLeague2/public/assets/models/fennec.glb`).
2. Defence in depth: enable `GL_RESCALE_NORMAL` (uniform scale) or
   `GL_NORMALIZE` in `Renderer::Init`.
3. Add a mesh-integrity check (see V7) that fails on any non-unit normal.

**Acceptance.** `car_flat` and `car_tight` show a smooth, readable car body;
the integrity check reports 0 non-unit normals.

---

## V2 — P0 — Fennec vertical datum is wrong → the car is buried in the floor

**Symptom.** The car reads as a flat sliver / "upside down" blob; only its top
shows above the turf.

**Measured evidence.**
- Baked `assets/models/fennec.bin` bbox: `z ∈ [−48.91, −1.67]` — the **entire
  mesh is below its own origin**, and it is 47.24 uu tall.
- Cars are drawn at the sim's rest height `z = 17`
  (`src/game/main.cpp:914-915` pass `snap.carPos`, which is the RocketSim car
  origin; hitbox rest z = 17). So the mesh's lowest point lands at world
  `z ≈ −32` (below the floor at z = 0) and its roof at `z ≈ +15.3`.
- `RocketLeague2/src/render/FennecModel.ts:32` documents the intended datum:
  *"The wheel contact plane is at GLB y = −1.50, and a resting car's origin
  sits 1.43 above contact"*. With rest z = 17, the baked min-z should be
  **≈ −17**, not −48.91 → an error of ≈ **+32 uu**.
- `car_debug_zoom.png` (`--debug-colliders`): the cyan Octane hitbox sits
  correctly on the ground while the mesh is visibly sunk low inside/below it.

**Root cause (proven offset; converter matrix math is the suspect).** The
converter composes node matrices with `mul()` (`convert_fennec.py:69-70`), which
indexes column-major storage with row-major semantics; combined with the
point/normal confusion in V1 this produces a wrong translation. The offset is
measured; the exact faulty line needs a re-derivation pass against the source.

**Fix.** Re-derive the transform from `FennecModel.ts:25-32` and the source GLB,
re-convert, and assert the baked bbox min-z ≈ −17.00 ± 1 with size ≈
`140.4 × 71.4 × 47.2` (or the source's documented `140.42 × 51.65 × 72.47`
in its own Y-up frame).

**Acceptance.** With `--debug-colliders`, the mesh and the hitbox share the
same ground plane and are concentric; a side view shows wheels touching z = 0.

---

## V3 — P0 — Fennec lateral axis appears mirrored

**Symptom.** The car's left/right look swapped (asymmetric livery reads wrong);
compounds the "upside down / weird" impression.

**Evidence.**
- `FennecModel.ts:25-28`: `GLB +X = nose`, `GLB +Y = up`, `GLB −Z = right`
  (so GLB +Z = left).
- `tools/convert_fennec.py:93`: `our = (glb_x, −glb_z, glb_y − 15.5)`.
- The draw frame uses local **+y = LEFT**: `src/models/Models.cpp` CarFrame
  comment *"left = up x forward (local +y)"*, and `src/models/Fennec.cpp:126`
  builds `l = u × f` as the second matrix column.
- Therefore the model's left (+glb_z) maps to our −y = **right** → mirrored.

**Status.** Strong inference from two cited sources; **confirm visually** with a
before/after re-convert (cheap) before declaring the sign. Do not "fix" it by
flipping the draw matrix — fix the converter, or every other mesh inherits it.

**Fix.** Sign of the lateral term in the converter (`+glb_z`, not `−glb_z`),
plus a front/side capture pair in the acceptance evidence.

---

## V4 — P0 — Debug ground grid is drawn into the normal render path

**Symptom.** The floor reads as an endless "playground" grid; the arena walls
appear to stand in the middle of that grid.

**Evidence.**
- `src/game/main.cpp:780`: `renderer.DrawGroundGrid();` — unconditional, in the
  play path, right after the turf.
- `src/render/Renderer.h:58`: `DrawGroundGrid(float extent = 9000.0f, float step = 500.0f)`.
  The arena is ±4096 (x) / ±5120 (y), so the grid covers **±9000** — roughly
  **2× beyond the walls** — and it is drawn at `z = 0`, i.e. above the turf top
  (turf boxes are centred at z = −5, 8 tall → top at −1).
- Unlit bright colour `(0.35, 0.45, 0.55)` with lighting disabled
  (`Renderer.cpp:203-216`), so it pops against the dim lit scene.
- Corroboration: the ball-indicator gate's own comment
  (`tools/verify/visual_gates.py`, `indicator_radii`) notes *"an isolated bright
  pixel at the centre (the arena grid line showing through the disc)"* — the
  stray grid is already interfering with a gate.

**Fix.** Remove it from the play path; if still wanted as an instrument, gate it
behind `--debug-grid` (like `--debug-colliders`), clip its extent to the arena,
and draw it before the turf.

**Acceptance.** No grid lines in `kickoff`/`pad`/`wall` captures; turf reads as
a continuous mowed surface.

---

## V5 — P0 — Single-light model leaves the ceiling and half the walls unlit

**Symptom.** "No ceiling", dark navy slabs where walls should be, and a
generally dim blue arena.

**Evidence.** `Renderer::Init` (`Renderer.cpp:57-70`):

```cpp
glShadeModel(GL_FLAT);
GLfloat dir[] = {0.3f, -0.5f, 0.8f, 0.0f};   // direction TO the light
GLfloat dif[] = {0.75f, 0.75f, 0.75f, 1.0f};
GLfloat amb[] = {0.45f, 0.45f, 0.50f, 1.0f}; // global ambient (blue-ish)
glEnable(GL_COLOR_MATERIAL);
```

Any surface whose normal has `Ndot(dir) ≤ 0` is ambient-only. The ceiling's
only visible face has normal `(0,0,−1)` → unlit, over base colour
`(0.02,0.04,0.07)` (`main.cpp:866`) → reads as void. The +x wall's inner face
(normal −x) is unlit for the same reason, while the −x wall is lit → asymmetric
dark slabs.

Measured lit vs unlit (`--flat`) at fixed screen points:

| sample | LIT rgb | UNLIT rgb | lit/unlit luminance |
|---|---|---|---|
| turf (kickoff near floor) | (21, 54, 56) | (44, 113, 102) | 0.51 |
| back wall (kickoff) | (33, 53, 95) | (54, 92, 155) | 0.60 |
| ceiling / look-up | (49, 77, 118) | (88, 141, 196) | 0.57 |
| car body | (47, 73, 81) | (30, 95, 123) | 0.81 |

`--flat` proves the geometry is present and coherent — including the ceiling
boxes at `z ≈ 2028–2078` (`main.cpp:866-871`). This is a **lighting/palette**
bug, not missing geometry.

**Fix.** Replace the single directional light with a hemisphere + fill model
(or two-sided lighting with a raised ambient), enable normal normalisation
(V1), then re-tune the turf/wall/ceiling palette against the **lit** result
(the current palette was tuned against unlit colours). Keep `--flat` working as
the diagnostic.

**Acceptance.** Looking up shows a readable ceiling; both side walls read as
glass panels of similar brightness; no surface is ambient-only black.

---

## V6 — P1 — Pads render flat (plate, halo and orbs are unlit)

**Symptom.** "Boost pads are just flat textures/colours."

**Evidence.** `FX::DrawPads` (`src/fx/FX.cpp:85-144`) explicitly disables
lighting for all pad geometry — `glDisable(GL_LIGHTING)` at line 102,
re-enabled at line 143 — so the hexagonal plate (lines 103-120), the trim/halo
rings (125-136) and **both orb spheres** (lines 138-141) are drawn with flat
colours. A lit sphere with lighting off is just a flat disc, which is exactly
what the `pad` capture shows.

**Fix.** Enable lighting for the 3D parts (plate cylinder + orb spheres) and
keep the rings/skirt unlit for the glow; alternatively add a cheap shading ramp
so the orb reads as a ball. Verify at the `pad` scene pose
(`--cam -3072,-2900,320,-3072,-3600,0`).

---

## V7 — P1 — The visual gates cannot see any of this (why 12/12 passed)

**Evidence.**
- **VG3** (`visual_gates.py:365-388`) asserts only: each scene has ≥ 12
  quantized colours, ≥ 20 % non-background pixels, and is non-uniform. A dim
  blue mess with a bright grid satisfies all three.
- **VG9** tests the **procedural box car**, not the Fennec:
  `visual_gates.py:695,697` pass `--nofennec`. So the 84k-tri Fennec mesh is
  never validated — only that *something* renders (VG3).
- No gate asserts mesh integrity (unit normals, bbox/datum), and none compares
  the lit frame against `--flat`, so a lighting regression is invisible.

**Fix (land these as FAILING first, then fix — Rule 1 spirit).**
1. `VG13 mesh integrity`: baked normals unit-length (±0.01), bbox min-z ≈ −17,
   bbox size within tolerance of the hitbox.
2. `VG14 arena occupancy`: the turf (green-dominant) must cover ≥ X% of the
   floor region in the `kickoff` pose; no bright grid lines over the turf band.
3. `VG15 lighting sanity`: mean luminance of the turf/wall/ceiling patches must
   fall in a band derived from the unlit render — catches ambient-only surfaces.
4. Extend `VG3` with a per-scene brightness/contrast floor and a known-landmark
   colour assertion (turf green, wall blue, car body) instead of "non-uniform".

---

## V8 — P2 — Corner glass panels read as walls crossing the floor

At 45°, 1629 uu long and 2048 uu tall (`main.cpp:850-864`), the corner panels
seen from inside occupy a large screen area and, because glass renders dark and
opaque-ish (V5), they read as walls intruding into the playfield. **Re-check
after V4/V5**; if they still read wrong, the fix is glass shading (transparency
that actually shows the far side), not geometry.

## V9 — P2 — `render_pacing.log` is written to the CWD every run

`Renderer::EndFrame` (`Renderer.cpp:145-147`) does `fopen("render_pacing.log","a")`
on every pacing print, so the repo root accumulates a log. It is gitignored, but
it is a side effect in the play path; move it under `--soak`/`--log` or to a
runtime dir.

---

## V10 — P0 — Arena shell geometry did not match the collision octagon

Reported by the owner 2026-10-12 ("the arena is a cluttered mess full of
non-collidable walls and similar"). Four independent geometry defects in the
hand-drawn shell in `src/game/main.cpp`; all are visual-only, so each one was a
surface the car drives straight through.

1. **Corner glass + curb centred on `|x|+|y| = 5888`, not 8064.**
   `cornerGlass` / `wallCurb` were called with `(±2944, ±2944)`, but the 45 deg
   chamfer edge runs `(4096,3968)→(2944,5120)` and its midpoint is
   `(3520,4544)`. Each 1629 uu × 2048 uu panel therefore sat 2176 uu inside the
   arena, forming a smaller rotated square of glass walls across the corners —
   the single biggest source of the "clutter". (This also supersedes V8's
   "no geometry change" conclusion: the lighting fix was necessary but not
   sufficient.)
2. **Straight walls 1152 uu too long at each end.** The side panels spanned
   `y ∈ ±5120` where the collision side wall stops at `±3968`; the back panels
   spanned `x ∈ ±4096` instead of `±2944`. The overlap poked out past the
   chamfer as extra non-collidable sheets.
3. **`wallCurb` treated its degree literals as radians.** `glRotatef(yaw *
   180/π)` turned `-90°` into `243.4°` and `180°` into `233.2°`, so six of the
   eight coves swept the floor as diagonal ridges instead of following the wall
   base. (Only the `+Y` back wall, yaw 0, was correct — which is exactly the one
   cove VG11 sampled before this fix.)
4. **`glass()` handed degrees to `DrawHexNet`, which converts radians.**
   Side-wall nets were rotated 26.6° out of the wall plane; at `halfW = 5120`
   the ends swung ~2294 uu into the arena as diagonal honeycomb sheets.

Reproduce (exit 1 = FAIL):

```sh
make build/game
python3 tools/verify/visual_gates.py --only VG6,VG11 --diag
```

Measured, pre-fix → post-fix:

```
VG11 corner floor 21131 vs stray cove 41215   ->  33392 vs 19915
VG6  hex x-pitch 41 px (expected 92)          ->  92 px (expected 92)
```

VG6's old recipe (a vertical run spacing against `0.25*cell`) was not the
flat-top invariant and only matched while the net was rotated out of the wall;
it now measures the exact horizontal period `sqrt(3)/2 * cell`.

## V11 — P1 — Arena shell drawn double-sided: an eye outside a wall sees an
opaque backside instead of the arena

Reported by the owner 2026-10-20 ("when a camera goes through a wall
(intentionally) it is too opaque and stops rendering things inside the
arena"). Presentation-only: the sim never sees the shell.

**Mechanism (two defects, both from `glCullFace` never being enabled).** The
baked arena (`src/models/ArenaShell.cpp`) is a one-sided interior shell — wall
/ crown / skirt quads are authored with their normals pointing INTO the room —
but it was drawn with culling OFF, so the outward side rasterised too:

1. **Occlusion.** An eye pushed through a wall by the chase rig (`main.cpp` only
   clamps the eye's Z: `cz` 40..1928) or by freecam sees the wall's outer face
   instead of the interior.
2. **Depth-write from a translucent surface.** The glass is blended
   (`kGlassA = 0.40`) but still writes depth. Because the buckets are drawn
   BEFORE pads/cars/ball, the wall's depth is in the buffer first, so every
   interior object drawn afterwards failed `GL_LEQUAL` — the turf that was
   already drawn stayed visible (tinted), the cars and ball vanished. That is
   the "stops rendering things inside" half of the report.

**Fix.** (a) The bake re-winds every triangle so its CCW front face agrees
with its own vertex normals (`fixWinding`), which is valid for every emitter
(measured: 0/1056 opaque, 64/416 wallbase, 2/2 ceiling, 80/144 skirt, 152/472
glass, 4/4 weave tris needed a flip). (b) The two arena draws run inside a
`CullScope` (`GL_CULL_FACE`, `GL_BACK`, `GL_CCW`, saved/restored like the
existing depth-mask sandwich). From outside you now look INTO the arena; from
inside the same surfaces stay front-facing. Culling stays off for
cars/ball/pads/FX, so nothing else changes side-ness. `RL_NOCULL=1` restores
the old double-sided behaviour for A/B captures.

Reproduce (captures are `tools/verify/capture.sh OUT NAME --cam eye,target`):

```sh
make build/game
bash tools/verify/capture.sh /tmp/v11 outside --cam 4300,0,400,0,0,400
bash tools/verify/capture.sh /tmp/v11 inside  --cam 3000,0,400,0,0,400
RL_NOCULL=1 bash tools/verify/capture.sh /tmp/v11 old --cam 4300,0,400,0,0,400
```

Measured, pre-fix → post-fix (same scene, seed 7, tick 120):

```
eye 4300,0,400 (outside +X wall)  centre green 0.021 -> 0.429  (inside ref 0.432)
                                  full   green 0.079 -> 0.327
                                  RL_NOCULL=1 reproduces 0.021 exactly
car crop x480-800 / y370-400      variance 236 -> 1632, max channel 122 -> 249
                                  --nofennec vs Fennec: 66 px (rows 380-383)
eye 3000,0,400 (inside, control)  76309 px move: 64686 by 1-4/255 (the second
                                  blend layer) + 2208 by >8 (max 87, the 1 px
                                  bright cove lines)
```

The default chase-cam capture moves 53954 px (5.9%): 52662 by 1-4/255, 850 by
more than 8 (max 118, isolated 1 px depth-tie seams). Those deltas are the
`alpha = 0.9` beam/reveal/cove boxes, whose front and back faces land on the
same pixels and whose far face therefore passed `GL_LEQUAL` and blended a
second time; culling removes that duplicate layer. Nothing loses coverage — a
culled whole surface would show a large area at 50-120 and there is none.

`RL_NOCULL=1` reproduces the PRE-FIX image **byte-for-byte** in both views
(`md5 9027bf5b…` / `b5f8a7bb…`, identical to the pre-fix captures), so the
re-wind alone is pixel-neutral and every delta above is the cull pass alone.
Repeat captures in the same mode are byte-identical too (md5 `94905a06…`
twice); earlier apparent run-to-run jitter was capture-scheduling noise, so
compare spaced runs, not back-to-back ones.

Held by the full `visual_gates.py` suite (VG1 determinism, VG6 glass, VG11
curbs, VG14 turf, VG15 lighting, VG7 budget) — see
`src/render/BUILD_NOTES.md` for the run output. Note the inside-view numbers
above are the *reason* those gates stay the bar: they capture an inside eye,
so they can only catch a cull regression indirectly (V11 itself needs the
outside capture recipe above).

## Definition of done for this TODO

- V1–V5 fixed, each with a re-captured before/after PPM pair.
- V6 fixed.
- V7 gates added **and demonstrated failing on the pre-fix revision**, then
  passing post-fix.
- Every existing gate still green: `measured.py` 19/19, `mesh_gates.py` 13/13,
  `arena_test` 46/46, `make tests` PASS, `visual_gates.py` VG1–VG16 PASS.
- No physics gate, gate value, frozen tape or sim code path touched.
