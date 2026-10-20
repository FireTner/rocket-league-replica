# CarBaller camera — agent reference (read-only)

## What this is

Vendored reference copy of the Rocket League-style camera from
[Prankiman/CarBaller](https://github.com/Prankiman/CarBaller) ("Rocket League
single player clone made with RocketSim"), a sibling clean-room C++/RocketSim
freeplay project on the same vendored RocketSim commit (`c2baacb8`) as ours.

- **Pin:** master `e11180e51c88e01acf598e85525d86c171894b49` (2026-10-13).
- **Files:** [`rl_camera.h`](rl_camera.h) (104 lines) + [`rl_camera.cpp`](rl_camera.cpp)
  (502 lines), verbatim upstream. Upstream `CameraSettings` lives in
  [settings.h](https://github.com/Prankiman/CarBaller/blob/master/src/settings/settings.h)
  (`fov 110 / distance 270 / height 100 / angle -4 / stiffness 0.5 /
  swivel 5.0 / transition 1.2 / shake ON`, plus `snap`, Hold-BallCam, ball
  indicator/arrow/floor-projection, flip-reset disc); `V3`/math in
  [core/math.h](https://github.com/Prankiman/CarBaller/blob/master/src/core/math.h);
  `SimSnapshot` in `src/sim/sim.h`; degeneracy probe in
  [tools/probe_camera.cpp](https://github.com/Prankiman/CarBaller/blob/master/tools/probe_camera.cpp).
- **License:** MIT © 2026 Arvid Kolsmyr (full text upstream
  [LICENSE](https://github.com/Prankiman/CarBaller/blob/master/LICENSE)).
  Compatible with repo Rule 4 (MIT-only). Attribution is given here; keep it if
  you copy code. Logged in `docs/THIRD_PARTY.md`.

## Usage rules

- **Reference only — never built.** Nothing in this directory is on any build
  path (`make` never sees it). Port ideas into `src/camera/` by hand; do not
  `#include` these files from shipped code.
- **Behavior facts are citable; constants are TUNED until re-measured.**
  Upstream marks its own inventions (`kBallAimSlew`, `kRigLagCap`, follow
  rates, snap timing…) as tuned in comments. See `camera.md` for the
  confidence split: what is RL-true vs what is a feel-fix guess.
- **Keep our Rocket-Science dynamics.** Upstream has NO speed/FOV dynamics;
  ours (`Camera.cpp`: `dynDist/dynAng/effFov_`) does. A port must not lose them.
- Owner boundary (`CONTRACTS.md` v2): `src/camera` is A4. Cross-owner changes
  need a failing test first (Rule 1); every landed change updates the owning
  `BUILD_NOTES.md`/doc (Rule 2).

## Where to look

- Start at [`camera.md`](camera.md) — architecture, feature map vs our
  `src/camera/Camera.{h,cpp}`, and what to port / keep / avoid.
- Then read [`rl_camera.cpp`](rl_camera.cpp) + [`rl_camera.h`](rl_camera.h)
  directly for the exact logic.
- For "is there anything closer to the real camera?
  exact-decomp paths + approximation ranking" see
  [`EXACT_PATHS.md`](EXACT_PATHS.md).
