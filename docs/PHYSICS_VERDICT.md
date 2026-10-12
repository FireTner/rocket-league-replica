# Physics 1:1 — final verdict (M6 close-out)

**Date:** 2026-10-10 · **Work order:** `MASTER_PROMPT_PHYSICS.md` §2/§11 ·
**Milestones:** M1–M6 complete · **Verdict: physics is gated 1:1.**

This is the one-page close-out. Every claim below is backed by raw command
output in Round 7 of `docs/PHYSICS_1TO1_LOG.md`; the §2 acceptance checklist is
marked in `MASTER_PROMPT_PHYSICS.md`. Visuals remain experimental scaffolding
and are not evidence of anything here.

## 1. Raw evidence (fresh, same source revision, clean rebuild)

```
make clean && make all                      -> exit 0 (warnings only, see §4)
/tmp/arena_test                             -> RESULT PASS: 46 passed, 0 failed
python3 src/physics/verify_vs_rocketsim.py   -> checked: 110 (presets=7 scalars=90 curves=13)
                                               RESULT: ALL CHECKS PASSED
python3 tools/verify/measured.py             -> # summary: 19 PASS, 0 FAIL
python3 tools/verify/mesh_gates.py           -> # summary: 13 PASS, 0 FAIL
make tests                                   -> ROUNDTRIP PASS / KEYBOARD+MENU PASS / GAMEPAD PASS
```

Mesh-mode identity and the collision gates (real user dump):

```
MESH mode=dumped-mesh shapes=16 tris=8020 verts=4468 hashMatched=1 pads=34
ticks=600 drops=0 arena=soccar-mesh clamps=0 body=car0

PASS C1a  ball floor bounce restitution         measured e = 0.5998     ck=ca79bb591cffda99
PASS C1b  max-speed ball stays in arena (+X)    measured max|x| = 4008.1
PASS C2a  goal mouth: centre shot scores        measured 1 event(s)    ck=9082a3c4e13838ff
PASS C2b  goal mouth: outside-post shot         measured 0 event(s)    ck=0fbe456cd6d11254
PASS C3a  chamfer contact stops at mesh surface measured 4002.4
PASS C3b  back-wall contact keeps ball inside   measured 5027.5
PASS C4a  car cannot escape the mesh shell      measured max|y|=5986.5 maxz=623.1
PASS C4b  subsonic car-car contact = bump       measured 0 demo events
PASS C6a  mesh mode exposes RocketSim's 34 pads measured 34 pads
PASS C6b  pad league has 6 big pads             measured 6 big pads
PASS C7   containment: 24 directions @1500 uu/s measured 0.00 uu, NaN=0
PASS D4   mesh-mode checksum regression         measured 2bd44e5e8ede6e48
PASS D4b  wall tape reaches the mesh            measured 5986
```

Determinism and the 30-minute tick-integrity run:

```
2x 10k-tick mesh runs (seed 1) -> CHECKSUM 7dd64cccedbb9a1f  (both)
cmp                            -> BYTE-IDENTICAL, matching sha256
replay.py compare              -> ZERO-DIVERGENCE PASS (tol=0.0), drift 0.000000

[game] exit ticks=215996 drops=0 frames=1133336 wall=1800.0s p50=2.00ms p99=2.00ms
```

Frozen fallback path, re-confirmed unchanged:

```
./build/headless_sim --tape tools/tas/tapes/straight_boost_600.csv --ticks 100 --seed 1
CHECKSUM 73db10410bd8a8ad
ticks=100 drops=0 arena=flat-floor-fallback clamps=0 body=car0
```

## 2. The three tiers of §2

| Tier | Status | Evidence |
|---|---|---|
| **Constant-exact** | MET | `verify_vs_rocketsim.py` 110/110 vs vendored source with `file:line` citations. |
| **Geometry-exact** | MET | `arena=soccar-mesh` / `hashMatched=1`; C1–C4, C6, C7 all PASS on the real dump. |
| **Semantic-exact** | MET (gated behaviors) | G1–G10 + I1–I3 PASS; 10k-tick mesh determinism byte-identical; frozen tapes unchanged. |

## 3. What is deliberately NOT claimed

- **"1:1" is per-gated-behavior only.** Every number not measured is still
  `UNVERIFIED`: the gamepad `deadzone` 0.08 (`docs/INI_SCHEMA.md`), the
  single-source `CEIL_RADIUS` 256 (`src/arena/ArenaConstants.h`), and the
  inferred ceiling-fillet / goal-recess rows in `docs/MESH_SETUP.md`.
- **Fallback is not evidence of 1:1.** It is the no-mesh development path and
  keeps its own green gates, but walls/ceiling/ramps there are analytic
  (`ContainFallbackBodies`), and the G1 fallback-vs-mesh friction/coast caveat
  in `src/sim/BUILD_NOTES.md` still applies.
- **Mesh-mode evidence depends on the mesh being present.** `collision_meshes/`
  is user-supplied, Psyonix-owned runtime data: loaded, never committed. Without
  it the mesh gate suite prints loud SKIPs rather than passing.
- **The soak was run offscreen** (`gamescope --backend headless`). The drop
  counter comes from the fixed-step accumulator and is independent of the
  presentation target; p99 2.00 ms means the render waiver was not needed.

## 4. Residuals (non-blocking, recorded for honesty)

- The clean build emits pre-existing `-Wmisleading-indentation`,
  `-Wunused-parameter` and vendored-Bullet warnings. Zero errors, and sim-path
  determinism flags (`-ffp-contract=off -fno-fast-math -fno-strict-aliasing`)
  are intact.
- `tools/soak/SOAK_PLAN.md` §2 wording ("`phys_drops > 0` at any point = FAIL")
  is now satisfied literally with no warmup exclusion, so the Round 15 wording
  debt is closed.
- The repo still has **no git history** (initial commit pending) and G5/G6/G7
  stub rows in `docs/number_gates.md` still sit next to the MEASURED table.
  Neither affects the physics verdict.

## 5. Reproduce

```sh
cd ~/Projects/RocketLeagueLaptop
make clean && make all
python3 tools/verify/measured.py        # 19 PASS, 0 FAIL
python3 tools/verify/mesh_gates.py      # 13 PASS, 0 FAIL
python3 src/physics/verify_vs_rocketsim.py
make tests && /tmp/arena_test           # all PASS, 46/46
gamescope --backend headless -W 1280 -H 720 -r 60 -- \
  ./build/game --soak 1800 --ticks 240000 --seed 7 --no-vsync --no-audio \
      --meshdir collision_meshes        # drops=0 over 1800 s
```
