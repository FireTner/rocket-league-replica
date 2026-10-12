# PhysicsAPI.md — A3 CAR & BALL PHYSICS

Ownership: `src/physics/` (CarPresets.h, Tuning.h, FeelNotes.md, this file, BUILD_NOTES.md).
Authority: RocketSim (`third_party/RocketSim` @ `c2baacb`) for every number.
License: MIT only. RocketSim © 2022 ZealanL, credited. No asset rips.

## What A3 owns

| File | Contents |
|---|---|
| `CarPresets.h` | 7 hitbox+wheel presets, `kDefaultPreset`, `FindPreset` / `SetPreset` / `ApplyPreset` |
| `Tuning.h` | All `Physics::Tuning::*` constexprs, each with `RLConst.h:<line>` citation |
| `PhysicsAPI.md` | This contract (what A1/A6 consume) |
| `FeelNotes.md` | Tuning rationale + number-gate table |
| `BUILD_NOTES.md` | How to build / verify this module headless |
| `verify_vs_rocketsim.py` | Headless check: asserts our tables == RocketSim source |

## Consuming the preset API (for A1 Sim, A6 harness)

```cpp
#include "physics/CarPresets.h"
#include "physics/Tuning.h"

// 1. Default construction: Octane.
const Physics::CarPreset* cur = &Physics::kDefaultPreset;

// 2. Menu / config switch by name (case-insensitive, all 7 switchable).
if (!Physics::SetPreset("Dominus", cur)) { /* unknown name, cur untouched */ }

// 3. Push into the sim. A1 implements ConfigureCar on its sim class:
//      void ConfigureCar(const Physics::CarPreset& p);
//    Then either overload works (duck-typed, no dependency on A1's type):
Physics::ApplyPreset(sim, "Merc");          // by name, returns false if unknown
Physics::ApplyPreset(sim, *cur);            // by preset ref, infallible
```

Valid names: `Octane` (default), `Dominus`, `Plank`, `Breakout`, `Hybrid`,
`Merc`, `Psyclops`. `kNumCarPresets == 7`.

### Minimal `ConfigureCar` mapping (A1 reference)

```cpp
void MySim::ConfigureCar(const Physics::CarPreset& p) {
    car.hitboxSize      = {p.sizeX, p.sizeY, p.sizeZ};   // FULL size, uu
    car.hitboxOffset    = {p.offX, p.offY, p.offZ};      // origin offset, uu
    car.frontWheel      = {p.frontWheels.wheelRadius,
                           p.frontWheels.suspensionRestLength,
                           {p.frontWheels.connectX,  p.frontWheels.connectY,
                            p.frontWheels.connectZ}};
    car.backWheel       = { /* ... same ... */ };        // negate Y for wheel #2
    car.threeWheels     = p.threeWheels;   // Psyclops steering/friction path
    car.dodgeDeadzone   = p.dodgeDeadzone; // 0.5f
}
```

Notes:

- Sizes are FULL extents; halve them for Bullet `btBoxShape` (RocketSim does
  `hitboxSize * 0.5 * UU_TO_BT`). Offsets do NOT move the center of mass.
- Wheel connection Y is always stored positive; mirror it for the left wheel.
- `threeWheels` selects the alternate steer/lateral-friction/sticky curves
  (`*_THREEWHEEL`); it does not change the simulated wheel count (RocketSim
  `Car.cpp:262` builds 4 wheels either way).

## Consuming tuning (`Physics::Tuning::*`)

Include `physics/Tuning.h` and use the `k`-prefixed constexprs directly, e.g.
`Tuning::kBoostAccelGround` (991.667), `Tuning::kJumpMaxTime` (0.2 s).
Curve tables (`kSteerAngleFromSpeed`, `kDriveSpeedTorqueFactor`,
`kBallCarExtraImpulseFactor`, bump curves, friction curves) are
`CurvePoint[]` arrays with the same points and order as RocketSim's
`LinearPieceCurve`s — sample with piecewise-linear interpolation.

## SimAPI relationship (A1 is authority for behavior)

- A1's `SimAPI` owns the tick loop, integration, and Bullet wiring. A3 only
  supplies data (presets + constants) and never writes `src/sim/*`.
- Until A1 lands, `ApplyPreset` compiles against any stub exposing
  `ConfigureCar(const CarPreset&)` — see `verify_vs_rocketsim.py`'s C++
  smoke test, which uses exactly such a stub.
- Physics runs at 120 Hz and must never drop (A1/A6 contract); A3's headers
  are constexpr-only (zero per-tick cost by construction).

## Verification

```sh
python3 src/physics/verify_vs_rocketsim.py [--rocketsim third_party/RocketSim]
g++ -std=c++20 -fsyntax-only src/physics/CarPresets.h src/physics/Tuning.h
```

See `BUILD_NOTES.md` for details and the number-gate checklist.
