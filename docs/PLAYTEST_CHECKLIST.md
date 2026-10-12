# Playtest checklist — human feel gate (5 minutes)

Goal: turn play sessions into evidence. After each item, note PASS/ODD with
one line. ODD items become physics/camera work orders (they never change
gates — gates are RocketSim truth; feel work is presentation/tuning).

Keys: W/S throttle, A/D steer, Space jump (tap = hop, hold = high, press
again mid-air = double jump / with direction = flip), Shift boost,
S or LeftCtrl powerslide (in air: + steer = air-roll), Q/E roll, C/B ballcam,
F freecam, Esc menu, R reset car, T reset ball, F1–F5 kickoffs, M mute,
F12 screenshot.

## Script
1. **Drive-out** (30 s): hold W from kickoff. Expect: straight sprint to the
   far half, engine pitch rising, supersonic trail past ~2200. ODD if: pulls
   left/right, engine silent, no trail.
2. **Turns** (30 s): full-lock circles at low speed, then at full speed.
   Expect: tight rotation slow, wide arcs fast (RL scrubs speed in turns).
3. **Jump ladder** (60 s): tap-hop, held jump (rises ~2.1 m), double jump
   (~4.7 m — about 3/4 of the 6.4 m crossbar). Front flip (W + double-tap
   Space): full rotation, land driving.
4. **Pads** (60 s): drive the center line (0,-2300 → 0,+2300). Expect two
   +12 blips with HUD boost readout jumping; then swerve to a corner big pad
   for the 0→100 fill + deeper blip. Boost (Shift): hiss + flame + trail.
5. **Ball** (60 s): dribble into the ball, then hit it hard toward a goal.
   Expect: bounces off floor/walls/nets, posts ping (sharp edges!), goal
   mouth lets it in, GOAL banner + horn + center reset.
6. **Cameras** (60 s): C toggles ball-cam/car-cam (HUD tag confirms). F
   freecam: fly the shell — walls, ceiling grid?, goals, pads. Esc menu:
   change FOV/Distance/Height live (no restart), rebind a key, Esc out.
   **Wall-hug**: drive into a side wall and slide along it — the camera must
   pull inside the shell, never a fullscreen blue rectangle (F12 if it does).
7. **Soak feel** (rest of session): free play vs the bot (`--bot slow-chase`
   chases, `mirror` echoes you, `stationary` parks it). Note any stutter,
   audio glitches (M mutes), or physics weirdness with tick numbers from HUD.

## Reporting
One line per item: `PASS` or `ODD: <what> @ tick/HUD state + screenshot`.
Screenshots (F12) for anything visual.
