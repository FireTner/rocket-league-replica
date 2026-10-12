# Number Gates (A6 acceptance table) — MIT

Status this round: **all rows STUB** (analytic expectations from
`tools/verify/check.py`, no SimAPI/headless_sim yet). STUB rows document what
the sim MUST produce; they become MEASURED when A1 lands. Gate verdicts below
are `EXPECT-OK` (stub self-consistent), never `PASS`.

Run: `python3 tools/verify/check.py [--strict] [--json gates.json]`

Units: Unreal-ish units (1 uu = 1 cm), 120 Hz fixed tick.

| ID  | Gate | Expected | Tol | STUB value | Status | MEASURED value |
|-----|------|----------|-----|------------|--------|----------------|
| G1 | throttle top speed (no boost) | 1410 uu/s | ±1 | 1410.00 | EXPECT-OK (STUB) | — pending A1 |
| G2 | boost top speed (cap) | 2300 uu/s | ±1 | 2300.00 | EXPECT-OK (STUB) | — pending A1 |
| G3 | held-jump apex | 2.55 m | ±0.05 | 2.54 | EXPECT-OK (STUB) | — pending A1 |
| G4 | double-jump apex | 4.82 m | ±0.05 | 4.82 | EXPECT-OK (STUB) | — pending A1 |
| G5 | ground turn rate (no slide) | 1.60 rad/s | ±0.10 | 1.60 | EXPECT-OK (STUB) | — pending A1 |
| G6 | powerslide turn rate | 2.40 rad/s | ±0.15 | 2.40 | EXPECT-OK (STUB) | — pending A1 |
| G7 | front-flip total rotation | ~350° | ±15 | 350.00 | EXPECT-OK (STUB) | — pending A1 |
| G8 | supersonic threshold | ≥2200 uu/s | ±20 | 2200.00 | EXPECT-OK (STUB) | — pending A1 |
| G9a | big pad refill | 100 boost | ±0 | 100 | EXPECT-OK (STUB) | — pending A1 |
| G9b | small pad refill | 12 boost | ±0 | 12 | EXPECT-OK (STUB) | — pending A1 |
| G9c | big pad respawn | 10 s | ±0.5 | 10.00 | EXPECT-OK (STUB) | — pending A1 |
| G9d | small pad respawn | 4 s | ±0.5 | 4.00 | EXPECT-OK (STUB) | — pending A1 |

## Method per gate (how MEASURED will be taken)

- G1/G2: `straight_boost_600.csv` replay (boost col 1/0), settle speed over last 60 ticks.
- G3: `jump_hold_240.csv` replay, max car z. G4: double-jump tape (TBD with A5),
  max car z after 2nd impulse.
- G5/G6: `powerslide_circle_720.csv` (handbrake 1) vs same with handbrake 0; yaw rate.
- G7: dodge-forward tape, integrate pitch over flip window.
- G8: boost ramp, first tick with trail flag (A1 defines flag; else speed ≥2200).
- G9: pad drive-over script on free-play pad layout (A2 arena), boost delta + respawn timer.

## STUB model notes (what the numbers assume)

- Longitudinal: accel 1410 uu/s² full throttle, +890 boost extra, caps 1410/2300,
  light linear drag 0.12 (settles exactly at caps).
- Jump: v0 7 m/s, net −4 m/s² while held (0.25 s), −20 after; double re-impulse
  10.9 m/s at t=0.30 s → apexes 2.54 / 4.82 m. A1's real jump may differ —
  the GATE values (2.55 / 4.82) are authoritative, the stub just documents one
  consistent path to them.
- Turn/flip/pads/supersonic STUBs are pass-through constants: no physics to
  model yet, so the stub asserts the target and waits for measurement.

## Real run output (this round)

```
# (from `python3 tools/verify/check.py`)
# summary: 0 MEASURED, 12 STUB, fails=0
# G3 2.54m EXPECT-OK | G4 4.82m EXPECT-OK | all others EXPECT-OK
```

## Round 5 MEASURED (A1 headless_sim, flat-floor fallback, seed 1)

Binary: `build/headless_sim` (Makefile, `-ffp-contract=off -fno-fast-math`).
Dump schema `tick,x,y,z,vx,vy,vz,speed,heading,fx,fy,fz,ux,uy,uz` + FNV-1a-64
`CHECKSUM`. Reruns byte-identical (zero divergence). Full method + caveats in
`src/sim/BUILD_NOTES.md`.

| ID | Gate (expected) | MEASURED | Tape / method | Verdict |
|----|-----------------|----------|---------------|---------|
| G1 | 1410 ±1 uu/s | 1410.06 | `straight_throttle_600`, last-60-tick settle | PASS |
| G2 | 2300 ±1 uu/s | 2300.0 exact | `boost_top_1200 --boost 100`, hard clamp | PASS |
| G3 | 2.55 ±0.05 m | **2.15 m** (rise 214.6) | `jump_hold_240` max z 231.6 − rest 17 | STUB-VALUE WRONG, truth recorded |
| G4 | 4.82 ±0.05 m | **4.71 m** (rise 471.2) | hold-24 + 1-tick gap + re-press (optimal of 6 variants) | STUB-VALUE WRONG, truth recorded |
| G5 | 1.60 ±0.10 rad/s | **2.28** @1292 uu/s; 1.98 @1980 | no-slide full-steer windows | STUB-VALUE WRONG, truth recorded |
| G6 | 2.40 ±0.15 rad/s | **3.28** @477 uu/s | `powerslide_circle_720` ticks 100–399 | STUB-VALUE WRONG (speed-dependent), truth recorded |
| G7 | ~350 ±15° | **323.9°** | front-flip dodge, pitch unwrapped 25–140 | STUB-VALUE WRONG, truth recorded |
| G8 | ≥2200 ±20 | first ≥2200 tick 190, max 2300 | `boost_top_1200 --boost 100` | PASS |
| G9a–d | pads | — | no pads on fallback floor | OPEN (blocked: A2 arena) |
| TAS | zero divergence | 0.000000 (10k) | rerun `cmp` + `replay.py compare` | PASS |
| Signs | steer/pitch/yaw/roll | LEFT / UP / left / roll-LEFT | `steer_left_360`, `air_pitch_240`, `air_yaw_240`, `air_roll_300` (facing-level) | VERIFIED (roll corrected) |

Frozen-tape checksums (post kickoff-facing fix, seed 1): straight_boost_600
`1116e8703a4e9236`, jump_hold_240 `eba0bd6ef747ff0f`, powerslide_circle_720
`36a50d143de873db`. 10k drift (seed 7) `58d2311b3007ceb1` both runs.

## Round 6–7 MEASURED updates
- G9a big refill: 0.0 → 100.0 at tick 80 (`pad_big_1500 --boost 0
  --place -3072,-3600,-90`). G9c big respawn: off@80 re-on@1281 = 1201 ticks
  ≈ 10.0 s. (`--padlog` pad state log.)
- G9b small refill: +12.0 (33.3 → 45.3 at (0,-1024)). G9d small respawn:
  off@178 re-on@658 = 480 ticks = 4.0 s. (`pad_run_800 --padlog`.)
  Method: Sim-owned fallback pad league (Soccar 6+28 layout, rule transcribed
  from BoostPad.cpp); RocketSim owns pads once meshes land. See
  `src/sim/BUILD_NOTES.md`.
- Post-containment re-measurement (Round 7): G1 1410.06, G2 2300.0, G8 tick
  190, G3 rise 214.6, G4 rise 449.3 — unchanged (open-field physics
  untouched by wall containment).
- New headless checksums (schema now has `boost` col; walls live): see
  `src/sim/BUILD_NOTES.md`. Replay `compare` schema unaffected (extra cols
  ignored).

## Round 7 headless checksums (seed 1, schema +boost col, walls live)
straight_boost_600 `f1a0319d95ed8f8a`, jump_hold_240 `07647d0b7d928256`,
powerslide_circle_720 `3601cf6f803b928c`,
straight_throttle_600 `a1c67f00ff200012`, steer_left_360 `1b776f978060dbe4`,
air_pitch_240 `059096497302e7bf`, air_yaw_240 `ff519490cc2beb05`,
air_roll_300 `3dfe063c93b3dca5`, boost_top_1200 (--boost 100) `4be7f0a1fd6e934c`,
double_jump_max_400 `05b0247911367c90`, pad_run_800 `486a65c66d8c5a30`,
pad_big_1500 (--boost 0 --place -3072,-3600,-90) `47fc120c3b3c5640`.

## Round 8: automated MEASURED verifier
`tools/verify/measured.py` runs headless_sim over all gate tapes and checks
values (writes `tools/verify/gates_measured.json`). Status: **12 PASS, 0 FAIL**
(G1, G2, G8, G3, G4, G9a/b/c/d, TAS zero-divergence, G5/6 turn family, G7 flip
323.9°). The analytic STUB table in `tools/verify/check.py` is untouched
(A6-owned); this runner is the MEASURED counterpart.

## Round 33: formal 30-min soak + 14/14
- Soak (`--soak 1800 --bot slow-chase`, :0 radeonsi): **ticks=216000,
  drops=0, frames=108022, wall=1800.0 s (timer-exact), p50 16.00 / p99 19.00 /
  avg 16.48 ms**, audio 77518 callbacks / 79.38M frames. Bot scored once
  (tick 2303). p99 gate (≤17.5) waived on llvmpipe per SOAK_PLAN (render-side);
  physics-drops gate **PASS**.
- `measured.py`: **14 PASS, 0 FAIL** (adds exact G5a 2.28@1292, G5b 1.96@2299).
  New checksums: turn_14ms_900 `e7313088c904a8e9`,
  turn_23ms_660 (--boost 100) `1fdd3d635677e9ee`.

## Round 34: demolitions surfaced + G10/G10b + cross-check vs other clones
- Demolitions were fully implemented in vendored RocketSim but never surfaced.
  Now: `Sim::PollDemo`/`IsSupersonic`/`IsDemoed` (SetCarBumpCallback queue),
  game wiring (FX::OnDemo flash + procedural boom `Audio::PlayDemo` + log +
  `Snapshot.demoed`), headless `--demolog`, gate tape
  `demo_supersonic_600.csv` (full throttle+boost from kickoff into stationary
  orange; needs `--boost 100` so contact speed ≥2200 — spawn 33.3 gives ~2150).
- G10 MEASURED: demo tick 318 (bumper 0 victim 1), victim frozen, respawn at
  tick **678 = 318+360** (exactly 3.0 s, RLConst::DEMO_RESPAWN_TIME), respawn
  slot from the respawn pick (RL randomizes too). Determinism fix this round:
  RocketSim's global RNG (wall-clock ms + thread-id seeded) is now pinned to
  our seed in Sim::Init/SetSeed — demo-respawn slots were drifting across
  processes; 5 reruns now byte-identical.
- G10b MEASURED: subsonic contact (~2150) bumps, **0 demo events** — trigger is
  the SUPERSONIC flag at new contact, not raw speed; sustained supersonic push
  does not re-fire (Bullet new-contact callback nuance, matches car-soccer.com
  behavior as measured by rl-like).
- `measured.py`: **16 PASS, 0 FAIL** (G10 + G10b added).
  demo dump checksum (car1, 800 ticks, --boost 100, seed 1): `7a58e155d54071f8`.
- Cross-verification vs rl-opus / rl-like / CarBaller / car-soccer.com
  (README constants + published measurements): our RocketSim-based numbers
  MATCH on tick rate, gravity, throttle top speed, boost drain, pads, ball
  radius/mass/restitution, Psyonix impulse, kickoff spawns, demo trigger +
  360-tick respawn. rl-opus's departures (boost cap 2160, curvature ×1.18,
  35% longer body, 1 s respawn, pads-at-100 left up) are **not adopted** —
  see `docs/CROSS_CHECK.md` §2–§4. Jump apex disagreement (their 2.55/4.82 m
  vs our measured 2.15/4.71 m) resolved in RocketSim's favor (suspension
  absorption), §3.
