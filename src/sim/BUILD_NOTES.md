# A1 SIM CORE — build notes Round 5 (MIT)

## What landed
- `Sim::SetCarPreset(idx, name)` implemented (was header-only): remove + re-add
  with `ConfigFromPreset(Physics::FindPreset)` so Bullet bakes the right
  hitbox/vehicle. Mutating `config` in place would NOT change collisions.
- `Sim::AddCar` default fixed: was `CarConfig()` (ZERO hitbox — Vec() is all
  zeros) → now `ConfigFromPreset(Physics::kDefaultPreset)` = Octane, matching
  `Arena::AddCar`'s `CAR_CONFIG_OCTANE` default. Cf.
  `third_party/RocketSim/src/Sim/Car/CarConfig/CarConfig.cpp:20-82`.
- Kickoff facing fixed: was Blue yaw 0 / Orange yaw π (both face SIDE walls —
  `Angle::GetForwardVec(0)=(1,0,0)`). Now Blue +π/2 → +y, Orange −π/2, mirroring
  `RLConst.h:356-364` back-spawn yaw. Verified: tick-0 forward = (0,1,0).
- `Sim::GetCarForward` / `GetCarUp` (pure state functions) + `Sim::SetBoost`
  (test-harness tank prefill; gameplay never uses it — fallback floor has no
  pickup pads).
- `tools/headless_sim` rewritten to TAPE_FORMAT v1.0.0: strict CSV parser
  (exact header, `#` before-header-only, sequential ticks, float clamp count,
  0/1 ints), `--tape/--ticks/--dump/--seed` + optional `--body/--boost`,
  dump `tick,x,y,z,vx,vy,vz,speed,heading,fx,fy,fz,ux,uy,uz` (CSV) or packed
  LE int32+12×f32 (.bin), `CHECKSUM <hex16>` FNV-1a-64 over dump bytes.
- Root `Makefile` (g++ only): `build/librocketsim.a` (all RocketSim src +
  Bullet, `-ffp-contract=off -fno-fast-math`), `build/headless_sim`,
  `build/render_stub`, `tests`, `tapes`, `verify`.

## MEASURED (flat-floor fallback, seed 1 unless noted)
- Zero-divergence: 600/720/10 000-tick reruns byte-identical (`cmp` + replay
  `compare` 0.000000). 10k CHECKSUM `58d2311b3007ceb1` (seed 7).
- G1 throttle-only: 1410.06 uu/s (`straight_throttle_600`, last 60 ticks).
- G2 boost cap: 2300.0 exactly (hard clamp, `boost_top_1200 --boost 100`).
- G8 supersonic: first ≥2200 at tick 190 of boost run.
- G3 held single jump: apex z 231.6, rise 214.6 uu = **2.15 m**.
- G4 optimal double jump: rise 471.2 uu = **4.71 m** (hold 24 ticks, 1-tick
  gap, re-press; 2nd-press hold adds nothing — edge-triggered, no hold bonus).
- Turn: no-slide 2.28 rad/s @1292 uu/s; 1.98 @1980; powerslide 3.28 @477.
- Front flip (dodge, pitch at 2nd press): 323.9° total pitch rotation.
- Signs (facing-level): steer+1 LEFT, pitch+1 nose UP, yaw+1 left,
  roll+1 roll-LEFT (up tips −x facing +y). Keyboard Q/E + HB mapping fixed to
  match; `tests/test_keyboard_menu.cpp` updated.
- Endurance: 216 000 ticks (30 sim-min) in 11.4 s wall, drops=0 (headless
  drives Step120 directly — game-loop soak still OPEN, needs game binary).

## Known limitations
- Fallback floor: throttle-only equilibrium on real Soccar mesh must re-confirm
  G1 (drive curves identical; tire/ground friction differs). Boost-coast above
  1410 shows near-zero drag on the plane (coasts 1617→1421) — mesh will differ.
- G3/G4/G5/G6/G7 STUB expectations were analytic guesses; MEASURED RocketSim
  truth differs (2.15/4.71/2.28/3.28/324°) — gate doc needs revision to truth.
- G9 pads: no pads on fallback (Arena meshes not vendored) — OPEN, blocked on
  A2 arena integration.
- 30-min wall-clock soak (p99 ≤17.5 ms, physics-drops=0): OPEN, needs game loop.

## Round 6–7: fallback pads, containment, game binary
- Fallback pad league: Sim-owned 34 pads (Soccar layout, positions verbatim
  RLConst.h:274-312, big-first order matching A2 kPads). Rule transcribed from
  BoostPad.cpp:62-110 (cylinder radii 144/208, height 95, amounts 100/12,
  cooldowns from MutatorConfig 10/4 s). Ticks inside Step120 only when
  !realSoccar; RocketSim owns pads on mesh arenas (Arena.cpp:704
  hasArenaStuff). Deterministic (fixed car/pad order, no RNG).
- Fallback containment (ContainFallbackBodies, !realSoccar only): A2 analytic
  CollideSphere keeps ball+cars in the 1:1 shell. Ball reflects with mutator
  ballWorldRestitution (floor contacts push-out only — Bullet owns the floor);
  cars skip floor contacts (rest z=17 < probe 50 by design) and get push-out +
  plastic wall (e=0.1). Goal mouth/net drivable per A2; IsGoal latches,
  Sim::PollGoal() drains it; game triggers FX::OnGoal + center reset + log.
- A2 fixes (failing arena_test, coordinator-noted): floor-fillet arc spanned
  [-90°,+90°] instead of [-180°,-90°] (walls reported 333 uu short — now
  exact); CollideSphere 1 mm tangent epsilon; test-side tangency points
  (y=5120) + pad[29]/[30] order vs RLConst.h:298-299. arena_test: 46/46 PASS.
- Headless: `boost` dump column, `--padlog` (G9), `--place x,y,yawdeg`
  (test teleport; Teleport clobbers boost → prefill applies after).
  Sim::Teleport/ResetCarToSpawn/ResetBall/GetBoost/IsBoosting/NumPads/GetPad.
- `build/game`: free-play loop (menu/keys→sim→camera/render/FX/pads/goals),
  bot opponent, `--bot/--soak/--ticks/--seed/--no-vsync`. Cameras 1:1 (see
  CAMERA_NOTES.md): ball-cam eye on car→ball axis, RL default stiffness 0.45,
  RL menu ranges, live settings sync, F freecam (dolly/orbit/height), B/C
  toggles, F1-F5/R/T resets, HUD input echo + BALLCAM/CARCAM tag.
- Soak (llvmpipe, Xvfb): 60 s run — 6552 ticks, drops=0, p50 16.0 ms,
  p99 18.0 ms, avg 16.49. p99 gate (≤17.5) marginal on software GL;
  physics-drops gate PASS. Human playtest traffic observed on the X display
  (inputs + window close) — not a sim/input bug (event probe clean).

## Round 8: on-screen menu/HUD, screenshots, measured.py, visual pass
- Renderer: 5x7 bitmap font (`src/render/Font.h`, authored, MIT),
  `DrawText2D`/`FillRect2D` screen-space overlay, `SaveScreenshot` (P6 PPM).
  Game: on-screen menu panel (760px, selection highlight), persistent HUD
  line (BALLCAM/CARCAM SPD/BST/TICK), F12 + `--shot-at-tick N --shot-path P`
  + `--menu` verification hooks. Screenshots verified by pixels (menu text
  bright-pixel delta, HUD bar) and by eye: turf, markings, team cars, ball.
- Visual 1:1 pass from screenshots: team-tinted car bodies (blue/orange),
  green mowed-stripe turf, midfield line + center circle (r=600 approx, TBC),
  white posts/crossbar, brighter walls (0.30 alpha). Menu panel widened.
- `tools/verify/measured.py`: 12/12 PASS (adds G7 flip 323.9° via frozen
  `flip_forward_240.csv`). `gates_measured.json` written beside it.

## Round 9: audio + soak instrumentation
- Procedural audio (`src/audio/Audio.*`, game-owned): engine hum by speed,
  boost hiss, jump/pickup/goal one-shots; `M` mute, `--no-audio`; never fatal.
  Probe + dummy-driver game run green (215 callbacks/5 s @44.1 kHz).
- Game: SDL_QUIT now logs its source line (soak forensics); HUD unchanged.
- Soak display lesson: private Xvfb :99 is pure softpipe (35 fps, drops!) —
  formal soak must run on :0 (radeonsi 60 fps, drops=0). Three early exits on
  :0 so far (8/54/103 s) with no in-process cause found (event probe clean);
  prime suspect is window management on the shared desktop. Relaunched with
  quit logging to confirm.

## Round 9b: soak hardening (in game/main.cpp)
- `--soak` ignores SDL_QUIT (logs it): four early exits on the shared :0
  desktop were all external window-close, never crashes. Interactive runs
  still quit on close.
- Warmup accounting: drops before tick 600 (one-time GL-context stall)
  reported separately; the gate counts steady-state drops.
- 30-min soak relaunched with both; progress in /tmp/soak30c.log.

## Round 15: soak-method correction (to implement post-soak)
- SOAK_PLAN.md says drops>0 at ANY point = FAIL; the current warmup exclusion
  (tick-600 baseline) conflicts with its letter. Correct fix (not yet built):
  resync lastMs after the first 3 presented frames so GL-init stall never
  enters the accumulator — zero startup drops honestly, no exclusion. The
  running 30-min formal uses the exclusion build; its steady-state drops=0
  stands regardless (warmup line timestamps the boundary).

## Round 33: formal soak verdict + rebuild
- 30-min soak: 216000 ticks / drops=0 / 1800.0 s wall, p50 16.00, p99 19.00.
  Formal physics-drops gate PASS (this run predates the clock-resync but had
  zero startup drops, so the exclusion was moot).
- Post-soak rebuild (`make all` now covers game+arena_test): clock-resync
  (no warmup exclusion), keyboard live-rebind sync, proportional bot steer,
  full --help, menu panel/HUD/screenshot all in. Smoke: drops=0 total, no
  warmup line. Unit tests + arena 46/46 + measured.py 14/14 green.

## Round 34: demolition surfacing (RocketSim-owned trigger, wrapper-only change)
- RocketSim already demolished victims on supersonic contact
  (`Arena.cpp _BtCallback_OnCarCarCollision`, `DemoMode::NORMAL` = attacker
  SUPERSONIC flag at new contact, `enableTeamDemos=false`) and auto-respawned
  after `RLConst::DEMO_RESPAWN_TIME` = 3.0 s — our wrapper just never exposed
  it. Added (wrapper only, zero vendored edits):
  `SetCarBumpCallback` → `Impl::demoQueue` (events stamped with tick during
  Step120; deterministic), `Sim::PollDemo`, `Sim::IsSupersonic` (flag with
  maintain semantics, RLConst.h:68-76), `Sim::IsDemoed`.
- headless: `--demolog P` (CSV `tick,bumper,victim`).
- MEASURED (seed 1, fallback floor): supersonic contact demos at tick 318,
  victim frozen 359 ticks, respawn tick 678 = +360 exactly; respawn position
  from the respawn slot pick (RL randomizes too). Subsonic contact (~2150)
  bumps with zero events — and a push that rises to supersonic *while still
  touching* never demos (new-contact callback only; matches car-soccer.com
  per rl-like's measurements).
- **Determinism fix**: RocketSim's global RNG (`Math::GetRandEngine`,
  thread-local) seeds from wall-clock ms + thread-id hash, and Car demo
  respawn consumes it — respawn slots drifted run-to-run (caught: 3 identical
  runs then divergence; back-to-back runs often share the same ms). Fix
  (wrapper-only, vendor pristine): seed it in `Sim::Init` and reseed on every
  `Sim::SetSeed`. 5 reruns byte-identical (`7a58e155d54071f8`); seed 7 differs
  by design (different respawn slot).
- measured.py 16/16 (G10 + G10b). Cross-check vs rl-opus/rl-like/CarBaller/
  car-soccer.com in `docs/CROSS_CHECK.md`.

## Round 1 (physics 1:1 work order) — mesh mode plumbing

- `Sim::CreateSoccar(meshDir)` is now actually reachable: `--meshdir DIR` on
  `build/game` + `build/headless_sim`, and `[arena] mesh_dir` in
  `config/settings.ini` (INI schema v1 extension; roundtrip test green). A
  meshdir that loads no Soccar mesh is a hard failure (clear message, non-zero
  exit) — never a silent fallback.
- New read-only API: `Sim::MeshMode` (`kMeshFallback` / `kMeshCleanroom` /
  `kMeshDumped`), `meshMode()`, `MeshReport`/`GetMeshReport()`,
  `arenaDescription()`, and `SetBall()` (test harness). Mesh hash is recomputed
  with RocketSim's own `UpdateHash()` algorithm and compared against the 16
  known SOCCAR hashes (`RocketSim.cpp:20-28`) to distinguish a dumped Psyonix
  mesh from our clean-room one.
- Goal scoring on mesh arenas was **dead**: `Sim` registered
  `SetCarBumpCallback` but never `SetGoalScoreCallback`, so `PollGoal()` could
  never fire in SOCCAR mode. Now wired, with rising-edge latching because
  RocketSim's callback fires every scored tick while `PollGoal()` is
  once-per-goal. Fallback checksum unchanged (`73db10410bd8a8ad`).
- `getAabb()` pre-pass over loaded meshes retained: it makes
  `btRSBroadphase`'s mesh cell registration deterministic. Measured to change
  nothing about contact outcome; not the fix for the open defects.
- Open defects (mesh back-wall fillet at low z does not contain bodies;
  `IsBallScored` ignores x) and all raw evidence: `docs/PHYSICS_1TO1_LOG.md`.
  Mesh gates: `tools/verify/mesh_gates.py` (8 PASS / 3 FAIL).


## Round 4 — real Soccar dump; back-wall defect resolved

- A real `RLArenaCollisionDumper` dump is staged in `collision_meshes/soccar/`
  (16 meshes, 8020 tris, all hashes in RocketSim's known SOCCAR set). Procedure
  and the Wine `WTSEnumerateProcessesA` shim: `docs/MESH_SETUP.md`.
- `hashMatched` is now computed from the `.cmf` files, not from loaded Bullet
  shapes. `CollisionMeshFile::UpdateHash()` is order-dependent over `tris`, and
  a `btBvhTriangleMeshShape` reorders/deduplicates triangles, so a shape-based
  replay can never match. `DirectoryHasKnownSoccarMesh()` parses each file with
  RocketSim's own reader and compares `hash`.
- `meshMode()` therefore reports `kMeshDumped` for a real dump and
  `kMeshCleanroom` for `make meshgen` output.
- The Round 1–3 back-wall defect (bodies escaping at `|x| in [892.8,2944]`) was
  a property of the generated clean-room geometry, not of RocketSim or this
  wrapper. With the real mesh, `tools/verify/mesh_gates.py` is **11 PASS / 0
  FAIL** (C3b max y 5027.5, C2b 0 goals, C4a contained).


## Round 5 — M5: mesh determinism/perf/checksums + a real drop bug

- **D1** mesh determinism: 10k-tick mesh runs byte-identical
  (`7dd64cccedbb9a1f`), `replay.py compare` drift 0.000000; 3-way + collision-heavy
  tape + packed `.bin` all identical.
- **D3** mesh perf: load ~99 ms (16 shapes / 8020 tris); per-tick 0.018 ms vs
  fallback 0.006 ms (3.0x, but only 0.2% of the 8.33 ms tick budget).
- **D4** mesh checksums recorded (wall-contact tape `2bd44e5e8ede6e48`); the
  frozen fallback value `73db10410bd8a8ad` is untouched. Note the first ~100
  ticks of `straight_boost_600` are identical in both modes because no mesh
  contact has occurred yet.
- **BUG FIXED — `StepAccumulator` discarded the tick backlog.** It ran at most
  `kMaxStepsPerFrame` ticks then threw away the remainder, so a 60 ms render
  hitch lost 2 ticks (`drops>0`, a gate violation). It now *keeps* the leftover
  for later calls: catch-up is 16 ticks/frame and only a backlog above
  `kMaxBacklogSteps = 120` ticks (1 s of sim) is dropped, which preserves the
  spiral-of-death guard. Measured `drops=0` over a 180 s mesh soak (21597
  ticks). Determinism and the frozen fallback checksum are unchanged.
- 30-min `--soak 1800` still outstanding (needs a real window; no Xvfb here).


## Round 6 — M5 CLOSED: D2 30-minute soak PASS (mesh)

- **D2**: `build/game --soak 1800 --ticks 240000 --seed 7 --no-vsync --no-audio
  --meshdir collision_meshes`, run offscreen via the gamescope headless backend
  (`Xvfb` is not installed here and `sudo` needs a password), mesh arena
  (`arena=dumped-mesh shapes=16 tris=8020 hashMatched=1`):

  ```
  [game] exit ticks=215996 drops=0 frames=1133336 wall=1800.0s p50=2.00ms p99=2.00ms avg=1.58ms unlimited=0 bot=stationary audio_cb=0 audio_frames=0
  ```

  119.998 Hz sustained for 30 minutes; 900/900 drop samples read `drops=0` and
  the log contains no nonzero `drops=` token at all. Frame p99 2.00 ms, so the
  `SOAK_PLAN.md` §5 llvmpipe render waiver was not needed.
- The 4-tick shortfall vs 216000 is the documented clock-resync boundary
  (`src/game/main.cpp:860-865` re-anchors `lastMs` after the first 3 presented
  frames, while `wall` is measured from before GL init) — not a skipped tick.
  Skips are exactly what `drops` counts, and it reads 0 from the first HUD
  sample (tick 204) through exit. `SOAK_PLAN.md`'s literal "`phys_drops > 0` at
  any point = FAIL" is therefore satisfied with **no** warmup exclusion
  (the Round 15 wording item is closed).
- The Round 5 `StepAccumulator` backlog fix is what makes this achievable; the
  Round 5 line "30-min `--soak 1800` still outstanding" is now closed by this
  entry. Do not revert the fix.
- M5 is complete (D1/D3/D4 in Round 5, D2 here). Next physics step is M6
  close-out (`MASTER_PROMPT_PHYSICS.md` §11). Raw log: `/tmp/soak_30min_mesh.log`;
  full round record: `docs/PHYSICS_1TO1_LOG.md` Round 6.


## Round 7 — M6 close-out (no code change)

- Clean rebuild `make clean && make all` -> exit 0 on the current source, so the
  gate evidence below is provably against these binaries. Sim-path compile
  lines still carry `-ffp-contract=off -fno-fast-math -fno-strict-aliasing`.
- Re-verified after the rebuild: `measured.py` 19 PASS / 0 FAIL;
  `mesh_gates.py` 13 PASS / 0 FAIL; `verify_vs_rocketsim.py` 110 checks ALL
  PASSED; `make tests` ROUNDTRIP + KEYBOARD+MENU + GAMEPAD PASS; `arena_test`
  46/46.
- Mesh identity and determinism re-confirmed: `MESH mode=dumped-mesh shapes=16
  tris=8020 hashMatched=1 pads=34`, `arena=soccar-mesh`; two 10k-tick mesh runs
  `CHECKSUM 7dd64cccedbb9a1f`, `cmp` byte-identical, `replay.py compare`
  `ZERO-DIVERGENCE PASS (tol=0.0)`. Frozen fallback checksum unchanged
  (`73db10410bd8a8ad`).
- The physics work order is closed (M1–M6). Verdict: `docs/PHYSICS_VERDICT.md`;
  round record: `docs/PHYSICS_1TO1_LOG.md` Round 7. Do not re-run M1–M6.
