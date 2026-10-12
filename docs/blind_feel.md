# Blind Feel Script (A6) — MIT

Status this round: **SKELETON** — script + checklist filed, no playable build
to run it on. Each item is pass/fail by a blindfolded-feel tester (or a
reviewer watching only inputs + outcome, not internal state). Run when playable
exists; file results in the table at the bottom.

## Procedure (common)

1. Load free-play, reset to kickoff spawn under test.
2. Tester performs ONLY the listed inputs (no improvisation).
3. Observer records outcome vs criterion. PASS = criterion met 3/3 attempts.

## Items

### F1 — Powerslide radius
- Inputs: full throttle + full steer + handbrake, 3 s, from standstill.
- Criterion: car traces a tight circle (diameter clearly < non-slide circle),
  no spin-out, speed retained enough to feel "drifty not grippy".
- PASS/FAIL: __ (pending playable)

### F2 — Air-roll rate
- Inputs: jump → hold boost off → full roll input 2 s.
- Criterion: car completes visibly 1+ full rolls in ~1 s; direction matches input;
  stopping input stops rotation promptly (no ice-skate tail).
- PASS/FAIL: __ (pending playable)

### F3 — Ball-cam tracking
- Inputs: toggle ball-cam, drive a figure-8 around the ball without touching it.
- Criterion: camera keeps ball centered every frame; car-relative controls still
  feel correct; no whiplash flips when passing under/over the ball.
- PASS/FAIL: __ (pending playable)

### F4 — Wall-to-roof carry
- Inputs: drive up the wall with ball, roll over the lip onto the roof.
- Criterion: transition feels continuous (no pop/snap); gravity direction change
  is readable; player can predict landing spot.
- PASS/FAIL: __ (pending playable)

### F5 — Five mirrored kickoff spawns
- Inputs: same kickoff input tape (boost + slight steer) from each of the 5
  spawns (center, left/right mid, left/right back, mirrored pairs).
- Criterion: mirrored spawns behave symmetrically (left run mirrors right run);
  center spawn reaches ball first-feel; no spawn feels "dead" (unwinnable).
- PASS/FAIL per spawn: C __ · LM __ · RM __ · LB __ · RB __ (pending playable)

## Results table (fill when playable exists)

| Item | Attempts | Passes | Verdict | Notes |
|------|----------|--------|---------|-------|
| F1 powerslide radius | 0/3 | — | BLOCKED (no build) | skeleton |
| F2 air-roll rate | 0/3 | — | BLOCKED (no build) | skeleton |
| F3 ball-cam tracking | 0/3 | — | BLOCKED (no build) | skeleton |
| F4 wall-to-roof carry | 0/3 | — | BLOCKED (no build) | skeleton |
| F5 kickoff spawns ×5 | 0/3 | — | BLOCKED (no build) | skeleton |

## Automation hooks (for later)

- F1/F2/F5 can reuse TAS tapes (`powerslide_circle_720.csv`, kickoff tapes TBD)
  with `replay.py sim --strict` asserting trajectory symmetry numerically.
- F3/F4 stay human-judged; record video + tick trace for the record.
