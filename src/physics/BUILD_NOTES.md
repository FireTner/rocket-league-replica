# BUILD_NOTES.md — src/physics (A3)

## Scope / non-goals

- A3 owns `src/physics/` ONLY: `CarPresets.h`, `Tuning.h`, `PhysicsAPI.md`,
  `FeelNotes.md`, `verify_vs_rocketsim.py`, this file.
- Read-only everything else: `src/sim/*` (A1 internals), renderer, config
  schema. `ApplyPreset` is duck-typed so it compiles without A1's types.
- Physics tick is 120 Hz, never drops (A1/A6 own the loop; our headers are
  constexpr-only and add zero per-tick cost).

## Toolchain

- `g++ 16`, C++20, no cmake (per machine constraints). No dependencies:
  headers use only `<cstddef>`; the verifier uses stdlib Python 3.

## Build / check (headless, < 10 s on 2× A6-9225)

```sh
# 1. Headers must compile standalone:
g++ -std=c++20 -Wall -Wextra -fsyntax-only src/physics/CarPresets.h src/physics/Tuning.h

# 2. Tables vs RocketSim authority ((hitboxes, wheels, every Tuning value):
python3 src/physics/verify_vs_rocketsim.py [--rocketsim third_party/RocketSim]

# 3. Optional: compile the ApplyPreset stub usage from PhysicsAPI.md:
g++ -std=c++20 -Wall -Wextra -fsyntax-only -x c++ - <<'EOF'
#include "src/physics/CarPresets.h"
struct StubSim { void ConfigureCar(const Physics::CarPreset&) {} };
int main() { StubSim s; return Physics::ApplyPreset(s, "Octane") ? 0 : 1; }
EOF
```

## Number-gate status

- Headless-covered now: preset hitboxes/wheels (7/7), ball r/m, boost
  rates/caps, jump/dodge/supersonic constants, pad amounts/cooldowns,
  extra-impulse + bump/demo curves (see verifier output).
- Pending live sim (A1 + A6 harness, RocketSim as authority): G1 throttle top
  1410±1, G2 boost cap 2300, G3 held jump 2.55 m, G4 double jump 4.82 m,
  G5 turn 2.2/2.02 rad/s, G6 flip ≈350°, G7 supersonic hysteresis.
  Coordinate via `PhysicsAPI.md`; do NOT re-derive — diff against RocketSim.

## Updating after a RocketSim bump

1. `git -C third_party/RocketSim rev-parse HEAD` → record new rev.
2. Re-run the verifier; fix any `MISMATCH` lines in `CarPresets.h`/`Tuning.h`.
3. Update `RLConst.h:<line>` citations that moved.

## Round 7 — M6 close-out (no constant changed)

- Re-ran the authority diff after a clean rebuild:
  `python3 src/physics/verify_vs_rocketsim.py` ->
  `checked: 110 (presets=7 scalars=90 curves=13)`, `RESULT: ALL CHECKS PASSED`.
  Every landed constant still matches the vendored RocketSim source with its
  `file:line` citation (`Tuning.h` / `CarPresets.h`).
- Live-sim gates that back these tables are green in `measured.py` (19/19):
  G1 1410.06, G2 2300 exact, G3/G4 jump rises, G5a/G5b turn rates, G7 flip,
  G9a–G9d pads, G10/G10b demo — all as listed in "Pending live sim" above, now
  measured. Nothing here is UNVERIFIED except the documented items (gamepad
  `deadzone` 0.08, `CEIL_RADIUS` 256); see `docs/PHYSICS_VERDICT.md` §3.
- Physics work order closed at M6; do not re-derive these tables.
