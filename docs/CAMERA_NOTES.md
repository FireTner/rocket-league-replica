# Camera notes — 1:1 RL replication (MIT)

## Model
Two modes, one shared RL setting set (`[camera]` in `config/settings.ini`,
schema v1, editable live in the menu AND applied every frame — no restart).

- **Ball-cam** (RL default ON): eye sits on the car→ball horizontal axis,
  behind the car at Distance, raised by Height, pitched by Angle; looks at the
  ball (locked, no smoothing on the target). If the ball is nearly under the
  car (< 50 uu horizontal), the axis falls back to car facing (no flip).
- **Car-cam**: eye behind car facing at Distance/Height/Angle; looks at a
  point 400 uu ahead, 40 uu up.
- Smoothing: exponential `k = 1-exp(-rate*dt)`, `rate = 2 + stiffness*18`.
  Ball target locks instantly; eye smooths in both modes.

## 1:1 defaults and ranges (RL values)
| Key | Default | Range |
|-----|---------|-------|
| fov | 110 | 60–130 |
| distance | 270 | 100–400 |
| height | 100 | 40–200 |
| angle | -3 | -30–15 |
| stiffness | 0.45 | 0–1 |
| ballcam | true | — |

FOV slider is treated as horizontal FOV at 16:9 and converted to vertical for
the GL frustum (keeps horizontal extent constant on 16:9).

## Known approximations (not hidden)
- No transition-speed / swivel-speed sliders yet (stiffness covers the feel;
  toggle swings with the same smoothing).
- Camera shake: an amplitude overlay now exists (`FX::ShakeOffset`, 7 uu at
  61/53 Hz, 2/s decay, set by goal/demo). It is applied by the coordinator on
  the smoothed eye and NEVER touches `CameraParams`, so the 1:1 defaults above
  stay exact; it is disabled for freecam/`--no-shake`, which keeps the visual
  gate captures byte-stable (VG1). Amplitude is TUNED (no permitted source).
- Still missing: ball-arrow indicator, transition/swivel-speed sliders;
  demo/goal slow-mo cuts remain FX-only hooks (`OnGoal`/`OnDemo`).
- Car-cam look-ahead (400/40) is a tuned constant, not a dumped Psyonix value.
- Camera collision is a shell clamp (eye kept 120 uu inside
  ±4096/±5120/2048), not RL's pull-in sweep — found via screenshot (wall
  filled the screen when hugging it), fixed, pending playtest confirm.

## Free camera (arena inspection)
`F` toggles. Throttle = dolly along view, steer = orbit around target,
jump/boost = height. Car parks while flying. For walking the 1:1 shell
(walls ±4096/±5120, ceiling 2048, goals, pads) without playing.
