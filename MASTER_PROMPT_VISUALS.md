# MASTER PROMPT — Visual pass to 1:1 direction (physics stays frozen)

This is the standing work order for the visual pass. Hand it to an agent as its
complete brief. Read `AGENTS.md` first, then this file, then `CONTRACTS.md`.

> **CLOSED at V6 (2026-10-10).** V1-V6 landed; evidence is in
> `docs/VISUAL_PARITY_LEDGER.md`, `docs/VISUAL_VERDICT.md` and
> `tools/verify/gates_visual.json` (VG1-VG12, 12 PASS / 0 FAIL / 0 SKIP), with
> the round log appended to `docs/VISUAL_NOTES.md` (Round 36). Physics gates
> were re-proven green on the final revision. Do not re-run V1-V6; remaining
> gaps are listed as UNVERIFIED in the ledger.

**Prerequisite state: physics is CLOSED (M1–M6).** `docs/PHYSICS_VERDICT.md` is
the record; `docs/PHYSICS_1TO1_LOG.md` Rounds 1–7 are the evidence. The physics
gates are now the **regression bar**, not the work. Do not reopen M1–M6.

---

## 1. Mission

Make free-play *look* as close to Rocket League freeplay as the licence allows,
and make the physics legible while doing it — **without touching a sim code
path** and **without regressing a single physics gate**.

Two outcomes, in priority order:

1. **Legibility.** Anything the physics does that is currently invisible or hard
   to read — contact surfaces, pad state, ball height, car orientation,
   supersonic state, demo timing — gets a debug-instrument-quality visual. This
   is the part with objective value.
2. **Look.** Silhouette, palette, materials, lighting and feedback FX pulled
   toward RL's look, built only from code and licence-clean sources.

The visual pass never blocks, gates, or delays physics, and is **never evidence
for physics**.

---

## 2. Definition of done — what "visual" means here

There is **no asset parity with RL and there never will be** (Rule 1: no Psyonix
assets). So "1:1" means three weaker, checkable tiers:

| Tier | Meaning | Evidence |
|---|---|---|
| **Number-exact** | Every visual number that has a real source equals it: arena shell dims, goal mouth, pad/orb radii, ball radius, camera defaults + ranges. | `docs/VISUAL_NOTES.md` / `docs/CAMERA_NOTES.md` values with citations; asserted by gates. |
| **Recipe-exact** | Colours, proportions and FX recipes ported from a permitted reference **as numbers**, each cited. | `BallIndicatorMesh`-style constants in `docs/VISUAL_NOTES.md` with provenance. |
| **Coverage-complete** | Every visible element is named in the parity ledger as `MEASURED` (cited) or `TUNED`/`UNVERIFIED` (reason given), and is covered by a screenshot + pixel assertion. | `docs/VISUAL_PARITY_LEDGER.md` + `VG*` gate PASS lines. |

Completion bar for the whole pass:

- [x] `docs/VISUAL_PARITY_LEDGER.md` exists and every row is `MEASURED`,
      `TUNED` or `UNVERIFIED` — no blank cells, no unlabelled guesses.
- [x] Every ledger row has a gate or an explicit "not gated, here's why".
- [x] `tools/verify/visual_gates.py` runs green offscreen and SKIPs loudly
      (never fakes a PASS) when it cannot capture. (VG1-VG12: 12 PASS, 0 FAIL,
      0 SKIP.)
- [x] Triangle/draw-call budget is **measured**, not on paper, and the Fennec
      overage is either reduced or justified in writing. (187,986 tris / 253
      draws with two Fennecs; justified in the ledger §Budget and held by
      `TRI_CAP = 240000` in VG7.)
- [x] Physics regression evidence quoted: `measured.py` 19/19,
      `mesh_gates.py` 13/13, `arena_test` 46/46, `make tests` PASS, and a soak
      showing `drops=0` (`--soak 120`, `ticks=14396 drops=0 glerr=0`).
- [x] `AGENTS.md`, `CONTRACTS.md`, `docs/VISUAL_NOTES.md`, `docs/CAMERA_NOTES.md`,
      `docs/AUDIO_NOTES.md` and the owning `BUILD_NOTES.md`
      (`src/render/BUILD_NOTES.md`) reflect the new state.

---

## 3. Non-goals and the scope fence

**Do not spend effort on** beauty for its own sake: screenshot prettiness,
marketing shots, crowd, stadium props, decal libraries, menu restyling, sound
quality, or "make it look next-gen". This is a physics replica with a
licence-clean look, not a shipping game.

**Explicitly NOT visual tasks** (they live in `docs/CROSS_CHECK.md` §5 feature
parity or in physics — do not sneak them in as "visual"):

- Goal-explosion **car blast impulse** — that is physics/feature parity.
  The *shockwave ring geometry* is visual; the impulse is not.
- Interactive ball-launcher UI — feature parity.
- Wall/ceiling **flip-reset detection** — needs sim state; it is a physics
  accessor plus a feature, not a drawing task.
- Match clock / overtime / kickoff countdown — feature parity.
- Multiplayer — out of scope for a single-process sim.

**Renderer-backend policy (owner decision point).** The renderer is GL 2.1
fixed-function and stays that way by default. If an effect is genuinely
impossible in fixed-function, record the limitation in the ledger as
`UNVERIFIED (GL2.1 limit)` — do not silently migrate to GL 3.x/shaders. A
backend migration needs an explicit owner decision plus a `VG*` gate proving no
physics/pacing regression.

---

## 4. Ground truth, references and authority

Authority order, never inverted:

1. **Measured arena/camera/game numbers** — the values already landed and
   gated (`VISUAL_NOTES.md`, `CAMERA_NOTES.md`). Changing one of these is a
   regression, not a preference.
2. **Permitted references, ported as numbers only**:
   - the user's own RL screenshots (observe proportions/palette; never extract
     or trace assets),
   - `RocketLeague2` (BrowserRL) render recipes — numbers only,
   - `rl-opus` / `CarBaller` visual recipes — numbers only, and their
     departures stay documented in `docs/CROSS_CHECK.md`.
3. **Our own judgement** for anything unmeasurable — allowed, but it must be
   labelled `TUNED` in the ledger with the reason, never presented as measured.

Never copy a texture, mesh, decal, sound, or UV layout from the game.
Never claim a look is "1:1" — claim a **number** is equal, or a **recipe** is
ported, and cite where from.

---

## 5. Hard constraints (breaking one is a failed round)

1. **No Psyonix assets, ever.** No game meshes, textures, decals, sounds,
   dumps, screenshots-as-assets, or traced geometry. The only external geometry
   is `assets/models/fennec.bin` (CC-BY-4.0, Jako/fairlight51) — attribution is
   mandatory and already wired into the startup log + `docs/THIRD_PARTY.md`.
   Any new dependency goes in `docs/THIRD_PARTY.md` before it is used.
2. **Visuals never alter a sim code path.** Render reads `Snapshot` only. If a
   visual needs sim data that does not exist, add a **read-only accessor to
   `Sim.h`** — that file is `src/sim`-owned, so it needs a failing test first
   (Rule 1) and a note in `CONTRACTS.md`. Never couple the renderer to sim
   internals, never recompute physics in the renderer.
3. **The physics gates are the regression bar.** `tools/verify/measured.py`
   (19/19), `tools/verify/mesh_gates.py` (13/13), `arena_test` (46/46),
   `make tests`, `verify_vs_rocketsim.py` (110/110) must stay green, and the
   soak must keep `drops=0`. Never edit a physics gate, gate value, or frozen
   tape (`tools/tas/tapes/` is FROZEN v1.0.0) to make a visual change pass.
4. **Renderer may repeat a frame; physics may not skip a tick.** Do not add a
   blocking wait, a synchronous readback, or a shader compile in the frame loop.
   Frame pacing is measured (`Renderer::Stats`, `render_pacing.log`); 30-min
   soak `p99 <= 17.5 ms`, currently 2.00 ms — do not spend that headroom
   recklessly.
5. **Capture must be deterministic and offscreen.** Same seed + tick + camera
   must produce the same pixels. Any procedural noise/randomness must be
   seed-pinned (RocketSim's global RNG is physics-owned — do not consume it).
   Run captures offscreen: `gamescope --backend headless` (no `Xvfb` here, no
   `sudo` password; see §13). Never hijack the user's display for a gate.
6. **Budget, measured.** <50k tris is the on-paper budget; the Fennec alone is
   58851 verts / 84823 tris — a known overage. Add real counters
   (tris drawn, draw calls, GL errors) and hold the line: a visual change may
   not silently multiply geometry. Prefer drawing fewer, better things.
7. **INI schema v1 is normative.** New visual knobs (e.g. wall opacity) extend
   `config/settings.ini` per `docs/INI_SCHEMA.md` and keep the roundtrip test
   green. Never silently diverge from the schema.
8. **Owner boundaries** per `CONTRACTS.md`: `src/render|camera|fx|models|audio`
   are presentation; `src/settings` + `src/input` own config/keys;
   `src/game/main.cpp` is coordinator; `tools/verify` + `tools/tas` own the
   harness. No cross-owner rewrite without a failing test first.
9. **Audio stays procedural.** No samples, no recordings, no assets.
10. **Every landed change updates its owning `BUILD_NOTES.md`**, and every round
    appends to `docs/VISUAL_NOTES.md` (append-only, never rewrite history).

---

## 6. Verified baseline (do not re-derive; extend)

Already landed and documented in `docs/VISUAL_NOTES.md`:

- **Fennec body**: CC-BY-4.0 community mesh, VBO path, bake transform
  `(x, −z, y−15.5)` (GLB is already RL units, +X nose), wheels touch ground at
  the RocketSim rest height 17; fallback box car if the asset is missing.
- **Ball**: procedural equirect hex-panel texture 512x256; visual radius 92.75
  (collision 91.25).
- **Ball ground indicator**: outer ring 0.98r..1.25r + inner disc 0.62r, fade
  and +18% growth with height to ~1950 uu.
- **Pads/orbs**: plate radii big ~116 / small ~66, emissive gold trim, halo,
  orb 54/28, respawn dim + spin-up growth driven by the real cooldowns.
- **Glass walls**: shell ±4096 / ±5120 / ceiling 2044, corner planes
  `|x|+|y| = 8064`, 420 uu hex cells on the inner surface.
- **Curbs**: fillet sweeps side 256 / back 160 / corners 208, ceiling lip band.
- **Goals**: mouth dims ±892.755 / 642.775, team trim, posts, crossbar.
- **Boost gauge**: RL-style circular dial, orange segmented 260° arc.
- **Camera 1:1**: FOV 110 (60–130), distance 270 (100–400), height 100 (40–200),
  angle −3 (−30–15), stiffness 0.45 (0–1), ballcam true; live-reloadable.

Debug/capture hooks that already exist — use them, do not reinvent:

| Hook | What it does |
|---|---|
| `--cam eye...,target...` | fixed camera for reproducible shots |
| `--shot-at-tick N --shot-path P` | one screenshot at a tick (P6 PPM) |
| `--shot-every N --shot-dir D` | frame sequence for a filmstrip |
| `--calibrate` | writes `calibrate.ppm`: 5 colour bands + text + 3D probe quad |
| `--flat` | unlit path (debug/weak-GPU shading) |
| `F` / `--cam` | free camera for walking the shell |
| `RL_TEXDBG` / `RL_NOTEX` | dump the uploaded texture / render untextured |
| `F12` | screenshot key |
| `build/render_stub` | renderer-only harness (spinning box, camera, FX) |

---

## 7. Workstreams

### W1 — Evidence harness first (P0, blocks everything else)

You cannot prove a visual change without deterministic pixels. Land, in order:

- A signed screenshot step: `build/game --seed S --ticks N --cam ... --shot-at-tick N --shot-path P`
  under `gamescope --backend headless`, with the driver string recorded
  (`GL_VENDOR`/`GL_RENDERER`, as the soak log does).
- `tools/verify/visual_gates.py` with `VG*` gates that assert on **measurable
  framebuffer properties**, not vibes:
  - `VG1` capture works + deterministic (two captures byte-identical; if driver
    variance forbids byte-equality, sample N probes with a documented tolerance
    and record the driver).
  - `VG2` `--calibrate` bands land in the right order at the right colours.
  - `VG3` scene coverage: kickoff wide, ball close, pad close, wall/ceiling,
    car close — each non-empty and not all-one-colour.
  - `VG4` ball ground indicator radii in pixels match the 0.98/1.25/0.62 recipe
    at a known camera height.
  - `VG5` boost gauge arc spans 260° at boost=100 and scales with the value.
  - `VG6` wall alpha and hex cells: sample inner-surface pixels, assert the
    documented alpha/period.
  - `VG7` no GL errors during a scripted 60 s run; counters sane.
- Counters: triangles drawn, draw calls, and any GL error, printed in the soak
  line and asserted by `VG7`. If the renderer cannot report them yet, that is
  W1 work (a render-owned counter, read by the coordinator's log line).

Loud `SKIP:` (never a fake PASS) when no GL context/capture is available.

### W2 — Budget and counters (P0)

Make the budget real. Report tris/draw calls per scene; decide the Fennec
overage explicitly (reduce, LOD, or justify in `docs/VISUAL_NOTES.md` with
numbers). No visual change lands without its counter delta in the round report.

### W3 — Car and ball fidelity

Paint/decal look that is team-agnostic and licence-clean, wheel visuals with
steer + spin driven from the snapshot (yaw, speed — no new sim coupling),
ball decal treatment, and correct scale checks against the collision radii.
`TUNED` unless a real number exists.

### W4 — Environment, palette and lighting

Arena turf/markings palette, wall/curb/goal consistency, hex-net period, and a
deliberate lighting model (single directional + ambient today; document the
choice). Any per-map palette work waits until a mesh is present, and must not
depend on `collision_meshes/` existing.

### W5 — Feedback FX

Goal shockwave ring geometry, supersonic wheel embers, ball trail polish,
demo/pickup feedback, and camera shake **as an overlay that does not change the
1:1 camera defaults or break capture determinism** (shake must be disableable
for gates, e.g. off by default in capture mode).

### W6 — Instrumentation for physics legibility

`--debug-colliders`-class wireframes of Bullet shapes and the loaded mesh,
contact point/normal readout, trajectory trails. These are allowed by the
physics brief as instruments and are the most valuable visual work here. Read
only — see constraint 2.

---

## 8. Execution order and milestones

Each milestone ends with a green verify run and a written report. Do not start a
later milestone while an earlier one is red.

- [x] **V1 — Evidence harness.** `visual_gates.py` + deterministic offscreen
  capture + counters. *Nothing else can be proven without this.*
- [x] **V2 — Budget.** Measured tris/draw calls; Fennec overage decision recorded
  (`VG7`).
- [x] **V3 — Car/ball fidelity** (extend `VG4`, `VG5` as needed).
- [x] **V4 — Environment/palette/lighting** (`VG2`, `VG6`).
- [x] **V5 — Feedback FX** (a new `VG*` gate per effect; each effect must be
  capturable).
- [x] **V6 — Close-out.** Parity ledger complete, docs updated, `docs/VISUAL_VERDICT.md`
  written one page, physics regression re-proven on the final revision.

---

## 9. Evidence and reporting contract

Every round produces a report containing:

1. **What changed** — files, one line each, with why.
2. **Raw command output** — the capture command, screenshot paths, `VG*` gate
   PASS lines, tri/draw counters, pacing p50/p99, and `GL_VENDOR`.
3. **Ledger delta** — rows added/changed with `MEASURED` / `TUNED` /
   `UNVERIFIED` and the citation or reason.
4. **Physics regression** — `measured.py` summary line, `mesh_gates.py` summary
   line, `arena_test` count, test names, and the soak `drops=0` line with its
   tick count.
5. **Regressions** — any previously green gate (physics or visual) now red,
   with the diff.
6. **Blockers** — concrete, with the exact command that reproduces.
7. **Next round's first step.**

Rules for claims:

- A visual claim needs a screenshot path **and** a pixel assertion. "Looks
  better" is not a claim.
- Anything not measured is `TUNED` or `UNVERIFIED` — say which one, and why.
- Never present a screenshot as physics evidence, and never present a physics
  gate as visual evidence.
- Never report a SKIP as a PASS.

Durable memory:

- `docs/VISUAL_NOTES.md` — append a round entry every round; never rewrite
  history.
- `docs/VISUAL_PARITY_LEDGER.md` — the single source of truth for element
  status. Update it in the same round that changes an element.

---

## 10. Stop conditions and escalation

Stop and report (do not improvise a workaround) when:

- A look can only be matched by copying a Psyonix asset or tracing game
  geometry. That is a hard stop, not a challenge.
- A visual needs sim state that does not exist, and the read-only accessor
  would alter physics behaviour or touch a gated value.
- An effect is impossible in GL 2.1 fixed-function. Record the limitation as
  `UNVERIFIED (GL2.1 limit)`; do not migrate the renderer backend unilaterally.
- A visual change can only go green by editing a physics gate, a frozen tape, or
  a measured value.

Escalate by writing the blocker into `docs/VISUAL_NOTES.md` with the reproducing
command, then report. Do not widen scope into features or physics to dodge a
blocker.

---

## 11. First 60 minutes — bootstrap

1. `make all`, then confirm the regression bar on this exact revision:
   `measured.py` 19/19, `mesh_gates.py` 13/13, `/tmp/arena_test` 46/46,
   `make tests` PASS, `verify_vs_rocketsim.py` 110/110. **Any red = stop**; the
   baseline is broken and that is the first thing to report.
2. Capture the current reference set offscreen (§13): kickoff wide, ball close,
   pad close, wall, car close. Keep them in `/tmp/visual_ref/` so later rounds
   can diff against them.
3. Run `--calibrate`; confirm `calibrate.ppm` is written and the five bands are
   in the documented order (turf, wall, carblue, goalorange, white).
4. Read `src/render/Renderer.{h,cpp}`, `src/fx/FX.{h,cpp}`,
   `src/models/Models.*`, `src/models/Fennec.cpp`, `src/camera/Camera.cpp`, and
   the draw path in `src/game/main.cpp` (where `Snapshot` is filled and where
   the shot hooks fire).
5. Create `docs/VISUAL_PARITY_LEDGER.md`, seeded with every element in §6, each
   row `MEASURED` (with citation), `TUNED`, or `UNVERIFIED` (with reason). This
   ledger is the instrument for the rest of the pass.
6. Land V1 (capture harness + counters). Where it needs a flag in another
   owner's file, write the failing test first (Rule 1). Report V1 status plus
   the triangle-budget question.

---

## 12. Quick reference — files that matter

| Path | Role in this brief |
|---|---|
| `MASTER_PROMPT_VISUALS.md` | this brief |
| `docs/VISUAL_NOTES.md` | landed visual inventory + "Next candidates"; append-only round log |
| `docs/VISUAL_PARITY_LEDGER.md` (new) | element → `MEASURED`/`TUNED`/`UNVERIFIED` + gate |
| `docs/VISUAL_VERDICT.md` (new, V6) | one-page close-out |
| `docs/CAMERA_NOTES.md` | 1:1 camera values, ranges, known approximations |
| `docs/AUDIO_NOTES.md` | procedural audio state |
| `docs/THIRD_PARTY.md` | every dependency + the mandatory CC-BY-4.0 attribution |
| `docs/CROSS_CHECK.md` §5 | feature-gap scan (feature parity, **not** visual scope) |
| `docs/PHYSICS_VERDICT.md` | frozen physics verdict; regression bar |
| `src/render/Renderer.{h,cpp}` + `Font.h` | the only draw API; `Snapshot` definition |
| `src/render/stub_main.cpp` | renderer-only harness (`build/render_stub`) |
| `src/fx/FX.{h,cpp}` | goal/demo/pad/trail FX + ball & pad indicators |
| `src/models/Models.{h,cpp}`, `Fennec.{h,cpp}` | ball/pad geometry, Fennec loader |
| `src/camera/Camera.{h,cpp}` | 1:1 camera + freecam |
| `src/audio/Audio.{h,cpp}` | procedural synth |
| `src/game/main.cpp` | coordinator: `Snapshot` fill, HUD, capture/calibrate hooks |
| `tools/verify/visual_gates.py` (new) | `VG*` gates, deterministic capture, loud SKIP |
| `config/settings.ini` + `docs/INI_SCHEMA.md` | normative config; new knobs go here |

---

## 13. Offscreen capture recipe (reusable, no display takeover)

```sh
cd ~/Projects/RocketLeagueLaptop
gamescope --backend headless -W 1280 -H 720 -r 60 -- \
  ./build/game --seed 7 --ticks 600 --no-vsync --no-audio \
      --meshdir collision_meshes \
      --cam 3000,-3000,1200,0,0,200 \
      --shot-at-tick 300 --shot-path /tmp/visual_ref/kickoff.ppm
```

- `Xvfb` is not installed here and `sudo` needs a password; `gamescope`
  (installed) has a headless backend that opens **no window** and no DRM
  output. The renderer is `SDL_WINDOW_SHOWN`, so this is the only
  non-intrusive way to capture.
- Record `GL_VENDOR` / `GL_RENDERER` with every capture set (the soak log does
  this already). Byte-compare captures from the same revision and driver; if
  the driver forbids byte-equality, probe N pixels with a documented tolerance
  and record the tolerance in the gate.
- Filmstrips for FX motion: `--shot-every N --shot-dir D`.
- Palette/geometry probe: `--calibrate` writes `calibrate.ppm` and exits.

---

## 14. The one-sentence brief

Make free-play look like RL's freeplay using only code and licence-clean
sources, make the physics legible while you do it, and prove every visual claim
with a deterministic screenshot plus a pixel assertion — never by touching the
sim, the physics gates, or someone else's art.
