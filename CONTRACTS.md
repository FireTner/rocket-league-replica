# Interface Contracts (coordinator-owned, v2 — Round 12; physics work order closed at M6, 2026-10-10)

All streams code against these paths. Owner-only writes; others shim + file failing contract test before requesting change.
Rule-1 log (coordinator cross-owner fixes, all with failing tests): Sim zero-hitbox default, kickoff facing, Keyboard roll sign + test, Menu camera ranges, A2 fillet-quadrant/pad-order/tangency (arena_test 31→46 PASS). Camera settings round (A4+A5): shipped ini shipped car-cam/0.5 stiffness (ROUNDTRIP FAIL `camera.ballcam == true`), Settings.cpp clamps wider than Camera.cpp/Menu (dist 500/h 30-250/angle +30), FOV cap 130 vs RL 110 — fixed with the extended roundtrip test + new `tests/test_camera_dynamics.cpp` (7 checks) failing first; added transition_speed/swivel_speed/shake/invert_swivel (schema v1 additive, both directions safe) + Rocket-Science stiffness dynamics; no physics gate, gate value or frozen tape touched. Visual pass (Round 36): the blend-state leak (Renderer.cpp), the 90°-off wall curbs/hex nets and the quadratic boost-gauge arc (main.cpp) and the FX lighting re-enable (FX.cpp) were each reproduced by a FAILING visual gate (VG4/VG6/VG5) before the fix; no physics gate, gate value or frozen tape was touched.

## Layout + owners
- `third_party/RocketSim/` — A1 (MIT ZealanL 2022, record hash; vendor pristine)
- `src/sim/Sim.h` (+`src/sim/BUILD_NOTES.md`) — A1: `CreateSoccar(meshDir)/AddCar/SetControls/Step120`, `SetCarPreset`, `Teleport/ResetCarToSpawn/ResetBall`, `GetBoost/IsBoosting`, `NumPads/GetPad`, `GetCarForward/Up`, `PollGoal`, `PollDemo/IsSupersonic/IsDemoed` (demolitions, RocketSim-owned trigger; global RNG pinned to Sim seed), 120Hz accumulator, `-ffp-contract=off`. Mesh mode: `usingRealSoccar()`, `meshMode()` (`fallback`/`cleanroom-mesh`/`dumped-mesh`), `GetMeshReport()`; a `--meshdir` that loads no Soccar mesh is a hard error, never a silent fallback. Fallback owns pads (transcribed rule) + analytic containment when meshes absent; RocketSim owns them when present.
- `src/arena/ArenaConstants.h` + `ArenaGeometry.{h,cpp}` — A2: constexprs + `Collide/CollideSphere/Raycast/IsGoal/kPads[34]` (big-first, RLConst order). Mesh mode does not consult this shell or `ContainFallbackBodies`; `CEIL_RADIUS` is single-source UNVERIFIED.
- `src/physics/CarPresets.h` + `Tuning.h` + `PhysicsAPI.md` — A3: 7 presets (Octane default), mechanics constexprs with citations (`verify_vs_rocketsim.py`: 110 checks ALL PASSED)
- `src/render/` (+`Font.h`) + `src/camera/` + `src/fx/` + `src/models/` + `src/audio/` — A4/render: `Renderer::Init/BeginFrame/EndFrame/DrawBox/DrawSphere/DrawGroundGrid/DrawText2D/FillRect2D/SaveScreenshot`, `Snapshot`, 720p60 vsync; camera 1:1 (ball-cam on car→ball axis, RL defaults/ranges, F freecam); audio procedural synth (engine/boost/jump/pickup/goal). **Visual pass (Round 36, doc'd in `src/render/BUILD_NOTES.md`):** `Snapshot` gained `carForward[3]`/`carUp[3]`/`carVel[3]` (filled from the EXISTING read-only `GetCarForward/GetCarUp`/`BodySample` - no new sim accessor); new draw API `DrawWireBox/DrawWireSphere/DrawWireCylinder` + `Stats3D()` counters (tris/lines/draws/glErrors, polled once per `EndFrame`) + `LightingOn()`; `BeginFrame` now resets blend/depth/lighting every frame (the 2D helpers save/restore instead of clobbering). The renderer budget bar is now the MEASURED cap `TRI_CAP=240000` asserted by VG7 (was an on-paper <50k).
- `config/settings.ini` + `docs/INI_SCHEMA.md` + `src/input/Controls.h` + `src/input/Gamepad.{h,cpp}` — A5 (NORMATIVE schema v1 + Controls fields; canonical signs MEASURED: steer+1 LEFT, pitch+1 UP, yaw+1 left, roll+1 LEFT). Gamepad: radial deadzone, sign-correct stick mapping, rebindable `pad_*` buttons, hot-plug, additive with the keyboard; `deadzone` default 0.08 is UNVERIFIED.
- `tools/tas/TAPE_FORMAT.md` + `tools/tas/replay.py` + `tools/verify/` + `src/bot/BotStub.h` — A6 (tape spec FROZEN v1.0.0; `measured.py` is the MEASURED gate runner 19/19, `mesh_gates.py` is the mesh gate suite 13/13, `visual_gates.py` is the VG1-VG16 visual suite)
- `src/game/main.cpp` (`build/game`) — coordinator-integrated: free-play loop, `--bot/--soak/--ticks/--seed/--menu/--shot-at-tick/--shot-path/--no-vsync/--no-audio/--meshdir`, F1-F5/R/T/B/C/Esc/F/M/F12 keys. Visual-harness hooks (debug-only, coordinator-owned, no sim path change): `--ballpos x,y,z` (existing `Sim::SetBall`), `--boost N` (existing `Sim::SetBoost`), `--drive t,s,p,y,r,j,b,h` (constant injected Controls, beats freecam parking), `--nofennec`, `--debug-colliders`, `--no-shake`, `--fx-probe goal|demo|pickup`, `--fx-probe-tick N` (fires on the exact tick, so use it in capture mode with `--no-vsync`, where the accumulator lands every tick). FX advance on tick delta, not wall time.

## Tools (all g++, no cmake)
- `make build/headless_sim build/game` — headless CLI: `--tape/--ticks/--dump/--seed [--body/--boost/--padlog/--demolog/--place/--meshdir/--meshreport/--goallog/--ballplace]` + `CHECKSUM <hex16>`; dump schema `tick,x,y,z,vx,vy,vz,speed,heading,fx,fy,fz,ux,uy,uz,boost` (superset of replay compare schema)
- `make tests` (`t_rt`, `t_kb`, `t_gp`), `/tmp/arena_test` (55/55), `python3 tools/verify/measured.py` (19/19 green), `python3 tools/verify/mesh_gates.py` (13/13 on the real dump; loud SKIP with no mesh), `python3 src/physics/verify_vs_rocketsim.py` (110/110)

## Provisional schema v1 (A5 may extend, never silently diverge)
```
[version] schema=1
[camera] fov=110 distance=270 height=100 angle=-3 stiffness=0.45 ballcam=true
[input] throttle=W/S steer=A/D pitch=W/S yaw=A/D roll=Q/E jump=Space boost=Shift handbrake=S ballcam_toggle=C reset_car=R reset_ball=B unlimited=U menu=Esc
[physics] tick=120
[boost] unlimited=false drain=33.33 recharge=10
[arena] soccar=true mesh_dir=""
[game] kickoff_index=0
```
`[input]` also carries the gamepad block (schema v1, rebindable): `deadzone`,
`pad_jump`, `pad_boost`, `pad_handbrake`, `pad_ballcam_toggle`, `pad_reset_car`,
`pad_reset_ball`, `pad_unlimited`, `pad_menu`, `pad_kickoff_0..4`.

## Rules
1. No cross-owner rewrites without a failing contract test.
2. Each stream ships `BUILD_NOTES.md` with exact build commands + real output.
3. Every reused constant cites source file + line; single-source constants flagged UNVERIFIED.
4. MIT/Apache-2.0/CC0 only; every dep logged for `THIRD_PARTY.md`; no Psyonix assets in the repo ever (user-local runtime inputs only, per AGENTS.md Rule 1).
5. Physics 120Hz never drops; renderer may repeat a frame rather than stall physics.
