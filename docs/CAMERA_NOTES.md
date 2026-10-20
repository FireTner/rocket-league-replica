# Camera notes — 1:1 RL replication (MIT)

## Model
Two modes, one shared RL setting set (`[camera]` in `config/settings.ini`,
schema v1, editable live in the menu AND applied every frame — no restart).

- **Ball-cam** (RL default ON): eye sits on the car→ball horizontal axis,
  behind the car at Distance, raised by Height, pitched by Angle; looks at the
  ball through the slewed aim (#1). If the ball is nearly under the car
  (< 5 uu horizontal), the aim holds instead of flipping. The mixed ideal eye
  passes through the stiffness lag (#3) in every mode, ballcam included:
  followRate 1.5+14×stiffness, vertical halved airborne, rig-speed saturation
  caps the boost-run trail at 40 uu, and the swivel's own vertical orbit is
  split off via rig-relative height so jumps/aerials keep the slow linger.
  At rest the follower converges exactly, so static captures are
  pixel-identical to the pre-lag build.
- **Car-cam**: eye behind car facing at Distance/Height/Angle; looks at a
  point 400 uu ahead, 40 uu up. The eye AND target ride the same stiffness
  follower as ballcam (horizontal kx / vertical kz); low-speed recentering
  keeps the stiffness response. Air heading (#2): on the ground the facing is rigid;
  airborne it eases onto the velocity heading (blend 200→900 uu/s, rate
  2.5+7×stiffness), pitch-immune w/ half-turn continuity.
- **Transition**: toggling ballcam blends eye+target between the two rigs at
  `0.5+2.5×transition_speed` units/s (1.2 swings in ~0.29 s).
- **Swivel**: the right stick orbits the rig around the car (yaw) and on
  the rig sphere (pitch) at 90 deg / 1.3 s per swivel unit of 1.0
  (linear in the slider). Pitch clamps asymmetrically at −0.8/+0.7; past
  0.05 s idle the offsets snap back linearly at 2× swivel rate (6 rad/s
  floor) while `snap` is true. `invert_swivel` flips pitch. Keyboard has
  no swivel binding (gap, see below).
- Smoothing (#3): exponential `kx/kz = 1-exp(-rate*dt)` on the mixed ideal
  eye in every mode; `followRate = 1.5 + stiffness*14`, vertical halved in
  air, plus `rigSpeed/40uu` (horizontal) and `|rigVz|/40uu` rig-relative
  (vertical) saturation. Car-cam target rode the same follower before #5;
  since #5 every mode's look target goes through the one-euro filter
  instead — ballcam keeps the 1.2 Hz floor, car-cam the 3.0 Hz floor.
- Dynamics (Rocket Science #2, speed fraction over the G8 2200 threshold):
  `dynamicDist = (1-stiffness)*(100*frac + 14.7 supersonic)`,
  `dynamicAngle = (1-stiffness)*2*frac` (degrees, toward zero),
  `dynamicFov = 5*frac + 5 supersonic`. At rest every delta is exactly zero,
  so static captures are pixel-identical to the pre-dynamics build.

## 1:1 defaults and ranges (RL values)
| Key | Default | Range | Provenance |
|-----|---------|-------|------------|
| fov | 110 | 60–110 | Rocket Science #2 (cone diagram = horizontal), Liquipedia 60..110, CarBaller (horizontal) |
| distance | 270 | 100–400 | v1.74 default per CarBaller; Thespike/ Dot guides |
| height | 100 | 40–200 | guides consensus |
| angle | -3 | -30–15 | Zen -3.0; range kept (CarBaller says -45..0 — conflict, UNVERIFIED) |
| stiffness | 0.45 | 0–1 | guide consensus (Squishy/Thespike/keymap); CarBaller claims 0.5 = v1.74 default — conflict, UNVERIFIED |
| ballcam | true | — | RL default ballcam ON |
| transition_speed | 1.2 | 1–2 | CarBaller RL range; Thespike best 1.2 |
| swivel_speed | 5.0 | 1–10 | CarBaller RL range; Thespike best 5.5 |
| shake | true | — | RL ships with shake ON (CarBaller) |
| invert_swivel | false | — | BakkesModSDK CameraSave field |
| snap | true | — | CarBaller `CameraSettings.snap` (RL "Snap Camera to Default"); INI-only, no menu slider |

FOV slider is treated as horizontal FOV at 16:9 and converted to vertical for
the GL frustum (keeps horizontal extent constant on 16:9).
OPEN PROJECTION QUESTION (not changed): feeding the horizontal slider value
through the converter is an identity at 16:9, so we render 110 deg vertical
(~137 horizontal). If the slider is truly horizontal, vertical should be
~77.5 deg at 16:9 (our own `--calibrate` probe uses 77.0 deg). RL is Hor+
(WSGF). Fixing this re-baselines every absolute visual gate, so it needs an
explicit owner decision + gate re-baseline — see V9-adjacent. Do not "fix"
it inside a camera-settings change.

## Field list source (real, no code copied)
BakkesModSDK `ProfileCameraSettings` = FOV/Height/Pitch/Distance/Stiffness/
SwivelSpeed/TransitionSpeed and `CameraSave` = InvertSwivelPitch/CameraShake
(mirrors Psyonix `FProfileCameraSettings`). `CameraWrapper` confirms the
runtime model: swivel state + die rate + `GetDesiredSwivel(LookUp, LookRight)`
+ `UpdateSwivel`, dynamic FOV offsets (`UpdateFOV`), and a state blender for
transitions. Our INI key names are our own; the field SET is RL's.

## Known approximations (not hidden)
- Ported 2026-10-20 (#6 transition re-time, `docs/carballer-facts/camera.md`
  #6): the toggle blend advances at `0.5+2.5×transition_speed` units/s
  (`rl_camera.cpp:398-403`), so 1.2 swings in ~0.29 s (was the flat
  `transition_speed` units/s, ~0.83 s — floaty). The blend SHAPE stays
  linear (upstream is linear too — a constant per-frame step toward the
  target mode). No INI key, no speed/FOV dynamics change
  (`src/camera/Camera.{h,cpp}`). R1 probe
  `tools/probe_camera_transition.cpp` went step 0.0200 → 0.0583, swing
  51 frames/0.85 s → 18 frames/0.30 s at 1.2; rest geometry unchanged
  (VG4 PASS, `/tmp/t_cam` CAMERA PASS incl. re-timed [5]).
- Ported 2026-10-20 (#1 ball-aim slew, `docs/carballer-facts/camera.md` #1):
  unwrapped `ballYaw_/measYaw_` chased at 20 rad/s, hold when ball horiz
  < 5 uu, vertical `ballPitch_` slewed too (`src/camera/Camera.{h,cpp}`).
  R1 probe `tools/probe_camera.cpp` went 539.3 uu/173.4° → 89.5 uu/19.1°
  (overhead), 539.3/163.0 → 0.0/4.5 (dribble), 381.3/79.3 → 89.5/17.3
  (drift); rest geometry unchanged (VG4 PASS, `/tmp/t_cam` CAMERA PASS).
  Ours keeps the angle slider in the eye placement (not the view pitch).
- Ported 2026-10-20 (#2 car-cam air heading, `docs/carballer-facts/camera.md`
  #2): ground = rigid facing (12/s ease); air = eased onto the horizontal
  velocity heading, smoothstep blend 200→900 uu/s, rate 2.5+7×stiffness;
  facing axis kept pitch-immune w/ half-turn continuity (air-only; ground
  re-reads the raw bearing every frame, healing at landing)
  (`src/camera/Camera.{h,cpp}`, wired in `src/game/main.cpp` from the
  existing `snap.carForward/carVel/airborne` — no new Sim API, no INI key).
  R1 probe `tools/probe_camera.cpp` went 90.0° off → 0.0° (air flip),
  399 uu → 0 uu travel (pitch through vertical); legacy callers keep
  ground-on-nose behavior, rest geometry unchanged (VG4 PASS, `/tmp/t_cam`
  CAMERA PASS).
- Ported 2026-10-20 (#3 stiffness world-space lag,
  `docs/carballer-facts/camera.md` #3): mixed ideal eye through the stiffness
  follower in every mode (ballcam included) — `followRate = 1.5+14×stiffness`,
  vertical halved in air, rig-speed saturation (`kRigLagCap = 40 uu`),
  swivel-vertical split via rig-relative Z (`src/camera/Camera.{h,cpp}`;
  `src/game/main.cpp` already passed stiffness/velocity/airborne — no new
  Sim API, no INI key, dynamics/FOV untouched). R1 probe
  `tools/probe_camera_stiffness.cpp` went trail 209.1 → 19.5 uu (boost run),
  jump dz 250.0 → 15.7 uu (jump linger); rest geometry unchanged (VG4 PASS,
  `/tmp/t_cam` CAMERA PASS after re-baselining [3] chord-inside-sphere and
  [7] saturation/nudge/settle). Ballcam target stays on the slewed aim (#1);
  its one-euro filter went in as #5 below.
- Ported 2026-10-20 (#5 one-euro target filter,
`docs/carballer-facts/camera.md` #5): the mixed look target passes
through `fc = 1.2+1.8*(1-blend)+0.01*derivS` Hz (`derivS` = 3 Hz EMA of
target speed, `rl_camera.cpp:466-489`) in every mode — ballcam keeps
the 1.2 Hz floor (kills dribble micro-chatter), car-cam the 3.0 Hz
floor (stays tight through maneuvers), fast motion opens the cutoff
via `derivS` (`src/camera/Camera.{h,cpp}`; no new Sim API, no INI key,
speed/FOV dynamics untouched). The pre-#5 ballcam rigid lock and the
car-cam stiffness-follower target are superseded. R1 probe
`tools/probe_camera_target.cpp` went ratio 1.000 → 0.311 (micro-step),
chatter swing 10.41 → 4.32 uu; rest geometry unchanged (VG4 PASS,
`/tmp/t_cam` CAMERA PASS).
- Ported 2026-10-20 (#4 sphere-orbit swivel + snap,
  `docs/carballer-facts/camera.md` #4): asymmetric pitch clamp −0.8/+0.7,
  snap-back linear at 2× swivel rate (6 rad/s floor) past 0.05 s idle gated
  by the additive `snap` key (default true, INI-only — no menu slider),
  elevation floor clamp (ground: carZ−17 ride height; air: 16 uu floor) with
  the unabsorbed pitch reaching the view as tilt in both rigs
  (`src/camera/Camera.{h,cpp}`; `src/settings/Settings.{h,cpp}` +
  `src/game/main.cpp` sync + `config/settings.ini` + `docs/INI_SCHEMA.md`).
  Kept ours: swivel RATE (Rocket Science 90 deg/1.3 s — cited; upstream's
  0.7× is tuned), speed/FOV dynamics, angle slider in the eye placement
  (tilt only reaches the view pitch, clamped ±1.4), car-cam 400/40
  look-ahead (rebuilt on the same ray, identical with tilt = 0). The old
  flat 40 uu eye clamp and exponential 6/s die-back are superseded. R1 probe
  `tools/probe_camera_swivel.cpp` went 6 FAIL (offsets 0.027/0.164 rad after
  release, clamp ±1.0, eye flat at 40, no tilt) → 8/8 PASS (exact-0 snap,
  −0.8/+0.7, eye rides 16.0, view +0.35 rad); rest geometry unchanged
  (VG4 PASS, `/tmp/t_cam` CAMERA PASS incl. new [8]). t_cam [8] mirrors the
  probe thresholds, so it fails on the pre-#4 behavior by the same numbers;
  only its `snap=false` gate is new-API by construction.
- Camera shake: an amplitude overlay now exists (`FX::ShakeOffset`, 7 uu at
  61/53 Hz, 2/s decay, set by goal/demo) gated by the `[camera] shake`
  setting (default ON = RL factory) plus `--no-shake`. It is applied by the
  coordinator on the smoothed eye and NEVER touches `CameraParams`. Triggers
  are goal/demo only — RL also shakes on boost/dodge/hits (gap).
- Still missing: ballcam Hold mode, ball-arrow indicator, ball-floor
  projection, flip-reset indicator (CarBaller documents all three as RL
  features); keyboard swivel bindings; demo/goal slow-mo cuts remain FX-only
  hooks (`OnGoal`/`OnDemo`).
- Car-cam look-ahead (400/40) is a tuned constant, not a dumped Psyonix value.
- Camera collision is a shell clamp (eye kept 120 uu inside
  ±4096/±5120/2048), not RL's pull-in sweep — found via screenshot (wall
  filled the screen when hugging it), fixed, pending playtest confirm.

## Free camera (arena inspection)
`F` toggles. Throttle = dolly along view, steer = orbit around target,
jump/boost = height. Car parks while flying. For walking the 1:1 shell
(walls ±4096/±5120, ceiling 2048, goals, pads) without playing.

## G7 verdict 2026-10-17 (GRAPHICS45 §5 — camera fidelity guard, NO code change)
RLViser v0.9.2 follow rig (`docs/rlviser-facts/camera.md` ← `src/udp.rs`
`fn update_camera`: ballcam 300/150/y>=30, carcam 280/110/pitch -6deg;
`spectator.rs`: base 2500/alt 750/sens 0.001; director = nearest-car-to-ball
on 12 s timer) is a hardcoded DEBUG rig: no stiffness, no FOV dynamics, no
transition blend, no swivel, no shake. Ours (§§Model + 1:1 defaults above:
FOV 110, dist 270, h 100, angle -3, stiffness 0.45 + Rocket-Science speed
dynamics + transition 1.2 + swivel + shake) is more 1:1 — KEEP OURS.
Do NOT copy RLViser 300/150 or 280/110/-6deg as gameplay defaults (nor
2500/750/0.001 as gameplay values).
Director + spectator + TrackCar indexing are accepted as DEV TOOLS ONLY.
Spec (not implemented): `[camera] director=false` (bool, default OFF,
INI-schema-additive, schema stays v1 — same pattern as the v1
transition_speed/swivel_speed/shake/invert_swivel extension). Wiring point
(coordinator-owned, `src/game/main.cpp`): init sync lines ~292-310 +
per-frame live sync ~762-771, inserting the director block immediately before
the `camera.Update` call at ~804-810: when ON, pick the followed car each
frame from the already-built `snap` (player, ~707-719) vs `osnap` (bot,
~744-759) by nearest-to-ball distance², re-evaluated when unassigned or on a
12 s timer (RLViser rule); when OFF (default) the path is byte-identical to
today. Needs `Settings.h CameraSettings += bool director=false` +
`Settings.cpp` getB/save + menu exposure before the hook reads
`settings.camera.director`. Spec-only because the timer state + retarget
branch exceeds the <10-line trivial-change limit — no edit made, no
`src/camera/*` gameplay number touched (270/100/-3/0.45 intact).
Proof this date: `/tmp/t_cam` → `CAMERA PASS`; `visual_gates.py --only VG4`
→ `VG4  PASS  ball indicator radii (recipe) ...` / `1 PASS, 0 FAIL, 0 SKIP`.
Explicit: NO code change proposed (no failing-test-first artifact required).
