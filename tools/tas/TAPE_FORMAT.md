# TAS Tape Format (A6 contract for A1/A5)

Version: 1.0.0 · Status: FROZEN for this build round · License: MIT

## 1. File layout

- Plain CSV, UTF-8, LF line endings, no BOM.
- Line 1 MUST be exactly the header:
  ```
  tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake
  ```
- One row per simulation tick, `tick` starting at 0, strictly +1 per row, no gaps.
- Fixed tick rate: **120 Hz** (`dt = 1/120 s`). Tapes record sim ticks, never wall time.
- Comment lines (`#` first char) allowed BEFORE the header only (metadata); never
  between data rows.

## 2. Column spec

| Column     | Type  | Range / values              | Meaning                                   |
|------------|-------|-----------------------------|-------------------------------------------|
| tick       | int   | ≥ 0, sequential             | Sim tick index                            |
| throttle   | float | [-1.0, 1.0]                 | Forward (+) / reverse (-) drive           |
| steer      | float | [-1.0, 1.0]                 | Left (+) / right (-) steering             |
| pitch      | float | [-1.0, 1.0]                 | Nose up (+) / down (-)                    |
| yaw        | float | [-1.0, 1.0]                 | Left (+) / right (-)                      |
| roll       | float | [-1.0, 1.0]                 | Air-roll input                            |
| jump       | int   | 0 or 1                      | 1 = jump button held this tick            |
| boost      | int   | 0 or 1                      | 1 = boost held this tick                  |
| handbrake  | int   | 0 or 1                      | 1 = powerslide/handbrake held this tick   |

- Floats: decimal with ≤6 places, `0` written as `0.0`. Parser clamps out-of-range
  to range and counts a clamp event (reported, not silent).
- Ints: exactly `0` or `1`; anything else is a parse error.

## 3. Example (5 ticks)

```
# game=rl-clone, rate=120Hz, map=freeplay
tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake
0,1.0,0.0,0.0,0.0,0.0,0,1,0
1,1.0,0.0,0.0,0.0,0.0,0,1,0
2,1.0,0.2,0.0,0.0,0.0,1,1,0
3,1.0,0.2,0.0,0.0,0.0,0,1,0
4,0.0,0.0,0.0,0.0,0.0,0,0,1
```

## 4. Determinism contract (for A1 headless_sim)

- `headless_sim` CLI MUST accept:
  ```
  headless_sim --tape <tape.csv> --ticks N --dump <states.bin|states.csv> [--seed S]
  ```
  and print per-tick car/ball state to `--dump`, plus a final FNV-1a-64 checksum
  line `CHECKSUM <hex16>` on stdout.
- Same binary + same tape + same seed ⇒ byte-identical dump (zero divergence).
- `tools/tas/replay.py` implements three modes:
  1. `sim` — drives `headless_sim` if found on PATH / at `build/headless_sim`.
  2. `self` — pure-Python analytic reference model (kinematic only, documents
     EXPECTED curves; NOT a physics oracle).
  3. `compare` — checks two dump files tick-by-tick, reports max/mean drift.
- Drift metric: Euclidean position error `|p_a - p_b|` in Unreal-ish units
  (1 uu = 1 cm). Reported over 10 000 ticks; curve MUST be filed even if it is
  analytic-only this round (label `STUB` vs `MEASURED`).

## 5. Conformance checklist for A1/A5

- [ ] A5 Controls parser accepts every column, clamps floats, rejects bad ints.
- [ ] A1 `headless_sim` honors the CLI above and the header exactly.
- [ ] No wall-time, RNG-without-seed, or pointer-iteration inside the tick loop.
- [ ] Reference tapes in `tools/tas/tapes/` replay with zero divergence same-binary.

## 6. Reference tapes shipped

- `straight_boost_600.csv` — 600 ticks (5 s): full throttle + boost, straight.
- `jump_hold_240.csv` — 240 ticks (2 s): throttle 0, jump held ticks 10..130.
- `powerslide_circle_720.csv` — 720 ticks (6 s): throttle 1, steer 0.6, handbrake on.
