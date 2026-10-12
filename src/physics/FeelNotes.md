# FeelNotes.md — tuning rationale (A3 CAR & BALL PHYSICS)

Policy: **we do not invent physics.** Every number is transcribed from RocketSim
(`third_party/RocketSim` @ `c2baacb`); "feel" work is limited to (a) choosing
which authority value governs each behavior and (b) defining headless
number-gates that prove the sim reproduces Rocket League. Citations below are
`RLConst.h:<line>` unless noted.

## 1. Speed: why 1410 / 2300 are different gates

- Throttle-only top speed (~1410 uu/s ±1) is an EMERGENT property: the drive
  torque factor curve (`RLConst.h:447-453`) scales wheel torque 1.0 → 0.1 over
  0 → 1400 uu/s and hits exactly 0.0 at 1410. Past that, throttle does nothing;
  rolling resistance settles the car at ≈1410. If A1's sim lands at 1409–1411
  with full throttle on flat ground, the whole longitudinal chain (torque
  180·400, brake, friction) is right.
- 2300 is a HARD clamp (`kCarMaxSpeed`, `RLConst.h:53`, enforced `Car.cpp:195`)
  reachable only with boost. Boost accel itself (997.667 ground / 1058.333 air,
  `RLConst.h:59-60`) never exceeds the clamp — the sim must clamp AFTER
  integrating boost, or boost would read 2300+ε.

## 2. Jump: impulse + hold, and the 1.25 s window

Jump is two stacked mechanisms (`Car.cpp`, constants `RLConst.h:94-99`):

1. Immediate velocity kick `875/3 ≈ 291.67` ("impulse 292").
2. While held (up to 0.2 s, min 0.025 s), upward accel `4375/3 ≈ 1458.33`,
   scaled ×0.62 before the min-time elapses (`Car.cpp:585`).

Releasing early truncates phase 2 — that is the entire variable-jump-height
mechanic; there is no separate "short hop" constant. Tap ≈ 0.4 m, full hold
≈ 2.55 m (gate). The second jump (flip/dodge) is allowed up to 1.25 s after
the first jump ENDS (`kDoubleJumpMaxDelay`). Mission text cites a 1.25–1.45 s
range from RLBot jumping-physics notes; RocketSim implements the 1.25 s bound
(`RLConst.h:99`), so 1.25 s is the value A1 must enforce — the extra 0.2 s in
community notes covers input buffering, not physics.

## 3. Dodge: 0.65 s of torque, ~350° of flip

Dodge applies directional torque (`FLIP_TORQUE_X 260` / `FLIP_TORQUE_Y 224`,
`RLConst.h:110-111`) for 0.65 s (`RLConst.h:105`), plus an initial velocity
impulse 500 uu/s scaled by the pre-flip speed ratio (forward 1.0× / side 1.9× /
back 2.5×, `RLConst.h:112-115`). The Z-damping schedule
(0.35 → 0.15 → 0.21, `RLConst.h:102-104`) is what lets a flip rotate ≈350°
without oscillating — do not "smooth" it. Pitch lock (1.0 + 0.3 s,
`RLConst.h:107-108`) freezes nose authority right after the flip; removing it
makes double-jump aerials feel loose.

## 4. Boost: why ground and air differ

Ground 2975/3 vs air 3175/3 (`RLConst.h:59-60`) — air boost is ~6.7% stronger
to compensate for zero wheel torque in the air. Drain is mode-independent
(100/3 ≈ 33.33/s, `RLConst.h:57`), min burn 0.1 s (`RLConst.h:58`) so a tap
always costs ≈3.33 boost (prevents 0-cost feathering exploits). Recharge
10/s after 0.25 s (`RLConst.h:63-64`) is the2018+ "recharge" mutator path;
free-play default recharge ON (A1 owns the flag, A3 owns the rate).

## 5. Supersonic: hysteresis, not a threshold

Enter at ≥ 2200, keep while ≥ 2100 for ≤ 1 s (`RLConst.h:69-76`). The 100 uu/s
hysteresis band plus the 1 s timer is what makes supersonic feel sticky
through small touches — a single-threshold implementation flickers the trail/
demo-eligibility state and breaks bump windows. Demo eligibility additionally
requires the bump pair conditions (0.25 s cooldown, 64.5 uu forward approach,
`RLConst.h:144-145`).

## 6. Car↔ball: the extra-impulse curve is the "pop"

Bullet resolves the contact; then RocketSim adds Psyonix's extra impulse
(`Car.cpp`, scale 0.35 vertical / 0.65 forward, `RLConst.h:135-137`), factored
by car speed along 0.65 → 0.65 → 0.55 → 0.30 at 0/500/2300/4600 uu/s
(`RLConst.h:496-503`) and capped at 4600 uu/s delta (`RLConst.h:138`). Hard
shots feel hard because of this curve, not restitution (car-ball restitution
is 0.0, friction 2.0 — `RLConst.h:34-35`). Nerfing the curve's top end is the
only lever that changes shot power without breaking dribbling (low-speed end
is flat 0.65).

## 7. Turn rates are gates, not constants

2.2 rad/s @ 14 m/s and 2.02 rad/s @ 23 m/s emerge from the steer-angle curves
(`RLConst.h:418-443`) × speed × grip; there is no "turn rate" constant to set.
If A1 misses these, suspect (in order): steer curve sampling, lateral friction
curve, then suspension force scales — not the torque values.

## 8. Number gates (verify with A1/A6, RocketSim as authority)

| # | Gate | Expected | Source |
|---|---|---|---|
| G1 | Throttle-only top speed | 1410 ± 1 uu/s | curve zero at 1410, `RLConst.h:451` |
| G2 | Boost cap | 2300 (hard) | `RLConst.h:53` |
| G3 | Held jump apex | 2.55 m | jump stack, `RLConst.h:94-98` |
| G4 | Double jump apex | 4.82 m | + 2nd jump in window, `RLConst.h:99` |
| G5 | Turn rate | 2.2 rad/s @14 m/s; 2.02 @23 m/s | steer curves, `RLConst.h:418-443` |
| G6 | Flip rotation | ≈350° | 0.65 s torque, `RLConst.h:105` |
| G7 | Supersonic | enter ≥2200, hold ≥2100 ≤1 s | `RLConst.h:69-76` |
| G8 | Pads | 100 / 12 boost, respawn 10 s / 4 s | `RLConst.h:263-267` |
| G9 | Ball | r 91.25, m 30 vs car m 180 | `RLConst.h:29,117` |
| G10 | Presets | all 7 switchable, Octane default | `CarConfig.cpp:20-40` |

G3–G6 need a live sim (A1) + RocketSim cross-check (A6 harness); G1/G2/G7–G10
are additionally covered headless by `verify_vs_rocketsim.py` (constants) —
except G3–G6 heights/rates, which that script records as pending-sim gates.
