# AGENTS.local.md — personal agent overlay (not part of the repo contract)

`AGENTS.md` still holds the rules. This file only tunes how navigation and
verification are done, to keep sessions fast and cheap.

- **Symbol lookup before file reading.** For "where is X / what does X do", call
  `tokensave_search` / `tokensave_body` / `tokensave_context` first. The graph
  covers `src/`, `tools/`, `docs/`, `tests/`; `third_party/RocketSim/libsrc/**`
  (bullet3) is excluded as vendored noise, so hits are project code.
- **Scope every grep.** `third_party/` is ~234 MiB and ripgrep runs with
  `--no-ignore --hidden`, so `.gitignore` does not shield it. Pass `path`
  (`src`, `tools`, `docs`, `tests`) or `include`, never a bare repo-root search.
- **Never bulk-read `third_party/`.** Read the cited RocketSim file+line ranges
  only (`read` with `offset`/`limit`).
- **Whole-file `read` is a last resort** for files over ~200 lines; slice with
  `offset`/`limit`.
- **Cheapest proof first.** `tests` is `.PHONY`, so `make all` ALWAYS
  recompiles + reruns every suite (100–300 s on 2 cores, never ~20 ms).
  Per edit use the single target (`make build/game`, one `/tmp/t_*`) plus
  the single gate (`--only VGx` / `check_touched.py`); `make check` once at
  the end. Tier rules live in `docs/GATE_POLICY.md` — camera/render edits
  never trigger physics gates.
- **Bounded steps, durable logs.** Long work runs in ≤5-min steps, each
  ending with an append to the owning `docs/*_LOG.md` + `todo_write` state.
  Background jobs are collected (`job_output`) or killed (`job_kill`) in
  the same step — never fire-and-forget. `timeout 1800` foreground steps
  are banned; visual gates go `--only VGx` with `timeout 120`.
- **Banned stall patterns.** No `find /`, no per-prompt `vulkaninfo` /
  `glxinfo` / `lspci` probes (record once). Scope every search with `path`
  / `include`. `run_offscreen` (45 s) + gamescope teardown can hang: fail
  fast to `SKIP`, don't retry it in a loop.
- **Gate selector.** Run `python3 tools/verify/check_touched.py`
  (or `make check-touched`) before verifying — it maps the dirty tree to
  the hard/advisory/harness/docs tier and prints the exact commands.
- **Renderer / camera / model / FX work:** load the
  `raw-cpp-opengl-compatibility-builder` skill before editing.
- **Shell output via `rtk` (mandatory).** `rtk` 0.51.0 is on `PATH`
  (`~/.local/bin/rtk`); the `dsh-bash-rtk` auto-routing plugin is NOT installed
  (blocked: it caps DSH at <0.2.0, this harness runs 0.2.0-rc.2), so prefix
  eligible commands with `rtk` by hand. Route every simple `git` (`status`,
  `log`, `diff`), build/test (`cargo test`, `pytest`, `vitest`, `jest`) and
  `pnpm` invocation through `rtk`; complex shell (pipes, `&&`/`;`, redirects,
  `$( )`) always runs native. Never regress to bare commands where an `rtk`
  subcommand exists. Check savings with `rtk gain` at session end.
- **Dev shell (mandatory).** Prefix every `bash` session that builds with
  `source tools/dev-local.sh &&` (one line: ccache, parallel make, git→rtk).
  Never edit the Makefile to add it.
