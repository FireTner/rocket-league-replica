# MASTER PROMPT — GL 4.5 hard-target + pipeline optimize (RLViser-inspired)

> **Status: OPEN (2026-10-17, rev 2).** Owner decisions recorded: (a) hard-target **GL 4.5 Compatibility, no fallback** — supported machines are **only** this `AMD R4 / Mesa 26.2.4` box and the `RTX 4060 / NVIDIA 615.71.09` box; (b) **no UDP thin-viewer**; (c) **PBR / cascades / atmosphere / sun-disk enter as optional experimental flags** to answer "60 FPS on the R4?" with measurements. Physics M1–M6 CLOSED, Visuals V1–V6 + Fixes V1–V8 CLOSED — this prompt extends them, never re-runs them.
> Read order: `AGENTS.md` → `CONTRACTS.md` v2 → `docs/GATE_POLICY.md` → this file → `docs/VISUAL_PARITY_LEDGER.md` + `docs/CAMERA_NOTES.md`.
> Load before any edit: `raw-cpp-opengl-compatibility-builder` skill. For delegation use `cavecrew` (investigator/builder/reviewer), `lean-build`, `surgical-patch`, `safe-refactor`, `verify-and-stop` as labelled below.

Machine record (do not re-probe per prompt): **A = `AMD Radeon R4 (radeonsi, stoney, ACO, DRM 3.64)`, Mesa 26.2.4-arch1.1, GL 4.5 Compat** (Archcraft rolling, `SDL 2.32.74`); **B = `RTX 4060 Laptop, NVIDIA 615.71.09, GL 4.6`**. All perf claims quote **both** `GL_VENDOR/RENDERER/VERSION` strings. Hard fail (not fallback) when a 4.5-compat context cannot be created.

## 1. Mission

Get to **1:1 Rocket League feel** on machines A + B only by making the pipeline do what RLViser proves works:

1. **GL 4.5-compat hard requirement** (all fixed-function code keeps working; VBOs/FBOs/GLSL available without a core break). No 2.1 path, no llvmpipe support, no Vulkan, no core-profile migration.
2. **Fewer, static draws**: arena `325 draws → <60` via merged static buckets (the portable version of RLViser's spawn-once). Per-frame only transforms move.
3. **Full mipmap chains + aniso** (our `GL_LINEAR`-only single level is the distant-net shimmer + bandwidth bug).
4. **Optional eye-candy flags, default OFF, each with a 60 FPS verdict on the R4**: PBR-ish specular/IBL-lite, cascaded shadow map, atmosphere/aerial-view LUT-lite, sun-disk + daylight cycle — all re-implemented in compat-safe GLSL, inspired by RLViser `camera.rs`/Bevy (MIT), never copied as engine code. Ship the verdict, not just the feature.
5. Camera gameplay numbers frozen (ours beats RLViser's debug rig — no copy); director-style dev view allowed only as a debug flag.
6. Every gate stays green; every change measured on **both** A and B.

"1:1" keeps the Visuals-pass meaning: **number-exact** (shell/pads/ball/camera defaults frozen) + **recipe-exact** (ported numbers cited) + **coverage-complete** (ledger row + gate or explicit why-not). Never claim asset parity.

## 2. Hard constraints (breaking one fails the phase)

1. **Rule 1 (AGENTS.md:43-54 + CONTRACTS.md Rule 4): repo stays MIT-clean — no Psyonix byte vendored/committed/redistributed, no exceptions in this prompt.** That means: no `cache/mesh/*.bin`, no `.tga` (ForcefieldHex, Ball_Default00, ENVPack…), no `Stadium_P_MeshObjects.json` transforms, no `Body_*_SK`, no `*_MIC` material dump, no copied UV/normal arrays enter the tree — even though the owner asked to "take" them. RLViser is a **cited source for facts** (layout dims, algorithms, performance shapes) exactly like CarBaller/BakkesModSDK — its **bytes are not**. The only lawful use of game art is the existing `--meshdir`-style split: the game **may read the user's own install at runtime from a gitignored asset-dir, never committed, never re-uploaded**, and **all gates run the clean-room path**. Subagents that cannot do their phase without game bytes must SKIP loudly and stay clean-room; "import the dump" is never an acceptable resolution. Every new dep in `docs/THIRD_PARTY.md` before use.
2. **Sim frozen.** `src/sim`, `src/arena`, `src/physics`, `Tuning.h`, tapes, sim-path flags (`-ffp-contract=off -fno-fast-math -fno-strict-aliasing`) untouched. Renderer reads `Snapshot` only. A missing sim field needs a read-only `Sim.h` accessor + failing test first (cross-owner Rule 1, log in `CONTRACTS.md`).
3. **Gates are the bar, never the clay.** Hard tier (`measured.py` 19/19, `mesh_gates.py` 13/13, `arena_test` 46/46, `verify_vs_rocketsim.py` 110/110, `make tests`) runs only on sim-touching changes — this prompt should never touch them; prove it with `make check-touched` + sim-file checksum. Advisory tier (`visual_gates.py` VG1–VG16, `--only VGx` per edit) blocks presentation work. Feel/juice never edits a gate value; gates move only on a MEASURED RocketSim disagreement (Rule 5).
4. **Backend policy (this prompt IS the owner decision §3 asked for).** Target is **4.5 Compatibility, hard fail**: request `GL 4.5 compat profile`, abort with a loud error when unavailable — no 2.1/3.3 fallback, no llvmpipe path, no core-profile-only code, no Vulkan. Fixed-function (`glBegin/glEnd`, `glPushMatrix`, `GL_NORMALIZE`) keeps running; VBOs/FBOs/GLSL 4.50-compat are first-class (not "opt-in behind version checks" — the version IS 4.5). Our arc-length UVs/normals stay; game-authored UVs/normals never enter. Eye-candy (G6) is GLSL but compat-safe (no `#version 330 core`-only pipeline).
5. **Determinism + pacing.** Same seed+tick+`--cam` = same pixels (`VG1`). Procedural noise seed-pinned, never consumes RocketSim RNG. `glGetError()` stays once per frame in `Renderer::EndFrame` — no per-draw checks. No blocking wait, sync readback, or shader compile in the frame loop. Soak keeps `drops=0`.
6. **Budget measured.** `TRI_CAP=240000` (VG7) holds. Every phase reports `tris/draws/glerr` delta from `Renderer::Stats3D`. Fewer, better draws — never silent geometry multiplication.
7. **INI schema v1 normative.** New knobs (`[render] msaa/merged_shell/pbr/shadows/atmosphere/sundisk/daylight`, `[camera] director`) extend `config/settings.ini` per `docs/INI_SCHEMA.md`, roundtrip test green, menu live-reload where camera/render already does. All eye-candy defaults OFF so gated captures stay on the clean-room fixed-function path unless a gate explicitly enables one flag.
8. **Owner boundaries (CONTRACTS.md v2).** `render|camera|fx|models|audio` presentation; `settings|input` config; `game/main.cpp` coordinator; `tools/verify|tas` harness. 1–2 files per builder subagent; cross-owner needs failing test first.
9. **Logs + cheap proof (AGENTS.local.md).** `source tools/dev-local.sh &&` + `rtk` prefix; single target (`make build/game`) + single gate (`--only VGx`, `timeout 120`); `make check-touched` before verifying; ≤5-min steps ending in `docs/*_LOG.md` append + `todo_write`; collect/kill background jobs same step; scope every grep (`path`/`include`, never bare root — `third_party/` is 234 MiB); symbol lookup before whole-file reads.

## 3. What we take from RLViser (and what stays out of the repo)

Source: `VirxEC/rlviser` v0.9.2, Bevy 0.18.1 + `bevy_framepace` ([Cargo.toml](https://raw.githubusercontent.com/VirxEC/rlviser/master/Cargo.toml)). Correction 2026-10-17 (owner right, earlier draft wrong): the Git tree does **not** ship game bytes — `./assets/*` + `./cache/*` are gitignored ([`.gitignore`](https://raw.githubusercontent.com/VirxEC/rlviser/master/.gitignore)); `src/assets.rs:umodel::uncook()` extracts the user's own `TAGame/CookedPCConsole` via UModel and `cache_handler.rs` generates `./cache/mesh/*.bin` locally (release embeds the locally built `cache.zip`). Accuracy comes from **runtime extraction**, same split as our `--meshdir`. Citable base: **`docs/rlviser-facts/README.md`** (facts only, no bytes) — subagents verify against it, never paste from it.

| Take (re-implemented in our GLSL/GL, MIT-safe ideas only) | Stays OUT of the repo (Psyonix-owned, refused even though requested) |
|---|---|
| static-once + transform-per-frame split; merged buckets | `cache/mesh/*.bin` (77 files, ~16 MB), `ForcefieldHex.tga`, `Ball_Default00_D.tga`, `ENVPack.tga`, car `Body_*_SK` |
| `NotShadowCaster`-style skip + material blacklist + 60 Hz limiter thinking | `Stadium_P_MeshObjects.json` placements/transforms/bindings |
| PBR-lite, cascade shadows, atmosphere/aerial perspective, sun-disk, daylight cycle, Reinhard tonemap — **as optional `[render]` flags, default OFF, each with an R4-60FPS verdict** | authored UVs (`ATTRIBUTE_UV_0` dumps), authored normals, `*_MIC` material graphs/names copied as data |
| director + spectator + TrackCar indexing as **dev tools** | their camera numbers as gameplay cam — **ours (270/100/-3/0.45 + dynamics) is more 1:1**; RLViser is a debug viewer, do not copy `300/150` |

Bevy/RLViser engine code itself is MIT — studying its *technique* (how cascades/tonemap/framepace are wired) is fine with attribution in `THIRD_PARTY.md`; copying Psyonix art through it is not. Lawful local-only path (unchanged): a gitignored asset-dir may read the user's own install at runtime; gates never use it.

Camera verdict for the ledger: RLViser ballcam/car-cam are hardcoded debug rigs with no stiffness/FOV-dynamics/transition/swivel/shake. Keep ours; director only as debug flag.

## 4. Execution model — SUBAGENTS ONLY (read this twice)

The main agent is an **orchestrator that never edits code inline**. Every task below runs in a subagent. This keeps main context cold while allowing big parallel + sequential workloads.

Rules:

- Use `subagent` for fresh briefs (pass full context: goal, files, acceptance, gates — child has no conversation memory) and `subagent_fork` for follow-ups on completed turns. Independent phases launch **together in one turn**; keep working while they run; collect with `job_output` (wait only when genuinely blocked), `job_kill` what stopped mattering.
- Even sequential chains (G2→G3→G4) go through subagents: orchestrator spawns G2-builder, collects, then spawns G3-builder with G2's result paths + checksums. Never collapse a chain into inline edits "to save a round".
- Size: one subagent = one phase-step = 1–2 files. Splits: `investigate-first`/`cavecrew-investigator` to locate, `lean-build`/`cavecrew-builder` or `surgical-patch` for the edit, `caveman-review`/`cavecrew-reviewer` for the diff, `verify-and-stop` for gate proof. Structural moves use `safe-refactor` (verification brackets the move).
- Each delegation prompt must state: (a) exact files owned, (b) forbidden files, (c) failing-test-first if cross-owner, (d) gate command + expected line, (e) log file + lines to append, (f) return contract (paths changed, checksums, `tris/draws/glerr`, gate output), **(g) the `docs/rlviser-facts/*.md` file(s) the child must verify against + the availability sentence it must quote back**. No return contract = failed delegation.
- Owner feature loop: **every time the owner names a feature, the orchestrator's first line for that feature states the 1:1 availability** ("We have reliable 1:1 reference for X in `docs/rlviser-facts/<file>.md` (RLViser <file>:<symbol>, <value>)") before any plan or spawn. The facts base is the solid base the owner asked for — keep it cited, keep it fresh (new verified numbers land there first, then in the ledger).
- Parallelism map: G1 ∥ G2 ∥ G7-camera-spec can start together. G3 (merge) ∥ G4 (mipmaps) after G2 lands (different files, no conflict). G5 (pacing/counters) after G3+G4. G6 (eye-candy options) after G3+G4 (needs the merged buckets + version log as its test bed; split one subagent per flag). G8 (gates/docs) last, `verify-and-stop` style. No UDP phase — deleted in rev 2.

## 5. Phase briefs (copy-paste into subagent `prompt`)

### G1 — Baseline + probe record (investigator, docs-only)
Owns: nothing code. Record A + B `GL_VERSION/VENDOR/RENDERER` (A already taken 2026-10-17 — do not re-probe A; capture B once), `make build/game` checksum, `check_touched.py` mapping, `visual_gates.py --only VG1,VG7` + `measured.py` + `arena_test` green lines on A, current `tris=187982 draws=268` (VG7) and arena `~325 draws` claim with `Stats3D` evidence. Append to `docs/VISUAL_NOTES.md` + `src/render/BUILD_NOTES.md`. Return: two-machine baseline table. Gates: cite greens, no rebuild needed.

### G2 — 4.5-compat hard-target (builder: `src/game/main.cpp` SDL_GL attrs, `src/render/Renderer.cpp` init/version log)
Request `4.5 compatibility profile`, **abort loudly when unavailable (no fallback)**. Log `GL_VERSION/VENDOR/RENDERER` on startup (like soak log), expose `[render] msaa` INI (schema-extend + roundtrip green). Prove: `--shot-at-tick` capture + `VG1` + `VG7 glerr=0` on A **and** B. Forbidden: core-profile-only pipeline, fallback path, sim files.

### G3 — Merged static shell (builders, sequential sub-steps — still subagents)
Owns: `src/game/main.cpp` arena section + `src/render/Renderer.*` (+ optional new `src/models/ArenaShell.*` presentation-owned). Buckets: (a) opaque colored, (b) textured (turf/curb/hex/weave, our arc-length UVs), (c) transparent glass sorted last, (d) emissive/lines. Build once at startup into static VBOs (4.5-compat `GL_STATIC_DRAW`, no client-array fallback needed); per-frame one draw per bucket. Keep normals/`GL_NORMALIZE` until G6 proves its lighting replacement, preserve V5/V6 fixes (face-down ceiling, outward wall normals). Targets: draws `325→<60`, tris unchanged ±1%, `VG1` byte-stable (or documented re-baseline + owner sign), `VG3/VG6/VG7/VG15` green on A + B. Split into G3a-sweep, G3b-bucket, G3c-wire — one subagent each, chained.

### G4 — Texture chains (builder: `src/models/ArenaTextures.cpp`, `src/models/Models.cpp`)
`glGenerateMipmap` (4.5-core call, no `glu` fallback needed) + `GL_LINEAR_MIPMAP_LINEAR`, aniso when `GL_EXT_texture_filter_anisotropic` (cap 4–8x on R4, 8–16x on B), `GL_REPEAT` preserved, `RL_TEXDBG/RL_NOTEX` still work. Prove: `VG3/VG6` green + close/mid/far captures showing shimmer reduction + `VG7` bandwidth note on both machines. No new assets, no NPOT break, no game textures.

### G5 — Pacing + counters (builder: `src/render/Renderer.*`, coordinator log line)
Keep once-per-frame `glGetError`; extend `Stats3D` (`tris/lines/draws/glerr/frame_ms` + per-flag cost when G6 lands), limiter à la `bevy_framepace` for `--no-vsync` (`[render] max_fps`, default vsync-on unchanged). Prove on A + B: 60 s `--soak` + `VG7` (`drops=0 glerr=0`), `render_pacing.log` (V9) no longer sprays CWD or is documented.

### G6 — Eye-candy options, default OFF, each with an R4-60FPS verdict (builders, one subagent per flag, new `src/render/*Gl.*` preferred)
Re-implement (never paste engine code) behind `[render]` flags: **G6a** PBR-lite (specular + roughness + simple IBL ambient); **G6b** cascaded shadow map (2-cascade on R4, 3–4 on B, `NotShadowCaster`-style skips for glass/pads); **G6c** atmosphere + aerial-view tint + sun-disk + daylight cycle + Reinhard tonemap (RLViser `camera.rs` technique, our GLSL). Each flag: fixed-function path stays default; enabling one flag must not break `VG1` on the default path. Each subagent returns: stills on/off, `frame_ms p50/p99` + `drops` on A and B, and a verdict line `FLAG on R4: 60FPS YES/NO @720p (margin …)`. Keep gameplay cam numbers untouched; our generated UVs stay.

### G7 — Camera fidelity guard (investigator + `verify-and-stop`)
Write the ledger note: RLViser `300/150`, `280/110/-6°` vs ours `270/100/-3/0.45 + dynamics/transition/swivel/shake` with sources (`CAMERA_NOTES.md`, `udp.rs` lines). Add failing-first test if any `Camera.cpp` change proposed — default is **no change** except director dev flag + `[camera] director` INI. Prove: `t_cam` + `VG4/VG6/VG8` projection checks green on A.

### G8 — Re-baseline + close-out (`verify-and-stop` subagent, runs last)
Full bar on **A, plus VG1/VG7 spot-check on B**: `measured.py` 19/19, `mesh_gates.py` 13/13 (or loud SKIP), `arena_test` 46/46, `verify_vs_rocketsim.py` 110/110, `make tests`, `visual_gates.py` VG1–VG16, A `--soak` excerpt + `VG7` line, G6 verdict table (4 flags × 2 machines). Update ledger/notes/`THIRD_PARTY.md`/`INI_SCHEMA.md`/`BUILD_NOTES.md`, `PLAYTEST_CHECKLIST.md` 5-min pass. Return: PASS-line table + checksums + `tris/draws` before→after + eye-candy verdicts.

## 6. Definition of done

- [ ] Context `4.5-compat` on A and B, hard fail elsewhere (startup log + `VG1` captures from both).
- [ ] Arena draws `<60`, tris within ±1% of baseline, `VG7 tris_last/glerr` quoted for both machines.
- [ ] Mipmaps + aniso on, shimmer captures attached, `VG3/VG6` green.
- [ ] G6 verdict table: PBR / cascades / atmosphere / sun-disk each `60FPS YES/NO` on the R4 @720p (and B for reference); defaults OFF so the default path stays fixed-function clean-room.
- [ ] Gameplay cam numbers untouched (director dev flag only).
- [ ] Full bar green (table in G8) + ledger/notes/schema/`THIRD_PARTY.md` updated + `PLAYTEST_CHECKLIST.md` pass.
- [ ] No Psyonix byte in `git status`; `collision_meshes/` + asset dirs still gitignored; `rtk gain` + per-phase `BUILD_NOTES.md` entries present.

## 7. Non-goals

Vulkan or core-profile migration; 2.1/llvmpipe fallback; UDP thin-viewer (deleted); any vendored game asset, texture dump, or placement-JSON import (refused — local-only runtime dir is the only lawful path and gates never use it); physics/feature-parity (blast impulse, launcher UI, flip-reset detection, clock/countdown, multiplayer); menu restyle; crowd/props/decals; `tools/verify/gates*` value edits; `find /` or per-prompt `glxinfo` re-probes.
