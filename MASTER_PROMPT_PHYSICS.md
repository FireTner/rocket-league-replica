# MASTER PROMPT — Finish Physics to 1:1 (experimental visuals)

This is the standing work order for this repo. Hand it to an agent as its
complete brief. Its **sole goal is finishing physics**. Visuals are explicitly
experimental and must never block, gate, or delay physics work.

Read `AGENTS.md` first, then this file, then `CONTRACTS.md`.

---

## 1. Mission

Take the free-play build from "RocketSim-validated physics core on an analytic
fallback arena" to **finished 1:1 physics**: the same collisions, the same
input semantics, the same tick-for-tick behavior as Rocket League freeplay
when driven through the vendored RocketSim authority.

Two things are P0 and must be *exactly* 1:1, not "close":

- **Collisions** — every car/ball/world/pad/goal contact resolved by
  RocketSim's own collision geometry and Bullet pipeline, not by analytic
  stand-ins.
- **Inputs** — every device input maps to RocketSim `CarControls` with
  identical semantics and identical signs, including analog curves, deadzones,
  dodge thresholds, and edge/hold timing.

Visuals are a throwaway debug surface for physics work. You may draw
colliders, contact normals, mesh wireframes, and on-screen telemetry. You may
not polish art, and visual screenshots are never acceptance evidence.

---

## 2. Definition of done — what "1:1" means here

Three tiers, all required:

| Tier | Meaning | Evidence |
|---|---|---|
| **Constant-exact** | Every physics number equals the vendored RocketSim source value (already largely true). | `verify_vs_rocketsim.py` PASS; citations in `Tuning.h`. |
| **Geometry-exact** | Collision surfaces are the actual Soccar collision mesh loaded through RocketSim, not the analytic shell. | `Sim::usingRealSoccar() == true`; mesh-mode gate run. |
| **Semantic-exact** | For the same physical device state and tick sequence, the sim produces the same `CarControls` and the same contact outcomes as RL/RocketSim. | Input gates + contact gates + frozen tapes, all PASS, byte-identical reruns. |

Acceptance checklist (all must be true to call physics "finished"):

**Status: CLOSED — M6, 2026-10-10.** Every box below is checked, with the raw
command output recorded in Round 7 of `docs/PHYSICS_1TO1_LOG.md` and summarised
in the one-page verdict `docs/PHYSICS_VERDICT.md`. Claims stay
per-gated-behavior: anything not measured is still marked `UNVERIFIED`.

- [x] `--meshdir` (or equivalent) loads a Soccar collision mesh and
      `usingRealSoccar()` reports true, with pads, goals, walls, ceiling,
      ramps and corner geometry all owned by RocketSim.
      → `MESH mode=dumped-mesh shapes=16 tris=8020 verts=4468 hashMatched=1
      pads=34`, `arena=soccar-mesh`; mesh gates C1/C2/C3/C4/C6/C7 PASS.
- [x] All existing gates still PASS: `python3 tools/verify/measured.py`
      → 16/16 (or more, never fewer).
      → **19 PASS, 0 FAIL**.
- [x] New mesh-mode gates PASS: containment, wall/ceiling/ramp contact,
      goal-mouth detection, pad pickup/respawn, demo trigger — all on mesh.
      → **13 PASS, 0 FAIL** (`python3 tools/verify/mesh_gates.py`).
- [x] New input gates PASS: sign table, deadzone curve, dodge threshold,
      jump-hold/double-jump/flip windows, powerslide and air-roll modifiers.
      → I1–I3 PASS in `measured.py`; `tests/test_gamepad.cpp` (radial deadzone,
      sign table, hot-plug) PASS; `tests/test_keyboard_menu.cpp` (signs,
      air-roll/powerslide modifiers, rebinding) PASS.
- [x] Fallback mode still builds and still passes its own gates (it is the
      no-mesh development path; do not delete it).
      → clean rebuild exit 0; `measured.py` runs on fallback (19/19); frozen
      checksum `73db10410bd8a8ad` unchanged; `/tmp/arena_test` 46/46.
- [x] Determinism holds with meshes: identical `--seed` runs are
      byte-identical (`cmp` + `replay.py compare`, drift 0.000000).
      → two 10k-tick mesh runs `CHECKSUM 7dd64cccedbb9a1f`, `cmp` identical,
      matching sha256, `ZERO-DIVERGENCE PASS (tol=0.0)`.
- [x] `--soak` shows physics `drops=0` with mesh collisions loaded.
      → `ticks=215996 drops=0 frames=1133336 wall=1800.0s p99=2.00ms`
      (Round 6).
- [x] Every number landed cites file+line in the vendored source, or is
      flagged `UNVERIFIED` with the reason.
      → `verify_vs_rocketsim.py`: 110 checks, ALL PASSED. Open UNVERIFIED
      items, each with its reason: gamepad `deadzone` 0.08
      (`docs/INI_SCHEMA.md`), `CEIL_RADIUS` 256 single-source
      (`src/arena/ArenaConstants.h`), and the inferred ceiling-fillet /
      goal-recess rows in `docs/MESH_SETUP.md`.
- [x] `AGENTS.md`, `CONTRACTS.md`, and the owning `BUILD_NOTES.md` reflect the
      new state.
      → updated in M6; `src/sim`, `src/physics` and `src/input` BUILD_NOTES all
      carry the close-out entry.

---

## 3. Non-goals and the visuals policy

Do **not** spend effort on: art assets, lighting/shadows, decals, car paint,
stadium props, crowd, UI polish, menu restyling, sound quality, or screenshot
beauty. Visuals are experimental scaffolding.

Visual work is allowed **only** when it is a physics instrument:

- `--debug-colliders` wireframe of Bullet shapes and the loaded mesh.
- Contact point/normal/impulse on-screen readout, paused-frame inspection.
- Trajectory trails for ball/car over N ticks.
- Freecam already exists (`F`, `--cam`) — use it, don't extend it.

Rule: no visual change may alter a sim code path. Render reads snapshots only.
If a visual experiment needs sim data that doesn't exist, add a read-only
accessor to `Sim.h` instead of coupling the renderer to internals.

---

## 4. Ground truth and authority

Authority order, never inverted:

1. **Vendored RocketSim** `third_party/RocketSim` @ `c2baacb8` (MIT, pristine)
   — constants, mechanics, collision geometry, Bullet pipeline.
2. **Our MEASURED runs** through `build/headless_sim` (frozen tapes,
   checksums).
3. Other open clones — `docs/CROSS_CHECK.md`; used only to corroborate, and
   their departures are documented as NOT adopted.

Never adopt a community/wiki number over a vendored-source or measured value.
If RocketSim and the wiki disagree, RocketSim wins and the wiki number is
recorded as a rejected candidate.

---

## 5. Hard constraints (breaking one is a failed round)

1. **No Psyonix assets in the repo.** Collision mesh dumps (`.cmf`, from
   RLArenaCollisionDumper) are the **user's** runtime input: loaded, never
   vendored, never committed, never redistributed. `docs/THIRD_PARTY.md`
   updated if the loading path needs a new dependency.
2. **RocketSim stays pristine.** All integration lives in `src/sim/Sim.cpp`
   and `Sim.h`. If a genuine engine-level fix is unavoidable, it needs a
   failing contract test first and must be documented as a deliberate
   deviation in `CONTRACTS.md`.
3. **Determinism flags unchanged**: `-ffp-contract=off -fno-fast-math
   -fno-strict-aliasing` on all sim-path compiles.
4. **120 Hz fixed tick never drops.** Renderer may repeat a frame; physics may
   not skip a tick. `StepAccumulator` guard behavior unchanged.
5. **Seed pinning.** Any new RocketSim global state (RNG, thread-local,
   statics, mesh hash) must be seed-pinned or proven irrelevant to sim output.
6. **Owner boundaries** per `CONTRACTS.md`: `src/sim` / `src/arena` /
   `src/physics` / input+settings / tools. No cross-owner rewrites without a
   failing test first (Rule 1).
7. **Gates are the bar.** Never edit a gate to make it pass. A gate value
   changes only when a new MEASURED run proves the old expectation wrong, with
   the evidence and the reason recorded.
8. **Keep both modes alive.** Mesh mode is the target; fallback mode remains a
   first-class development path and must keep its own green gates.

---

## 6. Verified baseline (do not re-derive; re-confirm only)

Current, this session:

- `python3 tools/verify/measured.py` → **16 PASS, 0 FAIL**.
- `make tests` → KEYBOARD+MENU PASS, settings roundtrip PASS.
- `arena_test` → **46/46 PASS**.

Already 1:1 (measured truths — keep them intact):

- Tick 120 Hz; gravity 650; throttle cap 1410.06; boost cap 2300 exact.
- Supersonic 2200 start, maintain to 2100 (flag semantics, not raw speed).
- Held jump rise 2.15 m; double jump 4.71 m.
- Turn 2.28 rad/s @ 1292 uu/s; 1.96 @ 2299; powerslide 3.28 @ 477.
- Front flip 323.9°.
- Pads: big 100 / small 12, respawn 10 s / 4 s (Sim-owned fallback rule today).
- Demo: SUPERSONIC flag at new contact, 360-tick (3.0 s) respawn; subsonic
  contact bumps only.
- Input signs: `steer+1 = LEFT`, `pitch+1 = nose UP`, `yaw+1 = left`,
  `roll+1 = roll-LEFT`, verbatim passthrough with zero negation.

Known gaps this brief exists to close:

- **G1. No mesh arena in practice.** `Sim::CreateSoccar(meshDir)` supports it,
  but `src/game/main.cpp:149` and `tools/headless_sim/main.cpp:323` call
  `CreateSoccar()` with no directory, so the build always runs the analytic
  fallback. There is no `--meshdir` flag and no settings key.
- **G2. Collisions are analytic in fallback.** Walls/fillets/goal mouths are
  A2 shapes and `ContainFallbackBodies` hand-rolls push-out; pads are a
  transcribed Sim-owned rule. Real wall/ceiling/ramp/corner contact behavior
  is unproven.
- **G3. Inputs are keyboard-only and digital.** No SDL game controller code
  exists anywhere in `src`. Analog curves, controller deadzone, dodge
  threshold, and the full jump/flip timing ladder are not gated.
- **G4. No contact-level gates.** Nothing asserts contact normal, restitution,
  or impulse on car-wall / car-car / car-ball beyond top-speed and demo
  outcomes.

---

## 7. Workstream A — Geometry-exact collisions (P0)

Goal: RocketSim's Soccar mesh arena is the collision world; the analytic shell
becomes a development-only fallback.

A1. **Wire the mesh directory through every entry point.**
   - Add `--meshdir DIR` to `build/game` and `build/headless_sim`; pass it to
     `Sim::CreateSoccar(dir)`.
   - Add a settings key (extend INI schema v1 per `docs/INI_SCHEMA.md`;
     never silently diverge). Default to `./collision_meshes/` (RocketSim's
     `COLLISION_MESH_BASE_PATH`) or empty.
   - Keep the existing `InitRocketSimOnce` once-per-process semantics;
     document that the first Sim wins because RocketSim init is global.
   - Fail loudly (clear message, non-zero exit for headless) when a meshdir is
     given but no Soccar shapes load — never silently fall back.

A2. **Prove mesh mode is actually mesh.** Add a headless assertion/report:
   `usingRealSoccar() == true`, mesh triangle count, `NumPads() == 34`,
   RocketSim owning pads/containment. `--meshdir` run must never touch
   `ContainFallbackBodies` or the Sim-owned pad tick.

A3. **Remove fallback interference when mesh is present**, without deleting
   the fallback: floor plane, `ContainFallbackBodies`, and Sim-owned pads all
   stay gated behind `!realSoccar`. Confirm with a test that flips the flag.

A4. **Acquire and document the mesh.** Write `docs/MESH_SETUP.md`: how the user
   generates `.cmf` dumps with RLArenaCollisionDumper, where to place them,
   the exact `--meshdir` invocation, and the legal note (Psyonix-owned,
   runtime-only, never committed). A `.gitignore` entry must prevent accidental
   commits of `collision_meshes/`.

A5. **Fallback parity documentation.** Where fallback and mesh differ, record
   it (G1 friction/coast caveat in `src/sim/BUILD_NOTES.md` is the known
   example). Fallback exists to unblock development; it is never evidence of
   1:1.

---

## 8. Workstream B — Inputs 1:1 (P0)

Goal: a physical device state maps to the same `rl::Controls` → RocketSim
`CarControls` that RL would produce, with identical signs and timing.

B1. **Gamepad support (missing entirely).** Add SDL2 `SDL_GameController`:
   - Left stick → throttle/steer (invert per measured sign table).
   - Face/shoulder buttons → jump/boost/handbrake, plus ballcam toggle.
   - Standard RL-ish default bindings, all rebindable in the existing menu
     (extend the binding model; keep `tests/test_keyboard_menu.cpp` green and
     add gamepad cases).
   - Hot-plug: connect/disconnect at runtime without dropping ticks.
   - Keyboard remains a parallel input source; device selection is additive.

B2. **Analog semantics, cited.** Implement and gate:
   - Stick deadzone (radial, not per-axis) with a configurable value; cite the
     RL default from a licensed reference or mark `UNVERIFIED`.
   - Normalized stick response matching RL's mapping to `[-1, 1]`; no
     accidental squaring/clamping. Document the curve.
   - Trigger/digital handling: boost as held state, jump as edge+hold.

B3. **Control-semantics ladder.** The dodge/flip layer is currently implicit
   in RocketSim; make the *input side* explicit and gate it:
   - Dodge threshold uses the preset `dodgeDeadzone` (`CarConfig.h` default
     `0.5`) — i.e. directional jump input beyond threshold = flip, below =
     dodge-less double jump.
   - Timing windows must equal `RLConst.h`: `JUMP_MIN_TIME` 0.025 s,
     `JUMP_MAX_TIME` 0.2 s, `DOUBLEJUMP_MAX_DELAY` 1.25 s,
     `JUMP_RESET_TIME_PAD` 1/40 s. Cite exact lines in the gate doc.
   - Flip variants: forward/back/side flips, flip-cancel (hold opposite
     pitch), air-roll modifier (handbrake + steer in air), powerslide on
     ground. Each gets a tape and a measurable outcome.
   - No input buffering across ticks; exactly one `SetControls` sample per
     tick; edge detection is explicit and deterministic.

B4. **Input tapes + gates.** Extend `tools/tas/` with tapes that isolate input
   semantics (deadzone sweep, threshold sweep, tap-vs-hold jump ladder,
   flip-cancel, air-roll pairs) and add `I*` gates to the verifier. All strings
   inputs are float-exact; assert the resulting `CarControls` and the physical
   outcome.

B5. **Sign regression must never break.** The frozen `air_*`/`steer_*` tapes
   and the sign table in `Controls.h` are contract. Any inversion is a bug,
   not a preference.

---

## 9. Workstream C — Collision coverage gates (P0)

Goal: every collision class is asserted against RocketSim's own geometry, not
against our analytic shapes.

Add mesh-mode gates (auto-skip with a loud `SKIP: no mesh` marker when
`--meshdir` is absent — never fake a PASS):

- C1 **Ball vs floor/wall/ceiling**: bounce angles, restitution
  (`ARENA_COLLISION_BASE_RESTITUTION`), speed loss per bounce, no tunneling at
  max ball speed (6000).
- C2 **Ball vs goal mouth/net/posts**: counts as goal exactly where the real
  mouth is; post/rim contacts behave as sharp geometry.
- C3 **Car vs wall/ceiling/ramp/corner**: driving up a wall, roof transition,
  corner fillet tangency, no pop/snap; car cannot escape the mesh shell.
- C4 **Car vs car**: bump impulse, supersonic demo trigger (new-contact
  semantics), subsonic sustained push = no demo, teammate exemption.
- C5 **Car vs ball**: hit impulse including the Psyonix curve
  (`Ball::_OnHit`), pop/redirect behavior, no double-impulse on the same
  contact.
- C6 **Pads**: pickup on the real mesh (RocketSim-owned), 100/12 amounts,
  10 s/4 s cooldowns, consumption at 100 boost clamp.
- C7 **Containment/no-escape stress**: long stochastic run with random inputs
  and seed-pinned RNG; assert every body stays inside the mesh and no NaN.

Every gate records: tape, seed, tick window, checksum, tolerance, verdict.

---

## 10. Workstream D — Determinism and performance under physics load

- D1 **Determinism with meshes**: two identical `--seed` mesh runs must be
  byte-identical (`cmp` on dumps, `replay.py compare` drift 0.000000). Include
  a 10k-tick run. If mesh triangle ordering or a hash leaks nondeterminism,
  pin it.
- D2 **Tick integrity**: `--soak` with mesh collisions loaded must report
  physics `drops=0`; renderer repeats frames if needed. Re-check the
  clock-resync path documented in `src/sim/BUILD_NOTES.md` Round 15.
- D3 **Mesh perf**: measure mesh-load time and per-tick cost with the real
  Soccar mesh; record p50/p99 and the triangle count. No tick budget
  regression vs fallback beyond a documented threshold.
- D4 **Cross-run contract**: the frozen checksums for existing tapes are
  fallback-mode values. Mesh mode gets its own checksums recorded in the docs;
  do not overwrite fallback checksums to make mesh numbers fit.

---

## 11. Execution order and milestones

**All six milestones are CLOSED (M6, 2026-10-10).** M1–M4 in
`docs/PHYSICS_1TO1_LOG.md` Rounds 1–4, M5 in Rounds 5–6, M6 in Round 7; the
one-page verdict is `docs/PHYSICS_VERDICT.md`. The list below is kept as the
record of what each milestone required — do not re-run it.

Do these in order; each milestone ends with a green verify run and a written
report. Do not start a later milestone while an earlier one is red.

- **M1 — Mesh plumbing (A1, A2).** `--meshdir` on game + headless, settings
  key, loud failure, mesh-mode report. Fallback still 16/16. *Proves the path
  exists but may still skip mesh gates if no dump is present.*
- **M2 — Mesh acquisition (A4).** `docs/MESH_SETUP.md` + `.gitignore`. **Hard
  prerequisite for all mesh evidence.** If the user cannot supply a dump, stop
  and report the blocker; do not fake mesh gates.
- **M3 — Mesh collision gates (C1–C7).** Land gates first (they will fail on
  fallback), then confirm they PASS on mesh.
- **M4 — Inputs (B1–B5).** Gamepad, analog semantics, timing ladder, tapes,
  `I*` gates. Keyboard + gamepad both green.
- **M5 — Determinism/perf (D1–D4).** Mesh determinism, soak, perf record,
  checksums documented.
- **M6 — Close-out.** Update `AGENTS.md`, `CONTRACTS.md`, owning
  `BUILD_NOTES.md`; mark the acceptance checklist; write the final one-page
  verdict with real command output.

If M2 is blocked, M4 can proceed in parallel (inputs are mesh-independent);
M3 and M5 cannot be honestly completed without a mesh.

---

## 12. Evidence and reporting contract

Every round produces a report containing:

1. **What changed** — files, one line each, with why.
2. **Raw command output** — not summaries. `measured.py` summary line,
   checksums, `arena_test` count, test names.
3. **New gates** — id, tape, seed, window, expected, measured, tolerance,
   verdict, and the RocketSim file+line citation for the expectation.
4. **Regressions** — any previously green gate now red, with the diff.
5. **Blockers** — concrete, with the exact command that reproduces.
6. **Next round's first step.**

Maintain a durable round log at `docs/PHYSICS_1TO1_LOG.md` (append-only):
date, milestone, state of every gate, open blockers. This is the cross-round
memory; never delete history from it.

Rules for claims:

- "1:1" may only be claimed for a specific, gated behavior with RAW evidence.
- Anything not measured is `UNVERIFIED` — say so explicitly.
- Never report a SKIP as a PASS.

---

## 13. Stop conditions and escalation

Stop and report (do not improvise a workaround) when:

- No usable Soccar collision mesh is available after M2 (mesh gates are
  unprovable — this is a legitimate hard blocker).
- A required behavior can only be matched by editing vendored RocketSim
  source beyond the documented pristine policy.
- A gate can only go green by changing an expectation rather than the code.
- Determinism cannot be restored without dropping the tick rate.

Escalate by writing the blocker into `docs/PHYSICS_1TO1_LOG.md` with the
reproducing command, then report. Do not widen scope to features or visuals to
avoid a physics blocker.

---

## 14. First 60 minutes — bootstrap

1. `make all` (confirm clean build), then
   `python3 tools/verify/measured.py` (confirm 16/16),
   `make tests`, `/tmp/arena_test` (46/46).
2. Read `src/sim/Sim.cpp` Init/CreateSoccar (lines ~82–218),
   `src/sim/Sim.h`, `src/arena/ArenaGeometry.*`, `src/input/Controls.h`,
   `src/input/Keyboard.cpp`.
3. Check for a mesh dump: does `./collision_meshes/*.cmf` exist? Does the
   user have a dump path? If not, immediately raise M2 as the critical path.
4. Land M1 plumbing (`--meshdir` on both binaries + settings key + loud
   failure) behind a failing test first where it touches another owner's file.
5. Create `docs/PHYSICS_1TO1_LOG.md` with the baseline table (all current
   gates + current fallback checksums) so every later round diffs against it.
6. Report M1 status and the M2 acquisition question in the round report.

---

## 15. Quick reference — files that matter

| Path | Role in this brief |
|---|---|
| `src/sim/Sim.cpp` / `Sim.h` | Mesh wiring, fallback gating, read-only accessors, mesh-mode report |
| `src/game/main.cpp` | `--meshdir` flag, input device wiring, debug-collider draw calls |
| `tools/headless_sim/main.cpp` | `--meshdir`, mesh-mode headless report, gates |
| `src/input/Controls.h` | Frozen sign contract (do not reorder/rename) |
| `src/input/Keyboard.cpp` | Existing key mapping; gamepad lands beside it |
| `src/settings/Settings.*` + `docs/INI_SCHEMA.md` | meshdir key, deadzone config, bindings |
| `src/physics/Tuning.h` + `CarPresets.h` | Cited constants; `dodgeDeadzone` source of truth |
| `tools/verify/measured.py` | Gate runner — extend here, never weaken |
| `tools/tas/` | New input/collision tapes + `TAPE_FORMAT.md` version bump if needed |
| `third_party/RocketSim/src/RLConst.h` | Jump/flip/tick constants to cite |
| `third_party/RocketSim/src/Sim/Arena/Arena.cpp` | Mesh/shape ownership reference |
| `third_party/RocketSim/src/CollisionMeshFile/` | `.cmf` format + `COLLISION_MESH_BASE_PATH` |
| `docs/MESH_SETUP.md` (new) | How the user produces the dump (M2 deliverable) |
| `docs/PHYSICS_1TO1_LOG.md` (new) | Cross-round durable memory |

---

## 16. The one-sentence brief

Make cars, ball, and world collide exactly as RocketSim's real Soccar mesh
does, and make every input map exactly as RL's does — prove both with
seed-pinned gates and raw output — and let the visuals stay whatever ugly,
experimental debug surface is convenient while you do it.