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
- **Cheapest proof first.** `make all` no-ops in ~20 ms. Run the single gate for
  the area touched, then the full bar once at the end — not the whole suite after
  every edit.
- **Renderer / camera / model / FX work:** load the
  `raw-cpp-opengl-compatibility-builder` skill before editing.
