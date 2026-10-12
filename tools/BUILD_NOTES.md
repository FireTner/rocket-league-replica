# A6 Build Notes (verification + bot stub) — MIT

## Files owned by A6

```
tools/tas/TAPE_FORMAT.md        tape spec v1.0.0 (FROZEN) — contract for A1/A5
tools/tas/replay.py             validate|self|sim|compare|drift harness
tools/tas/tapes/straight_boost_600.csv
tools/tas/tapes/jump_hold_240.csv
tools/tas/tapes/powerslide_circle_720.csv
tools/verify/check.py           12-gate checklist runner (STUB/MEASURED labels)
tools/verify/sim_shim.py        SimAPI/headless_sim probe, never blocks
tools/soak/SOAK_PLAN.md         30-min soak plan + physics-drop counter spec
tools/soak/smoke.py             60 s METHOD-SMOKE (counter logic, no binary)
tools/soak/analyze.py           real-run analyzer (for when binary exists)
docs/number_gates.md            gate table + STUB/MEASURED status
docs/blind_feel.md              feel script skeleton (5 items, BLOCKED: no build)
docs/drift_curve.md             10k-tick STUB drift curve + method
src/bot/BotStub.h               VS-bot stub: stationary/mirror/slow-chase
tools/BUILD_NOTES.md            this file
```

Untouched per contract: `src/sim`, `src/arena`, `src/render`, config schema.

## How to run everything (python3 + g++ only)

```sh
python3 tools/tas/replay.py validate tools/tas/tapes/straight_boost_600.csv
python3 tools/tas/replay.py self tools/tas/tapes/straight_boost_600.csv --dump /tmp/self.csv
python3 tools/tas/replay.py compare /tmp/self.csv /tmp/self.csv   # zero-divergence demo
python3 tools/tas/replay.py drift --out /tmp/drift.csv            # 10k STUB curve
python3 tools/verify/check.py                                     # 12 gates
python3 tools/soak/smoke.py --seconds 60                          # soak method smoke
g++ -std=c++20 -Wall -Wextra -fsyntax-only src/bot/BotStub.h      # bot stub
```

## Contracts for siblings

- A1: `headless_sim --tape T --ticks N --dump D [--seed S]`, stdout
  `CHECKSUM <hex16>`; same binary+tape+seed ⇒ byte-identical dump.
  Fixed-step rule in `tools/soak/SOAK_PLAN.md` §2 (slow down, never skip ticks).
- A5: Controls parser accepts the 9 tape columns; clamps floats, rejects bad
  ints; tape header matched exactly.
- A2: pad layout + respawn timers feed G9a–d; kickoff spawns feed F5.

## Known limitations (not hidden)

- All 12 gates STUB (0 MEASURED) — no SimAPI/headless_sim in tree yet.
- Drift curve analytic-vs-analytic; method final, data pending sim.
- Feel script BLOCKED (no playable); soak gate OPEN (smoke validates method only).
- Jump STUB constants are one consistent path to 2.55/4.82 m, not physics truth.
