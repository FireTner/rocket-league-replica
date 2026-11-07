# MASTER PROMPT — Shade and edges

Work order for **one worker**. Opened 2026-11-06.

This is image quality of the draw that already exists: the stair-steps on
edges, then the shade the pack shader actually computes. It is not a new
texture pass and not a new renderer.

Read first: `AGENTS.md` and `CONTRACTS.md`. Then this file. Presentation
only. The local stadium pack is already loaded by `./build/game`.

The pitch surfaces from `MASTER_PROMPT_PITCH_DETAIL.md` landed the same
day. The force-field lattice is `MASTER_PROMPT_FORCEFIELD.md` and is a
different worker. Do not move either. Do not reopen grass wear, V1–V8,
M1–M6, or Arena 1:1.

---

## 1. Goal

On DFH Stadium (Day), with the user's local pack loaded, at the shipped
720p:

1. Silhouette edges (post against sky, curb against grass, car against
   the wall) lose the stair-step that a single sample produces.
2. The pack shader's lit surfaces use the normal data the pack already
   stores, so metal reads as metal under the existing key light. Grass
   stays the unlit field skin. Team neon stays the flat unlit colour.
3. An edge that multisample does not cover (alpha-tested grate, hex
   mask, foliage) is named, and fixed only if the census shows it is
   still jagged after pass 1.

A playtest that only swaps textures will still look aliased, and the
pack fragment's shade is a narrow `ndl` mix. That is the gap. A softer
picture that costs the Radeon R4 its frame is not the goal.

---

## 2. Shared rules

A violation fails the round.

1. **Physics stays the authority.** No edit under `src/sim`, `src/arena`,
   `src/physics`, no gate number, no tape. Do not add a sim view.
2. **No Psyonix asset in the tree.** No new texture, dump, or overlay
   file. A look that only appears when `material_overlays.json` is
   present does not count.
3. **One context, one pack program.** GL 4.5 Compatibility. GLSL
   `#version 450 compatibility` or lower. The pack program already in
   `Renderer.cpp` (`kArenaVs` / `kArenaFs`) is the only fragment shader
   this task may edit. No second shader file. No compute, geometry,
   tessellation, or a core-profile context. The shell stays
   fixed-function. `PbrGl` stays the car/ball fixed-function coat; do
   not fold it into the pack program.
4. **Defaults stay off until the bar says otherwise.** `[render] msaa`
   ships at 0. Pack-shader switches ship off. With both off, the shell
   frame and the pack frame at `msaa=0` stay byte-identical to the
   hashes you record in pass 0. A quality path becomes the default, or
   enters a gated capture, only when PG1 is green on A and on B.
   PG1 is the numbers in `AGENTS.md` and `docs/GATE_POLICY.md`: A at
   least 60 FPS, p99 ≤ 16.67 ms, at most 1.15× the default path's
   `avg`; B p99 ≤ 8.33 ms; both `drops=0` and `glerr=0`. The gate is
   not implemented in `visual_gates.py`. Quote the `[game] exit` line.
   Hand-written numbers do not make the path the default.
   `docs/RENDER_POLICY.md` is cited by those files and is not in the
   tree. Do not write a replacement policy as this task.
5. **Cite the number or mark it.** Every new constant names a
   measurement from this run (sample count the context actually
   created, pixel delta, frame time, a pack texture channel) or it is
   `UNVERIFIED`. Do not copy a sample count from a guide.
6. **One change at a time.** Census first, no shader edit. Then
   multisample, shot against the census frame. Then one shade term,
   shot. Then the edge multisample did not fix, shot — or a recorded
   skip if pass 1 already cleaned it.
7. **Leave the bars this task does not touch.** `gates_visual.json` is
   not overwritten by a partial `--only` run. Physics gates are not
   required when no sim file changed. `glerr=0` on every capture.
   Quote `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`. Window stays 720p.
   Do not raise the resolution to hide aliases.
8. **Machine A is the floor and may be unreachable.** A result from B
   alone cannot claim 60 FPS and cannot flip a default. If A is not
   measured, write `A NOT MEASURED` and leave the default off. Do not
   decimate a mesh. Quote pack triangle count before and after. Do not
   touch `TRI_CAP`.
9. **Say what you could not verify.** A live Rocket League frame is the
   picture the shade is matching. If you cannot capture the game, match
   the census and list the gap.

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

Re-confirm against a real run. Do not treat a miss as permission to
add a second technique.

### 4.1 Multisample exists and defaults off

`[render] msaa` in `config/settings.ini` is an int, default 0, range
0..16 (`docs/INI_SCHEMA.md`). `Renderer` applies it at context
creation with `SDL_GL_MULTISAMPLEBUFFERS` / `SDL_GL_MULTISAMPLESAMPLES`.
It needs a restart. The shipped single-sample path is what keeps gated
captures pixel-stable. Turning the default above 0 changes every
capture hash. That is a fail until PG1 is green on A and B and a full
visual suite is re-recorded on purpose.

Query `GL_MAX_SAMPLES` and the samples the context actually created
(`GL_SAMPLES` after init). The log line is `[render] msaa`. A request
the driver ignores is a stop. Write the requested count and the count
you got. Do not invent a custom resolve to paper over a refused
request.

### 4.2 The pack shader is one narrow shade

`kArenaFs` is `#version 450 compatibility`. It samples albedo,
lightmap, normal, and mask. Emissive replaces the shade when that
switch is on. With the normal switch off, shade is
`mix(0.92, 1.0, ndl)` against a fixed light vector. With the normal
switch on, shade is `mix(0.38, 1.22, ndl)` after the tangent-space
normal. Switches default off. The goal-mouth frame matched the
previous capture byte for byte with them off (`src/render/BUILD_NOTES.md`,
arena visual finish, 2026-11-06). Keep that property.

`PbrGl` is fixed-function specular on the car and the ball. Default
off (`[render] pbr`). It is not a fragment shader. Leave it.

### 4.3 Edges this task may touch, and edges it may not

Pitch-detail pixels (2026-11-06, `src/render/BUILD_NOTES.md`) are a
regression: the team curb lip, the white marks, the centre hex disc,
`Field_STD_Floor_Hex`, grass on the approach, no purple sheet. A shade
or an AA change that recolors those surfaces fails. Aliasing on their
silhouettes is in scope. Their colour is not.

The force-field lattice is the other worker's draw. Do not edit the
hex-mask conversion or the glass blend to "improve" it.

Alpha test is a hard `discard` at 0.5. That is the grate, the foliage,
and any mask that uses it. Multisample does not grey those edges.
Name them in the census before changing the discard.

---

## 5. Work

### Pass 0 — Census

**Blocks every later pass.** No shader edit. No default change.

Record, in `src/render/BUILD_NOTES.md`:

- `GL_MAX_SAMPLES`, the `[render] msaa` value, and `GL_SAMPLES` after
  the context is up, on the machine you have.
- The shell hash and the pack hash at `msaa=0`, seed 7, tick 120, the
  high-corner camera in §6. These are the byte-identical bars.
- The `[game] exit` line at `msaa=0` (`avg`, `p99`, `drops`, `glerr`,
  `tris_last`).
- One close crop of a silhouette (post against sky) and one of an
  alpha-tested edge (grate or foliage), same seed and tick. Count the
  jagged run in pixels along a named edge. That count is the alias
  this task has to shrink.

**Done when.** The notes hold those numbers from a real run. No code
change.

### Pass 1 — Multisample

**Goal.** The existing `msaa` key, set to the largest of 2, 4, or 8
that the context actually creates, shortens the jagged run from the
census. The default in the INI stays 0.

**Do not.** Add FXAA, SMAA, TAA, or a second resolve pass. Raise the
window size. Change the default. Re-record `gates_visual.json`.

**Done when.** A pair of crops, `msaa=0` and the accepted count, shows
the shorter run, and the notes quote requested samples, `GL_SAMPLES`,
the frame-time delta, and `glerr=0`. If the driver creates fewer
samples than requested, the smaller count is the result. If it creates
zero, stop. Shell and pack hashes at `msaa=0` match pass 0.

### Pass 2 — One shade term

**Goal.** With a switch that defaults off, lit pack surfaces that
already bind a normal map take a specular lobe from that normal and
the existing key light. Roughness is a channel you measured in a pack
texture, or the term is marked `UNVERIFIED` and the switch stays off
in the notes. Grass, the team lip, and the white marks stay unlit and
the same colour.

**Do not.** Add a second shader file. Build a material graph. Light
the unlit surfaces to "make them pop". Change `PbrGl`. Move the
pitch-detail colours.

**Done when.** Switch off: pack hash at `msaa=0` matches pass 0.
Switch on: the high-corner and the post close-up show a highlight on
the frame metal that follows the carbon normal, and the grass and the
lip match the pass 0 colour. `glerr=0`. Frame time quoted. Default
remains off unless PG1 is green on A and on B.

### Pass 3 — The edge multisample missed

**Goal.** Only if pass 1's crop of the alpha-tested edge is still
jagged. Soften that edge inside the same fragment shader, switch
defaulting off. Coverage from `GL_SAMPLE_ALPHA_TO_COVERAGE` counts if
you measure it; a new full-screen pass does not.

**Do not.** Invent the pass when pass 1 already cleaned the edge.
Write that as a skip. Retune the force-field mask.

**Done when.** The alpha-tested crop is smoother than pass 0, or the
notes record the skip with the pass 1 crop as the reason. Hashes at
`msaa=0` and switches off still match pass 0.

---

## 6. Cameras

Same seed, same early tick, pack loaded, no freecam drift. Eye, then
target, as `--cam`:

| View | Eye | Target |
|---|---|---|
| High corner | `4200,-3800,2400` | `-200,800,200` |
| Post against sky | `900,-5600,400` | `0,-5120,500` |
| Grate / mask edge | `400,-5600,220` | `0,-4200,160` |
| Chase, car on the centre spawn | the game's own chase cam | the car's target |

The high corner is the regression frame for the pitch surfaces. The
post crop is the silhouette. The inside-goal camera is the
alpha-tested edge and the mouth. A pass that looks smooth only in the
chase view and fails the high corner is not done.

Shots go under `/tmp/shade_vis/`. Record path and md5. Machine line
with every shot: `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`. `glerr=0`.

---

## 7. Acceptance

All four must be true:

1. Pass 0's numbers are in `src/render/BUILD_NOTES.md`. Passes 1–3
   each cite the alias or the shade term they changed, or the skip.
2. At the accepted sample count, the silhouette run is shorter than
   the census count. The pitch-detail colours are unchanged. The
   force-field draw is unchanged. `msaa=0` and switches off match the
   pass 0 hashes.
3. The default is still `msaa=0` and shade switches off, unless PG1
   is quoted green on A and on B. `A NOT MEASURED` leaves the default
   off. `glerr=0`. `drops=0` on the capture run. Pack triangle count
   quoted before and after.
4. `git diff` under `src/sim`, `src/arena`, `src/physics`, `tools/tas`,
   and `tools/verify/measured.py` shows no change from this task.
   `gates_visual.json` is not overwritten by a partial `--only` run.

---

## 8. Board

| Pass | Goal | Status | Evidence |
|---|---|---|---|
| 0 | Census of samples, hashes, jagged runs | DONE | `src/render/BUILD_NOTES.md` shade section. B only. |
| 1 | Multisample via the existing key, default stays 0 | DONE | 8 samples created. Stair 16 px / 3 double-steps → 13 px / 0. Default still 0. |
| 2 | One specular term in the pack shader, default off | DONE | `pack_spec` off matches the census hashes. On shows the carbon lobe. |
| 3 | Alpha-tested edge, or a recorded skip | DONE | Skip. Leaf crop at 8 samples is the reason. Coverage trial not shipped. |

```
Pass: 0
Goal (one sentence): Record sample counts, the msaa=0 hashes, and the jagged runs before any shade edit.
Issue closed: Census is in src/render/BUILD_NOTES.md from a real run.
Still wrong: The crossbar stair is 16 px with three 2 px steps. The leaf card's hard tread is 5 rows.
Evidence (command, GL strings, what the shot shows): gamescope headless, seed 7, tick 120, 1280x720. GL_VENDOR=NVIDIA Corporation RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2 VERSION=4.6.0 NVIDIA 615.78.08. GL_MAX_SAMPLES=32, msaa=0, GL_SAMPLES=0. Pack md5 e76031b40311d36dae281e191d3aeb23. Shell md5 632a0906a760a5eeef36d99610e17a46. Exit ticks=720 avg=0.47ms p99=1.00ms drops=0 glerr=0 tris_last=681540. Pack tris=558367.
Sites changed: none
Blocker: Machine A NOT MEASURED. No live Rocket League frame.
Timeline: done

Pass: 1
Goal (one sentence): The existing msaa key at 8 samples shortens the crossbar stair, and the default stays 0.
Issue closed: Pure beam-sky contacts 14→8 and the 2 px steps 3→0.
Still wrong: The leaf card is still a hard cutout. avg frame time rose 0.47→3.08 ms.
Evidence (command, GL strings, what the shot shows): requested=8 GL_SAMPLES=8 GL_SAMPLE_BUFFERS=1 GL_MAX_SAMPLES=32. Same GL strings. p1_post.ppm md5 c421b0a03552abb22d900101188ae064. Exit ticks=720 avg=3.08ms p99=5.00ms drops=0 glerr=0. Interior grass and both lips match the census bytes. msaa=0 hashes match pass 0. A 16-sample request is refused.
Sites changed: src/render/Renderer.cpp logs the sample count the context created.
Blocker: A NOT MEASURED, and 3.08/0.47 is past the 1.15× bar, so the default stays 0.
Timeline: done

Pass: 2
Goal (one sentence): One Blinn lobe on pack surfaces that already bind a normal, switch off by default.
Issue closed: With pack_spec on, the post metal brightens where the carbon normal faces the key, and grass and the lips stay the census colours.
Still wrong: The exponent map of the measured alpha is UNVERIFIED. The switch stays off.
Evidence (command, GL strings, what the shot shows): ENV_CarbonFiber_N alpha 203..255 mean 248.8, corr −0.53 with XY tilt. Off hashes match pass 0. On post md5 2415e0622c6bf83b982e624e17f8aaaa. Sample (228,225,217)→(248,247,245) beside an unchanged (198,196,188). glerr=0. 180-tick on-run avg=0.53ms p99=2.00ms drops=0. Pack tris=558367.
Sites changed: kArenaVs/kArenaFs, Settings pack_spec, config/settings.ini, docs/INI_SCHEMA.md.
Blocker: A NOT MEASURED. Default stays off.
Timeline: done

Pass: 3
Goal (one sentence): Soften the alpha-tested edge only if 8 samples left it jagged.
Issue closed: Skip. The leaf crop at 8 samples already moved 1111 pixels (tread 5→4). A coverage trial left that crop identical to the 8-sample frame, so it was not shipped.
Still wrong: The card outline is still a short stair. The 0.5 discard is unchanged.
Evidence (command, GL strings, what the shot shows): p1 leaf box vs p0 differs by 1111 pixels, max channel delta 202. Coverage vs that 8-sample frame differs by 0 pixels in the leaf box. Final msaa=0 pack and shell hashes match pass 0.
Sites changed: none kept
Blocker: none for the skip
Timeline: done
```

---

## 9. Out of scope

Temporal AA, upscalers, ray tracing, a deferred pipeline, SSAO, bloom,
and a colour grade. A second shader file. Supersampling by raising the
window. New textures and new meshes. The pitch surfaces, the
force-field lattice, grass wear, the car/ball `PbrGl` coat, physics,
and making any of this the default before PG1 is green on both
machines.
