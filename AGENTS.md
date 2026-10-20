# AGENTS.md — RocketLeagueLaptop

Agent guide for this repo. Read this before touching anything.

**Physics work order: `MASTER_PROMPT_PHYSICS.md` — CLOSED at M6 (2026-10-10).**
Physics is gated 1:1; see `docs/PHYSICS_VERDICT.md`. Do not re-run M1–M6. Keep
the physics gates green as the regression bar.

**Visual REGRESSION work order: `MASTER_PROMPT_VISUAL_FIXES.md` — CLOSED
(2026-10-10).**
The V6 pass claimed a clean visual state but a human playtest found the scene
visibly broken; the fixes pass resolved every ranked bug in
`docs/VISUAL_BUGS_TODO.md` (V1–V8) and hardened the gates. The record is
`docs/VISUAL_FIXES_LOG.md`, the close-out is the top of
`docs/VISUAL_VERDICT.md`, and `tools/verify/visual_gates.py` now runs **VG1–VG16
(16 PASS/0 FAIL/0 SKIP)**. Key roots: the Fennec converter composed
column-major matrices as row-major (buried + sheared car) and put translation
into normals; the renderer used `GL_RESCALE_NORMAL` against non-uniformly
scaled boxes so lit walls were grey; the debug grid was in the play path.
**V9 (`render_pacing.log` in the CWD) remains open**; so do the `UNVERIFIED`
items in the verdict. V6 below is superseded, not deleted.

**Visual work order: `MASTER_PROMPT_VISUALS.md` — CLOSED at V6 (2026-10-10).**
The visual pass landed V1–V6; the record is `docs/VISUAL_VERDICT.md`, the
element-by-element status is `docs/VISUAL_PARITY_LEDGER.md`, the harness is
`tools/verify/visual_gates.py` (VG1–VG16, 16 PASS/0 FAIL/0 SKIP). It kept the
physics constraints: no Psyonix assets ever, visuals never touch a sim code
path, and every physics gate stays green.

## What this is

A free-play Rocket League replica in C++20 (SDL2 + GL 2.1, g++ only, no
cmake). The physics core is the **vendored RocketSim**
(`third_party/RocketSim`, MIT © 2022 ZealanL, commit `c2baacb8` —
replay-validated), wrapped by a deterministic 120 Hz `Sim` API. Visuals are
license-clean recreations (procedural textures + one CC-BY-4.0 community
Fennec mesh) — **no Psyonix assets ever**.

Goal: 1:1 replication of RL freeplay — physics first, then visuals/features.

## Non-negotiable rules

1. **No Psyonix assets in the repo — user-local runtime inputs allowed.**
   Nothing Psyonix-owned is ever vendored, committed, or redistributed: no
   game meshes, textures, sounds, or dumps in the tree, ever. The shipped,
   gated visuals stay 100% clean-room (procedural + the CC-BY-4.0 Fennec).
   What IS allowed, following the existing `--meshdir` precedent: the game
   may load extracted assets at runtime from the user's OWN install and
   machine only (collision dumps today; stadium/car/texture packs tomorrow
   via an asset-dir flag), gitignored like `collision_meshes/`, never
   committed, never re-uploaded. Rationale (2026-10-17): RLViser-style
   accuracy comes from real assets; this machine can use them locally while
   the repository stays MIT-clean and redistributable. Gates always run the
   clean-room path. Every dependency logged in `docs/THIRD_PARTY.md`.
2. **RocketSim stays pristine.** Wrapper-only changes in `src/sim/Sim.cpp`.
   Anything sim-engine-level needs a failing contract test first.
3. **Physics determinism.** 120 Hz fixed tick, never drops a tick;
   `-ffp-contract=off -fno-fast-math -fno-strict-aliasing` on all sim-path
   compiles (see Makefile — do not remove). All runs seed-pinned (`--seed`);
   RocketSim's global RNG is pinned in `Sim::Init`/`SetSeed`.
4. **Constants cite sources.** Every physics number cites
   `third_party/RocketSim/src/...` file+line (see `src/physics/Tuning.h`,
   `docs/CROSS_CHECK.md`). Single-sourced constants are flagged UNVERIFIED.
5. **Gates are RocketSim truth.** Feel/work-order changes never modify gate
   values; gates only update when a MEASURED RocketSim run disagrees (see
   `docs/number_gates.md` G3/G4 history — community wiki numbers lost,
   measured RocketSim won).
6. **Owner boundaries** per `CONTRACTS.md` v2: `src/sim` (sim core),
   `src/arena` (geometry/fallback containment + pads), `src/physics`
   (presets/tuning), `src/render|camera|fx|models|audio` (presentation),
   `src/settings`+`src/input` (config + keys, normative INI schema v1),
   `tools/tas`+`tools/verify` (harness/gates), `src/game/main.cpp` +
   `config/` (coordinator). No cross-owner rewrites without a failing test.
7. **MIT-only** (CC-BY-4.0 for the Fennec mesh, attribution required).

## Build & verify (run these before claiming anything works)

```sh
make all                              # headless_sim, render_stub, game, tests, arena_test
python3 tools/verify/measured.py      # the gate runner — must stay >= 19/19 PASS
python3 tools/verify/mesh_gates.py    # mesh collision gates — 13/13 (SKIPs loudly with no mesh)
make tests                            # settings roundtrip + keyboard/menu
/tmp/arena_test                       # 46/46 analytic arena checks (built by make all)
python3 src/physics/verify_vs_rocketsim.py   # preset/tuning tables vs vendored source
python3 tools/verify/visual_gates.py  # visual gates VG1-VG16 (offscreen; loud SKIP)
cat tools/verify/mesh_integrity.py   # (read) Fennec asset integrity: VG13's backend
```

- `build/headless_sim` — deterministic CLI sim:
  `--tape CSV --ticks N --seed S --dump F [--body car0] [--boost X]
  [--place x,y,yawdeg] [--padlog F] [--demolog F]`, prints `CHECKSUM <hex16>`.
  Dump schema: `tick,x,y,z,vx,vy,vz,speed,heading,fx,fy,fz,ux,uy,uz,boost`.
- `build/game` — the playable free-play binary:
  `--bot stationary|mirror|slow-chase --soak SECS --ticks N --seed S
  --no-vsync --no-audio --flat --menu --shot-at-tick N --shot-path P
  --shot-every N --shot-dir D --cam eye...,target...`.
- Frozen 12-gate tapes live in `tools/tas/tapes/` (spec:
  `tools/tas/TAPE_FORMAT.md`, FROZEN v1.0.0 — do not mutate existing tapes).
- Perf gate: 30-min `--soak 1800` must show physics `drops=0`. `StepAccumulator`
  keeps a render-hitch backlog instead of discarding it (only a >1 s backlog is
  dropped), so hitches no longer cost ticks. **CLOSED — D2 PASS (Round 6):**
  measured `drops=0` over the full mesh run — `ticks=215996` (119.998 Hz),
  `frames=1133336`, `wall=1800.0s`, p50/p99 2.00 ms. Run it offscreen with
  `gamescope --backend headless` (no `Xvfb` here); recipe in
  `docs/PHYSICS_1TO1_LOG.md` Round 6 / HANDOFF.

## Key facts (do not re-derive)

- Authority chain: vendored RocketSim > our MEASURED runs > other open clones
  (`docs/CROSS_CHECK.md`: rl-opus departures like boost cap 2160 and
  curvature ×1.18 were deliberately NOT adopted; ours stays 2300 / stock).
- MEASURED truths (all PASS in `measured.py`): throttle cap 1410.06, boost cap
  2300 exact, supersonic 2200 (maintain semantics to 2100), held jump rise
  2.15 m, double jump 4.71 m, turns 2.28 @1292 / 1.96 @2299 uu/s, powerslide
  3.28 @477, front flip 323.9°, pads 100/12 @ 10 s/4 s, demo on SUPERSONIC
  flag at new contact + 360-tick (3.0 s) respawn, subsonic contact = bump
  only. Signed inputs: steer+1 LEFT, pitch+1 UP, yaw+1 left, roll+1 LEFT.
- **Arena status: both modes live.** With `--meshdir` (a real user dump) the sim
  runs RocketSim's own mesh geometry and all mesh gates pass; without it, the A2
  analytic Soccar shell (walls/fillets/goal mouths, `kPads[34]`,
  analytic containment `ContainFallbackBodies`, Sim-owned pads) — 46/46
  geometry checks pass, and pads/cooldowns are gated PASS, but wall/ceiling/
  mesh-level bounce behavior is analytic, not RocketSim-mesh. `Sim::
  usingRealSoccar()` reports which mode you got.
- Renderer is GL 2.1 fixed-function at 720p60 vsync, **key+fill+ambient
  lighting with `GL_NORMALIZE`** (not `GL_RESCALE_NORMAL`: `DrawBox` scales
  non-uniformly, which left lit boxes with unusable normals). The old on-paper
  <50k tri budget is superseded by a MEASURED one: two Fennecs = 187,982 tris /
  268 draws, procedural box car = 18,412 tris, and `visual_gates.py` VG7 fails above
  `TRI_CAP = 240000`. The 84,823-triangle Fennec is clean (0 degenerate / 0
  duplicate tris), so the overage is justified in writing, not decimated — see
  `docs/VISUAL_PARITY_LEDGER.md` §Budget. Visual details in
  `docs/VISUAL_NOTES.md`; camera 1:1 values in `docs/CAMERA_NOTES.md`
  (FOV 110, dist 270, h 100, angle −3, stiffness 0.45, live-reloadable).
- INI schema v1 (`config/settings.ini`, `docs/INI_SCHEMA.md`) is normative —
  A5 may extend it, never silently diverge. Roundtrip test must stay green.

## Repo map

```
AGENTS.md, CONTRACTS.md      ← you are here / ownership + rules
MASTER_PROMPT_*.md           ← standing work orders (all three CLOSED: PHYSICS M6, VISUALS V6, VISUAL_FIXES)
docs/                        ← gates, cross-checks, camera/visual/audio notes, visual parity ledger + verdict, playtest + blind-feel scripts
src/sim/Sim.{h,cpp}          ← RocketSim wrapper (the only file that may touch vendored headers)
src/arena/                   ← analytic shell + fallback containment + pads + arena_test
src/physics/                 ← 7 car presets + Tuning constexprs (RocketSim-cited) + PhysicsAPI.md
src/{render,camera,fx,models,audio,menu,input,settings}/  ← presentation + config layers
src/game/main.cpp            ← the free-play loop (kickoffs, HUD, soak harness, screenshot hooks)
tools/headless_sim/          ← deterministic CLI (gates run through it)
tools/verify/measured.py     ← gate runner (19/19 = the bar); mesh_gates.py = mesh gates (13/13); visual_gates.py = visual gates (VG1-VG16); mesh_integrity.py = Fennec asset integrity (VG13)
docs/PHYSICS_VERDICT.md      ← M6 close-out verdict: what is 1:1, what is UNVERIFIED
tools/tas/                   ← tape format + replay/drift tooling + frozen tapes
tools/soak/                  ← soak plan/analysis
third_party/RocketSim/       ← vendored physics authority (pristine) + libsrc/bullet3-3.24
assets/models/fennec.bin     ← CC-BY-4.0 community Fennec mesh (license beside it)
```

## Physics 1:1 work order — mesh mode (CLOSED at M6, kept for reference)

`MASTER_PROMPT_PHYSICS.md` was the work order for this phase; it is **closed at
M6** and kept for reference (its §2 checklist is marked, evidence in
`docs/PHYSICS_1TO1_LOG.md` Rounds 1–7). Mesh plumbing landed as follows:

- `--meshdir DIR` on `build/game` and `build/headless_sim`, plus
  `[arena] mesh_dir` in `config/settings.ini` (`docs/INI_SCHEMA.md`). A mesh dir
  that loads no Soccar mesh is a hard error, never a silent fallback.
- `make meshgen` writes a deterministic **clean-room** Soccar collision mesh to
  `collision_meshes/soccar/` (our own geometry, RocketSim `.cmf` format; never
  committed — `.gitignore`). See `docs/MESH_SETUP.md` for the legal split and
  the fidelity ledger. `Sim::meshMode()` reports `cleanroom-mesh` vs
  `dumped-mesh` (a user-supplied RLArenaCollisionDumper dump).
- `tools/verify/mesh_gates.py` runs the C1–C7 mesh collision gates and SKIPs
  loudly when no mesh is present.
- A **real dump is now in place** (`~/Games/Heroic/scripts/dump-collision-meshes.sh`
  — Linux/Proton instructions in `docs/MESH_SETUP.md`; the dumper needs a
  `wtsapi32.dll` shim because Wine's `WTSEnumerateProcessesA` is a stub).
  `--meshreport` shows `mode=dumped-mesh hashMatched=1 shapes=16 tris=8020`.
- Round state and raw output are in `docs/PHYSICS_1TO1_LOG.md`.
  **Mesh gates: 13 PASS / 0 FAIL** on the real arena (was 8/3 on the
  clean-room mesh).
- **Inputs (M4)** have real SDL_GameController support
  (`src/input/Gamepad.{h,cpp}`): radial deadzone, sign-correct stick mapping,
  rebindable buttons, hot-plug, additive with the keyboard. Gated by
  `tests/test_gamepad.cpp` (`make tests` -> `GAMEPAD PASS`) and I1–I3 in
  `tools/verify/measured.py` (now **19 PASS, 0 FAIL**). All gamepad buttons and
  the stick deadzone are rebindable in the existing menu (section
  "INPUT — GAMEPAD"); `deadzone` default is UNVERIFIED.

## Current state (verified this session, 2026-10-10)

- `python3 tools/verify/measured.py` → **19 PASS, 0 FAIL** (G1–G10, I1–I3
  input gates, zero-divergence TAS).
- `python3 tools/verify/mesh_gates.py` → **13 PASS, 0 FAIL** on the real dump.
- `python3 tools/verify/visual_gates.py` → **VG1–VG16: 16 PASS, 0 FAIL, 0 SKIP**
  offscreen (`gamescope --backend headless`, NVIDIA 615.71.09). VG1 captures are
  byte-identical; VG7 60 s run `ticks=7200 drops=0 glerr=0 tris_last=187982`;
  VG4 measures the ball-indicator recipe to <1 px; VG6 measures the 420 uu hex
  row spacing; VG8 matches the shockwave curve; VG9 shows the car silhouette
  rotating 21.3 deg under held pitch; VG13–VG16 gate the Fennec asset, turf
  occupancy, lighting sanity and the Fennec render path.
- `make tests` → ROUNDTRIP + KEYBOARD+MENU + GAMEPAD PASS.
- arena_test → **46/46 PASS**. Binaries build clean from source.
- `build/game --soak 1800` (mesh, offscreen via gamescope headless) →
  `ticks=215996 drops=0 frames=1133336 wall=1800.0s p50=2.00ms p99=2.00ms`
  — D2 PASS, M5 complete (Round 6).
- **M6 close-out done (Round 7):** clean rebuild + every gate re-verified on the
  rebuilt binaries — `measured.py` 19/19, `mesh_gates.py` 13/13,
  `verify_vs_rocketsim.py` 110/110, `make tests` all PASS, `arena_test` 46/46,
  10k-tick mesh determinism `7dd64cccedbb9a1f` byte-identical, fallback checksum
  `73db10410bd8a8ad` unchanged. §2 acceptance checklist marked; verdict in
  `docs/PHYSICS_VERDICT.md`.
- Working tree state: binaries in `build/`, git history has **no commits yet**
  (initial-commit housekeeping pending).

## Open work (priority order for new agents)

1. **Visuals are CLOSED (V1–V6, then the V1–V8 regression fixes).**
   `docs/VISUAL_FIXES_LOG.md` is the latest record, the top of
   `docs/VISUAL_VERDICT.md` is the close-out, `docs/VISUAL_PARITY_LEDGER.md`
   classifies every element, and `tools/verify/visual_gates.py` (VG1–VG16) is
   the regression bar for the look. The regression pass fixed the Fennec
   converter (matrix composition, normals, mirror, datum), the debug grid in
   the play path, the lighting model + `GL_NORMALIZE`, the arena palette and
   the unlit pads. Landed: deterministic offscreen capture + counters, a measured
   budget with a `TRI_CAP` gate, full car orientation + procedural wheel
   steer/spin, fixed blend/curb/hex-net/boost-gauge bugs, goal shockwave ring,
   demo/pickup FX, camera shake, `--debug-colliders`. Remaining visual gaps are
   listed as `UNVERIFIED` in the ledger (Fennec baked wheels/paint,
   mesh+contact wireframes, per-map palettes) — do not re-run V1–V6.
   **Still open from `docs/VISUAL_BUGS_TODO.md`: V9**, `render_pacing.log` is
   written to the CWD on every pacing print (`Renderer::EndFrame`).
2. **Physics is closed — M1–M6 complete.** The work order is done: mesh
   geometry, inputs, collision gates, determinism/perf and close-out are all
   evidenced (`docs/PHYSICS_VERDICT.md`, `docs/PHYSICS_1TO1_LOG.md` Rounds 5–7).
   Do not re-run M1–M6. The physics gates are now the regression bar: keep them
   green. Note the G1 fallback-vs-mesh friction caveat in
   `src/sim/BUILD_NOTES.md` still applies, and `UNVERIFIED` items stay listed in
   the verdict §3.
3. **Feature parity**: goal-explosion car blast impulse, interactive ball
   launcher UI, wall/ceiling flip-reset indicator, transition/swivel camera
   sliders, camera shake/ball arrow, match clock/countdown (candidate list
   with provenance in `docs/CROSS_CHECK.md` §5).
4. **Audio**: procedural placeholders only — no doppler, no distance
   attenuation, no crowd bed (`docs/AUDIO_NOTES.md`).
5. **Process debts**: no git history (make the initial commit), G5/G6/G7 stub
   table in `number_gates.md` still shows old wrong expectations next to
   MEASURED. (The Round 15 soak-wording debt is closed: Round 6 measured
   `drops=0` with no warmup exclusion, satisfying SOAK_PLAN's literal letter.)

## Handoff

- **The physics work order is CLOSED (M1–M6).** D1/D3/D4 are evidenced in Round 5,
  D2 in Round 6, and the close-out (clean rebuild + all gates re-verified, §2
  checklist marked, verdict written) in Round 7 of `docs/PHYSICS_1TO1_LOG.md`.
  Do not redo any of them. `docs/PHYSICS_VERDICT.md` records exactly what is 1:1
  and what remains `UNVERIFIED`.
- **Next up is product work**, in this order: (1) feature parity
  (`docs/CROSS_CHECK.md` §5), (2) audio (`docs/AUDIO_NOTES.md`), (3) the
  `UNVERIFIED` visual gaps in `docs/VISUAL_PARITY_LEDGER.md` (Fennec
  wheels/paint; `--debug-colliders` mesh + contact wireframes need a read-only
  `Sim` accessor + a failing Rule-1 test), (4) process debts (initial git
  commit; G5/G6/G7 stub rows in `docs/number_gates.md`; `render_pacing.log`
  still lands in the CWD — VISUAL_BUGS_TODO V9).
- Current state, all green: `measured.py` 19/19, `mesh_gates.py` 13/13,
  `verify_vs_rocketsim.py` 110/110, `make tests` all PASS, `arena_test` 46/46,
  `visual_gates.py` VG1–VG16 16/16, 30-min mesh soak `drops=0` plus the VG7
  60 s visual soak `drops=0 glerr=0`. The HANDOFF section at the end of
  `docs/PHYSICS_1TO1_LOG.md` has the offscreen soak recipe.

## Agent etiquette

- Verify before you claim: run the gate runner + relevant tests, and quote
  actual output (checksums, PASS lines) in your notes.
- Every landed change updates the owning `BUILD_NOTES.md`/doc file (Rule 2).
- Feel/juice changes go through `docs/PLAYTEST_CHECKLIST.md` /
  `docs/blind_feel.md` scripts; they never touch `tools/verify/gates*`.
- Cross-owner fixes require a failing test that reproduces the bug first
  (Rule 1 — log the fix in `CONTRACTS.md`).
