# MASTER PROMPT — Arena 1:1 from the user's own install (`--arenadir`)

> **Status: CLOSED at A8 with A7 DEFERRED (2026-10-20, rev 2).** What
> landed: **A1** all 119 `StaticMesh` exports parse (there was no second
> layout — the `BoolProperty` value byte made the stream byte-aligned, not
> 4-byte-aligned); **A2** the 12 B/vertex block is 2×`FPackedNormal`
> (tangent, normal) + 2×binary16 UV, and the 24-byte section table is
> located by content (119/119 meshes, 174 sections); **A3/A3b** the material
> graph is **stripped at cook time**, so the measured diffuse coverage is
> **248/640 = 38.75 %** of the used section slots — the **≥90 % target is NOT
> met and is proven unreachable** from this install; **A4** placements
> complete (451 + 26 imported, 112 meshes, **0 orphans**; the 650 zeroed
> export slots are filtered by any census); **A5** the pack
> `arena_assets/stadium_day/{arena.json,arena.bin}` (gitignored),
> byte-identical reruns, `--verify` 19/19, textures stored compressed
> (DXT1 × 8, DXT5 × 5); **A6a** the C++ reader
> `src/models/ArenaRip.{h,cpp}` (CPU DXT byte-identical to `rldds.py` 13/13,
> 796 contract checks PASS, loud SKIP when the pack is absent); **A6b** the
> engine: `--arenadir DIR` + `[arena] arena_dir` (flag > ini), a loud FATAL
> naming `arena.json` with exit 1 for a bad dir, 96 static buckets uploaded
> once (`GL_STATIC_DRAW`), fixed-function + `GL_NORMALIZE` +
> `glGenerateMipmap`, frustum cull — `tris=749325 draws=258 glerr=0` with the
> flag, `tris=188544 draws=189 glerr=0` without.
> **A7 is DEFERRED by owner decision**: no fidelity captures, no A/B perf
> verdict lines, and `tools/verify/arena_gates.py` (AN1–AN7) does not exist
> yet. **A8 (close-out)**: hard tier green and unchanged
> (`measured.py` 19/19, `mesh_gates.py` 13/13, `arena_test` 55/55,
> `verify_vs_rocketsim.py` 110/110, `make tests` exit 0), VG1 measured and
> reported with its cause, Rule 1 audited, and the docs refreshed
> (`docs/ARENA_RIP.md`, `docs/ARENA_PACK.md`, `docs/THIRD_PARTY.md`,
> `docs/INI_SCHEMA.md`); the round record is
> `docs/ARENA_RIP_LOG.md` § "Round A8 (2026-10-20) — close-out".
> **The VG1 baseline shift is NOT the arena phase** — the arena phase is
> **byte-neutral** to VG1; the shift comes from a pre-existing uncommitted
> **V11** one-sided-shell / `GL_CULL_FACE` change in `src/models/ArenaShell.cpp`
> that the owner must commit (re-baselining VG1) or revert.
> **"1:1 arena" is an opt-in local render mode, not a redistributable asset
> pack** — the pack is Psyonix-owned, gitignored, and read at runtime.
> Read order: `AGENTS.md` → `CONTRACTS.md` v2 → `docs/GATE_POLICY.md` →
> `docs/ARENA_RIP.md` (the measured facts base — **read it before touching
> anything**) → this file → `docs/VISUAL_PARITY_LEDGER.md`.
> Load before any render/model edit: `raw-cpp-opengl-compatibility-builder`.
> Physics M1–M6, Visuals V1–V6, Fixes V1–V8, Camera-exact are **CLOSED**. This
> prompt extends them; it never re-runs them.

Machines (fixed, do not re-probe per prompt): **A = `AMD Radeon R4 (radeonsi,
stoney, ACO)`, Mesa 26.2.4, GL 4.5 Compat** (the performance floor; every perf
claim needs an `A` verdict) and **B = `RTX 4060 Laptop`, NVIDIA 615.71.09, GL
4.6**. Quote `GL_VENDOR/RENDERER/VERSION` with every perf line.

## 1. Mission

Render **DFH Stadium (Day), 1:1, from the owner's own Rocket League install**,
loaded at runtime through `--arenadir`, with zero Psyonix bytes in the
repository and every gate still running the clean-room path.

Today only `--meshdir` exists (collision meshes), the visual arena is the
analytic shell in `src/models/ArenaShell.cpp`, and `--arenadir` **does not
exist** — `src/game/main.cpp:221` rejects it as an unknown arg. Getting to 1:1
means, in order:

1. **Structure-complete**: all 119 `StaticMesh` exports of the package parse
   (positions + normals + UVs + indices + per-section material index), not the
   12 that parse today.
2. **Placement-exact**: all 76 `StaticMeshActor` and all 1 160
   `StaticMeshComponent` placements (5 `StaticMeshCollectionActor` containers)
   reproduce the map's transforms and per-instance material overrides.
3. **Texture-exact**: the `MaterialInstanceConstant` → `Material` →
   `TextureSampleParameter2D` → `Texture2D` chain resolves per section, and
   DXT1/DXT5 decodes to RGBA8 with the right flags.
4. **Recognisable + performant**: a capture of the ripped stadium is
   indistinguishable in composition from the real one, and holds the bar on A
   and B (§2.6).

### What "1:1" means here — and what it does not

- **Means**: the *owner's machine* draws the real arena — real mesh topology,
  real placements, real textures. Structure, placement and texture are each
  gated (§5, AN1–AN3).
- **Does not mean**: redistributable assets, or that a gate ever sees them. The
  repo stays MIT-clean; the clean-room analytic shell stays the only path the
  gates and `measured.py`/`visual_gates.py` ever exercise. "1:1 arena" is a
  *local render mode*, not a shipped asset pack.
- **Does not touch physics.** Collision stays RocketSim's; `--meshdir` behaviour
  is unchanged; no `src/sim|arena|physics` file may change for this prompt.

## 2. Hard constraints (breaking one fails the phase)

1. **Rule 1 — repo stays MIT-clean, no exceptions.** No `.upk`, no extracted
   mesh/texture byte, no pack, no `Stadium_Day_P_MeshObjects.json`-style
   transform dump, no copied UV/normal array enters the tree. Output dirs are
   gitignored (`arena_assets/` — add it, §5 A5). The package key is **never**
   committed: `~/.rl-decomp/upk.key` or `$RL_UPK_KEY` only (already enforced by
   `tools/decomp/rlupk.py:load_key()`). Verify with `git check-ignore -v` plus a
   tree scan gate (AN7) before claiming done.
2. **Sim frozen.** `src/sim`, `src/arena`, `src/physics`, `Tuning.h`, tapes, and
   the sim-path compile flags stay untouched. The renderer reads `Snapshot`
   only. A missing sim field needs a read-only `Sim.h` accessor + a failing
   test first, logged in `CONTRACTS.md` (cross-owner Rule 1).
3. **Gates are the bar, never the clay.** Hard tier (`measured.py` 19/19,
   `mesh_gates.py` 13/13, `arena_test` 55/55, `verify_vs_rocketsim.py` 110/110,
   `make tests`) must stay green and *unchanged*; this prompt touches no sim
   file, so prove it with `make check-touched` + sim checksums. Advisory tier
   (`visual_gates.py` VG1–VG16) blocks presentation work. Never edit a gate
   value to make a phase pass.
4. **Default path stays byte-stable.** `VG1` must remain byte-identical
   (`md5=0a2bbb529ea559daaf5c922efc3ba4b8`) with no `--arenadir`. The ripped
   arena is a **new opt-in code path**; it never becomes default, never enters a
   gated capture, and cannot perturb the analytic shell's output. Any VG1 change
   = phase rejected.
5. **GL 4.5 Compatibility, hard target.** Ripped arena rendering lives inside the
   existing 4.5-compat context (fixed-function or GLSL ≤ 4.50 + VAO/VBO, per
   `docs/RENDER_POLICY.md`). No core-profile context, no 2.1/3.3 path, no Vulkan,
   no llvmpipe. A programmable arena path must clear **PG1** before it may be
   default or gated.
6. **Budget measured, not assumed.** `TRI_CAP=240000` (VG7) is the clean-room
   cap; the ripped arena gets its own measured cap (AN5) set *from* the first
   successful full-map capture, not guessed. Report `tris/draws/glerr/frame_ms`
   from `Renderer::Stats3D` every phase. Arena draws must stay inside the G3
   merged-bucket philosophy: static upload once, one draw per bucket per frame —
   **no per-mesh per-frame `glBegin` storm** (the real map has 119 meshes ×
   instances; naive drawing is thousands of draws).
7. **Determinism + pacing.** Same seed+tick+`--cam` = same pixels; ripped-arena
   rendering must not consume sim RNG; `drops=0` on the 60 s soak; one
   `glGetError` per frame in `Renderer::EndFrame`; no per-frame compile, sync
   readback or blocking load.
8. **INI schema v1 is normative.** `[arena] arena_dir` extends
   `config/settings.ini` + `docs/INI_SCHEMA.md`; roundtrip test stays green.
   **Doc debt to fix while you are there**: `[arena] mesh_dir` exists in
   `config/settings.ini:64` but is *missing* from the `[arena]` table in
   `docs/INI_SCHEMA.md:150` — add both rows.
9. **Owner boundaries (CONTRACTS.md v2).** `src/models`, `src/render` are
   presentation; `src/game/main.cpp` + `config/` coordinator; `tools/verify` +
   `tools/arena` harness; `src/settings` config. 1–2 files per builder
   subagent; cross-owner needs a failing test first.
10. **Cheap proof (AGENTS.local.md).** `source tools/dev-local.sh &&` prefix;
    single target (`make build/game`) + single gate (`--only VGx`, `timeout 120`);
    `make check-touched` before verifying; ≤5-min steps each ending in a
    `docs/ARENA_RIP_LOG.md` append + `todo_write`; collect/kill background jobs
    in the same step; scope every grep (`path`/`include`; `third_party/` is
    234 MiB); symbol lookup before whole-file reads.

## 3. Measured facts base (do NOT re-derive — from `docs/ARENA_RIP.md`)

Reproduce any of these with `tools/arena/rlmesh.py` and `tools/decomp/rlupk.py`.

- **Package**: `Stadium_Day_P.upk`, 13 627 179 B; file_version **868**,
  licensee **34**; 2007 names / 347 imports / 3251 exports; inflated image
  35 185 084 B over 29 chunks; **serial-offset bias 4 250**
  (`rlupk.Package.data()` already applies it).
- **Inventory**: 119 `StaticMesh` (15.11 MB), 62 `Texture2D` (4.11 MB),
  84 `Material`, 58 `MaterialInstanceConstant`, 76 `StaticMeshActor`,
  5 `StaticMeshCollectionActor` (1 160 `StaticMeshComponent`), 18
  `LightMapTexture2D` (10.17 MB).
- **Tagged properties** (solved): preamble of 4 B (actors, meshes) or 8 B
  (components); UE3 tag = `FName`(int64) + type `FName` + `int32 Size` +
  `int32 ArrayIndex` + value. `ByteProperty` and `StructProperty` carry an
  **extra 8-byte FName inside the tag** before the value. The block terminates
  on a `None` tag whose type/size fields are **garbage** — test the *name*
  first, or the walk dies.
- **Geometry** (partial, solved for 12/119): positions are plain `float3`
  immediately preceded by `int32 [count=N][12][N]`; indices are
  `int32 count` + `uint16[count]` with every index `< N` and `count % 3 == 0`;
  the stream after the positions holds **FPackedNormal** bytes (values
  `0x7f`/`0x80`/`0xff`) — that is the normal+UV stream.
- **The denormal trap** (cost two false starts): RL writes small ints (12, 150)
  into float slots; they read back as `1e-44`-ish denormals, and a naive
  `|v| < 30000` "is this a coordinate" test accepts them and invents fake
  position buffers. Any coordinate predicate must exclude denormals.
- **Coverage + proof**: `python3 tools/arena/rlmesh.py <Stadium_Day_P.upk>`
  resolves 12 of 119 with RL-truthful bounds — field half-length 4096, goal
  mouth ±2688, centre circle 707, `DFH_Day_StadiumBase2` ±31 000. Those numbers
  are the acceptance oracle for A1.
- **Unresolved**: 107 meshes (city/crowd/grass/stands) have **no** `(N, 12, N)`
  anchor anywhere in the export, and no count/stride variant either. Two live
  hypotheses: **multi-LOD ordering** (a second LOD block with different counts)
  or the **quantised `bUsePackedPosition` path** (int16 positions + `Mins`/
  `Extents` floats). This is the crux of A1.
- **Not started**: normals/UV decode, materials/textures, transforms, pack
  format, `--arenadir` itself.

## 4. Execution model — subagents

The orchestrator does not edit inline. One subagent = one phase-step = 1–2
files. Independent steps launch together; keep working while they run; collect
with `job_output` (wait only when genuinely blocked) and `job_kill` what stopped
mattering. Even sequential chains (A1→A2→A3) go through subagents, each spawned
with the previous step's returned paths + checksums.

Every delegation prompt states: (a) exact files owned, (b) forbidden files,
(c) failing-test-first when cross-owner, (d) gate command + expected PASS line,
(e) log file + lines to append, (f) return contract (paths, checksums,
`verts/tris/draws/glerr`, gate output), (g) the facts above it must quote back
rather than re-derive. **No return contract = failed delegation.**

Parallelism: A1 (layout) blocks everything; A3 (materials/textures) and A4
(transforms) can run *after* A1 in parallel with A2; A5 (pack) after A2+A3+A4;
A6 (loader) after A5; A7 (capture/fidelity) after A6; A8 (close-out) last.

## 5. Phase briefs (copy-paste into subagent `prompt`)

### A1 — Finish the mesh layout (107 unresolved meshes) — **the crux**
Owns `tools/arena/rlmesh.py`. Find the second layout. Highest-value probes, in
order: (1) diff a resolving mesh against a non-resolving one of similar size;
(2) test `bUsePackedPosition`: look for a `Mins`/`Extents` `float3` pair and an
int16/quantised position run, decode
`pos = Mins + (v/65535) * (Extents-Mins)` style; (3) test multi-LOD: does a
non-resolving export contain **two** coordinate runs with separate headers?
Acceptance: `python3 tools/arena/rlmesh.py <pkg>` reports **119 resolved, 0
unresolved**, every mesh's `bytes_read` ≤ `export size`, and the known-bounds
oracle still matches (§3). Forbidden: hard-coding per-mesh offsets, ignoring a
mesh back to "unresolved" without a written reason.
Return: resolved count, the layout rule in words, per-mesh `verts/tris` table
hash, and the 3 meshes whose bounds you spot-checked against RL truth.

### A2 — Normals + UVs + sections
Owns `tools/arena/rlmesh.py`. Decode the FPackedNormal stream (12 B = 3 packed
normals, then UVs, then a colour int) and the per-section table
(`material index`, `first index`, `num faces`). Existing measured clues: after
the positions, the ints `1, 12, 150, 0, 12, 150` precede packed-normal bytes in
`Field_STD_Collision_SideBot` — pin down what each field means before coding.
Acceptance: every resolved mesh yields `len(normals) == len(uvs) == num_verts`,
unit-length normals (`|n| ∈ [0.98, 1.02]`), UVs finite and inside
`[-64, 64]`, sections tile `[0, num_indices)` with no gaps/overlap.
Return: rule description + a validation table (mesh, verts, normals ok, uvs ok,
sections cover).

### A3 — Materials + textures (parallel with A2)
Owns `tools/arena/rlmesh.py` (+ a new `tools/arena/rldds.py` if DXT decode
needs its own home). Resolve `StaticMeshComponent`/section material →
`MaterialInstanceConstant` (overrides) → `Material` → `TextureSampleParameter2D`
→ `Texture2D`; decode DXT1/DXT5 (and uncompressed `PF_A8R8G8B8`/`PF_B8G8R8A8`)
to RGBA8, honouring `SRGB` and `bDirty`-style flags only if measured.
Acceptance: ≥90 % of *used* sections resolve to a diffuse texture; a decode
self-test (known solid-colour DXT block) in the module; report the unresolved
list with reasons (`no diffuse`, `virtual/param mask`, `unsupported format`).
Forbidden: guessing a material by name, shipping any pixel data in the tree.
Return: resolution %, format histogram, unresolved reasons.

### A4 — Placements (parallel with A2)
Owns `tools/arena/rlmesh.py`. Parse the 76 `StaticMeshActor`
(`Location`, `Rotation`, `DrawScale`/`DrawScale3D`, `StaticMeshComponent` ref)
and the 1 160 collection components (`Translation`, `Rotation`/`Scale3D`,
`StaticMesh` ref, material overrides). Then compose each instance's world
matrix in the repo's convention — **note the known trap from the Fennec
converter: compose column-major, and never put translation into normals**
(`docs/VISUAL_FIXES_LOG.md`).
Acceptance: every placed instance names a mesh that exists in the parse; report
instance count, distinct meshes used, and the transform range
(`|t| < 40000`, scales `∈ [0.01, 100]`); spot-check that field collision meshes
sit at X≈±4096.
Return: instance count, orphan count, transform sanity table.

### A5 — Pack format + rip CLI
Owns `tools/arena/rip_arena.py` (new) + `docs/ARENA_PACK.md` (new) + `.gitignore`
(`arena_assets/`). Write a **versioned, documented, deterministic** pack: a
manifest (`arena.json`: format version, source package + build identity
(engine/licensee), mesh table, material/texture table, instance table) plus a
binary blob (`arena.bin`: vertex/index/UV/normal arrays + RGBA8 textures).
Determinism: byte-identical output for the same package (sorted keys, fixed
endianness, no timestamps).
Acceptance: `python3 tools/arena/rip_arena.py --pack <pkg> --out arena_assets/stadium_day`
produces a pack whose `manifest.mesh_count == 119`; a `--verify` mode re-reads
the pack and asserts every array length agrees with the manifest; `git
check-ignore -v arena_assets/stadium_day/arena.json` proves it is ignored; no
file under the repo tree outside `arena_assets/` is created.
Return: pack sizes, manifest hash, verify output.

### A6 — `--arenadir` loader in the engine
Owns `src/game/main.cpp` (arg + wiring), `src/models/ArenaRip.{h,cpp}` (new,
presentation), `src/render/Renderer.*` (draw path), `config/settings.ini` +
`docs/INI_SCHEMA.md` (schema v1 extension), `tests/` (one contract test).
Contract: `--arenadir DIR` and `[arena] arena_dir`, precedence flag > ini; a dir
with no loadable pack is a **loud `FATAL` naming the expected manifest**, exit
non-zero — never a silent fallback to the analytic shell; when the flag is
absent, **nothing changes** (VG1 byte-identical). Static upload once
(`GL_STATIC_DRAW`), one draw per material/bucket per frame, textures with
`glGenerateMipmap` (G4 precedent), `GL_NORMALIZE`-safe lighting or the existing
GLSL path. Also fix the `[arena] mesh_dir` schema debt (§2.8).
Acceptance: `build/game --arenadir arena_assets/stadium_day` runs and draws the
stadium; `build/game` with no flag passes `--only VG1` byte-identical; a bad dir
exits non-zero with the `FATAL` line; roundtrip test green with the new key.
Return: startup log excerpt, `Stats3D` (`tris/draws/glerr`), VG1 md5 for both
modes, FATAL line for a bad dir.

### A7 — Fidelity captures + perf verdict
Owns `tools/verify/arena_gates.py` (new) + `docs/ARENA_RIP_LOG.md` append +
captures under the (gitignored) asset dir. Produce side-by-side captures of the
ripped arena from fixed `--cam` poses (same poses the clean-room parity
captures use) and record `frame_ms p50/p99`, `drops`, `glerr`, `tris`, `draws`.
Acceptance: A and B each get a verdict line
`ARENA on <machine>: 60FPS YES/NO @720p (p99 …ms, margin …)`, `drops=0`,
`glerr=0`, and the capture set is reproducible (`--shot-at-tick` pinned).
Return: verdict table (2 machines), capture paths + md5s.

### A8 — Close-out (`verify-and-stop`, runs last)
Owns docs only. Prove: hard tier unchanged (`measured.py` 19/19,
`mesh_gates.py` 13/13, `arena_test` 55/55, `verify_vs_rocketsim.py` 110/110,
`make tests`), `visual_gates.py --only VG1` byte-identical to the recorded
baseline, `arena_gates.py` AN1–AN7 green or loudly `SKIP` (clean-room machines
without a pack must `SKIP`, never fail), `git status` clean of asset bytes,
`docs/THIRD_PARTY.md` + `INI_SCHEMA.md` + `ARENA_RIP.md` + `ARENA_PACK.md`
updated, `ARENA_RIP_LOG.md` round summary written.
Return: PASS/SKIP table with exact lines, checksums, and the A/B verdict table.

## 6. New gates (`tools/verify/arena_gates.py`, advisory tier)

All of these **SKIP loudly (exit 0)** when no pack is present — they must never
be the reason a clean-room machine fails.

| Gate | What it proves |
|---|---|
| **AN1** | Structure completeness: every mesh in the pack parses; `verts > 0`, `tris > 0`, indices in range, sections tile the index buffer. |
| **AN2** | Normal/UV sanity: `|n| ≈ 1`, UVs finite, no NaN anywhere in the pack. |
| **AN3** | Placement sanity: every instance references a packed mesh; transforms finite and inside the documented range; placed field meshes land at X≈±4096. |
| **AN4** | Texture resolution: ≥90 % of used sections resolve to a decoded RGBA8 texture; every texture's byte length == `w*h*4`. |
| **AN5** | Measured budget: `tris ≤ ARENA_TRI_CAP` and `draws ≤ ARENA_DRAW_CAP`, both set from the first full-map capture and recorded in the gate file with its provenance. |
| **AN6** | Perf bar: `drops=0`, `glerr=0`, and the PG1 thresholds (A: p99 ≤ 16.67 ms; B: p99 ≤ 8.33 ms) on the `--arenadir` run. |
| **AN7** | Rule 1: no pack/asset byte is tracked by git (`git ls-files` scan) and the key is absent from the tree. |

Also extend `tools/verify/check_touched.py` so `tools/arena/**` maps to the
advisory tier + `arena_gates.py`, and `arena_assets/**` maps to nothing
(ignored input, never verified).

## 7. Definition of done

- [ ] A1: 119/119 meshes parse; bounds oracle matches §3.
- [ ] A2: normals/UVs/sections validated on all 119.
- [ ] A3: ≥90 % of used sections resolve to a texture; DXT self-test green.
- [ ] A4: 76 + 1 160 placements compose; no orphans; field meshes at X≈±4096.
- [ ] A5: pack reproducible byte-identically; `--verify` green; gitignored.
- [ ] A6: `--arenadir` loads, bad dir is a hard FATAL, no-flag VG1 byte-identical,
      INI roundtrip green, `mesh_dir` schema debt fixed.
- [ ] A7: A and B verdict lines with captures + `drops=0 glerr=0`.
- [ ] A8: hard tier unchanged, AN1–AN7 green or loud SKIP, docs/THIRD_PARTY/INI
      updated, `ARENA_RIP_LOG.md` closes the round.
- [ ] Repo contains **no** Psyonix byte: `git ls-files | grep -i 'upk\|arena_assets'`
      is empty; `docs/ARENA_RIP.md` still describes the split truthfully.

## 8. Non-goals / do not

- Do **not** touch `src/sim|arena|physics`, `Tuning.h`, tapes, or gate values.
- Do **not** re-run M1–M6, V1–V8, or the camera-exact passes.
- Do **not** make the analytic shell or the ripped arena the default silently —
  the flag is opt-in forever unless an owner decision changes it in writing.
- Do **not** ship an external exporter's output as "the" pipeline without a
  `docs/THIRD_PARTY.md` row: if UE Viewer / UModel is ever tried (RL packages are
  encrypted; forum reports say UModel breaks past licensee ~22 and this install
  is **34**), it is a local, offline, GPL tool — never linked, never vendored,
  and its output still lands only in the gitignored asset dir.
- Do **not** claim parity from a gate. The owner's eyes are the authority
  (`docs/PLAYTEST_CHECKLIST.md`); a screenshot that "looks right" is a claim to
  verify against the bounds/oracle numbers, not a substitute for them.

## 9. Handoff

Start with A1 — everything else is blocked on it, and it is the only step whose
outcome is genuinely uncertain. Budget it as a spike: if two independent layout
hypotheses (packed-position, multi-LOD) are both falsified within the first
session, stop and record the negative result in `docs/ARENA_RIP_LOG.md` rather
than building the pack/loader on 10 % coverage (that mistake was already avoided
once — see the rev-1 history in `docs/ARENA_RIP.md`). A partial arena is worse
than the analytic shell: missing stands/crowd/city meshes read as broken, not
as "in progress".
