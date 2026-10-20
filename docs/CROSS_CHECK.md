# Cross-check vs other open-source Rocket League clones (MIT)

Purpose: corroborate our 1:1 physics/visuals against other popular open clones
and catch divergences early. Our authority stays the **vendored RocketSim**
(`third_party/RocketSim`, MIT © 2022 ZealanL, commit `c2baacb8` — replay-validated),
so a disagreement with another clone is resolved by *our vendored source*, not
by majority vote. This file records the comparison and what we adopted/rejected.

Checked this round: **rl-opus (Rocket Arena)**, **rl-like**, **CarBaller**,
**car-soccer.com** (via rl-like's published measurements of its RocketSim build).
The site often named "car-soccer.io" does not resolve (DNS ENOTFOUND, 2026-10-06);
the live RocketSim-in-browser project is car-soccer.com.

## 1. Projects compared

| Project | Stack | Physics core | License |
|---|---|---|---|
| [rl-opus / Rocket Arena](https://github.com/am-will/rl-opus) | Three.js + Rapier (browser) | hand-ported RL constants + approximations | open source |
| [rl-like](https://github.com/JatinMunjal2k/rl-like) | TS + Rapier (browser, WASM-free) | own port of RocketSim constants; publishes `src/sim/rl.ts` (verified values w/ sources) + `tuning.ts` (approximations) | open source |
| [CarBaller](https://github.com/Prankiman/CarBaller) | C++ SDL2 + GL3.3 | **RocketSim directly** (same core as ours) | open source |
| [car-soccer.com](https://car-soccer.com) | browser | RocketSim build | closed site; used by rl-like as an oracle |

## 2. Physics constant comparison (uu, 120 Hz)

Ours = vendored RocketSim `RLConst.h` + `measured.py` MEASURED (16/16 green).

| Constant | Ours (RocketSim) | rl-opus | rl-like | Verdict |
|---|---|---|---|---|
| Tick rate | 120 Hz | 120 | 120 | MATCH |
| Gravity | 650 | 650 | 650 `[RS]` | MATCH |
| Throttle top speed | 1410.06 (G1) | 1411 (their sim) | curve 1410→0 `[RS]` | MATCH |
| Boost cap | **2300 exact** (G2) | **2160** — deliberate 6% trim "flat out was a handful" | 2300 `[RS]` | theirs is a departure; WE KEEP 2300 |
| Supersonic start | 2200 (G8) | 2060 (follows trimmed cap) | 2200 `[RS]` | theirs is a departure; WE KEEP 2200 |
| Boost drain | 33.33/s (INI) | 33.3/s | 33.33 `[RS]` | MATCH |
| Pads big/small | 100/12 @ 10 s/4 s (G9a–d) | 100/12 @ 10 s/4 s | 100/12 @ 10 s/4 s `[RS]` | MATCH |
| Pad behavior at 100 boost | consumed (clamped), matches RocketSim | **left up** ("stay up for whoever needs them") | unimplemented | theirs is a departure; WE MATCH ROCKETSIM |
| Ball radius / visual r | 91.25 / 92.75 | 91.25 | 91.25 `[RS]`, visual 92.75 `[WIKI]` | MATCH (visual radius is a TODO for our renderer) |
| Ball restitution / mass | 0.6 / 30 (car 180 = 6×) | ≈0.63 / 30 | 0.6 / 30 `[RS]` | rl-opus 0.63 is a departure |
| Ball max speed / ang | 6000 / 6.0 rad/s | — | 6000 / 6.0 `[RS]` | MATCH |
| Psyonix impulse | vendored (`Ball.cpp _OnHit`) | zScale 0.35 hand-rolled | zScale 0.35, forwardScale 0.65, maxΔv 4600, curve .65→.30 `[RS]` | MATCH (we get it free from RocketSim) |
| Turn model | RocketSim curvature via wheel forces | curvature table ×**1.18** — deliberate | wheel forces per btVehicleRL `[RS]` | rl-opus ×1.18 is a departure; WE KEEP STOCK |
| Held-jump apex | **2.15 m rise** (G3, RocketSim) | "2.55 m" | — | DISAGREEMENT — see §3 |
| Double-jump apex | **4.71 m** (G4) | "4.82 m" | — | DISAGREEMENT — see §3 |
| Octane hitbox | 120.507×86.699×38.659 + offset (13.876, 0, 20.755), rest z 17 | **35% longer body** (1.59 m vs 1.18 m) — deliberate `BODY_STRETCH` | 120.507×86.699×38.659 `[RS]` | rl-opus departs; WE KEEP STOCK |
| Kickoff spawns | 5 RL slots (F1–F5), verified facing | 5 RL spawns random+mirrored | 5 RL spawns `[RS]` | MATCH |
| Demolition trigger | attacker SUPERSONIC flag (new contact) | supersonic (their 2060) | SUPERSONIC flag, measured vs car-soccer.com: 1040 rel speed demos when flag set, 1508 bump when clear | MATCH (flag semantics, not raw speed) |
| Demo respawn | **360 ticks = 3.0 s** (G10 MEASURED tick 318→678) | 1 s (their choice) | 360 ticks measured | rl-opus departs; WE MATCH 3.0 s |
| Team demos | disabled (MutatorConfig `enableTeamDemos=false`) | disabled | — | MATCH |

## 3. Jump-height disagreement (resolution)

- rl-opus README: "Held jump peaks at 2.55 m, double jump at 4.82 m."
- Our MEASURED (RocketSim, flat-floor fallback, seed 1): held **2.15 m** rise
  (apex z 231.6, rest 17), double **4.71 m** (G3/G4 in `number_gates.md`).
- Analytic full-hold from RocketSim constants (impulse 291.67 uu/s + hold accel
  1458.33 uu/s² for 0.2 s, gravity 650) gives ≈2.33 m rise for the car origin —
  above our measured 2.15 because Bullet suspension absorbs part of the impulse
  while wheels are still loaded. Community figures like 2.55 m match
  no-absorption hand-integration variants, which is what a Rapier port
  (rl-opus) produces; RocketSim reproduces the real suspension absorption.
- **Resolution: keep RocketSim truth (2.15 / 4.71).** G3/G4 stay as-is. rl-opus
  numbers are their engine's, not RL's.

## 4. RocketSim nuances we inherit (documented, not bugs)

- **Sustained supersonic push does not demo.** RocketSim triggers demolition in
  the Bullet *new-contact* callback (`Arena.cpp _BtCallback_OnCarCarCollision`);
  a contact that began subsonic never re-fires even if speeds rise to 2200
  while touching. MEASURED: subsonic contact at ~2150 bumps, no demo ever
  (G10b). car-soccer.com (via rl-like) shows the same flag-at-contact model.
- **Demo respawn position uses RocketSim's global RNG** over RL's spawn list.
  That engine seeds from wall-clock ms + thread-id (`Math::GetRandEngine`), so
  we pin it to our seed in `Sim::Init`/`SetSeed` (wrapper-only, vendor stays
  pristine). Deterministic per `--seed`; RL also randomizes respawn spawns.
- Fallback pads consume at 100 boost (clamped grant) — same as RocketSim
  `BoostPad::_PostTickUpdate`. rl-opus's "pads stay up at 100" is their own
  behavior, not adopted.

## 5. Feature-gap scan (what others have that we don't)

| Feature | Seen in | Status here |
|---|---|---|
| Demolitions + auto-respawn | rl-opus, CarBaller(n/a), car-soccer.com | **ADDED this round** (G10/G10b, game FX + boom + log) |
| Goal explosion w/ car blast + slow-mo beat | rl-opus | partial (FX goal beat + horn + reset; no car-blast impulse) — candidate |
| Supersonic wheel embers | rl-opus | we have a position trail (FX::DrawTrail) — visual polish candidate |
| Camera settings ranges (Liquipedia) + v1.74 defaults | CarBaller | 1:1 defaults kept (FOV 110, dist 270, h 100, angle −3, stiffness 0.45); ADDED this round: swivel 1..10 d5.0, transition 1..2 d1.2, shake dON, invert dOFF + Rocket-Science stiffness dynamics (see §8) |
| Ball-arrow / indicator / flip-reset disc | CarBaller (documents as RL features) | not present — candidates |
| Wall opacity slider | CarBaller | our walls are fixed 0.30 alpha — candidate |
| Ball launch any angle / shot presets | CarBaller | we have `--shot-at-tick/--shot-path` + F1–F5; interactive launcher is a candidate |
| Flip-reset indicator | CarBaller | not present (needs wall/ceiling reset detection) — candidate |
| Multiplayer | rl-like (WebRTC host-authoritative), rl-opus (Cloudflare WS) | out of scope for now (single-process sim) |
| Match clock / overtime / kickoff countdown | rl-like | free-play only; match flow is a candidate |

## 6. License hygiene (Rule 4)

- All four reference projects consulted via public READMEs/repo files only;
  **no code copied** — comparisons are of measured numbers and behavior.
- rl-like's `rl.ts` values were used as *citations to check against*, and every
  one we relied on is independently present in our vendored RocketSim source.
- No Psyonix assets involved (meshes, dumps, sounds) — see `THIRD_PARTY.md`.
- car-soccer.com was only used through rl-like's published measurements; no
  scraping of game data.

## 7. Camera sources (real, consulted read-only — no code copied)

Two sources with genuine RL provenance were used for the missing camera
settings (numbers/behavior facts only; every line of our implementation is
original, per §6 hygiene):

- **BakkesModSDK** ([CameraWrapper.h](https://github.com/bakkesmodorg/BakkesModSDK/blob/master/include/bakkesmod/wrappers/GameObject/CameraWrapper.h),
  [wrapperstructs.h](https://github.com/bakkesmodorg/BakkesModSDK/blob/master/include/bakkesmod/wrappers/wrapperstructs.h)):
  `ProfileCameraSettings` = FOV/Height/Pitch/Distance/Stiffness/SwivelSpeed/
  TransitionSpeed and `CameraSave` = InvertSwivelPitch/CameraShake (mirrors
  Psyonix `FProfileCameraSettings` — the authoritative FIELD SET, no values).
  `CameraWrapper` confirms the runtime model: swivel state + die rate +
  `GetDesiredSwivel(LookUp, LookRight)` + `UpdateSwivel`, dynamic FOV offsets
  (`UpdateFOV`), state blender for transitions.
- **Rocket Science #2** ([camera settings explained](https://rocketscience.fyi/know/videos/camera-settings),
  HalfwayDead, rev. 2023): FOV slider 60..110 drawn as a horizontal cone;
  dynamic FOV +5 with speed +5 at supersonic; stiffness = dynamic distance
  (+100 max at 0; supersonic formula `(1-stiffness)*114.7`) + dynamic angle
  (+2 max at 0) + smoothing with ballcam OFF only; swivel 90°/1.3 s at 1.0,
  linear; shake fires on boost/dodge/hits.
- **CarBaller** ([settings.h](https://github.com/Prankiman/CarBaller/blob/master/src/settings/settings.h),
  sibling clean-room C++/RocketSim project, same vendored commit `c2baacb8`):
  RL ranges fov 60..110 (horizontal), distance 100..400, height 40..200,
  stiffness 0..1, swivel 1..10, transition 1..2; defaults swivel 5.0,
  transition 1.2, shake ON. Adopted RANGES + swivel/transition/shake
  defaults. Two conflicts logged UNVERIFIED (not adopted): CarBaller's angle
  default −4.0 (ours stays −3.0, Zen/Thespike side) and stiffness default 0.5
  "v1.74" (ours stays 0.45, guide-consensus side).

## 8. Re-verification recipe
```sh
make build/headless_sim && python3 tools/verify/measured.py   # 16/16 incl. G10/G10b
./build/headless_sim --tape tools/tas/tapes/demo_supersonic_600.csv \
  --ticks 800 --body car1 --boost 100 --demolog /tmp/dl.csv --dump /tmp/d.csv --seed 1
cat /tmp/dl.csv   # tick,bumper,victim → 318,0,1 ; respawn at 678 = 318+360
```
