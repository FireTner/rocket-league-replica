# Camera feel port — agent prompt

Fix the "feels off" chase camera by porting EXISTING mechanisms from the
vendored reference into `src/camera/`. No new modes, sliders, or HUD features.

**Reference (read first):** `docs/carballer-facts/camera.md` (feature map +
confidence split), `docs/carballer-facts/rl_camera.h` + `rl_camera.cpp`
(source), `docs/CAMERA_NOTES.md` (our current model — keep its dynamics).

**Scope, in order:** (1) ball-aim slew (#1), (2) car-cam air-velocity heading
(#2), (3) stiffness world-space lag (#3), (4) sphere-orbit swivel + snap (#4),
(5) one-euro target filter (#5), (6) transition re-time (#6). One item per
change; stop when playtest (`docs/blind_feel.md`) says it feels right.

**Constraints:** keep our Rocket-Science speed/FOV dynamics and INI schema v1
(additive keys only); `CONTRACTS.md` R1 — reproduce each bug with a failing
probe (port `tools/probe_camera.cpp` scenarios: overhead crossing, dribble
teleport/hold, overhead drift, air flip, boost run) BEFORE touching
`src/camera/`; A4-owned, presentation-only, GL 2.1 fixed-function untouched.

**Verify:** `make build/game` + `visual_gates.py --only VG4` per change; full
bar at the end (`measured.py`, `mesh_gates.py`, `visual_gates.py` VG1–VG16,
`make tests`, `arena_test`, `verify_vs_rocketsim.py`).
