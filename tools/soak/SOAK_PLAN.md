# 30-Minute Soak Plan + Physics-Drop Counter Spec (A6)

License: MIT. Gate: **30-min soak, 720p60, physics-drops = 0, frame p99 ≤ 17.5 ms.**
Render repeats allowed; physics stalls forbidden.

## 1. Why this gate exists

On a 2× AMD A6-9225 / Mesa llvmpipe box the renderer WILL miss 60 Hz frames.
That is fine and must never back-pressure the fixed 120 Hz physics loop.
The soak therefore counts two independent things:

| Counter        | Source                              | Gate        |
|----------------|-------------------------------------|-------------|
| `phys_drops`   | fixed-step accumulator: ticks owed but skipped | **must be 0** |
| `render_miss`  | frame time > 17.5 ms (p99 reported) | informational (llvmpipe will miss; allowed) |
| `frame_p99`    | p99 of present-to-present deltas    | ≤ 17.5 ms on real GPU; **waived on llvmpipe**, still recorded |

## 2. Fixed-step rule (for A1 — normative)

```
acc += min(frame_dt, 0.25);          // clamp avoids spiral of death
while (acc >= DT /* 1/120 */) { step(DT); acc -= DT; steps++; }
if (steps_this_frame > 4) phys_drops += (steps_owed - steps_done);
```

- Physics runs at 120 Hz fixed, decoupled from render.
- If a frame owes > 4 catch-up ticks, the sim MUST still run them all
  (slow the game before dropping physics). Any skipped tick = +1 `phys_drops`.
- `phys_drops > 0` at any point in 30 min = FAIL, no exceptions.

## 3. How to run the full soak (when playable exists)

```
build/game --resolution 1280x720 --soak 1800 --bot mirror \
  --log-perf perf_30min.csv --headless-physics-trace phys_trace.csv
python3 tools/soak/analyze.py perf_30min.csv --p99-budget 17.5 --drops phys_trace.csv
```

- Duration 1800 s, free-play map, VS-bot in `mirror` mode (deterministic load).
- `analyze.py` prints PASS/FAIL per counter + p50/p99/max frame table.
- Artifacts filed: `perf_30min.csv`, `phys_trace.csv`, analyzer stdout.

## 4. Smoke version for this round (60 s, no binary needed)

```
python3 tools/soak/smoke.py --seconds 60
```

Simulates a 120 Hz accumulator against a synthetic llvmpipe-ish frame-time
distribution (mean ~22 ms, spikes to 60 ms) and asserts the accumulator logic
itself never drops a physics tick even when render misses constantly.
This validates the COUNTER/METHOD, not the game — output is labelled
`METHOD-SMOKE`, and the real gate stays OPEN until the binary soak runs.

## 5. llvmpipe note

This machine cannot hold 720p60 in the renderer. Expected smoke/full-run
observation: `render_miss` high, `frame_p99` > 17.5 ms, `phys_drops` = 0.
That combination = PASS for physics, with render waived per machine profile.
On a real-GPU machine all three counters must pass unwaived.
