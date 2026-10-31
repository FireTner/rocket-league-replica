# MASTER PROMPT — Ball, boost, contact FX, and DFH Stadium

Work order for **one coordinator** and **eight workers**. Opened 2026-10-29.
Workstream H (exact stadium) was added the same day.

The coordinator does not implement, and does not hand out file paths. It
tracks goals, issues, timelines, and progress. Each worker gets the shared
rules plus **one** workstream, finds the code, and reports back in the note
format in §3. The coordinator writes those reports into §8.

Read first: `AGENTS.md` and `CONTRACTS.md`. Then only your own section.

---

## 1. Coordinator

You oversee. You do not write product code. You do not tell a worker which
file to edit. You assign a **goal**, you record the **issue**, you keep the
**timeline**, and you update **progress** from worker reports.

On each check-in:

1. Read §8. Dispatch every workstream whose status is `READY` and whose
   overlap in §6 is still clear.
2. Paste that worker **only** §2, §3, §6, and their workstream in §5. Do not
   add paths, line numbers, or a suggested edit.
3. When a report arrives, update that row's Goal / Issue / Timeline /
   Progress. Append new blockers to §7. Leave the workstream text alone.
4. If two reports are changing the same behavior, stop the later worker,
   mark them `BLOCKED`, and name the order in §7. Do not merge the code
   yourself.
5. Accept a row only when its **Done when** checks were actually run and the
   worker quoted the result. A description of the code is not acceptance.

You may reject a result that drifts into another workstream's goal. Send it
back with the goal it missed. Do not prescribe the fix.

---

## 2. Shared rules

A violation fails the round.

1. **Physics stays the authority.** Presentation reads a snapshot. If a visual
   needs a fact the snapshot does not carry, add a read-only view and a
   failing test first. Never change a physics result, a gate number, or a
   tape.
2. **No Psyonix assets in the tree.** Recreate looks procedurally. A
   user-local runtime load of their own install is allowed only if it stays
   gitignored. Gates always run the procedural path.
3. **Own your goal.** Another workstream's outcome is off limits. If you
   cannot finish without changing it, stop and file an issue (§7). The
   coordinator sequences you.
4. **Find the code yourself.** Report the sites you changed in the progress
   note. Do not wait to be pointed at a file.
5. **Cite the number or mark it.** Every new constant names a measurement or
   a published source, or it is `UNVERIFIED` with the reason. BakkesMod
   plugin constants are not the game. Psyonix said those plugins inspired
   Free Play ball control; they are not the implementation.
6. **Effects that a capture can see stay deterministic in sim time.** No
   wall-clock randomness.
7. **Leave the existing bars green:** physics gates, `make tests`, and the
   visual gates that already pass. Quote the commands you ran. The
   indicator-size gate in particular must stay green (workstream E).
8. **Say what you could not verify.** A live Rocket League capture is the
   best source. If this machine cannot run the game, ship the closest
   cited behavior and list the gap as an open issue.

---

## 3. Progress note (workers)

Post this. The coordinator copies it into §8. Do not edit §8 yourself.

```
Workstream:
Goal (one sentence):
Issue closed:
Still wrong:
Evidence (what you ran, what you saw):
Sites changed (you discovered these; the coordinator did not assign them):
Blocker for another workstream:
Timeline: on time | slipped (why) | done
```

---

## 4. What is already known

These are the issues. Re-confirm them in play. Do not treat the numbers
below as the target values; the targets are in each workstream.

**Numeric ball commands (keys 1–5).** Five drills exist. Observed behavior:

| Key | What it does now | What the owner wants |
|---|---|---|
| 1 | Ball appears in front of the car and **stops** | In front, **same linear momentum as the car** |
| 2 | Ball appears above the car and copies the car's velocity | Rocket League **Start Dribble** (on the hood). Current placement is not that |
| 3 | Ball is **teleported** far in front, then sent back toward the car | Ball **stays where it is** and is sent roughly along **the direction the car is travelling** |
| 4 | Ball pops upward from the car | Official **Launch Ball** (see B) |
| 5 | Ball is moved toward one end and given a velocity | Official **Defend Shot** (see B) |

**Boost and supersonic.** The plume is a stack of cones plus smoke balls.
The supersonic mark is a chain of boxes and orange spheres. While the car
is spinning in the air, circles are drawn around it. The owner wants those
circles gone, a trail that reads as a speed streak, and **black sparkles**
as the spark stand-in.

**Ball surface.** The ball is a repeating hex lattice with gray seams. It
does not read as the standard ball.

**Ball spin.** The ball's physics already has spin. The drawn ball ignores
it, so the panels stay locked to the world.

**Ground indicator.** A ring and a disc sit under the ball. They fight the
turf and sometimes disappear under it. Their size behavior is already gated;
keep that.

**Ball trail.** A fast ball in Rocket League leaves a short coloured streak.
This game does not draw one.

**Contact sparks.** Grinding the ground, and an awkward touch on the ball,
should throw sparks at the contact. What exists today is tied to a
supersonic slide, and nothing sparks off the ball.

**DFH Stadium (Day).** The opt-in local arena already has the real meshes
and placements. Most surfaces are still flat colour. That is a closed
result, not a starting point: the cooked material graph resolves a diffuse
for **248 of 640** used sections (38.75 %). The other 392 have no texture
expression, and that is true of every copy of those materials in the
install. Of those, 331 are untextured-by-design, 34 are the runtime
`CustomColors` crowd atlas, 16 are parameter masks, 9 point at another
material package, and 2 are empty slots. A name-stem guess was measured
and rejected (of 38 untextured materials, 5 had one candidate, 4 had
several, 29 had none). Do not walk that graph again and do not guess.

The pixels and the shade are still in the owner's install. The package
holds 18 lightmaps (`DirectionalMax` and `NormalizedAverageColor`). 79 of
81 sampled meshes name which UV channel is the lightmap channel. The pack
we load stores UV channel 0 only. 65 of 1176 mesh components carry ints
that match those lightmap textures, in pairs; the floor and the stadium
base are among them. That pair is a lead, not a parsed structure. Each
cooked material ends in a short native tail (84–320 bytes). None of those
tails start with a texture-reference list. Several start with a fixed
header and 16 bytes that look like an id. Beside the stadium package are
`RefShaderCache-PC-D3D-SM5.upk`, `Engine_MI_Shaders.upk`,
`GlobalShaderCache-PC-D3D-SM5.bin`, and `GuidCache.upk`. That cache is the
binding the game draws with. UModel (`-game=rocketleague`, and do not pass
`-nolightmap`) is the short way to read it; it was not installed here, and
an older note says UModel has failed on licensee 34. Measure which is true
before building anything.

11 textures have an empty first mip, including the poplar branches and the
stairs; their pixels live in a sibling file. The current stadium picture
uses stand-in tints, a forced field-skin unwrap (`u = x/8192 + 0.5`,
`v = 1 - |y|/8192`, measured off the floor's second UV), and a sky dome
pulled in to radius 36000 because the real dome sits past the camera's far
plane. Those are look hacks. Remove one only after the real binding covers
that surface.

The performance floor is machine A, the Radeon R4. Machine B is the 4060
laptop this session used. A 60 FPS claim that was not measured on A is not
a claim.

---

## 5. Workstreams

### A — Freeplay ball commands

**Goal.** Keys 1–5 match Rocket League's five Free Play ball controls, with
the owner's correction on the pass.

**Issue.** Key 1 kills momentum. Key 2 copies velocity but does not place
the ball the way Start Dribble does. Key 3 teleports the ball. Keys 4 and 5
are rough stand-ins for the other two official commands.

**Published Rocket League behavior** (Psyonix, Free Play ball control, patch
v2.06 and the "Using the New Free Play Controls" post):

- **Take Possession** — places the ball in front of the car.
- **Start Dribble** — places the ball on the hood.
- **Pass Ball** — "sends the ball towards your car." Psyonix also says the
  pass is sometimes just left or right of the car, depending on angle and
  position. Defend Shot and Pass Ball misbehave in some extra modes; standard
  soccar is the target.
- **Launch Ball** — behaves like the Hoops tip-off.
- **Defend Shot** — shoots the ball toward the closest goal.
- The controls can be combined (launch, then defend, to mimic a high shot).

Psyonix credited BakkesMod as the inspiration. Do not copy plugin offsets
(`ballontop`'s `(velX, velY, 170)` clamp, redirect-plugin speeds, and similar)
as if they were the game. Use them only as a hint, and label anything you
keep `UNVERIFIED` until a live capture or a decompile of the **game**
function replaces it.

A 2025 player report says Take Possession and Start Dribble should not keep
leftover ball spin, because leftover spin makes the next touch random. A
reply on that thread says current Rocket League already shows no leftover
spin and the ball rolls off the hood along the car's forward axis because of
the hitbox, not because of leftover spin. Measure. Do not pick the thread
over a capture.

**Owner decision (this beats the Pass Ball sentence).**

- **Key 1 = Take Possession.** Ball in front of the car, low enough to play,
  **linear velocity equal to the car's at the moment of the command**.
- **Key 2 = Start Dribble.** Ball on the hood, travelling with the car.
  "Above the origin" is the bug. Hood position, height, and how much of the
  car's vertical velocity is kept are research results. Zero leftover spin
  unless a capture shows Rocket League keeps it.
- **Key 3 = the owner's pass.** The ball **does not move to a new spawn**.
  From its current position it is given a velocity roughly along the car's
  direction of travel (horizontal velocity if the car is moving, car forward
  if it is stopped). Record Psyonix's "towards your car" sentence in your
  note as a known conflict. Do not implement the teleport. If a live capture
  shows the real Pass Ball still relocates the ball, stop and file an issue;
  do not silently switch.
- **Key 4 = Launch Ball.** After 1–3. Match the Hoops-tip-off behavior in
  standard free play. Position and speed are `UNVERIFIED` until you cite a
  measurement.
- **Key 5 = Defend Shot.** After 1–3. The ball is sent toward the **closest
  goal**, from a placement that reads as a shot to defend. Current behavior
  of shoving the ball toward the end the car happens to face is not that.

Keys stay on 1–5. Do not rebind them.

**Done when.**

- At speed, key 1: the ball is in front and keeps moving with the car. It
  does not drop dead.
- At speed, key 2: the ball rests on the hood and moves with the car. Quote
  the offset and the velocity rule, and the source.
- Key 3: the ball's position is unchanged on the command tick; its new
  velocity points along travel. Quote positions from a short scripted run.
- Keys 4 and 5 match the official sentences above as far as you could
  source, with gaps listed.
- Physics gates you can run are still green. Quote them.

**Out of scope.** How the ball looks, trails, sparks, the ground marker.

**Timeline.** Ready now. Keys 1–3 first. Keys 4–5 in the same session after
1–3 pass. One worker.

---

### B — Boost, supersonic trail, black sparkles

**Goal.** Boost and the supersonic state look like Rocket League, using
**black sparkles** as the spark stand-in, and the circles around the car
are gone.

**Issue.** The plume reads as stacked cones and smoke balls. The supersonic
mark reads as a chain of boxes plus orange spheres. Airborne rotation draws
circles around the car. The owner called that whole set horrid.

**Target.**

- Boost plume: a short bright exhaust that follows the car in full 3D
  (pitched and rolled), only while boost is actually firing. Length may
  follow remaining boost. It is not a smoke cloud.
- Supersonic streak: a speed trail behind the car while the car is in the
  supersonic state. That state is already a measured physics fact: on at
  2200 uu/s, held down to 2100, off below 2100. The trail follows **that
  state**, not a second threshold.
- Spark stand-in: **black sparkles** shed while supersonic, until a measured
  reference replaces them. Label the look `TUNED — owner stand-in`.
- The circles drawn around the car during an airborne spin are removed.
  A flip should still be readable from the car's motion. Do not replace the
  circles with a new ring.

**Done when.**

- Holding boost in the air shows a plume on the exhaust and no smoke-ball
  trail as the main read.
- Crossing into supersonic shows the streak and black sparkles. Dropping
  out of the supersonic state removes both.
- A flip in the air no longer draws circles or axes around the car.
- A short capture or screenshot set shows all three. Physics gates you can
  run stay green.

**Out of scope.** Sparks from grinding the ground or from hitting the ball
(workstream G). The ball's own trail (workstream F). Goal explosion rings.

**Timeline.** Ready now. Remove the circles first so G is not guessing which
rings are yours. One worker.

---

### C — Ball texture

**Goal.** The ball surface reads as Rocket League's standard ball.

**Issue.** The surface is a flat repeating hex lattice with gray seams.

**Target.** The standard ball is a truncated icosahedron: white hexagons and
black pentagons, with a gloss that reads as a ball under the existing
lights. Build it procedurally. Do not commit a ripped texture. Side-of-field
tint and the small lamps on the ball are a follow-up issue, not this goal,
unless they fall out of the same surface work without delaying the panels.

**Wait for D.** D owns orientation. You own the pattern. Start research now.
Land the pattern only after D has reported the orientation change, and do
not remove that orientation when you land.

**Done when.**

- A still of the ball at rest shows pentagons and hexagons in the standard
  layout, not a flat hex grid.
- After D, a spinning ball shows those panels rotating (you did not freeze
  the mesh).
- The procedural path is what gates run. Quote the visual checks you ran.

**Out of scope.** Spin math, the ground marker, trails.

**Timeline.** Research immediately. Implementation is `BLOCKED` on D's
orientation landing. One worker.

---

### D — Ball spin

**Goal.** The drawn ball rotates with the ball's physics spin, the way it
does in Rocket League.

**Issue.** Physics already spins the ball. The mesh does not. Panels stay
locked to the world, including after a grazing hit that should put the ball
into a visible roll.

**Target.**

- Orientation comes from the ball's angular velocity (axis and rate),
  integrated in sim time. A ball with zero spin does not rotate. A ball
  rolling on the turf rolls the way that spin implies.
- Do not invent spin the physics does not have. Do not change bounce spin.
- If the render snapshot has no ball spin, add a read-only view and a
  failing test first (shared rule 1).
- Leave the surface pattern alone so C can replace it. A solid colour that
  makes the rotation obvious is fine as a temporary check, and it must be
  gone or clearly marked temporary before you call the row done if it would
  block C. Prefer proving spin with the current pattern, ugly as it is.

**Done when.**

- A scripted roll: panels (or a marked point on the ball) change orientation
  in the direction of the spin, and a zero-spin ball stays fixed across
  frames. Quote the check.
- Physics gates you can run stay green.

**Out of scope.** The texture layout, trails, keybinds.

**Timeline.** Ready now. Land before C. One worker.

---

### E — Ball ground indicator

**Goal.** The marker under the ball stays visible on top of the turf.

**Issue.** The ring and disc fight the ground texture and sometimes hide
under it, especially with the ball on the ground.

**Target.** At every camera angle used in play, with the ball on the turf,
both circles stay visible and stable. No flicker as the camera moves. The
size, growth with height, and fade that the existing indicator-size gate
measures stay as they are. Do not repaint the turf to hide the fight.

**Done when.**

- Ball on the ground, camera orbit: the marker does not disappear under the
  turf and does not shimmer. Say what you did to check (capture sequence or
  play).
- The indicator-size gate still passes. Quote it.

**Out of scope.** Ball trail, turf art, indicator colours except where a
change is required to stop the depth fight. If you change colour, mark it
`TUNED` and say why.

**Timeline.** Ready now. One worker. F may run beside you; if F's trail
drawing changes the marker, file an issue.

---

### F — Ball trail

**Goal.** A fast ball leaves a short coloured trail, matching when Rocket
League shows one, in the colour Rocket League uses, with a cheap draw.

**Issue.** This game draws no ball trail. The only streak in the scene is
the car's supersonic mark (workstream B).

**What is known.**

- Colour is the **team that last touched the ball** (player reports from
  2017 and 2023, and broadcast commentary). It is not "orange on one half
  of the field." Field-side lamps on the ball are a different effect.
- A soft touch does not show the trail. The speed floor is **not** the car's
  2200 uu/s supersonic line; a comment claiming that is contradicted by the
  "soft hit, no trail" reports. The floor is `UNVERIFIED` until you measure
  it or cite a game function. Do not invent a number and call it exact.
- The look is a short streak along the recent path, hugging the ball, fading
  quickly. It is not smoke and not a long ribbon.

**Free play mapping.** Before any touch, no trail. After the player's car
touches it, that car's team colour. After the other car touches it, the
other team colour. If last-touch is not in the snapshot, add a read-only
view and a failing test first.

**Efficiency.** A short history of positions, drawn as one faded streak.
Stay inside the existing triangle budget. Quote the ball-trail cost you
added (triangles or draws) on a fast shot.

**Done when.**

- A hard hit shows a short trail in the touching car's team colour.
- A soft touch shows none. Quote the speed you used and why.
- Colour switches when the other car is the last touch.
- The streak dies quickly after the ball slows under the floor you chose.
- Visual and physics gates you can run stay green.

**Out of scope.** The car's supersonic trail, the ground indicator's size,
contact sparks.

**Timeline.** Ready now. If your draw shares a path with E or B, report that
in the first note before rewriting their effect. One worker.

---

### G — Contact sparks

**Goal.** Sparks appear where the car scrapes a surface, and where a touch
on the ball is awkward.

**Issue.** Nothing marks a belly or wheel scrape on the ground or wall
unless the car is already in a supersonic slide, and that effect is orange
spheres. An awkward ball touch has no sparks.

**Target.**

- **Surface scrape:** car in contact with floor or wall while sliding across
  it (grind, not a clean roll). Sparks come off the contact patch, backward
  along the slide. They happen below supersonic too.
- **Awkward ball touch:** a scrape, pinch, or glancing hit throws a short
  burst at the contact point. A clean hit may have a smaller flash only if
  a source says Rocket League does that; otherwise leave clean hits quiet
  and mark the choice.
- These are contact sparks. B's black sparkles are the supersonic stand-in
  and stay B's. If both cannot exist without one shared particle path, stop
  and file an issue. Do not delete B's sparkles.
- Deterministic in sim time.

**Done when.**

- A slow grind on the turf throws sparks at the wheels or belly, and they
  stop when the scrape stops.
- A glancing ball touch throws a short burst at the contact; the ball is
  otherwise unchanged.
- Supersonic black sparkles from B still appear on a straight supersonic
  run with no scrape. Quote that check.
- Physics gates you can run stay green.

**Out of scope.** Boost plume, the speed streak, ball texture, ball spin.

**Timeline.** Research now. Land after B has reported the sparkle approach,
unless you can add contact sparks without touching that effect. One worker.

---

### H — Exact DFH Stadium, at 60 FPS on the R4

**Goal.** With the local stadium loaded, DFH Stadium (Day) matches the
game's meshes, the textures the compiled materials bind, and the baked
shade, and it holds 60 FPS on the Radeon R4.

**Issue.** The loaded stadium is the real mesh in the right places, painted
with stand-in colours. There is no shadow on the bowl because the baked
lightmaps are never sampled. The default game, with no local stadium
loaded, must stay pixel-identical.

**Already measured. Do not repeat.** The expression graph binds a diffuse
on 248 of 640 sections. The rest have no texture expression anywhere in
the install. A name guess failed (5 unique hits, 4 ambiguous, 29 none out
of 38). The binding to read is the shader cache next to the stadium
package (`RefShaderCache-PC-D3D-SM5.upk`, `Engine_MI_Shaders.upk`,
`GuidCache.upk`). The shade to sample is the 18 `DirectionalMax` /
`NormalizedAverageColor` lightmaps, on the UV channel the mesh names.
Material tails are 84–320 bytes and are not a texture list. 11 textures,
including poplar and stairs, have an empty first mip. `CustomColors` (34
sections) is runtime. The field unwrap, the stand-in tints, and the sky
clamped to radius 36000 are hacks, removed per surface only after that
surface resolves. Full table is in §4.

**Target.**

- **Binding first.** Resolve each material from the shader cache (UModel is
  allowed as a local offline tool, output gitignored, never vendored). Count
  how many of the 606 sections that are not `CustomColors` gain a real
  diffuse. **Stop** if that is under 90 %. Write the count and the structure
  that is still missing. Do not add more stand-in colours to get past a miss.
- **Then the surface.** Store the UV channel the diffuse uses, the lightmap
  UV channel the mesh names, and vertex colour where the mesh has it. Parse
  the lightmap pair (the two lightmap classes in §4, plus scale and bias)
  for the floor and the stadium base before trusting any other component.
  One floor lightmap must show real shade, not a flat colour.
- **Then the draw, only while the local stadium is loaded.** One program,
  compiled once, inside the existing GL 4.5 compatibility context:
  albedo × vertex colour × lightmap, plus emissive. Alpha-test the tree
  cards. Upload compressed textures as compressed blocks. Static geometry
  is uploaded once and drawn in merged batches. No compile, no texture
  upload, and no sync readback inside a frame.
- **Moving shadows.** One sun shadow for the car and the ball. The bowl
  does not cast a second live shadow on top of its lightmap.
- **Grade last.** A day grade and a small bloom only after the four cameras
  already show real textures and baked shade.
- **Speed.** 1280×720, vsync off, 60 seconds. Machine A: at least 60 FPS,
  p99 at most 16.67 ms, no dropped ticks, no GL errors. Machine B: p99 at
  most 8.33 ms, same zeros. Quote both machines' GL vendor, renderer, and
  version. If A misses, merge batches, keep the compressed upload, and drop
  extra LODs. Do not decimate a mesh, do not drop the lightmaps, and do not
  fall back to the stand-in tints and call it exact.
- **Crowd.** `CustomColors` is a script, not a picture. The seats get the
  right mesh and a stable colour. Do not call the crowd exact.

**Cameras** (eye → target), same seed and the same early tick, no car mesh
in the way: top `0,-180,7200` → `0,0,0`; corner `5200,6400,1800` →
`0,200,60`; midfield toward a goal `0,-1400,420` → `0,4600,160`; goal
toward midfield `0,5480,380` → `0,-200,140`. A straight-down view is
degenerate; the small Y offset is required.

**Done when.**

- The binding count is written down and clears 90 % of the 606 sections, or
  the row is `BLOCKED` with the missing structure named. A blocked row is
  an honest finish of the measurement. It is not permission to keep tinting.
- The four cameras show cache textures and lightmap shade. Pitch markings
  and concrete are textures. The forced field unwrap and the stand-in tints
  are gone for every surface that resolved.
- Car and ball take one sun shadow. The stands do not take a second one.
- A stays at or above 60 FPS inside the table above. B's p99 is inside it.
  `drops=0` and `glerr=0` on both.
- With no local stadium loaded, the existing visual baseline capture is
  byte-identical. Quote the hash from before your change and the hash after.
- Physics gates you can run stay green. No extracted game byte is tracked
  by git.

**Out of scope.** Ball commands, ball pattern, ball spin, ground marker,
ball trail, boost, supersonic sparkles, contact sparks. Collision meshes.
Making the local stadium the default. A frame-identical crowd.

**Timeline.** Measurement is ready now and may run beside wave 1. The draw
is `BLOCKED` until the binding count is recorded and clears the bar. One
worker. If your draw changes how the ball, the boost, or a spark looks with
no stadium loaded, stop and file an issue.

---

## 6. Overlap

Behavior boundaries. The coordinator does not resolve these with paths.

| If this happens | Order |
|---|---|
| D and C both change how the ball is drawn | D lands orientation first. C changes only the pattern and keeps that orientation |
| B and G both spawn particles on the car | B owns sparkles that mean "supersonic." G owns sparkles that mean "contact." Shared particle path → stop and file an issue |
| E and F both draw under or behind the ball | E owns the ground marker. F owns the path streak. Neither retunes the other's size or colour |
| A changes ball velocity | A may set velocity on the five commands. A does not change drawing. D and F consume the result |
| Anyone needs a new physics fact | Read-only view + failing test. Coordinator records the issue. No physics edit |
| H changes a picture that A–G also draw | H owns the stadium only while the local stadium is loaded. With it unloaded, the picture stays byte-identical. If H's change moves the ball, the plume, the marker, the trail, or a spark, stop and file an issue |
| A–G need a stadium texture or lightmap | They do not. File an issue. H owns that binding |

---

## 7. Issue log

Coordinator appends. Workers propose an issue in their progress note; they
do not write here.

| ID | Opened | Workstream | Issue | Status |
|---|---|---|---|---|
| I1 | 2026-10-29 | A | Psyonix documents Pass Ball as "towards your car," sometimes offset left or right. The owner wants key 3 to keep the ball's position and fire along travel. Owner decision stands until a live capture reopens it | OPEN — implementing the owner's pass |
| I2 | 2026-10-29 | F | Ball-trail speed floor is unpublished. The landed value is 1500 uu/s, marked UNVERIFIED: a measured soft touch at 386.6 uu/s draws nothing, a boosted hit at 2797.6 uu/s draws the streak. Not a game function | OPEN |
| I3 | 2026-10-29 | A | Take Possession / Start Dribble offsets and whether hood placement keeps vertical velocity are unpublished here | OPEN |
| I4 | 2026-10-29 | B | Black sparkles are an owner stand-in, not a claim that Rocket League's sparks are black | OPEN — accepted as the current look |
| I5 | 2026-10-29 | H | Expression-graph diffuse coverage is 38.75 % and the stem guess failed. The shader-cache measurement is recorded at 368/606 (60.73 %), under the 90 % bar. No further tint pass | CLOSED — count written, draw not started |
| I6 | 2026-10-29 | H | UModel build 1590 with `-game=rocketleague` opens licensee 34 packages. It has no `ShaderCache` reader (`Unknown class "ShaderCache"`). `GuidCache.upk` fails with a bad allocation. The miss stops the draw | CLOSED — measured, draw stopped |
| I7 | 2026-10-29 | H | Machine A (Radeon R4) is the 60 FPS bar and was not measured for this stadium. A result from B alone cannot close H | OPEN |
| I11 | 2026-10-29 | H | Owner asked to finish a proper arena render past the 90 % binding stop. Named diffuse where the cache or graph resolves it. Vertex colour × baked lightmap where the material has no colour texture. No new stand-in tints. No stem guess. Crowd stays flat | OPEN — draw landed under override; cameras still fail the bar |
| I12 | 2026-10-29 | H | Floor_Team1 FLightMap2D atlas rect on the bound NormalizedAverage / DirectionalMax pair is empty (MEASURED rect lit 1.5–1.7%). Correct `uv*s+b` lands at ~8.6% lit. Raw UV1’s higher hit rate is wrong-neighbor sampling. No alternate channel in the component blob fills that rect. StadiumBase `uv*s+b` works (70.5% lit) | OPEN — structural miss |
| I8 | 2026-10-29 | H | `CustomColors` is runtime. The crowd will not be frame-identical | OPEN — accepted gap |
| I9 | 2026-10-29 | D | Ball draw orientation. `noBallRot` leaves `rotMat` stuck. D integrated `angVel` in the view and does not write it back. Scripted roll quoted. One-tick contact phase stays UNVERIFIED | CLOSED |
| I10 | 2026-10-29 | A | Wave 1 is writing one primary checkout. There is no extra worktree. A later row is accepted only for its own behavior. A mixed `git status` is not one worker's diff | OPEN |

---

## 8. Board

Coordinator only.

| ID | Goal | Status | Timeline | Progress |
|---|---|---|---|---|
| A | Keys 1–5 match Free Play ball control, with the owner's pass | DONE | Wave 1. Accepted 2026-10-29. I1 and I3 stay open | [Freeplay ball keys](d993ef2c-1243-4d46-bf0f-c07c9677bc3b). Throttle held at 2300: hood gap 91.250 through 1.0 s. Key 3 position unchanged. `measured.py` 19 PASS. Coasting with throttle released still lifts the ball. See A report |
| B | Boost plume, supersonic streak, black sparkles; circles gone | DONE | Wave 1. Accepted 2026-10-29. I4 stays open | [Boost plume and sparkles](0a7d0eca-7169-4ab8-ade9-1317deba2f74). Dropout at tick 366, 1797 uu/s, `super=0`, is plain turf. The earlier pale band matches the centre circle. See B report |
| C | Standard-ball surface | DONE | Research on time. Pattern accepted 2026-10-29 | [Ball texture research](0020153e-5091-490e-abd5-6445a675cc00). Rest still shows pentagons and hexagons. Spin moves the panels. VG1 pair `30bbbe441a6fa32f4d58c43f30fbc65c`. See C report |
| D | Ball mesh spins with physics | DONE | Wave 1. Accepted 2026-10-29. I9 closed | [Ball spin orientation](873666d6-9e7d-4cb8-801d-11167af9f2d9). Zero-spin mark fixed 240 ticks. Roll mark moved 1.354 about +Y. Fallback checksum `73db10410bd8a8ad`. See D report |
| E | Ground indicator stays above the turf | DONE | Wave 1. Accepted 2026-10-29 | [Ground indicator visibility](75d9f7e5-f735-4ead-82ed-d51b651afb00). VG4 PASS, radii unchanged. Play-camera orbit stays up. See E report |
| F | Fast-ball team-coloured trail | DONE | Wave 1. Accepted 2026-10-29. I2 stays open | [Fast ball trail](3f096bdf-2d14-478d-8319-d1fa172f0f9c). Hard 2797.6 blue, soft 386.6 none, switch to orange, fade in 24 ticks, 20 tris / 1 draw. See F report |
| G | Scrape and awkward-touch sparks | DONE | Wave 1. Accepted 2026-10-29. Wall grind not driven | [Contact spark research](8921329a-e82a-4e16-a709-0b2fcd71fd63). Turf grind 349.8 uu/s, off-nose burst, and black plus-signs at tick 280 (2300 uu/s) with the ball parked aside. See G report |
| H | Exact DFH Stadium from the shader cache and lightmaps, ≥60 FPS on the R4 | BLOCKED | Owner override draw landed. Cameras fail day-stadium bar. I12 | [Finish DFH stadium draw](b73ecd35-78c4-40b7-887a-8e889182ebe5). Stairs_Mat PASS. `uv*s+b` correct on StadiumBase (70.5% lit). Floor_Team1 atlas empty (I12). Bowl still grey; goal blue frame ~21%. VG1 `30bbbe…`. See H reports |

### A report (2026-10-29) — accepted, I1 and I3 open

Workstream: A
Goal (one sentence): At speed, key 2 rests the ball on the hood and moves it with the car.
Issue closed: none. I1 and I3 stay open.
Still wrong: Throttle released, the coasting brake slows the car out from under the ball. At 0.2 s that run was gap 99.900 along car up, ball z 157.099, vz 67.139. Hood offsets and one-shot velocity biases did not hold that contact. Those offsets stay unpublished (I3). Key 3 is still the owner's pass, not Psyonix's "towards your car" (I1).
Evidence: Throttle held after the run-up. At the command, car speed 2300 and the ball velocity matches, spin 0, on the Octane roof. After 0.2 s, 0.4 s, and 1.0 s, `gapUp` is 91.250, 91.250, and 91.259 against radius 91.250. The test asserts that gap, not separation under 220. `measured.py` → `# summary: 19 PASS, 0 FAIL`. Keys 1, 3, 4, and 5 were not retuned this round. Coordinator confirmed the script checks `gapUp` against the radius at those three times.
Sites changed this round: `tests/test_freeplay_ball.cpp`, `src/game/BUILD_NOTES.md`. Placement remains the Octane roof center plus the soccar radius, copying the car's linear velocity.
Blocker for another workstream: none.
Timeline: done for driving at speed. The coast-with-throttle-released pop stays listed under I3.

### A report (2026-10-29) — first pass, not accepted

Workstream: A — Freeplay ball commands
Goal (one sentence): Keys 1–5 follow Rocket League’s five Free Play ball controls, with the owner’s pass (no teleport) on key 3.
Issue closed: none. I1 and I3 stay open.
Still wrong: No live capture. Take Possession clearance 40 uu, full vertical copy, and the hitbox-roof hood point are UNVERIFIED (I3). Stopped pass is 1200 uu/s along car forward, UNVERIFIED. Launch keeps position and uses Hoops tip-off 1000 uu/s (`RLConst.h:50`); the 0.265 s delay is not applied. Defend Shot placement (3600 uu in front of the goal line, height 400, horizontal speed 2000) and the crossbar cutoff are UNVERIFIED. Psyonix’s “towards your car” sentence is not key 3 (I1). After 0.2 s the dribble ball was off the roof (z 156.6, upward speed 65.9).
Evidence: `make /tmp/t_ball` ended `KEYS 1-3 PASS` and `PASS freeplay ball`. Key 3 script: position `(1234.500, -567.250, 250.000)` before and after the command; velocity became the car’s horizontal velocity. At 2300 uu/s, key 1 stayed moving (speed 2211 after 36 ticks). `python3 tools/verify/measured.py` → `# summary: 19 PASS, 0 FAIL`. The 0.2 s key-2 check that passed was separation under 220 uu, which does not show the ball resting on the hood.
Sites changed (worker’s list): `src/game/FreeplayBall.h`, `src/game/FreeplayBall.cpp`, `src/game/main.cpp`, `tests/test_freeplay_ball.cpp`, `Makefile`, `src/game/BUILD_NOTES.md`. The checkout also holds other workstreams (I10). A said it did not touch `src/sim`.
Blocker for another workstream: none.
Timeline: not done. Sent back. Goal missed: at speed, key 2 rests on the hood and moves with the car.

### E report (2026-10-29) — accepted

Workstream: E — Ball ground indicator
Goal (one sentence): The ring and disc under the ball stay visible on the turf at play cameras, without changing the size, growth, or fade the indicator-size gate measures.
Issue closed: The circles were coplanar with the floor depth and dropped into the turf as the camera moved. They now draw with a depth bias and do not write depth.
Still wrong: An extreme graze (eye 500 uu back, 42 uu up) is still a thin aliased arc. It was not thickened, because that would change the size the gate measures. Colour was not changed. The bias is UNVERIFIED.
Evidence: Ball on the turf, machine B `NVIDIA GeForce RTX 4060 Laptop GPU` `4.6.0 NVIDIA 615.78.08`, `glerr=0`. Eight cameras at height 150 and eight at height 52: ring pixels stayed in band (15061–15383 and 8997–9230). A 12 uu nudge kept every ring pixel within 3 px. `visual_gates.py --only VG4` → `VG4 PASS ball indicator radii (recipe) z600 outer=91 inner=71 disc=45; ratios 0.780/0.495; growth 1.022`. `measured.py` → `19 PASS, 0 FAIL`. Coordinator confirmed the radii recipe in `DrawBallIndicator` is unchanged and the offset is restored before the next draw.
Sites changed: `src/fx/FX.cpp` (`DrawBallIndicator` only), `src/render/BUILD_NOTES.md`.
Blocker for another workstream: none. The ball trail is a separate draw.
Timeline: done.

### F report (2026-10-29) — accepted, I2 still open

Workstream: F — Ball trail
Goal (one sentence): A fast ball leaves a short streak in the last-touch team colour, and a soft touch leaves none.
Issue closed: none. I2 stays open. Floor is 1500 uu/s, marked UNVERIFIED.
Still wrong: Streak length (≤280 uu), half-width (12 uu), and fade (0.12 s) are UNVERIFIED. Machine A was not measured. No chase-camera playtest.
Evidence: `LAST TOUCH PASS` (the first compile failed because `GetLastTouch` was missing; it is a read-only view). Soft hit speed 386.6 draws nothing. Hard hit speed 2797.6 draws `blue=120 orange=0 tris=20 draws=1`. Switch: `blue=0 orange=120`. Fade after 24 ticks under 386.6: `tris=0`. Untouched: no streak. `VG4 PASS` with the same radii as E. `VG7 PASS ticks=7200 drops=0 glerr=0 tris_last=189144`. `measured.py` 19 PASS / 0 FAIL. `mesh_gates.py` 13 PASS / 0 FAIL. Coordinator confirmed `DrawBallTrail` uses its own samples and does not retune the indicator or the car streak, and `GetLastTouch` only reads `ballHitInfo`.
Sites changed: `src/sim/Sim.h`, `src/sim/Sim.cpp`, `src/sim/BUILD_NOTES.md`, `tests/test_last_touch.cpp`, `src/render/Renderer.h`, `src/game/main.cpp`, `src/fx/FX.h`, `src/fx/FX.cpp`, `tests/test_ball_trail.cpp`, `Makefile`, `src/render/BUILD_NOTES.md`, `docs/VISUAL_PARITY_LEDGER.md`.
Blocker for another workstream: none.
Timeline: done.

### B report (2026-10-29) — accepted, I4 open

Workstream: B
Goal (one sentence): Dropping out of the supersonic state removes the streak and the black sparkles.
Issue closed: The pale band in the first dropout frame is the centre circle, not the trail. The rejected `/tmp/bfx/dropout.ppm` hashes to `f600f9e6344f206e45955d73285f5f70`, which the worker measured as identical with `DrawTrail` skipped. The camera was 160 uu behind the car, inside the circle.
Still wrong: I4 stays open. Sparkle size and streak length (320 uu) are UNVERIFIED. No live Rocket League capture. The slide draw still has orange grind spheres at a slide of 2200.
Evidence: New frame `/tmp/bfx/dropout_clear.png`, tick 366, speed 1797.3, `super=0`, boost 0. Coordinator counted 0 pixels behind the car that are lighter than the turf (the rejected frame had 2326). Near-white pixels are 67. Machine B `NVIDIA GeForce RTX 4060 Laptop GPU` `4.6.0 NVIDIA 615.78.08`, `glerr=0`. No draw change this round, so the earlier `measured.py` 19 PASS and VG4 PASS stand. The airborne jet, the in-band streak, the black plus-signs, and the flip with no circles were already in the first shots.
Sites changed this round: `src/render/BUILD_NOTES.md` only.
Blocker for another workstream: none.
Timeline: slipped on the first dropout camera, then done.

### B report (2026-10-29) — first pass, not accepted

Workstream: B — Boost, supersonic trail, black sparkles
Goal (one sentence): Boost and the supersonic state read as a short exhaust, a speed streak, and black sparkles, and the airborne spin circles are gone.
Issue closed: none. I4 stays open. The stacked cones, smoke balls, box chain, and airborne circles are gone from the shots that were checked. The orange grind spheres in the slide draw are still there, gated on a slide at 2200.
Still wrong: Plume length, nozzle offset, streak length (320 uu), and sparkle size are UNVERIFIED. Dropout does not clear the streak. No live Rocket League capture.
Evidence: `measured.py` 19 PASS / 0 FAIL, including `PASS G8 supersonic >=2200: first tick 190`. VG4 PASS with the same radii. Machine B `NVIDIA GeForce RTX 4060 Laptop GPU` `4.6.0 NVIDIA 615.78.08`. Shots in `/tmp/bfx`: plume at tick 45, speed 577, is a twin jet and not a smoke cloud. Supersonic at tick 250, speed 2300, shows the streak and black plus-signs. Maintain tick 274, speed 2199.7, `super=1`, boost off: streak and black specks, no flame. Dropout tick 297, speed 2099.1, `super=0`: the frame has 43 near-white pixels and a pale band behind the car about 40 px wide (2326 pixels lighter than the grass). A flip frame has no such band and no circles. `DrawAir` is empty. Sparkles are eight black quads inside `DrawTrail`, drawn only while `Snapshot.supersonic` is set, after the streak. Two kickoff runs match `md5=0b49cafafbb4a14785523f703b0b4eeb`; that is not the stored VG1 hash, and the difference they measured is 4 pixels on the ball. VG1 was not re-run. The stored `gates_visual.json` VG1 line is still `md5=5326135d54b86d53532d32d78670030d`.
Sites changed: `src/fx/FX.cpp`, `src/fx/FX.h`, `src/render/Renderer.h`, `src/game/main.cpp`, `src/render/stub_main.cpp`, `src/render/BUILD_NOTES.md`, `docs/VISUAL_PARITY_LEDGER.md`. Comments in `DrawContactSparks` were touched; that draw was not.
Blocker for another workstream: none. G's contact draw stays separate. The slide's orange spheres were left in place.
Timeline: not done. Sent back. Goal missed: dropping out of the supersonic state removes the streak and the sparkles.

### D report (2026-10-29) — accepted

Workstream: D — Ball spin
Goal (one sentence): The drawn ball rotates with the ball's physics angular velocity, integrated in sim time.
Issue closed: I9. The snapshot had no ball spin. `GetBallSpin` is read-only. `angVel` is integrated once per 120 Hz tick into a matrix that is never written back. `DrawBall` multiplies that matrix around the existing panels.
Still wrong: On a contact tick the integral uses the post-step rate for the whole tick. That one-tick phase is UNVERIFIED. The hex pattern is unchanged.
Evidence: `BALLSPIN PASS`. Zero spin, 240 ticks, mark `(1,0,0)` moved `0`. Roll `|w|=2.9912` about `+Y` with `+X` velocity; over 60 ticks the mark moved `(0.5971,0,0.8022)` → `(0.8491,0,-0.5282)`, `moved=1.3540`, and each tick matched `|ω|·dt`. Reset returns the mark to identity. Fallback checksum `73db10410bd8a8ad`. `measured.py` 19 PASS / 0 FAIL. `mesh_gates.py` 13 PASS / 0 FAIL. `verify_vs_rocketsim.py` 110/110. VG4 PASS with the same radii. Coordinator confirmed `AdvanceBallOrientation` reads `GetState` and does not write the body, and the panel UVs are unchanged.
Sites changed: `src/sim/Sim.h`, `src/sim/Sim.cpp`, `src/models/BallOrient.h`, `src/models/Models.cpp`, `src/models/Models.h`, `src/render/Renderer.h`, `src/game/main.cpp`, `tests/test_ball_spin.cpp`, `Makefile`, `src/sim/BUILD_NOTES.md`, `src/render/BUILD_NOTES.md`, `CONTRACTS.md`.
Blocker for another workstream: none. C may land a pattern on this orientation.
Timeline: done.

### G report (2026-10-29) — accepted

Workstream: G
Goal (one sentence): Sparks appear where the car scrapes a surface, and where a touch on the ball is awkward.
Issue closed: A powerslide below supersonic marks the wheel contact. An off-nose ball touch throws a short burst. A square hit stays quiet. On a straight supersonic run with no scrape, the black plus-signs still draw.
Still wrong: A wall or belly grind was not driven. No separate frame of the grind sparks was captured. Spark colour, the 80 uu/s slip floor, the 0.18 s burst, and the 0.75 nose cutoff are UNVERIFIED. On a supersonic slide the old orange drift spheres still draw as well; those stay with the slide draw.
Evidence: Slow grind tick 48, speed 349.8 uu/s, lateral slip 83.5, sparks stopped after 27 brake ticks at speed 39.8. Glancing touch tick 44, tangent 436.6, align 0.64; ball state identical on a second run. Square hit tangent 150.4, align 0.91, not awkward. Sparkle quote, no source edited: tick 280, speed 2300, throttle and boost only, ball parked at `(3500, 3500, 400)`. `/tmp/g_spark_quote/supnear.png` shows black plus signs behind the car (HUD `SPD 2300 TICK 280`). The same camera at tick 40, speed about 730, shows the boost flame and no plus signs. `measured.py` 19 PASS / 0 FAIL on the earlier pass. `CONTACT SPARKS PASS`. Machine B `NVIDIA GeForce RTX 4060 Laptop GPU` `4.6.0 NVIDIA 615.78.08`, `glerr=0`.
Sites changed: `src/sim/Sim.h`, `src/sim/Sim.cpp`, `src/fx/ContactSparks.h`, `src/fx/FX.h`, `src/fx/FX.cpp`, `src/game/main.cpp`, `Makefile`, `tests/test_contact_sparks.cpp`, `src/sim/BUILD_NOTES.md`, `src/render/BUILD_NOTES.md`. This quote pass edited nothing.
Blocker for another workstream: none.
Timeline: done.

### G report (2026-10-29) — first pass, not accepted

Workstream: G
Goal (one sentence): Sparks appear where the car scrapes a surface, and where a touch on the ball is awkward.
Issue closed: none. A powerslide below supersonic marks the wheel contact, and an off-nose ball touch throws a short burst. A square hit stays quiet.
Still wrong: The straight supersonic run did not quote B's black sparkles. A wall or belly grind was not driven. No frame was captured. Spark colour, the 80 uu/s slip floor, the 0.18 s burst, and the 0.75 nose cutoff are UNVERIFIED. On a supersonic slide the old orange drift spheres still draw as well; those stay B's.
Evidence: Slow grind tick 48, speed 349.8 uu/s, lateral slip 83.5, sparks stopped after 27 brake ticks at speed 39.8. Glancing touch tick 44, tangent 436.6, align 0.64; ball state identical on a second run. Square hit tangent 150.4, align 0.91, not awkward. Straight boost scrapes=0, top 2300, supersonic flag set — that is not the sparkle draw. `straight_boost_600` checksum `f1a0319d95ed8f8a`. `measured.py` 19 PASS / 0 FAIL. `make tests` exit 0, including `CONTACT SPARKS PASS`. Coordinator confirmed `DrawContactSparks` is its own spheres and `Advance` was not the issue; the sparkle quote is still missing. Spark motion uses sim-tick `dt`.
Sites changed: `src/sim/Sim.h`, `src/sim/Sim.cpp`, `src/fx/ContactSparks.h`, `src/fx/FX.h`, `src/fx/FX.cpp`, `src/game/main.cpp`, `Makefile`, `tests/test_contact_sparks.cpp`, `src/sim/BUILD_NOTES.md`, `src/render/BUILD_NOTES.md`.
Blocker for another workstream: none filed. Sent back for the sparkle quote only. Do not edit B's streak or sparkles to produce it.
Timeline: not done.

### C report (2026-10-29) — accepted

Workstream: C — Ball texture
Goal (one sentence): The ball surface reads as the standard ball: white hexagons and black pentagons on a truncated icosahedron, with a gloss under the existing lights.
Issue closed: The flat repeating hex lattice with gray seams.
Still wrong: Side-of-field tint and the small lamps stay a follow-up. Panel bytes `(242,244,247)` / `(18,18,20)` and shininess 32 are UNVERIFIED. Machine A was not measured.
Evidence: Still at tick 8, two runs md5 `5e924d5d60c25593d0b4f70c6b1d6f86`. The ball shows black pentagons and white hexagons with a highlight. Spin: black-panel centroid moved from `(-4.1, -14.8)` px at tick 1 to `(-19.7, -13.6)` px at tick 91. `VG1 PASS` two captures byte-identical, md5 `30bbbe441a6fa32f4d58c43f30fbc65c`. `VG4 PASS` z600 outer=91 inner=71 disc=45. Machine B `NVIDIA GeForce RTX 4060 Laptop GPU` `4.6.0 NVIDIA 615.78.08`, `glerr=0`. Coordinator confirmed `DrawBall` still multiplies `ballRot` and the print is built in that object frame. The stored VG1 line had been restored to the pre-pattern hash; it now records `30bbbe441a6fa32f4d58c43f30fbc65c`.
Sites changed: `src/models/Models.cpp`, `src/models/Models.h`, `src/render/BUILD_NOTES.md`, `docs/VISUAL_PARITY_LEDGER.md`.
Blocker for another workstream: none.
Timeline: done.

### C research note (2026-10-29) — superseded by the accepted report

Workstream: C — Ball texture
Goal (one sentence): The ball surface reads as the standard ball: a truncated icosahedron of white hexagons and black pentagons, with a gloss under the existing lights.
Issue closed: none. Research only. No pattern landed, no ball-draw edit, no orientation added or removed.
Still wrong: The ball is a smooth sphere with a flat hex wallpaper, and it cannot spin. The snapshot carries ball position only. `GetBall` drops `rotMat` and `angVel`.
Evidence: Source inspection only. No product diff, so no build, no `measured.py`, no `make tests`, and no visual gate. Those bars were not re-proven this round. Confirmed by the coordinator: `ArenaConfig.noBallRot` defaults true (`ArenaConfig.h`), and `m_noRot` selects `integrateTransformNoRot` while angular velocity is still written back.
Sites changed: none.
Blocker for another workstream: none filed by C. Coordinator opened I9 from the finding. C cannot land until D reports an orientation that moves when the ball spins.
Timeline: research done, on time. The block lifted when D was accepted; the pattern landing is in progress and is not accepted yet.

Research to keep for the landing pass: procedural truncated-icosahedron classification in the sphere's existing object frame (12 pentagons, 20 hexagons), black/white bytes `UNVERIFIED`, gloss as a ball-only fixed-function specular restored before return. Do not scroll `u` by an angle. Side-of-field tint and ball lamps stay a follow-up. VG1's stored kickoff hash will change when the pattern lands.

**Wave 1 (parallel):** A, B, D, E, F, G's research, and H's binding measurement.

**Wave 2:** A–G accepted. H is `BLOCKED` on I12 (Floor_Team1 empty lightmap atlas). Pack draw exists under I11; the four cameras do not clear a day-stadium bar.

### H report (2026-10-29) — second draw pass, blocked on I12

Workstream: H
Goal: Day stadium in the four cameras — bowl shade from a correct lightmap decode, real diffuse on resolved surfaces, no purple atlas junk.
Issue closed: none against that bar. Kept: Stairs_Mat PASS, pack `/2`, FieldSkin UV2, binding **375/606**, VG1 `30bbbe441a6fa32f4d58c43f30fbc65c`, soak `drops=0 glerr=0`. StadiumBase lightmap decode is `uv*s+b` + DirectionalMax: lit **70.5%** (raw was 26.2%).
Still wrong: Bowl flat grey in corner/midfield. Goal view ~21% solid blue frame. Floor_Team1 atlas empty (I12): `uv*s+b` lit 8.6%, rect lit 1.5%. Frame/Trim MICs bind only normals; SeatBase binds a 1×512 ramp. Machine A unmeasured (I7).
Evidence: `/tmp/arena_h/{top,corner,midfield,goal}.png`; proof `/tmp/arena_h/lm_proof/floor_team1_proof.png`. Coordinator: goal.png blueish 20.6%.
Sites changed: `tools/arena/{rip_arena,rllightmap}.py`, `src/models/ArenaRip.{h,cpp}`, `src/render/Renderer.{h,cpp}`.
Blocker: I12. Next needs a non-empty Team1 bake outside this component SIG, or an owner accept of pitch=FieldSkin-without-LM and a different bowl bar on grey concrete albedos.
Timeline: stopped.

### H report (2026-10-29) — first draw pass, not accepted

Workstream: H
Goal: DFH Stadium (Day) local pack looks like a proper stadium.
Issue closed: Stairs_Mat PASS. Pack `/2`. FieldSkin on the pitch. VG1 unchanged. Binding **375/606**.
Still wrong then: Bowl grey; CoordinateScale/Bias looked wrong; purple atlas junk.
Timeline: slipped. Sent back (superseded by the second pass).

### H report (2026-10-29) — measurement accepted, draw was blocked (superseded by owner override I11)

Workstream: H (measurement only)
Goal: Count how many of the 606 non-CustomColors sections gain a real diffuse from the shader cache.
Issue closed: I5 and I6 as measurements. Count was **368/606 (60.73%)** before the ShaderCache reader; later H-shader recount **375/606**.
Timeline: measurement done. Draw later opened under I11.

**Acceptance bar for the whole order.** All eight rows `DONE` (H may be
`BLOCKED` on a written cache miss, which defers the draw and does not fail
A–G), §7 conflicts either closed or explicitly deferred by the owner,
physics gates still green, visual gates that already passed still pass. The
no-stadium capture stays byte-identical.

---

## 9. Sources

- Psyonix, *Using the New Free Play Controls & Training Pack Refresh*:
  https://www.rocketleague.com/en/news/using-the-new-free-play-controls-and-training-pack-refresh
- Psyonix, patch notes v2.06 (Free Play ball control; BakkesMod credited as
  inspiration; Pass Ball and Defend Shot called out as unreliable in some
  extra modes): https://www.rocketleague.com/en/news/patch-notes-v2-06
- Player thread on leftover spin after Take Possession / Start Dribble
  (claim and a contradicting reply — measure, do not adopt a side):
  https://www.reddit.com/r/RocketLeague/comments/1jtipds/small_quality_of_life_change_for_free_play_that/
- Ball-trail colour is the last team to touch the ball:
  https://www.reddit.com/r/RocketLeague/comments/h8vsvx/why_and_when_theres_a_trail_behind_the_ball/
  and https://www.reddit.com/r/RocketLeague/comments/13gojbj/been_playing_since_2018_and_ive_only_just/
- Supersonic **state** (the car, not the ball trail): on at 2200 uu/s, held
  through 2100, off below 2100. Already a measured fact in this project.
  Patch notes v1.78 describe the same state as "trails visible/active."
