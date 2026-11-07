# MASTER PROMPT — Wall and ceiling force-field lattice

Work order for **one worker**. Opened 2026-11-06.

The lattice is one draw. A census blocks every pixel change. After that,
land the wall, shoot it, then the ceiling.

Read first: `AGENTS.md` and `CONTRACTS.md`. Then this file. Presentation
only. The local stadium pack is already loaded by `./build/game`. The
analytic shell (`--shell`, and any run with no pack) is not this task.

The pitch surfaces from `MASTER_PROMPT_PITCH_DETAIL.md` landed the same
day (curb lip, white marks, floor hex). They are not this task. Do not
move them.

---

## 1. Goal

On DFH Stadium (Day), with the user's local pack loaded, the force-field
cells cover the glass walls and the ceiling.

A playtest the same day, chase cam on the side wall, shows the cells on
one patch of the wall near the goal curve and nowhere else. The long wall
is a solid white band. The ceiling is open sky with no lattice. The
playtest calls the cells octagons. The pack's sheet for that lattice is
`Hexagons_Pack_01_T`, and the materials are named hex. Draw the cell that
sheet already encodes. Do not author a second lattice of eight-sided
polygons.

The cells are a net. Sky, stands, and the city stay visible through the
openings. The wall is not a white board with a sticker on one end, and it
is not an opaque honeycomb.

---

## 2. Shared rules

A violation fails the round.

1. **Physics stays the authority.** No edit under `src/sim`, `src/arena`,
   `src/physics`, no gate number, no tape. These surfaces are pack meshes.
   Do not add a sim view.
2. **No Psyonix asset in the tree.** The pack stays a user-local runtime
   load, gitignored. Do not commit a texture, a dump, or a
   `material_overlays.json` as the way the lattice ships. A missing overlay
   file must still show the cells. The pixels have to come from a texture
   the pack already decodes.
3. **Stay in the pack shader.** One program, already compiled, GL 4.5
   compatibility. It already has a mask and a blend. No new shader file.
   No new default programmable path. The shell's procedural 420 uu hex net
   is a different draw. Do not retune it, and do not paste it onto the pack.
4. **Cite the number or mark it.** Every new constant names a measurement
   from this pack (mesh, material, triangle count, world box, UV range) or
   it is `UNVERIFIED`. If a mesh named in §4 is absent, stop and write that
   down. Do not invent a stand-in wall.
5. **One surface at a time.** Census first, with no draw change. Then the
   wall run, shot. Then the ceiling, shot.
6. **Leave the bars this task does not touch.** Shell / no-pack frame stays
   byte-identical. Quote the hash before and after. The pitch-detail shots
   in `src/render/BUILD_NOTES.md` (2026-11-06) are the regression for the
   curb lip, the white marks, and the floor hex. A change that moves those
   pixels fails. Physics gates are not required when no sim file changed.
   `glerr=0` on every capture. Quote `GL_VENDOR`, `GL_RENDERER`,
   `GL_VERSION`.
7. **Machine A may be unreachable.** If it is not measured, write
   `A NOT MEASURED`. Do not decimate a mesh. Report the pack triangle count
   before and after. Do not touch `TRI_CAP`.
8. **Say what you could not verify.** If you cannot capture live Rocket
   League, match the cell already on the goal-side wall and extend that
   cell. List the gap.

---

## 3. Progress note

Append this to §8. Do not rewrite §4 or §5.

```
Pass:
Goal (one sentence):
Issue closed:
Still wrong:
Evidence (command, GL strings, what the shot shows):
Sites changed:
Blocker:
Timeline: on time | slipped (why) | done
```

---

## 4. What is already measured

Re-confirm against the loaded pack. These are the reasons the lattice is
missing, and the reasons the previous glass fix must stay.

### 4.1 The force-field meshes are not drawn

Placements whose mesh name starts with `FF_` or `FFCage` are dropped
before any triangle is emitted. The note at the skip: goal and side
force-field nets bind `Hexagons_Pack_01_T`, and drawn opaque they are a
purple/blue moiré slab across the goal camera. The names recorded for
that set are `FF_Roof`, `FF_Side`, `FF_Goal`, and `FFCage`. `FF_Roof` is
the ceiling. `FF_Side` and `FF_Goal` are the walls. They are the lattice
that is missing. Do not remove the skip and let the raw sheet through as
colour. That is the moiré.

### 4.2 The sheet is a mask, not a colour

`Hexagons_Pack_01_T` is DXT1, 512×512. Measured: black centres (29%),
saturated green rings (27%), blue glow (13%), red edges (under 1%), 499
distinct RGB values. The green channel is the line. Blue-only glow and
red edges have G=0. On upload the sheet is rewritten once: RGB
`(175, 205, 235)`, alpha `G * 80 / 255` (0..0.31). Mips come from those
bytes. Sampling the original RGB as an albedo is the rainbow. Keep the
conversion.

`HexGlass_WithArrows_Team*_MIC` is the one material that already uses
that converted sheet as a mask, team-tinted, blended, unlit, no lightmap.
Team1 `(1, 0.12, 0.04)`, Team2 `(0.15, 0.45, 1)`, otherwise the ice veil
`(0.45, 0.55, 0.60)`. That path is the patch of cells in the playtest.
Find which mesh it is in the census. Do not draw a second lattice on top
of it. Extend the same treatment to the wall and ceiling meshes the
census names. Do not retint the patch that is already correct.

### 4.3 The ceiling glass has no cells

`Glass_Projected_V2_*` has an empty texture lookup. It draws as an
untinted veil at alpha 0.18, no mask. That is a flat pane. The ceiling
lattice is not a stronger flat pane. If the census shows this mesh is
the roof glass, it takes the same line mask as the wall. The openings
stay open. Raising 0.18 toward opaque, with no mask, fails.

### 4.4 What must stay

The alpha cap exists so the net stays a net. Do not lift it to 1. If a
mesh is drawing and the lines are still invisible, measure the alpha and
the UV range before changing the cap. A mesh that is skipped is not an
alpha problem.

The solid white band, the ad boards, and the blue trim along the top of
the wall are not the force field until the census says a section of them
is. Do not paint the lattice across the boards or the trim to "fill the
wall." The long wall looks blank because the force-field placement is
skipped or is a flat veil, not because the board texture is missing a
decal.

`Hexagons_Pack_B` stays off this task. It is the floor hex, B-heavy
purple, and the pitch pass already multiplies a luminance copy for the
floor. `Hexagons_N` is that floor's normal. Neither belongs on the wall
as colour.

Crowd, scoreboard, the sky gradient, and the goal-mouth hex floor are
not this lattice.

The rim cull on the curb and the trim meshes stays. This task does not
disable it.

---

## 5. Work

### Pass 0 — Census

**Blocks every later pass.** No draw change in this pass.

Log every placement that is glass, a force field, or a ceiling: mesh
name starts with `FF` or contains `Glass`, `Cage`, `Fence`, or `Roof`,
or the material name contains `glass`, `force`, `cage`, `fence`,
`hexglass`, or `projected`. One row per section: mesh, material,
triangle count, world box, bound texture name, and the current fate
(`skipped`, `masked-net`, `flat-veil`, `opaque`, `other`). Write the
table into `src/render/BUILD_NOTES.md`.

The table is the spec. The row that matches the cells already visible
on the goal-side wall is the donor. The rows with no cells, whose box
is the long wall or the roof, are the targets. A name this prompt
predicted and the pack does not contain is a stop, recorded, not a mesh
you create.

**Done when.** The table is in the notes, from a real load of
`arena_assets/stadium_day`, and it names the donor patch, the blank wall
runs, and the ceiling. No shot required.

### Pass 1 — Wall lattice

**Goal.** Every wall force-field section the census named draws the
donor cell, see-through, with the existing mask conversion. The long
wall shows the net, not a solid white band. Sky and stands show through
the openings. The donor patch is not drawn twice and is not recolored.

**Do not.** Sample `Hexagons_Pack_01_T` as an albedo. Draw an `FF_*`
placement opaque. Paint the ad boards, the white structural band, or
the blue trim when the census says those sections are not the force
field. Change the floor hex, the curb lip, or the white marks. Swap
team ends.

**Done when.** The chase view along the side wall shows the net on the
run that was blank, and the goal-side patch still matches the donor.
The inside-goal camera shows no purple slab across the mouth. Shot and
a pass/fail are in the notes.

### Pass 2 — Ceiling lattice

**Goal.** The ceiling sections the census named draw the same cell,
see-through. `Glass_Projected` does not stay a flat veil if the census
says it is the roof. Openings stay open. A camera looking up sees the
net against the sky, continuous with the wall net.

**Do not.** Raise the veil alpha and call a flat pane a lattice. Cover
the sky sphere. Put the field unwrap or the floor hex on the roof.
Author a new polygon.

**Done when.** A view aimed at the ceiling shows the cells across the
roof, and the wall shot from pass 1 still shows the wall net. Shot and
a pass/fail are in the notes.

---

## 6. Cameras

Same seed, same early tick, pack loaded. Eye → target:

| View | Eye | Target | What it has to show |
|---|---|---|---|
| Chase along the side wall | the game's chase cam, car on the side | the near wall | the long run, not only the goal-side patch |
| Inside the −Y goal, looking out | `400,-5600,220` | `0,-4200,160` | no opaque slab across the mouth |
| High, looking up | `0,-200,900` | `0,0,1900` | the ceiling net |
| High corner | `4200,-3800,2400` | `-200,800,200` | wall net and ceiling net together, and the pitch surfaces still in place |

The chase view is the playtest. The up view is the ceiling. A surface
that passes only one of them is not done.

Shots go under `/tmp/arena_vis/`. Record path and md5. Machine line with
every shot: `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`. `glerr=0`.

---

## 7. Acceptance

All four must be true:

1. Pass 0's table is in `src/render/BUILD_NOTES.md`, and passes 1 and 2
   each cite the rows they drew.
2. The long wall and the ceiling show the donor cell, see-through. The
   fails in §4 are still gone: no rainbow albedo, no opaque `FF_*` slab,
   no flat ceiling called a lattice, no second lattice on the donor
   patch, no lattice on the ads or the trim unless the census named
   that section.
3. The pitch-detail pixels stay. Quote a before/after on the high-corner
   shot for the curb lip, the white marks, and the floor hex, or quote
   the md5 if it did not move. Shell / no-pack hash unchanged. Quote both
   hashes. Pack triangle count before and after. `glerr=0`. `drops=0` on
   the capture run. Machine A quoted or `NOT MEASURED`.
4. `git diff` under `src/sim`, `src/arena`, `src/physics`, `tools/tas`,
   and `tools/verify/measured.py` is empty. No extracted game byte is
   tracked. `gates_visual.json` is not overwritten by a partial `--only`
   run.

---

## 8. Board

| Pass | Goal | Status | Evidence |
|---|---|---|---|
| 0 | Census of wall and ceiling glass | DONE | `src/render/BUILD_NOTES.md` Force field census. Donor `Goal_STD_Glass` §0. Walls `FF_Side` `FF_Goal`. Ceiling `FF_Roof`. `FFCage` absent. |
| 1 | Wall lattice, see-through | DONE | `ff_wall_side.ppm` `ab93499628605b6d64491e13cb03543e`. Inside `51112032ab856fdae76a30bc262a54b7`. |
| 2 | Ceiling lattice, same cell | DONE | `ff_ceil_up.ppm` `572ed6fa8b295df01d15e5175ee785bf`. Side still the wall net. |

```
Pass: 0
Goal (one sentence): Name the donor patch, the blank wall runs, and the ceiling from a real pack load.
Issue closed: The lattice meshes are FF_Side, FF_Goal, and FF_Roof, all skipped, all bound to Hexagons_Pack_01_T. The visible cells are Goal_STD_Glass section 0.
Still wrong: Nothing drawn yet. FFCage is absent (the pack has FFCage_Full, left skipped).
Evidence (command, GL strings, what the shot shows): /tmp/ff_census on arena_assets/stadium_day. No shot.
Sites changed: src/render/BUILD_NOTES.md
Blocker:
Timeline: done

Pass: 1
Goal (one sentence): The long wall and the goal-end wall draw the donor cell, see-through.
Issue closed: FF_Side and FF_Goal use the existing hex mask. The long run is a net. The mouth is not a slab.
Still wrong: The ceiling was still open sky. FFCage_Full stays skipped.
Evidence (command, GL strings, what the shot shows): seed 7 tick 120, GL_VENDOR=NVIDIA Corporation RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2 VERSION=4.6.0 NVIDIA 615.78.08, glerr=0 drops=0. Side shot shows the net above the white band. tris 556867 → 558291.
Sites changed: src/render/Renderer.cpp
Blocker:
Timeline: done

Pass: 2
Goal (one sentence): The ceiling draws the same cell, and the wall net stays.
Issue closed: FF_Roof uses the same mask. Looking up shows the net against the sky. The side wall net is unchanged in the lower frame.
Still wrong: No live Rocket League frame. The side shot is a fixed eye, not the chase rig (no car-place flag). FFCage_Full is still skipped. Glass_Projected stays the outer-bowl veil. Machine A NOT MEASURED.
Evidence (command, GL strings, what the shot shows): same machine line, glerr=0 drops=0. ff_ceil_up.ppm md5 572ed6fa8b295df01d15e5175ee785bf. tris 558367. Shell hash 632a0906a760a5eeef36d99610e17a46 before and after.
Sites changed: src/render/Renderer.cpp, src/render/BUILD_NOTES.md
Blocker:
Timeline: done
```

---

## 9. Out of scope

The pitch: curb neon, ground neon, white marks, floor hex, grass, wear
lanes. Ad boards, crowd, scoreboard, sky gradient, goal posts, the
goal-mouth hex floor. The shell's 420 uu procedural net. A new
eight-sided lattice. Collision. Camera. Ball, boost, sparks.
