# Gate policy — tiered, owner-based verification (2026-10-17)

Why this exists: the full bar (~200 checks: `measured.py` 19 +
`mesh_gates.py` 13 + `visual_gates.py` VG1–VG16 + `arena_test` 55 +
`verify_vs_rocketsim.py` 110) costs 100–300 s per `make all` on a
2-core box, and history shows agents re-running all of it after every
edit — including physics gates on camera-only changes. V6 proved
green gates ≠ correct. So: gates are the regression bar, not the
oracle. Human playtest outranks them for feel.

## The three tiers

| Tier | What | Blocks? | When to run |
|---|---|---|---|
| **Hard** (physics) | `measured.py` 19/19, `mesh_gates.py` 13/13, `arena_test` 55/55, `verify_vs_rocketsim.py` 110/110, `make tests` | Yes, on sim-touching changes | Only when `src/sim`, `src/arena`, `src/physics`, `Tuning.h`, `tools/tas/tapes`, or sim-path flags change |
| **Advisory** (visual) | `visual_gates.py` VG1–VG16, relevant `--only VGx` | Yes, but only the touched `VG*` | On `src/render\|camera\|fx\|models\|audio\|menu`, `src/game/main.cpp` presentation paths |
| **Local only** (camera truth) | `camera_surface_gate.py` CS1–CS5; `camera_trace_gate.py` CT1–CT3 once P3 of `MASTER_PROMPT_CAMERA_EXACT.md` lands (SKIPs without `/decomp/traces/`) | **No** — SKIPs (exit 0) without `decomp/camera_surface.json`, which never ships | When `src/camera/` mechanisms change or RL patches its package (engine/licensee identity) |
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

## Supported machines (fixed — do not guess from the host)

| | Machine | GPU / driver | GL | Role |
|---|---|---|---|---|
| **A** | Archcraft laptop | `AMD Radeon R4 (radeonsi, stoney, ACO)`, Mesa 26.2.4 | 4.5 Compat | **performance floor** — every perf claim needs an A verdict (`60FPS YES/NO @720p`) |
| **B** | This dev box | `RTX 4060 Laptop`, NVIDIA 615.71.09 | 4.6 | correctness + reference numbers |

Both are targets. A <4.5 context is a **hard fail by design** (G2, no fallback),
so a gate that SKIPs because a context could not be created is an environment
result, never a PASS. Quote `GL_VENDOR/RENDERER/VERSION` with every perf line.

## Known gaps (recorded, not hidden)

- **`make check` swallows mesh-gate failures.** The target runs
  `python3 tools/verify/mesh_gates.py || true` (owner decision 2026-10-20), so a
  mesh FAIL cannot block that target. A missing mesh dir already exits 0 with a
  loud `SKIP`, so the `|| true` only masks real FAILs: **run
  `mesh_gates.py` bare before claiming the hard tier is green.**
- **PG1 — the programmable-pipeline perf bar — is policy, not yet a gate.**
  `docs/RENDER_POLICY.md` §3 fixes the numbers (A: `fps~ ≥ 60`, `p99 ≤ 16.67 ms`,
  `≤1.15×` the default run's `avg`; B: `p99 ≤ 8.33 ms`; both `drops=0`,
  `glerr=0`, VG7 `TRI_CAP` and a stable VG1 md5). Until `visual_gates.py`
  implements it, read those fields off the `[game] exit` line for both runs
  (default + programmable) and quote the machine strings. No programmable path
  may become default or appear in a gated capture before PG1 is green on A and
  B — recording the same numbers by hand does not substitute for the gate.
- **`gates_visual.json` is a partial record.** After the GRAPHICS45 pull it
  holds a single VG1 row (machine A, tolerance path); VG2–VG16 were dropped by
  that commit. Re-record on A and B before quoting a 16/16 bar — until then the
  VG1–VG16 *checker* is intact but the *evidence* is incomplete.
- **`visual_gates.py` overwrites its evidence file on every run.** The writer
  dumps only the current run's results to `tools/verify/gates_visual.json`, so a
  targeted `--only VGx` pass **erases** every other row (this is how the R4
  VG2–VG16 rows were lost upstream). Restore the record with one full-suite run
  per machine, and never commit a file produced by an `--only` pass.
- `arena_test` grew 46 → 55 checks in the same pull; older `46/46` figures in
  dated logs are historical and stay as written.

Spot checks recorded 2026-10-20 on **B** (post-pull): `VG1` PASS byte-identical
(`md5=0a2bbb529ea559daaf5c922efc3ba4b8`, NVIDIA 615.78.08 — note the driver
moved from the 615.71.09 in the machine record), `VG7` PASS
`ticks=7200 drops=0 glerr=0 tris_last=189144 draws_last=189`.

## Resumability (prompts die; logs don't)

- Long work runs in ≤5-min steps, each ending with an append to the owning
  `docs/*_LOG.md` + `todo_write` state, so a nudge costs one log read.
- Long objectives get a `create_goal`; `update_goal resume` after any
  session resume/fork (goals come back disarmed).
