# ARENA_RIP — ripping a real arena out of the user's own install

Status: **IMPLEMENTED, A7 DEFERRED (2026-10-20, rounds A1–A6b landed; A8 is
the close-out).** Nothing here puts a Psyonix byte in the tree. The rip reads
the user's own install at runtime; any output is gitignored; every gate keeps
running the clean-room path (AGENTS.md Rule 1, `docs/THIRD_PARTY.md`).

Target: **DFH Stadium (Day)** = `Stadium_Day_P.upk` (13.6 MB,
`TAGame/CookedPCConsole/`) — the daytime skin of the original Soccar arena, so
its collision counterpart is already the dump the mesh gates use.

**What "1:1 arena" means here, truthfully:** a **local render mode**. The
owner's machine can draw the real stadium — real topology, real placements,
real diffuse textures — through `--arenadir DIR`. It is **not** a
redistributable asset pack: the pack is Psyonix-owned data, it lives under the
gitignored `arena_assets/`, and the repository stays MIT-clean. The clean-room
analytic shell (`src/models/ArenaShell.cpp`) remains the **only** path the
gates, `measured.py` and `visual_gates.py` ever exercise.

## Status table (MEASURED)

| Step | State | Evidence |
|---|---|---|
| Locate install / package | **OK** | `~/Games/Heroic/rocketleague/TAGame/CookedPCConsole/`, 18 156 `.upk` |
| Package key | **OK (local)** | `~/.rl-decomp/upk.key`, 64 hex digits, `tools/decomp/rlupk.py:load_key()`; **never** in the tree (A8 Rule-1 scan) |
| Decrypt + inflate | **OK** | `2007 names, 347 imports, 3251 exports, image 35 185 084 B, 29 chunks, bias 4250` |
| Inventory | **OK** | 119 `StaticMesh` (15.1 MB), 62 `Texture2D` (4.1 MB), 84 `Material`, 58 `MaterialInstanceConstant`, 76 `StaticMeshActor`, 5 `StaticMeshCollectionActor` |
| Tagged properties | **OK** | below |
| Mesh **positions + indices** | **OK: 119/119** | A1 — there was no second layout; the `BoolProperty` value byte made the stream byte-aligned, not 4-byte-aligned |
| Normals / UVs | **OK: 119/119** | A2 — the 12 B/vertex block is 2×`FPackedNormal` (tangent, normal; `b/255*2−1`) + 2×IEEE-754 binary16 UV; **329 472** normals decoded over 119 meshes, only 10 outside `[0.98,1.02]` (all in `SkyScrapers_02_Combined`, proven to be source data — null `(0,0,0)` normals — not a decode error) |
| Sections | **OK: 119/119, 174 sections** | A2 — the **24-byte** record (`MinVertexIndex`, `MaxVertexIndex`, `Flags`, `MaterialIndex`, `FirstIndex`, `NumTriangles`), located by content, never by a fixed offset |
| Materials / textures | **PARTIAL — 38.75 %** | A3/A3b — the cook **strips the material graph**; 248 of 640 used section slots resolve to a decoded diffuse. The ≥ 90 % target is **NOT met** and is **proven unreachable** (see below) |
| Actor/component transforms | **OK** | A4 — 451 placements + 26 imported, 112 distinct meshes, **0 orphans**; UE3 rot order `Rz*Ry*Rx`, column-major `T*R*S`; the 650 zeroed export slots are filtered by any census |
| Pack format + writer | **OK** | A5 — `arena_assets/stadium_day/arena.json` (807 793 B) + `arena.bin` (29 121 416 B), gitignored; byte-identical reruns; `--verify` 19/19 |
| C++ reader | **OK** | A6a — `src/models/ArenaRip.{h,cpp}`; CPU DXT decode **byte-identical to `rldds.py` 13/13**; 796 contract checks PASS; loud SKIP when the pack is absent |
| `--arenadir` in the engine | **OK** | A6b — flag > `[arena] arena_dir`; a bad dir is a **loud FATAL naming `arena.json`, exit 1**; 96 static buckets uploaded once (`GL_STATIC_DRAW`); fixed-function + `GL_NORMALIZE` + `glGenerateMipmap`; frustum cull |
| Fidelity captures + A/B perf verdict | **DEFERRED (A7)** | owner decision — A7 is skipped; `tools/verify/arena_gates.py` AN1–AN7 does not exist yet |

### The measured engine result (A6b, machine B)

```
[game] arena pack: dir=arena_assets/stadium_day source=Stadium_Day_P meshes=119(+4 external) placements=451(+26 external) materials=83 textures=13
[arena] pack uploaded: buckets=96 (textured=48 fallback=48) textures=13 placements=451 skipped=0 slots=640 tris=653864
[arena] exit buckets=96 visible=78 culled=18 textured=48 fallback=48 tris=653864
[game] exit ... tris_last=749325 draws_last=258 ... glerr=0 ...
```

`tris=749325 draws=258 glerr=0` **with** `--arenadir`;
`tris=188544 draws=189 glerr=0` **without** it. The ripped arena is a **new
opt-in code path**; with the flag absent nothing is read, no GL object is
created and the default path is byte-identical (A8 re-measured: VG1 md5 is
unchanged by the whole arena phase — see `docs/ARENA_RIP_LOG.md` round A8).

### The ≥ 90 % diffuse target is NOT met — and cannot be

A3/A3b measured **248 of 640 used section slots = 38.75 %** resolving to a
decoded diffuse. The gap is **missing data, not a missing rip**: the cook
stores **no texture expression at all** for the remaining materials, and that
is true of *every copy* of those materials in the whole install (a head-only
sweep of all 12 016 readable files). Recovering them would mean *guessing the
texture by name*, which the work order forbids. The 392 unresolved slots break
down as `untextured-by-design` 331, `external:CustomColors` 34 (a runtime
scripted texture, not pixels), `param-mask` 16, `external:Materials` 9,
`null-slot` 2. **The remaining 61.25 % of used section slots render on the
pack's documented neutral-grey fallback material.**

## Tagged properties (MEASURED)

Cooked RL objects are a small preamble (4 bytes for actors and meshes, 8 for
components) followed by UE3 property tags: `FName` is **int64** (idx + number),
then type `FName`, `int32 Size`, `int32 ArrayIndex`, then the value. Two types
carry an extra 8-byte `FName` inside the tag before the value — `ByteProperty`
(the enum name) and `StructProperty` (the struct name). The block ends on a
`None` tag whose type/size fields are garbage bytes, so the name must be tested
**first**. Implemented and measured in `tools/arena/rlmesh.py` (`read_properties`).

## Raster streams (MEASURED, complete for this package)

RL's licensee-34 cook does **not** follow UEViewer's ArVer-868
`UStaticMesh::Serialize` order; parsing with that order yields impossible
counts. What was measured instead, on `Stadium_Day_P.upk` (all 119 exports):

* The stream after the tagged properties is **byte-packed, not 4-aligned**:
  `BoolProperty` costs one value byte after its 24-byte tag, so every bool
  shifts everything after it by one byte. **This single fact was the whole
  "12/119 vs 119/119" mystery (A1)** — there is no second layout.
* `+0` `FBoxSphereBounds` (origin, box extent, radius = 28 B), `+28` int32 flag,
  `+32` `FBox` (Min, Max = 24 B), `+56` int32 **version 6**, `+60` an opaque
  per-mesh int. Then an opaque middle (sections / per-LOD bookkeeping).
* **Positions** are plain `float3`, preceded by `[N][12][N]` (the declaration
  triple), and **pinned structurally** by the bounds head: `Origin ± BoxExtent`
  contains every vertex and equals the position bbox to `1.2e-07` relative in
  the worst of 119 meshes. No alignment and no magnitude window is assumed —
  `Statue_SM`, `SkyScrapers_*` and `Monorail_02` reach ±200 704.
* **Indices** are an `int32 count` followed by `uint16[count]`, every index
  `< N`, count divisible by 3; measured `max(index) == N-1` for 119/119.
* The **per-vertex block** that follows the positions is `stride` bytes
  (`8 + 4*A`, A = UV channel count): a packed **tangent** (`FPackedNormal`,
  3 bytes + a W byte), a packed **normal** (same), then **A × 2 IEEE-754
  binary16** UVs. The decode is `v = b/255*2 − 1` (A2). Only UV **channel 0**
  is stored by the pack.
* The **24-byte section table** (`MinVertexIndex`, `MaxVertexIndex`, `Flags`,
  `MaterialIndex`, `FirstIndex`, `NumTriangles` — 6 little-endian int32) is
  located by content, never by a fixed offset: 119/119 meshes, **174
  sections**.

### Coverage: 119 of 119 meshes

`python3 tools/arena/rlmesh.py <Stadium_Day_P.upk>` now prints
`geometry resolved 119, unresolved 0` (329 472 vertices, 310 017 triangles,
`max bytes_read / serial_size` = 0.9999). Representative rows, all matching
the arena oracle:

| mesh | verts | tris | bounds (measured) |
|---|---|---|---|
| `Field_STD_Collision_SideBot` | 150 | 252 | `(3713,-3840,-13)..(4106,3840,355)` |
| `Field_STD_Collision_Corner` | 2130 | 880 | `(2426,3326,-13)..(4107,5125,2071)` |
| `Goal_STD_Collision` | 5197 | 1966 | `(-2688,-784,-9)..(2688,880,2075)` |
| `Field_Center` | 207 | 207 | `(-707,-707,0)..(707,707,0)` |
| `DFH_Day_StadiumBase2` | 3658 | 3100 | `(-31073,-192,-23347)..(27472,23147,9472)` |
| `Statue_SM` | 36975 | 47080 | `(-362,-79,-212)..(174,142,288)` |
| `Crowd_01` | 15956 | 7978 | `(-22083,1908,617)..(-5697,18822,5026)` |

Those bounds are the check that the extraction is *real*: the field half-length
is 4096, the goal mouth ±2688, the centre circle radius ~707 — all RL-truthful
numbers, not noise. The table hash and the full evidence chain are in
`docs/ARENA_RIP_LOG.md` (round A1).

## The pipeline as it stands

| stage | tool | output |
|---|---|---|
| decrypt / inflate | `tools/decomp/rlupk.py` (ours) | the package's name/import/export tables + the inflated image |
| mesh layout | `tools/arena/rlmesh.py` (ours) | positions, normals, tangents, UV0, indices, sections — 119/119 |
| materials | `tools/arena/rlmat.py` (ours) | the per-slot diffuse resolution + the reason for every unresolved slot |
| textures | `tools/arena/rldds.py` (ours) | DXT1/DXT5/A8R8G8B8 → RGBA8 (self-test 37 PASS) |
| placements | `tools/arena/rlplace.py` (ours) | 451 placements + 26 external, world matrices, material overrides |
| sibling packages | `tools/arena/rlpkg.py` (ours) | the install-wide head index that resolves imported textures |
| pack | `tools/arena/rip_arena.py` (ours) | `arena.json` + `arena.bin` (`rl-arena-pack/1`) |
| C++ read | `src/models/ArenaRip.{h,cpp}` (ours) | the same tables in memory, RGBA8 pixels decoded on the CPU |
| draw | `src/render/Renderer.*` (ours) | 96 merged world-space buckets, one draw each per frame |

**No external exporter is used, linked or vendored** (no UE Viewer, no UModel,
no UAssetAPI). The pack format and every reader/writer are ours; the inputs
are the user's own install, read at runtime, gitignored. See
`docs/THIRD_PARTY.md` and `docs/ARENA_PACK.md`.

## `--arenadir` contract (LANDED, A6b)

- `--arenadir DIR` on `build/game`, plus `[arena] arena_dir` in
  `config/settings.ini` (`docs/INI_SCHEMA.md`); precedence **flag > ini**.
- A dir that yields no loadable pack is a **loud FATAL on stderr naming
  `arena.json`**, exit code **1** — never a silent fallback to the analytic
  shell (`src/models/ArenaShell`).
- The pack is loaded once at startup and uploaded once after `Init()`;
  never in the frame loop.
- Gates always run the clean-room path; `measured.py` / `visual_gates.py`
  never see an arena dir.
- The pack format is ours and versioned (`rl-arena-pack/1`), so the loader is
  ours to change.
- **DEBT:** the game reads `[arena] arena_dir` with its own tolerant reader
  (`rl::ReadIniKey`) because the `Settings` struct does not carry the key — a
  menu "Save & close" therefore **drops** `arena_dir` from
  `config/settings.ini` (asserted by the roundtrip test, so the debt is
  visible, not silent). The settings owner must fold `arena_dir` into
  `ArenaSettings`.

## Open debts (honest list, carried to the next owner)

1. **61.25 % of the used section slots are untextured** (392 of 640) — the cook
   stripped the material graph; the pack binds them to the neutral-grey
   fallback material.
2. **The `Grass` duplicate-name ambiguity** — 8 exports named `Grass`, 32
   placements name it; the reader resolves the first match deterministically,
   so 32 grass tiles use the wrong variant.
3. **The 26 imported placements are not drawn** — they are the sky sphere and
   the billboard sprites of the sibling package; none carries a decoded
   diffuse, so they are counted and reported, never silently dropped.
4. **`arena_dir` is dropped by `Settings::Save`** (see above) until the
   settings owner folds it into `ArenaSettings`.
5. **The 5 LOD meshes pack LOD0 only** — the extra LOD triangles are carried
   in the index buffer and flagged, but the buffer is not split per LOD.
6. **A7 is deferred** — no side-by-side fidelity captures, no A/B perf
   verdict lines, and `tools/verify/arena_gates.py` (AN1–AN7) does not exist.
