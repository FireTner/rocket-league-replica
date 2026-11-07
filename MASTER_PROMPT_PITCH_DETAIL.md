# MASTER PROMPT — Pitch detail: curb neon, ground marks, hex floor

Work order for **one worker**. Opened 2026-11-06.

The three gaps are one draw. They are not three parallel edits. A census
blocks every pixel change. After that, land one surface, shoot it, then the
next.

Read first: `AGENTS.md` and `CONTRACTS.md`. Then this file. Presentation
only. The local stadium pack is already loaded by `./build/game`. The
analytic shell (`--shell`, and any run with no pack) is not this task.

---

## 1. Goal

On DFH Stadium (Day), with the user's local pack loaded:

1. The curb lip carries a team-coloured neon strip. Orange on one end, blue
   on the other. The structural curb stays light metal.
2. The pitch shows the white painted marks the pack actually contains, and
   the neon that sits on the ground (center rings and any other trim-light
   whose box is on the pitch, not up on the wall).
3. The center platform and the hex floor tiles are metal hex. Grass stays
   grass, including the approach in front of the posts.

A playtest on 2026-11-06 showed the opposite of all three: grey curbs, a
green pitch with no white marks and no ground neon, and grass where the hex
metal belongs.

---

## 2. Shared rules

A violation fails the round.

1. **Physics stays the authority.** No edit under `src/sim`, `src/arena`,
   `src/physics`, no gate number, no tape. If a visual needs a fact the
   snapshot does not carry, stop and file an issue. Do not add a sim view
   for this task. These surfaces are pack meshes.
2. **No Psyonix asset in the tree.** The pack stays a user-local runtime
   load, gitignored, same as today. Do not commit a texture, a dump, or a
   new `material_overlays.json` as the way the look ships. A missing overlay
   file must still show the result. The pixels have to come from textures
   the pack already decodes, or from a flat team colour already measured.
3. **Do not build a material-graph importer.** A pack material is a name
   plus one texture index. The arena shader already has albedo, lightmap,
   normal, mask, and a flat emissive colour. Stay inside that shader, in
   the existing GL 4.5 compatibility context. No new default programmable
   path. No second shader file.
4. **Cite the number or mark it.** Every new constant names a measurement
   from this pack (mesh name, material name, triangle count, world box) or
   it is `UNVERIFIED`. The numbers in §4 were measured on earlier passes.
   Re-measure them against the loaded pack before you treat one as a clip
   plane. If a mesh named in §4 is absent, stop and write that down. Do not
   invent a stand-in mesh.
5. **One surface at a time.** Census first, with no draw change. Then curb,
   shot. Then ground marks and ground neon, shot. Then hex, shot. Do not
   land all three and call the last picture the proof of the first.
6. **Leave the bars that this task does not touch.** Shell / no-pack frame
   stays byte-identical. Quote the hash before and after. Physics gates are
   not required when no sim file changed; say that, and do not run them to
   look busy. `glerr=0` on every capture. Quote `GL_VENDOR`, `GL_RENDERER`,
   `GL_VERSION`.
7. **Machine A is the floor and may be unreachable.** A result from machine
   B alone cannot claim 60 FPS. If A is not measured, write `A NOT MEASURED`.
   Do not decimate a mesh to chase a number. Report the pack triangle count
   before and after. `TRI_CAP` (240000) is the fennec scene, not this pack.
   The pack is already well above that and stays above it. Do not "fix" the
   cap.
8. **Say what you could not verify.** A live Rocket League frame is the
   picture these surfaces are matching. If you cannot capture the game,
   match the placement census and the reference already in the playtest,
   and list the gap.

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

Re-confirm against the loaded pack. Do not treat a miss as permission to
guess. These are the reasons the current picture looks wrong, and the
reasons the previous fixes must stay.

### 4.1 Curb neon is flat metal

`Field_STD_Trim` and the frame sections of `Side_Trim`
(`Frame_01_V2_Mat`, `Frame_01_MIC`) are drawn as light metal
`(0.80, 0.79, 0.76)` with `ENV_CarbonFiber_N`. That is the structural
curb. Keep it.

`FrameTexture_PACK` is not bound. On the goal posts its v range is about
−11..5, and the sheet wraps as green and orange stripes. Binding it back
onto the posts, or onto a curb section with that same v range, is a fail.
Measure the curb section's own UV range before using any frame sheet.

The curb's lightmap stamp at `coord_scale` about 0.029 is one atlas texel
stretched over the whole piece. Left on, the curb is one flat red band.
Leaving that stamp off is correct. The neon is not that stamp. Do not turn
it back on to "get the colour."

`TrimLight_Glow` on meshes other than `Goal_STD_Glow` and `Goal_STD_Outer`
is already an unlit team colour. The note from the last pass says
`Field_STD_Glow` and the side-trim glow sit on the wall (z about 70..400)
and do not cross the pitch. The missing curb light is the strip on the
pitch-facing lip, not that wall band. Find it in the census (material,
triangle count, world box). If the lip is a `Frame_01_*` section, it is
being painted as the grey metal above. Split the lip from the structure.
Do not paint the whole curb orange.

`Goal_STD_Glow` and `Goal_STD_Outer` stay undrawn. They measure about
x ±2048 and about 1800 uu deep. Drawn unlit, they stick out of the floor
and the side walls. The replacement is a 4 uu unlit lip on the mouth
(posts and crossbar, mouth half-width 892.755, height 642.775). A 14 uu
edge read as a red border from inside the goal. Keep the 4 uu lip. Do not
draw the shells.

`TrimLight_None` is the off housing. It stays off. An earlier pass drew
`Field_Center_Trim` section 1 (`TrimLight_None_Mat`, 350 tris, rings about
r 272 and r 640–725, up to z 64) as near-white unlit, because a dark-metal
tint had caught the substring "trim". Those rings are ground neon (§4.2),
not the curb, and not a white mark.

Team colours already in the draw: Team1 `(1, 0.12, 0.04)`, Team2
`(0.15, 0.45, 1)`. The package vectors were measured as `(1, 0.10, 0.03)`
and `(0, 0.26, 1.06)`. Do not swap ends. The −Y goal is orange, the +Y
goal is blue, unless the census shows that placement says otherwise.

The rim meshes are one-sided so a ball-cam eye behind the curb still sees
the pitch: `Field_STD_Trim`, `Field_STD_Frame`, `Field_STD_Glow`,
`FieldFrame_Outer`, `Side_Trim`, `Sideline_A`, `Goal_STD_Quarterpipe`.
The pitch-facing side stays the front face. Do not turn that cull off.
The neon has to be on the pitch-facing side.

### 4.2 Ground neon and white marks are skipped or reskinned

Three placements are dropped before any triangle is emitted:
`Field_Center`, `Field_Center_Trim`, `Field_STD_Floor_Hex`. The reason
recorded at the skip: they bind `Hexagons_Pack_*` as albedo, and sampling
that sheet paints a purple / iridescent atlas over the pitch. The grass
under them is what you see. That skip also deletes the center rings
(§4.1) and the center platform (§4.3). Do not remove the skip and sample
`Hexagons_Pack_B` as a raw diffuse. That sheet was measured B-heavy
purple. The allowed use is the one the goal mouth already uses (§4.3).

White paint is a separate rule that loses to the field skin. A material
whose name contains `line`, `paint`, or `stripe` is unlit white, with a
polygon offset so it sits on the turf. Any section on a pitch mesh is
then forced to `FieldSkin_FutureGrid_D` when the mesh carries the field
UV channel, or when the material name contains `grass`, `groomed`, or
`field`. The pitch meshes named for that force are `Field_STD_Floor_Team1`,
`Field_STD_Floor_Team2`, `Field_STD_Floor_Thick`, `Field_CenterField_Team1`,
`Field_CenterField_Team2`, and `Field_Center`, plus any mesh whose diffuse
UV channel is 2 or higher. A line section that lives on one of those
meshes is drawn as grass. That is the missing white mark if the census
finds the marks there.

`FieldSkin_FutureGrid_D` itself is a dark olive (mean G about 130). Its
variation is the mow checker and faint markings. On upload it is lifted
red ×1.65, green ×1.40, blue ×1.15, alpha forced opaque, clamp-to-edge.
Those baked markings are not a separate white layer. If the census shows
the white pixels are already in that sheet, change only the texels that
are already near-white so the lift does not tint them. Do not retune the
grass. Do not add blade detail or the orange wear lanes. Those are out of
scope.

`DecalBlockingActor_TA`, `FXActor_TA`, and `BoostPill_WarmUp_FXActor` are
invisible helpers. Drawing them buries the field under opaque planes.
They are not the white marks. Leave them skipped. The same for `Sprite*`,
`FogMesh*`, `FieldSpotLights*`, `CloudPlane*`, `LightCone`, and `Fog`.

### 4.3 Hex metal is grass

`Goal_STD_Floor` is one slab. The −Y placement measures x ±2053 and
y −5120..−3106, so it covers the approach in front of the posts. The
whole slab is forced to the field skin. A second copy is mirrored across
the goal line and clipped to the post rectangle (half-width 892.755,
depth 880). That copy is the only hex: `Hexagons_N` as the normal,
`Hexagons_Pack_B` as a repeating albedo multiplied by the team colour.
That multiply is required. Raw `Hexagons_Pack_B` is the purple junk.

Widening that clip to the whole slab puts team hex across the approach.
That was a measured fail. The approach outside the posts stays grass.
If the census shows hex missing inside the goal, past the 880-deep clip,
widen the clip only to the measured hex region. Do not widen it to the
approach.

`Field_STD_Floor_Hex` and the hex-metal part of `Field_Center` are the
other ground hex, and they are inside the skip in §4.2. Draw those
triangles with the mouth's treatment (hex normal, pack albedo multiplied
by team colour, or by the neutral metal where the census shows no team).
Do not field-skin them. `Field_Center` was also on the field-skin list.
Drawing it again as the skin would still be grass. Split sections. A
grass section of that mesh stays grass. A hex section does not.

`Grass_Base_*` must not take the field unwrap. An earlier pass painted
the bowl with pitch swirls that way.

The recessed metal inlay at midfield is authored and stays. The grass
slab `Field_STD_Floor_Thick` (top z=10) has slots with no triangles
(none at x=0, covered again from about x=233). `Field_CenterField_Team1`
and `Team2` (|x|<233) and `Field_Mid_A` (|y|<269) `Frame_01` strips at
z=0 are a 10 uu recess, not a mapping bug. Do not "fill" that trench
with hex or with an extra grass card.

---

## 5. Work

### Pass 0 — Census

**Blocks every later pass.** No draw change in this pass.

Log every placement whose world box meets the pitch: z overlapping
about 0..80, and x,y inside the field footprint. One row per section:
mesh, material, triangle count, world box, bound texture name, and the
current fate (`skipped`, `field-skin`, `frame-metal`, `team-glow`,
`white-decal`, `hex-mouth`, `other`). Write the table into
`src/render/BUILD_NOTES.md` before changing a pixel.

The table is the spec for passes 1–3. A row this prompt predicted and
the pack does not contain is a stop, recorded, not a mesh you create.

**Done when.** The table is in the notes, taken from a real load of
`arena_assets/stadium_day`, and it names the curb lip, the white-mark
sections, the ground-neon sections, and the hex sections by mesh and
material. `glerr` is irrelevant here. No shot required.

### Pass 1 — Curb neon

**Goal.** The pitch-facing curb lip is a team-coloured unlit strip.
Orange and blue follow the placement's team. The structural curb stays
the light metal and the carbon normal.

**Do not.** Bind `FrameTexture_PACK` onto a section whose UV wraps.
Turn the 0.029 lightmap stamp back on. Draw `Goal_STD_Glow` or
`Goal_STD_Outer`. Paint the whole curb. Move the wall-band glow (z about
70..400) down onto the grass. Disable the one-sided rim cull.

**Done when.** The four cameras in §6 show a coloured lip on the curb
and a light-metal face beside it. The mouth lip is still about 4 uu,
not a red border. Inside-goal camera shows no shell sticking out of the
floor. Shot and a one-line pass/fail are in the notes.

### Pass 2 — Ground neon and white marks

**Goal.** Every white-mark section the census found draws unlit white,
offset above the turf, and is not replaced by the field skin. Every
ground-neon section the census found (center rings, and any trim-light
whose box is on the pitch) draws unlit in its team colour.
`TrimLight_None` stays dark.

**Do not.** Un-skip `DecalBlockingActor_TA` or the fog / sprite / light
shaft list. Retune the grass lift except for texels the census proved
are already near-white in `FieldSkin_FutureGrid_D`. Add wear lanes or
blade detail. Sample `Hexagons_Pack_B` as an unmultiplied diffuse to
"get the rings."

**Done when.** A chase view from the center spawn and the midfield
camera show the white marks the census listed, and the ground neon the
census listed. A mark the census did not find is not required. Shot and
the list of marks actually drawn are in the notes.

### Pass 3 — Hex floor

**Goal.** The center platform and `Field_STD_Floor_Hex` read as metal
hex, using the mouth's treatment: `Hexagons_N` as the normal,
`Hexagons_Pack_B` multiplied by the team colour (or the neutral metal
where the section has no team). Grass sections stay grass. The approach
outside the posts stays the field skin. The hex inside the mouth stays.

**Do not.** Remove the three-mesh skip and let the raw pack albedo
through. Widen the mouth clip to the whole `Goal_STD_Floor` slab.
Field-skin `Field_Center`. Put the field unwrap on `Grass_Base_*`.
Fill the midfield trench.

**Done when.** The high-corner camera and the inside-goal camera show
hex on the platform and inside the mouth, and grass on the approach.
No purple atlas. No pitch swirls on the bowl. Shot and a pass/fail are
in the notes.

---

## 6. Cameras

Same seed, same early tick, pack loaded, no freecam drift. Eye → target:

| View | Eye | Target |
|---|---|---|
| Side of the −Y goal | `1400,-3600,520` | `0,-5300,250` |
| Inside that goal, looking out | `400,-5600,220` | `0,-4200,160` |
| Midfield toward that goal | `0,-900,480` | `0,-5120,180` |
| High corner | `4200,-3800,2400` | `-200,800,200` |
| Chase, car on the center spawn | the game's own chase cam | the car's target |

The chase view is the one that has to show the curb lip, the white
marks, and the center hex together. The other four are the regression
set from the 2026-11-06 arena pass. A surface that passes only the
chase view and fails a regression camera is not done.

Shots go under `/tmp/arena_vis/`. Record path and md5 in the notes.
Machine line with every shot: `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`.
`glerr=0`.

---

## 7. Acceptance

All four must be true:

1. Pass 0's table is in `src/render/BUILD_NOTES.md`, and passes 1–3 each
   cite the rows they drew.
2. The three playtest gaps are gone on the cameras in §6, and the fails
   named in §4 are still gone: no flat red curb, no wrapped frame
   stripes, no glow shell, no purple hex, no hex on the approach, no
   pitch swirls on the bowl, no decal-blocker planes, `TrimLight_None`
   dark, mouth lip still about 4 uu.
3. Shell / no-pack capture hash is unchanged. Quote both hashes. Pack
   triangle count quoted before and after. `glerr=0`. `drops=0` on the
   capture run. Machine A quoted or explicitly `NOT MEASURED`.
4. `git diff` under `src/sim`, `src/arena`, `src/physics`, `tools/tas`,
   and `tools/verify/measured.py` is empty. No extracted game byte is
   tracked. `gates_visual.json` is not overwritten by a partial
   `--only` run. If a pack-frame gate hash must move, say so in the
   notes and replace the record only with a full suite run.

---

## 8. Board

| Pass | Goal | Status | Evidence |
|---|---|---|---|
| 0 | Census of pitch-height sections | DONE | `src/render/BUILD_NOTES.md` §Pitch detail |
| 1 | Curb team-neon, structure stays metal | DONE | side + corner lip; inside mouth unchanged |
| 2 | White marks and ground neon | DONE | centre, side, and goal-box lines lifted; `TrimLight_None` still dark |
| 3 | Hex floor, approach stays grass | DONE | centre disc + `Field_STD_Floor_Hex`; approach stays skin |

```
Pass: 0
Goal (one sentence): Census of pitch-height sections, no draw change.
Issue closed: the lip, the white marks, the centre rings, and the hex sheets are named by mesh and material.
Still wrong: nothing drawn yet.
Evidence (command, GL strings, what the shot shows): /tmp/pitch_census on arena_assets/stadium_day. No shot.
Sites changed: none.
Blocker: none.
Timeline: on time

Pass: 1
Goal (one sentence): Pitch-facing curb lip is team neon; the wall stays light metal.
Issue closed: grey curb lip.
Still wrong: white marks still under the grass; hex still skipped.
Evidence (command, GL strings, what the shot shows): seed 7 tick 120, GL_VENDOR=NVIDIA Corporation RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2 VERSION=4.6.0 NVIDIA 615.78.08, glerr=0 drops=0. Corner pixels that changed are team blue.
Sites changed: src/render/Renderer.cpp
Blocker: none.
Timeline: on time

Pass: 2
Goal (one sentence): Painted lines sit on the turf; TrimLight_None stays dark.
Issue closed: missing white marks.
Still wrong: hex still grass. The only on-pitch trim-light is TrimLight_None, so the centre rings stay off.
Evidence (command, GL strings, what the shot shows): midfield and chase show the centre circle, the centre line, and the goal-box marks.
Sites changed: src/render/Renderer.cpp
Blocker: none.
Timeline: on time

Pass: 3
Goal (one sentence): Centre platform and Field_STD_Floor_Hex read as metal hex; the approach stays grass.
Issue closed: grass where the hex belongs.
Still wrong: no live Rocket League frame to match. Machine A NOT MEASURED. Centre rings stay dark because their material is TrimLight_None.
Evidence (command, GL strings, what the shot shows): tris 556112 → 556867. Shell hash 632a0906a760a5eeef36d99610e17a46 before and after. Shots under /tmp/arena_vis/. glerr=0 drops=0.
Sites changed: src/render/Renderer.cpp, src/render/BUILD_NOTES.md
Blocker: none.
Timeline: done
```

---

## 9. Out of scope

Grass blade detail and the orange wear lanes. Crowd, scoreboard, sky,
city, seats. Goal glass, posts, and the carbon-fiber frame metal except
where a census row proves that section is the curb lip. Ball, boost,
sparks, trails, camera. Collision. Making the pack shader the default
for the shell path. A frame-identical copy of the retail material graph.
