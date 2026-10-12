# Visual verdict — MASTER_PROMPT_VISUAL_FIXES.md close-out (supersedes V6)

> **History.** The V6 pass below claimed a clean visual state from VG1–VG12.
> A human playtest then found the scene visibly broken (buried/mirrored Fennec,
> a debug grid over the pitch, an unlit ceiling, flat pads). The V6 record is
> kept verbatim in the second half of this file; **its claims are superseded by
> this section.** The evidence pack for the regression is
> `docs/VISUAL_BUGS_TODO.md`; the work order is
> `MASTER_PROMPT_VISUAL_FIXES.md`; the per-workstream record is
> `docs/VISUAL_FIXES_LOG.md`.

Revision: working tree, 2026-10-10. Renderer: GL 2.1 fixed-function, 1280x720,
offscreen via `gamescope --backend headless` on
`NVIDIA GeForce RTX 4060 Laptop GPU` driver `615.71.09`.

## What was actually wrong, and what is now true

| Bug | Root cause (proven) | Fixed | Regression gate |
|---|---|---|---|
| **V1** corrupt normals | converter applied the node **translation** to every normal via a point transform, no renormalise | yes — rotation-only inverse-transpose + renormalise | VG13 |
| **V2** car buried ~32 uu | the same converter's matrix composition transposed the transform (column-major treated as row-major) | yes — row-major composition; datum pinned to the measured rest height | VG13, VG16 |
| **V3** car mirrored | `our_y = -glb_z` while GLB `+Z` is LEFT and `CarFrame` has local `+y = LEFT` | yes — `our_y = +glb_z` | VG16 (visual) |
| **V4** debug grid over the pitch | `DrawGroundGrid()` unconditional in the play path, `±9000 uu`, drawn after the turf | yes — removed; `--debug-grid` instrument, clipped, drawn before the turf | VG14 |
| **V5** black ceiling / dim walls | one directional light + large ambient left any `Ndot<=0` face ambient-only; **and** `GL_RESCALE_NORMAL` skewed the non-uniformly scaled box normals | yes — key+fill+ambient, `GL_NORMALIZE`, palette re-tuned against the lit render | VG15, VG6 |
| **V6** flat pads | `FX::DrawPads` disabled lighting across plate **and** both orbs | yes — plate + orbs lit; rings/halo stay emissive | VG10, VG12 |
| **V7** gates could not see any of it | VG3 was colour-count only; VG9 always passed `--nofennec` | yes — VG13–VG16 added, each shown failing pre-fix | the new gates |
| **V8** corner glass read as walls | consequence of V5's grey/unlit glass | yes — resolved by the lighting fix; no geometry change | VG6 |
| **V9** `render_pacing.log` in the CWD | `Renderer::EndFrame` appends on every pacing print | **not addressed this pass** (see gaps) | — |

## Evidence

- `python3 tools/verify/visual_gates.py` → **VG1–VG16: 16 PASS, 0 FAIL, 0 SKIP**
  (`tools/verify/gates_visual.json`). Highlights: VG1 two captures
  byte-identical; VG7 60 s run `ticks=7200 drops=0 glerr=0 tris_last=187982`;
  VG13 `0/58851` non-unit normals and `min-z = -17.00`; VG14 turf 79.7 %
  green-dominant with 0.52 % bright-line pixels; VG15 lit/flat frame luminance
  0.74 with wall asymmetry 1.03 and ceiling `L=118`; VG16 the Fennec's own
  silhouette (22 px vs 16 px when buried).
- `python3 tools/verify/mesh_integrity.py` → **PASS** on the re-baked asset
  (`140.42 x 72.47 x 51.65 uu`, matching the source's documented bbox).
- The Fennec renders upright, on the ground, concentric with the cyan hitbox
  and not mirrored (front/side/debug captures in
  `docs/VISUAL_FIXES_LOG.md`).
- Physics untouched and re-proven on this revision (see §Physics below).

## Honest gaps (UNVERIFIED)

- Fennec **baked** wheels cannot spin/steer: the converter flattens the GLB's
  per-wheel nodes into one vertex buffer, so no wheel segmentation is available
  at draw time. (The GLB *does* have `Alpha - FR/FL/BR/BL` nodes; splitting them
  out is future work, not a regression.)
- Fennec paint is a single flat team colour; there is no permitted decal source.
- `--debug-colliders` does not draw the loaded arena mesh or contact manifolds:
  that needs a new read-only `Sim` accessor plus a failing Rule-1 contract test.
- The mesh's wheel-contact plane sits ~3.6 uu above the floor when the car
  rests at z = 17.0011. The authored datum (mesh min-z = −17.00) is honoured;
  the model's low-poly underbody extends below its own contact plane. Under one
  4-px sample at the gated poses and not separately gated.
- **V9 is still open**: `render_pacing.log` is still written to the CWD.
- Visual gates are pixel-based, so several of their detectors had to be
  re-derived after the lighting change (VG4, VG6, VG8, VG14, VG15). Each change
  preserves the assertion's intent and is documented in `docs/VISUAL_FIXES_LOG.md`;
  no assertion was weakened to hide a failure.

## Physics

Physics is untouched and remains closed at M6. Re-proven on this revision:

- `python3 tools/verify/measured.py` → 19 PASS, 0 FAIL
- `python3 tools/verify/mesh_gates.py` → 13 PASS, 0 FAIL (real dump)
- `/tmp/arena_test` → 46/46 PASS
- `make -B tests` → ROUNDTRIP + KEYBOARD+MENU + GAMEPAD PASS
- `python3 src/physics/verify_vs_rocketsim.py` → 110/110
- VG7 60 s scripted run `ticks=7200 drops=0 glerr=0`

No physics file, gate value, frozen tape or measured value was modified. All
visual work reads `Snapshot` only.

---

# (superseded) Visual verdict — MASTER_PROMPT_VISUALS.md close-out (V6)

Revision: working tree, 2026-10-10. Renderer: GL 2.1 fixed-function, 1280x720,
offscreen via `gamescope --backend headless` on
`NVIDIA GeForce RTX 4060 Laptop GPU` driver `615.71.09`
(`GL_VENDOR=NVIDIA Corporation`).

## What was asked, and what is true

The brief is explicit that there is no asset parity with Rocket League and
never will be (Rule 1). "1:1" here therefore means the three checkable tiers in
§2 of the brief, and this pass lands all three:

| Tier | Result |
|---|---|
| **Number-exact** | Arena shell, goal mouth, pad pickup radii, ball radii, camera defaults/ranges are unchanged 1:1 numbers and still cited in `docs/CAMERA_NOTES.md` / `docs/VISUAL_PARITY_LEDGER.md`. VG2 reads the palette back at 0–1/255 error; VG6 measures the 420 uu hex cell; VG8 matches the shockwave curve to <1 px. |
| **Recipe-exact** | The ball ground indicator recipe (0.98r / 1.25r / 0.62r + the +18%-at-1950 growth) is now **measured**, not asserted: VG4 reads 91/71/45 px against a projection prediction of 91.0/71.3/45.1 and ratios 0.780/0.495 against 0.784/0.496. |
| **Coverage-complete** | `docs/VISUAL_PARITY_LEDGER.md` names every visible element as MEASURED / TUNED / UNVERIFIED with a citation or a stated reason, and every row names a gate or an explicit "not gated, here's why". |

## Evidence

- `python3 tools/verify/visual_gates.py` → **VG1–VG12: 12 PASS, 0 FAIL, 0 SKIP**
  (`tools/verify/gates_visual.json`). Highlights: VG1 two captures
  **byte-identical** (md5 `f334befd5c5956c0e33c16428cd61f91`); VG7 `ticks=7200
  drops=0 glerr=0 tris_last=187790 draws_last=253`; VG5 257° @100 boost and
  128° @50, now linear; VG9 car silhouette rotating 21.3° under held pitch;
  VG10 instrument wireframes present; VG11 curb at the wall base.
- Budget is measured, not on paper: two Fennecs = 187,986 tris / 253 draws;
  the procedural box car = 18,412 tris. The 84,823-triangle Fennec is a clean
  mesh (0 degenerate / 0 duplicate triangles), so the overage is **justified in
  writing** and held by a numeric cap (`TRI_CAP = 240,000` in VG7). Measured
  p99 stays at 2.00 ms against the 17.5 ms frame budget.
- Physics regression on the same revision (see §Physics below) — every physics
  gate stays green; no physics gate, gate value or frozen tape was touched.

## What this pass actually fixed (found by pixels, not by eye)

1. `GL_BLEND` leaked off after the first 2D overlay draw, making every
   translucent surface opaque from frame 2 onward.
2. Wall curbs and hex nets were rotated 90° off, sweeping across the arena
   centre instead of lying along the wall.
3. Hex nets were depth-occluded by their own glass panel (blank walls).
4. The boost gauge arc grew quadratically, not linearly (65° at boost=50).

It also added: full car orientation (pitch/roll) read from the existing
read-only sim accessors, procedural wheels with steer + spin, deterministic
sim-time FX, a proper goal shockwave ring, demo/pickup feedback, a
disableable camera-shake overlay, and `--debug-colliders` instrumentation.

## Honest gaps (UNVERIFIED — see the ledger)

- Fennec **baked** wheels cannot spin/steer: the CC-BY-4.0 mesh has no wheel
  segmentation and a second asset is not justified.
- Fennec paint is a single flat team colour; there is no permitted decal source.
- Supersonic trail/embers and the boost flame are drawn and exercised by
  `build/render_stub` but not pixel-gated for a moving car (the chase camera is
  not byte-deterministic); VG9/VG12 gate the feedback FX that can be captured.
- `--debug-colliders` does not draw the loaded arena mesh or contact manifolds:
  that needs a new read-only `Sim` accessor plus a failing Rule-1 contract test,
  which this pass did not land.
- The Fennec overage is 3.8x the original on-paper budget; justified above and
  capped, not reduced.

## Physics

Physics is untouched and remains closed at M6. Re-proven on this revision:

- `python3 tools/verify/measured.py` → 19 PASS, 0 FAIL
- `python3 tools/verify/mesh_gates.py` → 13 PASS, 0 FAIL (real dump)
- `/tmp/arena_test` → 46/46 PASS
- `make tests` → ROUNDTRIP + KEYBOARD+MENU + GAMEPAD PASS
- `python3 src/physics/verify_vs_rocketsim.py` → 110/110
- soak: `build/game --soak 120 --bot slow-chase` (mesh, offscreen) →
  `ticks=14396 drops=0 wall=120.0s p50=2.00ms p99=2.00ms glerr=0`; plus the
  VG7 60 s scripted run `ticks=7200 drops=0 glerr=0`

No physics gate, gate value, frozen tape or measured value was modified. All
visual work reads `Snapshot` only; the two new `Snapshot` fields are filled from
sim accessors that already existed.

## Bottom line

Free-play now looks like a licence-clean recreation of RL freeplay rather than
placeholder geometry, the physics is legible while it plays (orientation, pad
state, ball height, gauge, colliders), and **every visual claim in this pass has
a deterministic screenshot plus a pixel assertion behind it**. The remaining
gaps are named above and in the ledger; none of them is hidden behind a
screenshot.
