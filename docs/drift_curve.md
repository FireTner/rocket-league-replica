# Drift Curve (A6) — MIT

Status this round: **STUB curve (analytic-vs-analytic)** — the replay/compare
method is final and demonstrated; same-binary sim data pending A1
`headless_sim`. Drift is quantified below, not hidden: the table shows exactly
what was compared and the residual.

## Method (final, used this round and later)

1. Record/replay a 10 000-tick straight-boost tape.
2. Produce two dumps: baseline vs second run (same binary ⇒ must be identical).
3. `replay.py compare A B` reports max/mean Euclidean position drift per checkpoint.
4. This round the "second run" is the analytic model with +1e-6 drag
   perturbation (stands in for a second binary) — labelled STUB.

Run: `python3 tools/tas/replay.py drift --out drift.csv`

## Curve (STUB, 2026-10-05)

| tick | max_pos_drift_uu | mean_pos_drift_uu | baseline_speed_uu/s |
|------|------------------|-------------------|---------------------|
| 1000 | 0.000427 | 0.000387 | 2300.000 |
| 2000 | 0.000427 | 0.000407 | 2300.000 |
| 5000 | 0.000427 | 0.000419 | 2300.000 |
| 10000 | 0.000427 | 0.000423 | 2300.000 |

- Baseline checksum (FNV-1a-64): `f2af84f93765ec94` (analytic model).
- Same-input re-run divergence: **0.000000 uu** (`compare` on identical dumps:
  `ZERO-DIVERGENCE PASS tol=0.0`, 600-tick demo; 10k pending sim).
- Interpretation: the harness resolves drift down to ~4e-4 uu under a 1e-6
  perturbation, i.e. it WILL catch real nondeterminism (float reassociation,
  RNG, pointer iteration) when the sim lands. A same-binary sim replay must
  show exactly 0.000000 at all checkpoints.

## What changes when headless_sim lands

- Replace STUB second-run with `headless_sim --tape … --dump` twice, same seed.
- Expected MEASURED row: all drift columns `0.000000`, verdict ZERO-DIVERGENCE.
- Any nonzero MEASURED drift = FAIL + bisect (seed, tape half-split) per
  TAPE_FORMAT.md §4. File the nonzero curve here — never delete it.

## Round 5 MEASURED (same-binary sim replay)
- 10 000-tick straight-boost tape, seed 7, run twice:
  `CHECKSUM 58d2311b3007ceb1` both runs, `cmp` identical,
  `replay.py compare` → max_pos_drift=0.000000uu mean=0.000000uu
  max_heading=0.000000rad → **ZERO-DIVERGENCE PASS (tol=0.0)**.
- 216 000-tick (30 sim-min) endurance: `CHECKSUM 52390563974d7563`,
  drops=0, 11.4 s wall (≈20 000× real-time; headless drives Step120 directly,
  so this proves sim stability, not game-loop frame budget — wall soak OPEN).
