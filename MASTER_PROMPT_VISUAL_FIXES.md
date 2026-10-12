# MASTER PROMPT — Parallel visual fixes (make free-play look correct)

Work order for a **team of parallel LLM agents**. Hand each agent its own
workstream section (WS-A…WS-E); each section is self-contained. The goal is to
fix every open bug in `docs/VISUAL_BUGS_TODO.md` without touching physics.

Read first: `AGENTS.md`, `CONTRACTS.md`, and **`docs/VISUAL_BUGS_TODO.md`**
(the evidence pack — every bug has measured numbers and `file:line` citations).

---

## 1. Mission

`build/game` renders a broken free-play scene: a buried/mirrored car with
corrupt normals, an endless debug grid, a black ceiling, dim navy walls and
flat unlit pads — while `tools/verify/visual_gates.py` reports 12/12 PASS.

Fix the rendering so the arena is coherent and the car is correct, harden the
gates so these regressions can never pass again, and **leave physics untouched**.

---

## 2. Non-negotiables (a violation is a failed round)

1. **No physics change.** Do not edit `src/sim/`, `src/arena/`, `src/physics/`,
   `tools/tas/tapes/`, or any gate value. All physics gates must stay green:
   `measured.py` 19/19, `mesh_gates.py` 13/13, `arena_test` 46/46,
   `verify_vs_rocketsim.py` 110/110, `make tests` PASS.
2. **No Psyonix assets.** The Fennec is the CC-BY-4.0 community mesh; the arena
   mesh is user-supplied runtime data. Never vendor or redistribute either.
3. **Render reads snapshots only.** No visual code may call into or alter a sim
   code path. New sim data requires a read-only accessor in `Sim.h` plus a
   failing Rule-1 test.
4. **Own your files; do not touch another workstream's files.** Ownership table
   in §4. Cross-file needs are requests, not edits.
5. **Reproduce before you fix.** Every workstream lands a **failing** check
   (gate or capture assertion) that demonstrates its bug on the current build,
   quotes the failure, then fixes it and shows the same check passing.
6. **Cite numbers.** Every visual constant added or changed names its source
   (measured pixel, measured mesh value) or is marked `TUNED`/`UNVERIFIED` with
   a reason, per `docs/VISUAL_PARITY_LEDGER.md` conventions.
7. `--flat` (unlit) must keep working — it is the diagnostic that separates
   lighting bugs from geometry bugs.

---

## 3. Evidence pack (do not re-derive; re-confirm)

From `docs/VISUAL_BUGS_TODO.md`, measured on the current revision:

- Fennec normals: **100 % non-unit**, |n| median 100.0 — converter applies node
  translation to normals (`tools/convert_fennec.py:90` via `xform`, line 72).
- Fennec bbox z ∈ **[−48.91, −1.67]** — entirely below origin; should be
  min-z ≈ −17 for rest z = 17 (`FennecModel.ts:32`). Car draws ~32 uu too low.
- Fennec lateral mapping `our_y = −glb_z` mirrors `GLB +Z = left`
  (`convert_fennec.py:93` vs `FennecModel.ts:25-28` vs local +y = left).
- `Renderer.cpp` has **no `GL_NORMALIZE`/`GL_RESCALE_NORMAL`**.
- `main.cpp:780` calls `DrawGroundGrid()` with defaults `extent=9000, step=500`
  (`Renderer.h:58`) in the play path; arena is ±4096/±5120.
- One directional light `(0.3,−0.5,0.8)` + ambient `(0.45,0.45,0.50)`, `GL_FLAT`
  (`Renderer.cpp:57-70`); ceiling under-face and +x wall inner-face are
  ambient-only. Lit/unlit luminance ratios: turf 0.51, wall 0.60, ceiling 0.57.
- `FX::DrawPads` disables lighting for plate + orbs (`src/fx/FX.cpp:102,143`).
- Gates: VG3 is colour-count only; VG9 passes `--nofennec` (box car).

Reproduce captures with the `/tmp/cap.sh` recipe in `VISUAL_BUGS_TODO.md`.

---

## 4. Ownership matrix (hard boundaries)

| Workstream | Owns (write) | Must not touch |
|---|---|---|
| **WS-A** Lighting & arena composition | `src/render/Renderer.cpp`, `src/render/Renderer.h`, the arena/render block of `src/game/main.cpp` | `src/fx/**`, `tools/**`, `assets/**`, `src/sim/**` |
| **WS-B** Fennec asset pipeline | `tools/convert_fennec.py`, `assets/models/fennec.bin`, `tools/verify/mesh_integrity.py` (new) | `src/render/**`, `src/game/**`, `tools/verify/visual_gates.py` |
| **WS-C** Pads & FX shading | `src/fx/FX.cpp`, `src/fx/FX.h` | `src/render/**`, `src/game/**` |
| **WS-D** Gate hardening | `tools/verify/visual_gates.py` | every `src/**` file |
| **WS-E** Integration & verdict | `docs/VISUAL_FIXES_LOG.md` (new), `docs/VISUAL_VERDICT.md` | product code (may only *request* fixes) |

**Interface contracts (agreed up front so the streams stay independent):**

- `GL_RESCALE_NORMAL`/`GL_NORMALIZE` is enabled by **WS-A** in
  `Renderer::Init`. WS-B fixes the asset; both are required and both are
  idempotent.
- The turf/wall/ceiling palette is owned by **WS-A** only. WS-C/WS-B do not
  change arena colours.
- New gate IDs are allocated: **VG13** mesh integrity (WS-B asserts via
  `mesh_integrity.py`; WS-D wires it into `visual_gates.py`), **VG14** arena
  occupancy, **VG15** lighting sanity (WS-D). WS-B must not edit
  `visual_gates.py`; it exposes `mesh_integrity.py --json` and WS-D calls it.
- The Fennec draw frame convention (local +x = forward, +y = left, +z = up) is
  frozen (`Models.cpp` CarFrame). WS-B adapts the asset to it; WS-A/WS-C never
  change it.

---

## 5. Workstreams

Each is a complete brief for one agent. Do not read or depend on another
workstream's in-flight edits; code to the interfaces above.

### WS-A — Lighting & arena composition (fixes V4, V5, V8)

**Deliverables.**
1. Remove `renderer.DrawGroundGrid()` from the play path (`main.cpp:780`); if it
   is useful as an instrument, add a `--debug-grid` flag that draws it clipped to
   the arena extent (`±4096/±5120`) and *before* the turf. Default off.
2. Replace the single-light model in `Renderer::Init` with a lighting setup that
   leaves **no visible surface ambient-only**: a hemisphere/sky-ground ambient
   plus a key light and a fill light, or two-sided lighting
   (`GL_LIGHT_MODEL_TWO_SIDE`) with raised ambient. Add
   `glEnable(GL_RESCALE_NORMAL)`. Keep `--flat` meaning "lighting off".
3. Re-tune the arena palette **against the lit render** (turf stripes, walls,
   ceiling at `main.cpp:766-871`) so it reads like the RL soccar look rather
   than tuning against unlit colours. State the chosen values and whether each
   is `MEASURED` or `TUNED`.
4. Make the ceiling read as a ceiling (visible structure/trim, not a black
   plane) and make both side walls read as glass of comparable brightness.
5. Re-check the 45° corner glass panels (V8) after the lighting fix and report
   whether they still read as walls in the playfield.

**Required evidence.** `kickoff`, `wall`, `pad`, `up`, `inside_hi` captures
before AND after, plus the lit/unlit luminance table recomputed. No `GL_NORMALIZE`
regression: `glerr=0` in the exit line.

**Do not.** Touch `FX.cpp`, the converter, or any gate.

### WS-B — Fennec asset pipeline (fixes V1, V2, V3)

**Deliverables.**
1. Fix `tools/convert_fennec.py`:
   - transform normals with the rotation-only 3×3 part of the node matrix and
     **renormalise** (never add translation to a normal);
   - correct the matrix composition (`mul`) if it is not producing the
     documented datum — see below;
   - correct the lateral mapping so the asset lands on the frozen draw frame
     (local +x = forward, +y = left, +z = up) per `FennecModel.ts:25-32`.
2. Re-convert `assets/models/fennec.bin` from
   `~/Projects/RocketLeague2/public/assets/models/fennec.glb`.
3. Verify the baked datum: bbox **min-z ≈ −17.00** (wheel contact at world z = 0
   when the car origin rests at z = 17), size ≈ `140.4 × 71.4 × 47.2`.
4. Confirm orientation visually against the frozen draw frame: wheels at the
   **bottom**, nose along **+x**, no left/right mirror. Capture a front and a
   side view and show them.
5. Add `tools/verify/mesh_integrity.py`: asserts (a) 0 normals with
   `| |n| − 1 | > 0.01`, (b) bbox min-z within ±1 of −17, (c) bbox size within
   ±3 uu of the hitbox, (d) non-degenerate triangle count. Exit non-zero on
   failure; support `--json` so WS-D can call it.

**Required evidence.** `mesh_integrity.py` output before (fails, with the
numbers above) and after (passes); `car_tight`, `car_flat`, `car_debug` captures
before/after showing the car upright, on the ground, concentric with the cyan
hitbox.

**Do not.** Edit `Renderer.cpp` (WS-A owns `GL_RESCALE_NORMAL`) or
`visual_gates.py` (WS-D owns it).

### WS-C — Pads & FX shading (fixes V6)

**Deliverables.**
1. In `FX::DrawPads` stop disabling lighting for the **3D** parts: the hexagonal
   plate and both orb spheres must be lit (or given an explicit shading ramp) so
   the orb reads as a ball and the plate as a solid.
2. Keep the emissive trim ring, ground halo and pickup burst unlit (they are
   glows).
3. Do not change pad sizes/positions/timers — those are gated physics values
   (radii 208/144, amounts 100/12, cooldowns 10 s/4 s).
4. Report the orb/plate brightness change as before/after pixel samples at the
   `pad` pose.

**Required evidence.** `pad` capture before/after; confirmation that `G9a–d`
pad gates and `VG10`/`VG12` still pass.

**Do not.** Touch `Renderer.cpp`, `main.cpp`, or the converter.

### WS-D — Gate hardening (fixes V7; makes every other fix regression-proof)

**Deliverables.** Add gates that **fail on the pre-fix build** and pass after.
Land them first and record the failures.

1. **VG13 mesh integrity** — shell out to `tools/verify/mesh_integrity.py --json`
   and fail on any assertion it makes (unit normals, datum, size).
2. **VG14 arena occupancy** — in the `kickoff` pose, the turf must cover the
   expected fraction of the floor region with green-dominant pixels, and there
   must be **no bright grid lines** across the turf band (catches V4).
3. **VG15 lighting sanity** — sample fixed turf/wall/ceiling patches in the lit
   capture and require each to fall inside a band derived from the `--flat`
   capture (e.g. lit/unlit luminance ratio ≥ 0.75 and no patch near-black);
   catches V5.
4. **Strengthen VG3** from "≥12 colours, non-uniform" to include a per-scene
   brightness/contrast floor and landmark-colour assertions (turf green, wall
   blue, car body present) — but keep it robust to legitimate palette tuning.
5. **Add a Fennec-path gate.** VG9 currently passes `--nofennec`; add a variant
   (or a new gate) that captures **with** the Fennec and asserts a non-trivial,
   correctly-oriented car silhouette (width/height ratio, wheels below the body
   centreline). Use WS-B's fixed asset.

**Required evidence.** A log showing each new gate **FAIL** on the pre-fix
revision with the measured value, and **PASS** on the fixed revision. Never
fake a SKIP as a PASS.

**Do not.** Edit any `src/**` file. If a gate needs a product change, file it as
a request in `docs/VISUAL_FIXES_LOG.md` (via WS-E).

### WS-E — Integration, regression proof, verdict

**Deliverables.**
1. Maintain `docs/VISUAL_FIXES_LOG.md`: per workstream, the failing check, the
   fix, the raw command output, and the re-captured evidence paths.
2. Enforce merge order (§6) and re-run the **full** suite on the integrated
   revision: `measured.py`, `mesh_gates.py`, `arena_test`, `make tests`,
   `verify_vs_rocketsim.py`, `visual_gates.py` (all gates incl. new).
3. Confirm physics is byte-identical where it must be: the frozen fallback
   checksum and the 10k mesh checksum unchanged from `docs/PHYSICS_VERDICT.md`.
4. Write the close-out into `docs/VISUAL_VERDICT.md` (supersede the V6 pass,
   keeping its history) with an updated `docs/VISUAL_PARITY_LEDGER.md` for every
   value any workstream changed.
5. Report honestly which items remain `UNVERIFIED`.

**Do not.** Fix product code yourself; route issues back to the owning
workstream with the failing output.

---

## 6. Merge order and integration protocol

1. **WS-D first** (gates exist and fail) — no product code, so it cannot
   conflict.
2. **WS-B** (asset) — self-contained; unblocks the car gates.
3. **WS-A** (renderer/main) — largest surface; rebase after B so capture
   baselines include the fixed car.
4. **WS-C** (FX) — independent file; can land any time after A.
5. **WS-E** integrates and re-verifies everything on the merged revision.

Conflict rules: exactly one writer per file (ownership table). If a workstream
needs a change outside its files, it writes a request line in
`docs/VISUAL_FIXES_LOG.md` naming the file, the change, and the failing evidence,
and waits — it does not edit.

Because gates are pixel-based, **capture baselines must be taken after each
landing**; a fix in WS-A that changes brightness will change WS-D's expected
bands, so WS-D's thresholds must be expressed as ratios/bands, not fixed pixel
values, wherever possible.

---

## 7. Final acceptance (all must hold on the integrated revision)

- [ ] V1–V6 fixed with before/after captures.
- [ ] `mesh_integrity.py`: 0 non-unit normals, datum within tolerance.
- [ ] Car renders upright, on the ground, concentric with the hitbox, not
      mirrored (front + side captures).
- [ ] No debug grid over the playfield; the arena floor reads as turf.
- [ ] Ceiling and both side walls readable; no ambient-only black surfaces.
- [ ] Pads read as 3D objects.
- [ ] `visual_gates.py` includes VG13–VG15 + a Fennec-path gate, all PASS, and
      each was shown failing pre-fix.
- [ ] All physics gates green and checksums unchanged.
- [ ] `docs/VISUAL_PARITY_LEDGER.md` updated for every changed value;
      `docs/VISUAL_FIXES_LOG.md` complete.

---

## 8. Anti-patterns (rejected on sight)

- "Fixing" the car by flipping the draw matrix in `Fennec.cpp` instead of
  fixing the asset — every future mesh would inherit the bug.
- Hiding the grid by making it transparent instead of removing it from the play
  path.
- Masking the lighting bug by brightening only the surfaces that are visible in
  the capture poses; every surface must be lit correctly.
- Weakening or deleting an existing gate, or editing a threshold so a FAIL
  becomes a PASS.
- Editing a physics file to make a visual test pass.
- Declaring "looks fine now" from a single screenshot — every claim needs the
  measured before/after pair.

---

## 9. Per-round report format

Each agent reports, every round, in this shape (append to
`docs/VISUAL_FIXES_LOG.md` via WS-E):

```
WS-<letter> round <n> — <one-line state>
changed:   <file:line> — <what and why>
failing-check (pre-fix): <command> -> <raw output showing the failure>
fix:       <what changed>
passing-check (post-fix): <command> -> <raw output>
carried:   <anything not done, and why>
blockers:  <concrete, with the exact reproducing command>
```

Rules for the report:

- Quote raw output, not summaries.
- Never report a SKIP as a PASS.
- Anything not measured is `UNVERIFIED` — say so.
- If a fix requires another workstream's file, write a request line naming the
  file, the change, and the failing evidence; do not edit it.

---

## 10. The one-sentence brief

Make the arena and the car render correctly — light every surface, remove the
debug grid, fix the Fennec asset (normals, datum, mirror) — and harden the
visual gates so the exact failures a human saw can never pass again, all
without touching physics.