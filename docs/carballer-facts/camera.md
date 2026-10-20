# CarBaller `RLCamera` — what it does, and what to take from it

Reference sources: [`rl_camera.h`](rl_camera.h), [`rl_camera.cpp`](rl_camera.cpp)
(line numbers below are into those vendored files); upstream probe
[tools/probe_camera.cpp](https://github.com/Prankiman/CarBaller/blob/master/tools/probe_camera.cpp).
Ours for comparison: `src/camera/Camera.{h,cpp}`, `docs/CAMERA_NOTES.md`.

## Architecture (one paragraph)

Two rigs are evaluated every frame — ball-cam (`computeBallCam`, .cpp:123) and
car-cam (`computeCarCam`, .cpp:282) — sharing one sphere-orbit eye placement
(`placeEye`, .cpp:105: `R = hypot(distance,height)`, `elev = e0 − pitchOff`,
floor-clamped, unabsorbed remainder returned as view `tilt`). A `blend_` weight
(.cpp:398–403, rate `0.5 + 2.5×transitionSpeed`) mixes the rigs during
ballcam toggles. The mixed ideal eye then passes through stiffness smoothing
(.cpp:413–464) and the look target through a one-euro filter (.cpp:466–489);
shake is an additive sine overlay (.cpp:491–501).

## Feature map vs ours (port candidates first)

| # | Upstream feature | Upstream location | Ours today | Take? |
|---|---|---|---|---|
| 1 | Ball-aim slew: unwrapped `measYaw_` + `ballYaw_` chased at `kBallAimSlew = 20 rad/s` (.cpp:47,74,138–166); hold when ball horiz < 5 uu; vertical aim slewed too (.cpp:172–179) | `computeBallCam` | Raw `atan2` each frame, 50 uu fallback, rigid snap — teleports on overhead/roof crosses | **YES — the core "feels off" fix** |
| 2 | Car-cam air heading: ground = rigid facing; air = eased onto velocity heading, blend 200→900 uu/s, rate `2.5+7×stiffness` (.cpp:218–244,328–355); pitch-immune axis w/ half-turn continuity (.cpp:270–326) | `computeCarCam` | Always behind `carYaw` (facing); pitch/flips swing the camera | **YES** |
| 3 | Stiffness as world-space lag: `followRate = 1.5+14×stiffness`, vertical halved in air, rig-speed saturation `kRigLagCap = 40 uu`, swivel-vertical split out (.cpp:413–464) | `update` | Ballcam rigid; car-cam-only lag `2+18×stiffness`; boost drags eye | **YES** |
| 4 | Sphere-orbit swivel + tilt remainder + floor clamp (16 uu / ride height), clamp −0.8/+0.7, snap-back 2× rate after 0.05 s idle (`.cpp:101–121, 373–396) | `placeEye` + `update` | Orbit exists but clamp ±1.0 rad, die 6/s, no snap setting | **YES (adapt ranges)** |
| 5 | One-euro look-target filter (`fc = 1.2+1.8×(1−blend)+0.01×deriv`, .cpp:477–488) killing dribble micro-chatter | `update` | Target locks instantly (ball) / same lag as eye (car) | **YES, small** |
| 6 | Transition `0.5+2.5×speed` (~0.29 s at 1.2, .cpp:400) | `update` | Linear `speed` u/s (~0.83 s at 1.2) — floaty | **YES (re-time)** |
| 7 | `snap`, Hold-BallCam, ball indicator/arrow/floor-projection, flip-reset disc (in `CameraSettings`, not the camera math) | settings.h | Missing (logged gaps in `CAMERA_NOTES.md`) | Features, not feel — separate tickets |
| — | Speed dynamics (`dynDist/dynAng/dynFov`) | **absent upstream** | We have them (Rocket Science #2) | **KEEP OURS — do not regress** |

## Confidence (do not over-claim)

- **RL-true:** field set + slider ranges + ballcam-ON default (binary/menu-extracted;
  two defaults still disputed: angle −3 vs −4, stiffness 0.45 vs 0.5 — keep ours).
- **Well-measured (single community source):** Rocket-Science dynamics — ours has
  them, upstream doesn't. Keep.
- **Plausible but upstream-tuned:** every constant in the table above (20 rad/s,
  5 uu, 200–900, 2.5+7×, 40 uu, 1.5+14×, snap timing, shake sines). They fix real
  degenerate cases (probe: 516 uu/75° pre-fix overhead snap) but the NUMBERS are
  guesses. Port the mechanism, then re-fit against playtest
  (`docs/PLAYTEST_CHECKLIST.md` / `docs/blind_feel.md`), never cannonize blindly.
- **Not camera:** ball arrow / floor projection / flip disc / wall opacity /
  director — HUD/game/dev-tools. Don't file them as camera work.

## Port guidance (anti-extravagance)

1. Mechanisms only (#1–#6 above). No new modes, sliders, or HUD features.
2. Keep our dynamics + FOV projection + INI schema v1 keys untouched; additive
   keys only, and only if a ported mechanism needs one (`snap` is the likely one).
3. Failing-test-first per `CONTRACTS.md` R1: port `tools/probe_camera.cpp`
   scenarios (overhead crossing, dribble teleport/hold, overhead drift, air flip,
   boost run) against our `Sim` snapshot BEFORE changing `src/camera/`.
4. Verify with the cheap tier first (`make build/game`, one capture,
   `visual_gates.py --only VG4`); full bar only at the end
   (`measured.py`, `mesh_gates.py`, `visual_gates.py` VG1–VG16, `make tests`,
   `arena_test`, `verify_vs_rocketsim.py`).
