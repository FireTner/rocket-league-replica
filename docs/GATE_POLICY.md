# Gate policy — tiered, owner-based verification (2026-10-17)

Why this exists: the full bar (~200 checks: `measured.py` 19 +
`mesh_gates.py` 13 + `visual_gates.py` VG1–VG16 + `arena_test` 46 +
`verify_vs_rocketsim.py` 110) costs 100–300 s per `make all` on a
2-core box, and history shows agents re-running all of it after every
edit — including physics gates on camera-only changes. V6 proved
green gates ≠ correct. So: gates are the regression bar, not the
oracle. Human playtest outranks them for feel.

## The three tiers

| Tier | What | Blocks? | When to run |
|---|---|---|---|
| **Hard** (physics) | `measured.py` 19/19, `mesh_gates.py` 13/13, `arena_test` 46/46, `verify_vs_rocketsim.py` 110/110, `make tests` | Yes, on sim-touching changes | Only when `src/sim`, `src/arena`, `src/physics`, `Tuning.h`, `tools/tas/tapes`, or sim-path flags change |
| **Advisory** (visual) | `visual_gates.py` VG1–VG16, relevant `--only VGx` | Yes, but only the touched `VG*` | On `src/render\|camera\|fx\|models\|audio\|menu`, `src/game/main.cpp` presentation paths |
| **Authority** (human) | `docs/PLAYTEST_CHECKLIST.md` (5 min), `docs/blind_feel.md` | Overrules both for feel/juice | Every feel/juice change; never touch `tools/verify/gates*` for feel |

Feel/juice changes never modify gate values (AGENTS.md Rule 5). Gates
only update when a MEASURED RocketSim run disagrees.

## Owner-based selection (mirrors CONTRACTS.md v2)

- `src/sim|arena|physics`, `Tuning.h`, tapes → full physics bar + `make tests`.
- `src/render|camera|fx|models|audio|menu|settings|input`, `src/game/main.cpp`
  presentation paths, `config/` → touched `VG*` via `--only` + the matching
  contract test (`t_cam`, `t_atex`, `t_gp`, `t_kb`, `t_rt`). No `measured.py`.
- `tools/verify|tools/tas` harness edits → the suite they own, plus a
  dry-run (`make -n`) to prove the Makefile still parses.
- Docs-only → nothing. No build, no gates.

`make check-touched` (new Makefile target, backed by
`tools/verify/check_touched.py`) prints this mapping for the current
`git status` — it never runs gates itself.

## Skip rules

- No sim diff since last green (same `build/headless_sim` checksum over
  sim-owned files) → cite the checksum, skip physics. Camera changes are
  presentation by rule and never invalidate physics.
- `make all` is the *end-of-work* bar, not the per-edit loop. During work:
  build the single target (`make build/game`, one `/tmp/t_*`), run the
  single gate (`--only VGx`), full bar once at the end.
- `timeout 1800 visual_gates.py` is banned as a foreground step. Use
  `--only` per step, foreground, bounded (`timeout 120`). Background jobs
  must be collected (`job_output`) or killed (`job_kill`) in the same step —
  never fire-and-forget.
- One-shot environment probes (`find /`, `vulkaninfo`, `glxinfo`,
  `gamescope --help`) are recorded once, not re-run per prompt. Scope all
  searches (`path`/`include`); never a bare repo-root grep (see
  AGENTS.local.md — `third_party/` is ~234 MiB).

## Resumability (prompts die; logs don't)

- Long work runs in ≤5-min steps, each ending with an append to the owning
  `docs/*_LOG.md` + `todo_write` state, so a nudge costs one log read.
- Long objectives get a `create_goal`; `update_goal resume` after any
  session resume/fork (goals come back disarmed).
