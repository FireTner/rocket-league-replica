# Game build notes — free-play ball commands (keys 1–5)

Workstream A, 2026-10-29. Keys stay 1–5. `Sim::SetBall` applies the pose
and clears angular velocity when the linear velocity is non-zero.

Key 2 places the ball on the Octane hitbox roof (local x = hitbox offset x,
local z = offset z + half height + ball radius 91.25) and copies the car's
linear velocity. That rests while the car is driving at speed. Releasing
throttle applies RocketSim's coasting brake (`COASTING_BRAKE_FACTOR` 0.15)
and the car slows out from under the ball; that case still leaves the roof
within 0.2 s. Offsets and the full vertical-velocity copy stay UNVERIFIED (I3).

## Commands

```
ctest --test-dir build -R test_freeplay_ball --output-on-failure
python3 tools/verify/measured.py
```

`make /tmp/t_ball` (2026-10-29, throttle held after the speed run, boost off):

```
car pos=(-0.006,337.010,17.013) vel=(-0.006,2300.000,0.000) spd=2300.000
ball on hood pos=(-0.006,352.144,148.208) vel=(-0.006,2300.000,0.000)
t=24 gapUp=91.250 (radius 91.250) relH=0.653 relZ=0.023
  ball pos=(-0.007,812.172,148.097) vel=(-0.005,2300.591,0.304)
  carVel=(-0.007,2299.938,0.281) carSpd=2299.938
t=120 gapUp=91.259 relH=6.494 relZ=0.302 carSpd=2299.810
KEYS 1-3 PASS
PASS freeplay ball
```

`python3 tools/verify/measured.py`: `# summary: 19 PASS, 0 FAIL`.

Did not edit `src/sim`, `src/arena`, `src/physics`, or tapes. Did not
run a 30-minute soak. Did not change how the ball is drawn.

## Goal burst (presentation)

`GoalBurst` arms from the same `PollGoal()` latch as the horn and
`FX::OnGoal`. It keeps the ball point from before `ResetBall`. Rings and
points are fixed-function. Life `0.80 s` and the orange/blue colours are
UNVERIFIED. No car impulse and no change to the sim tick.
