# Camera notes — 1:1 RL replication (MIT)

## Model
Two modes, one shared RL setting set (`[camera]` in `config/settings.ini`,
schema v1, editable live in the menu AND applied every frame — no restart).

- **Ball-cam** (RL default ON): eye sits on the car→ball horizontal axis,
  behind the car at Distance, raised by Height, pitched by Angle; looks at the
  ball (locked, no smoothing on the target). If the ball is nearly under the
  car (< 50 uu horizontal), the axis falls back to car facing (no flip).
  The rig is RIGID: stiffness only breathes distance/angle/FOV with speed
  (Rocket Science #2) — the eye never lags the ball rig.
- **Car-cam**: eye behind car facing at Distance/Height/Angle; looks at a
  point 400 uu ahead, 40 uu up. The eye AND target smooth with the stiffness
  lag (rate 2..20/s).
- **Transition**: toggling ballcam blends eye+target between the two rigs at
  `transition_speed` units/s (1.2 swings in ~0.83 s).
- **Swivel**: the right stick orbits the rig around the car (yaw) and tilts
  the eye on the rig sphere (pitch) at 90 deg / 1.3 s per swivel unit of 1.0
  (linear in the slider). Release dies back to zero. `invert_swivel` flips
  pitch. Keyboard has no swivel binding (gap, see below).
- Smoothing: exponential `k = 1-exp(-rate*dt)`, `rate = 2 + stiffness*18`.
  Applies to car-cam and to any in-flight transition blend only.
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
- Swivel pitch clamp (±1.0 rad), die-back rate (6/s), transition blend shape
  (linear) and floor clamp (40 uu) are TUNED — no published source.
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
