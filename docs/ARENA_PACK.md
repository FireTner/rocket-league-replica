# ARENA_PACK — the arena pack format (normative)

Status: **normative, version 1** (`rl-arena-pack/1`), 2026-10-20.
Producer: `tools/arena/rip_arena.py` (phase A5).  Consumer: the C++ arena
loader (phase A6).

An **arena pack** is a self-describing, deterministic container for one ripped
arena: its meshes, its placements, its material bindings and the diffuse
textures those bindings reach.  A pack is **Psyonix-owned data** and therefore
lives **only** under `arena_assets/` (gitignored) or outside the repository
tree — never in the repository (AGENTS.md rule 1).  The pack *format* is
documented here; the pack *bytes* are not.

## 0. Status + usage (2026-10-20, round A8 close-out)

**The pack exists.**  A built pack for DFH Stadium (Day) lives at
`arena_assets/stadium_day/` (gitignored, never committed):

| file | size | sha256 |
|---|---|---|
| `arena.json` | 807 793 B | `9f9625659da22794ca5543971f77a399c7f5c35d30db150d718f5f925d91d97d` |
| `arena.bin` | 29 121 416 B | `62d3cc3bd1136316847231c1fa697ac129d9db2876841c622f872cf779ddbe96` |

Two consecutive builds are **byte-identical** (§4).  The verifier passes
**19/19** checks (§6).  Textures are stored **compressed** (DXT1 × 8, DXT5 × 5;
12 664 832 B of payload instead of 70 909 952 B expanded).

```sh
# build (the pack may only land under arena_assets/ inside the repo)
python3 tools/arena/rip_arena.py --pack <Stadium_Day_P.upk> --out arena_assets/stadium_day
# verify (re-reads the pack, checks every manifest/blob agreement)
python3 tools/arena/rip_arena.py --verify arena_assets/stadium_day
```

**What the loader expects** (the C++ side, `src/models/ArenaRip.h`):

* a **directory** holding exactly the two files `arena.json` and `arena.bin`;
* the manifest's `format` must be `"rl-arena-pack/1"` — anything else is
  rejected with a message naming the format it did not understand;
* the blob's 32-byte header (`RLARENAP`, version, header size, total size,
  array count) must agree with the manifest's `blob` block, and the blob's
  sha256 is checked **before** any offset is trusted;
* every array offset/length comes from the manifest — the reader never guesses
  a stride or a padding rule;
* the mesh, instance, material, texture and section tables keep the manifest's
  order (the reader never re-sorts);
* `LoadArenaPack(dir, out)` returns `false` on any problem and leaves the
  reason in `ArenaPackError()`.  **A missing pack is a normal, clean state** —
  the game treats it as "no arena pack" (or, for `--arenadir`, as a loud
  FATAL naming `arena.json` with exit code 1 — never a silent fallback).

**The C++ DXT path** (A6a): the reader decodes the compressed payloads on the
**CPU** (plain C++ + STL, no GL, no SDL) into RGBA8 — `PF_DXT1` (BC1, 8 B per
4×4 block) and `PF_DXT5` (BC3, 16 B per 4×4 block) plus the raw `PF_*` forms.
The result is **byte-identical to the Python decoder** `tools/arena/rldds.py`
on **13/13** decoded textures (measured; the contract test asserts it), and
the pack's `rgba8` size invariant `w*h*4` holds on every texture.  The GL side
(A6b) is a thin upload layer on top of those CPU pixels.

**Still open** (owner decision, round A8): the fidelity captures and the A/B
perf verdict (**A7**) are deferred, and `tools/verify/arena_gates.py`
(AN1–AN7) does not exist yet.

---

## 1. Files

A pack is a directory with exactly two files:

| file | measured size (DFH Stadium, Day) | content |
|---|---|---|
| `arena.json` | 807 793 B | the manifest: every table, every offset, every hash |
| `arena.bin`  | 29 121 416 B | the binary blob: the arrays and the texture payloads |

Nothing else is written.  The ripper refuses to write a pack anywhere inside
the repository except under `<repo>/arena_assets/`:

```
$ python3 tools/arena/rip_arena.py --pack <Stadium_Day_P.upk> --out docs/pack_leak
rip_arena: error: refusing to write a pack inside the repository tree at
/home/cachyfire/Projects/RocketLeagueLaptop/docs/pack_leak (Rule 1: only
<repo>/arena_assets/ is allowed)
```

---

## 2. `arena.bin` — the binary blob

### 2.1 Header (32 bytes, little-endian)

| offset | size | type | field | value |
|---|---|---|---|---|
| 0  | 8 | char[8] | magic | `RLARENAP` (ASCII, no NUL) |
| 8  | 4 | u32 | `format_version` | 1 |
| 12 | 4 | u32 | `header_size` | 32 |
| 16 | 8 | u64 | `total_size` | the size of the whole blob in bytes |
| 24 | 4 | u32 | `array_count` | number of arrays laid down (6 groups + one per texture) |
| 28 | 4 | u32 | `flags` | 0 (reserved) |

### 2.2 Arrays

Every array starts on a **4-byte boundary**; the padding bytes between two
arrays are `0x00` and are **not** part of either array.  The manifest holds
the absolute byte offset and the exact byte length of every array, so a
reader never has to guess a stride or a padding rule.

The six **group arrays** are laid down in this fixed order:

| order | group | element | stride | type | measured (DFH Stadium, Day) |
|---|---|---|---|---|---|
| 1 | `positions` | vertex xyz | 12 | f32 | offset 32, 330 187 elements, 3 962 244 B |
| 2 | `normals`   | vertex xyz | 12 | f32 | offset 3 962 276, 330 187 elements, 3 962 244 B |
| 3 | `tangents`  | vertex xyz | 12 | f32 | offset 7 924 520, 330 187 elements, 3 962 244 B |
| 4 | `uvs`       | vertex uv  | 8  | f32 | offset 11 886 764, 330 187 elements, 2 641 496 B |
| 5 | `indices`   | triangle corner | 2 | u16 | offset 14 528 260, 933 633 elements, 1 867 266 B |
| 6 | `matrices`  | 4x4 transform | 128 | f64 × 16 | offset 16 395 528, 477 matrices, 61 056 B |

The **texture payloads** follow, one array per texture, in the order of the
manifest's `textures` table (sorted by texture name, then by file name).

All numbers are **little-endian**.  There is no padding *inside* an array
except the 4-byte alignment gap between arrays.

### 2.3 What each array means

* `positions` — the mesh's local-space vertex positions, exactly as stored in
  the package (float32, round-tripped bit-exact).
* `normals` / `tangents` — the **decoded** packed normals of the vertex
  stream.  The package stores two `FPackedNormal` records per vertex
  (tangent first, then normal), each 3 bytes; the measured decode is
  `v = b / 255 * 2 − 1` (A2: 315 046 of 315 056 channel-1 vectors are unit
  length to 1.2e-2 under this map, the classic `(b−128)/127` leaves 384
  outside `[0.98, 1.02]`).  The decode is a `double` computation, stored as
  f32.
* `uvs` — **UV channel 0 only**, decoded from the two IEEE-754 binary16
  values the vertex stream stores per channel.  A mesh may declare up to 4
  channels (`uv_channels`); only channel 0 is stored (`uv_stored_channels` =
  1), because the current reader decodes channel 0 only.
* `indices` — the triangle corner indices, `uint16`, `num_tris * 3` of them.
  Every index is `< num_verts` of its own mesh (checked by the verifier).
* `matrices` — one 4×4 **column-major** world matrix per placement, 16
  `float64` values.  `m[12..14]` is the translation and `m[15] == 1`
  (A4's convention, unchanged).

---

## 3. `arena.json` — the manifest

The manifest is a JSON object with **sorted keys**, two-space indentation,
`ensure_ascii`, and a trailing newline.  Top-level keys (17):

```
arrays  blob  counts  coverage  external_instances  external_meshes  format
format_version  generator  instances  materials  meshes  section_bindings
siblings  source  texture_storage  textures
```

### 3.1 `format` / `format_version` / `generator`

| field | type | value |
|---|---|---|
| `format` | string | `"rl-arena-pack/1"` |
| `format_version` | integer | `1` |
| `generator` | string | `"tools/arena/rip_arena.py"` |

### 3.2 `source` — the build identity of the ripped package

| field | type | measured (Stadium_Day_P.upk) |
|---|---|---|
| `file` | string | `"Stadium_Day_P.upk"` (basename only) |
| `name` | string | `"Stadium_Day_P"` |
| `file_version` | integer | `868` |
| `licensee_version` | integer | `34` |
| `guid` | string | `"e8eba3237450a04499cfaaf200572d62"` (16 raw bytes, hex) |
| `bytes` | integer | `13 631 852` |
| `sha256` | string | `"d15fef4a402502ab78bd4ac97dbf36d0b371465f63f875a029da94cedc0338ee"` |
| `export_count` | integer | `3267` |
| `import_count` | integer | `347` |
| `name_count` | integer | `2007` |

No absolute path, no install root, no timestamp.

### 3.3 `siblings` — the packages the pack borrowed from

One row per sibling package file, sorted by file name:

| field | type | meaning |
|---|---|---|
| `file` | string | the file's **basename** (never an absolute path) |
| `packages` | string[] | the internal package names the file declares that the pack uses, sorted |
| `bytes` | integer | the file size |
| `sha256` | string | the content hash of the file |

Measured for DFH Stadium (Day): `BG_Park_Snowy.upk` (22 235 952 B, packages
`FieldSkin_FutureGrid`, `Meshes`, `Noise`), `Engine.upk` (4 974 581 B,
`EngineMeshes`), `MENU_GarageComplex.upk` (3 531 282 B, `Textures`),
`Startup.upk` (46 865 803 B, `FieldSkin_FutureGrid`, `SM`), `TAGame.upk`
(81 930 613 B, `Textures`), `explosion_calculated_SF.upk` (1 412 549 B,
`Meshes`).  The source package itself is **not** repeated here; it is
described by `source`.

### 3.4 `meshes` / `external_meshes` — the mesh tables

`meshes` holds the **119** `StaticMesh` exports of the package, sorted by
name.  `external_meshes` holds the **4** meshes the package only *imports*,
resolved in the sibling package that owns them (A3b proved the same reader
parses them unchanged).  Both tables have the same shape:

| field | type | meaning |
|---|---|---|
| `name` | string | the mesh name |
| `source` | string | `"package"` or `"sibling"` |
| `package` | string | the internal package that declares it |
| `file` | string | the file's basename |
| `export` | integer | the export index inside that file |
| `num_verts` | integer | vertex count |
| `num_tris` | integer | triangle count (`num_indices / 3`) |
| `num_indices` | integer | index count |
| `uv_channels` | integer | channels the vertex stream declares (1..4) |
| `uv_stored_channels` | integer | channels actually stored (always 1) |
| `bounds` | object | `{"min":[x,y,z], "max":[x,y,z]}` — the local-space position bbox |
| `arrays` | object | per array: `{"offset", "length", "count", "stride"}` — **absolute byte offsets into `arena.bin`** |
| `sections` | array | the section table, in index order (see below) |
| `lod` | object | LOD bookkeeping (see below) |

A **section** row:

| field | type | meaning |
|---|---|---|
| `material_index` | integer | the section's material index (into the mesh's own material array) |
| `flags` | integer | the stored flags word |
| `first_index` | integer | the first index of the section, in indices |
| `num_triangles` | integer | the triangle count |
| `min_vertex` / `max_vertex` | integer | the stored vertex range (not always the exact min/max of the slice — A2) |

The sections **tile** the LOD0 part of the index buffer:
`first_index[0] == 0` and `first_index[i+1] == first_index[i] + 3*num_triangles[i]`.

An **LOD** row:

| field | type | meaning |
|---|---|---|
| `section_triangles` | integer | the triangles the sections describe (LOD0) |
| `buffer_triangles` | integer | the triangles in the whole index buffer |
| `extra_lod_triangles` | integer | `buffer_triangles − section_triangles` (a further LOD) |
| `tiled` | boolean | true when the sections cover the whole index buffer |
| `reason` | string | the reason string of the reader when `tiled` is false |

Measured: **114 of 119** in-package meshes are exactly tiled; the five that
are not carry a further LOD in the index buffer (e.g.
`Goal_STD_Collision_Half_Goal_STD_Collision`: 983 LOD0 triangles out of
3 932).

### 3.5 `instances` / `external_instances` — the placement tables

`instances` holds the **451** placements whose mesh is an export of this
package.  `external_instances` holds the **26** placements whose mesh is an
import (a sibling mesh); they are reported separately and are **not** part of
the 451.

| field | type | meaning |
|---|---|---|
| `component_export` | integer | the `StaticMeshComponent` export index |
| `mesh` | string | the mesh name (a key into `meshes` or `external_meshes`) |
| `mesh_external` | boolean | true for the 26 external placements |
| `owner` / `owner_class` | string | the owning actor |
| `source` | string | A4's source label (`actor:…`, `collector:…`) |
| `slot` | integer\|null | the slot inside the owning container, when there is one |
| `matrix` | object | `{"offset", "length", "count", "stride", "values", "layout"}` — the byte range of the 16-double matrix in `arena.bin` (`count` = 1 matrix, `stride` = 128 B, `values` = 16 doubles); `layout` is always `"column-major"` |
| `material_slots` | array | the component's material array, one row per slot (see below) |

A **material slot** row:

| field | type | meaning |
|---|---|---|
| `slot` | integer | the slot index (== the section index) |
| `material` | string\|null | the material object name (null for a null slot) |
| `kind` | string | `"export"`, `"import"` or `"none"` |
| `status` | string | the resolution status (see §3.8) |
| `texture` | string\|null | the diffuse texture name, when resolved |

### 3.6 `materials` — the material table

One row per distinct material object the placement bindings reach (83 rows:
75 through the 451 placements, plus the 8 further materials the 26 external
placements bind).  A name is not unique in the package — `BoostPad_Mat`
exists as two exports (405 and 408) — so the table is keyed by the export
index, and a row is one *export*, not one name.

| field | type | meaning |
|---|---|---|
| `name` | string\|null | the material name |
| `class` | string\|null | `Material` or `MaterialInstanceConstant` |
| `source` | string | `"package"`, `"sibling"` or `"none"` |
| `package` | string\|null | the internal package (for an imported material) |
| `file` | string\|null | the file's basename |
| `export` | integer\|null | the export index of the material |
| `status` | string | the resolution status (see §3.8) |
| `rule` | string\|null | which resolution rule fired (`no-sampler`, `mic-override-diffuse`, `sampler-external`, …) |
| `parameter` | string\|null | the sampler/parameter the diffuse came from |
| `diffuse_texture` | string\|null | the diffuse texture name |
| `texture_index` | integer\|null | the index into the `textures` table |
| `section_slots` | integer | how many section slots this material is bound to |

### 3.7 `textures` — the texture table

| field | type | meaning |
|---|---|---|
| `name` | string | the texture name |
| `class` | string | `Texture2D` |
| `package` | string | the internal package that declares it |
| `file` | string | the file's basename |
| `width` / `height` | integer | the mip-0 size |
| `format` | string | the **original** pixel format (`PF_DXT1`, `PF_DXT5`, `PF_A8R8G8B8`, …) |
| `mip_count` | integer | the declared mip count |
| `srgb` | boolean | the `SRGB` flag (reported, no gamma transform applied) |
| `storage` | string | `"compressed"` (default) or `"rgba8"` |
| `rgba8_bytes` | integer | what the texture would occupy expanded to RGBA8 |
| `offset` / `length` / `bytes` | integer | the byte range of the payload in `arena.bin` |
| `payload_sha256` | string | the hash of the **stored** bytes |
| `decoded` | boolean | the payload really decodes to `width*height*4` RGBA8 |
| `decode_error` | string | only when `decoded` is false |

**Textures are stored in their original compressed form** (DXT1/DXT5 blocks
or the raw `PF_*` bytes), never expanded, so the pack stays small and
lossless.  The loader (A6) decodes them itself.  `--store rgba` is available
for comparison and stores the expanded RGBA8 instead.

Measured for DFH Stadium (Day): **13** distinct textures, **12 664 832 B** of
compressed payload (`PF_DXT1` × 8, `PF_DXT5` × 5), which would be
**70 909 952 B** expanded to RGBA8 — a **5.6×** saving.

### 3.8 `coverage` — the texture-coverage truth

| field | type | meaning |
|---|---|---|
| `chain` | string | the binding chain the numbers are measured on |
| `used_section_slots` | integer | the slots the chain binds |
| `resolved_diffuse` | integer | the slots whose material resolves to a diffuse texture |
| `resolved_pct` | number | `resolved_diffuse / used_section_slots * 100`, 4 decimals |
| `decoded_diffuse` | integer | the slots whose diffuse texture really decodes to pixels |
| `decoded_pct` | number | likewise |
| `named_texture` / `named_pct` | integer / number | the slots whose material names a texture (decoded or not) |
| `reasons` | object | the per-reason breakdown of every slot |
| `external_placements` | object | the same numbers for the 26 external placements |
| `mesh_slot_list` | object | the **comparison** number (see below) |

The **chain** is:

```
instance  ->  component material array (slot i == section i)
          ->  MIC / Material
          ->  diffuse texture (in-package or sibling)
          ->  decoded pixels
```

A section slot exists for every `(placement, section)` pair, so
`used_section_slots` = Σ over the 451 placements of the section count of the
mesh it places = **640** (which is also the number of override entries A4
measured).

**Measured (DFH Stadium, Day):** 640 slots, **248 resolve to a decoded
diffuse = 38.75 %**.  The remaining 392 break down as:

| reason | slots | meaning |
|---|---|---|
| `untextured-by-design` | 331 | the material owns no texture sampler at all (the cook stripped the graph, A3b §7) |
| `external:CustomColors` | 34 | the diffuse lives in the `CustomColors` package and is a runtime scripted texture, not pixels |
| `param-mask` | 16 | the sampler parameter is masked out (a null `Expression`) |
| `external:Materials` | 9 | the diffuse lives in the `Materials` package and was not decodable there |
| `null-slot` | 2 | the component's material array holds a null entry in that slot |

The 26 external placements add 26 slots, 0 of them resolved (16
`external:CustomColors`, 10 `untextured-by-design`).

**The comparison number** (`coverage.mesh_slot_list`) is the *mesh's own*
material slot list — one list per distinct placed mesh — which is what A3/A3b
measured: **171 slots, 52 resolved = 30.41 %**, 72 with a named texture.  It
is a **name-based** join (a UE3 export name is not unique: `BoostPad_Mat`
exists twice, only one of which owns a diffuse), while the pack's own number
is a **reference-based** join through the export index, and it binds one slot
per *placement* instead of one per *mesh*.  The pack uses the **override
chain** number (38.75 %); the mesh-slot-list number is reported next to it
for comparison.

### 3.9 `section_bindings` — the binding table

One row per bound slot (666 = 640 placements + 26 external placements), in
placement order and then by section index:

| field | type | meaning |
|---|---|---|
| `scope` | string | `"placement"` or `"external_placement"` |
| `instance` | integer | the component export index |
| `mesh` | string | the mesh the slot belongs to |
| `section` | integer | the section index |
| `material` | string\|null | the material object name |
| `material_kind` | string | `"export"`, `"import"` or `"none"` |
| `status` | string | the resolution status |
| `texture` | string\|null | the diffuse texture name |
| `texture_file` | string\|null | the file the texture lives in |

### 3.10 `counts`, `arrays`, `blob`, `texture_storage`

`counts` is a flat summary: `meshes` (119), `external_meshes` (4),
`instances` (451), `external_instances` (26), `sections` (178 = 174 + 4),
`materials` (83), `textures` (13), `vertices` (330 187), `triangles`
(311 211), `used_section_slots` (640).

`arrays` describes the six group arrays of §2.2: `offset`, `length`, `count`,
`stride`, `type` (`f32`, `u16`, `f64`).  `count` is the number of **records**
in the array: a vertex for the geometry arrays, an index for `indices`, a
matrix for `matrices`.

`blob` describes the binary file: `file` (`"arena.bin"`), `bytes`,
`header_bytes` (32), `sha256` (of the whole blob) and `array_count`.

`texture_storage` records the storage mode (`compressed` / `rgba8`), the
compressed byte total, the RGBA8 byte total and the format histogram.

---

## 4. Determinism rules

A pack built twice from the same inputs is **byte-identical**.  The rules:

1. **JSON key order** — `json.dumps(..., sort_keys=True, indent=2,
   ensure_ascii=True)` plus one trailing `\n`.  Every object in the manifest
   is emitted with sorted keys.
2. **Floats** — a manifest float is always the shortest round-trip repr of a
   finite IEEE-754 double, with `-0.0` normalised to `+0.0`; `NaN` and `±Inf`
   are rejected outright.  The geometry floats (positions, normals, tangents,
   UVs) and the transforms are **not** in the JSON at all: they are binary in
   the blob, so they are bit-exact by construction.
3. **No timestamps** — nothing in the pack records a date, a time, a host
   name, a process id or a user name.
4. **No absolute paths** — the source package is identified by its
   **basename** + GUID + content hash; a sibling package by its internal
   package name + file basename + content hash.
5. **Fixed array order** — the blob's arrays are laid down in the order of
   §2.2; the textures follow the sorted `textures` table.
6. **Fixed table order** — `meshes` and `external_meshes` are sorted by name;
   `textures` by (name, file); `materials` by (name, file); `instances` and
   `external_instances` keep the package's component order (the export index);
   `section_bindings` follow the placement order.
7. **Fixed alignment** — every array starts on a 4-byte boundary and the gap
   bytes are `0x00`; `length` never includes the gap.
8. **Content hashes** — the manifest carries the sha256 of the source
   package, of every sibling package, of every texture payload and of the
   whole blob, so any drift is detectable.

**Measured proof** — two consecutive runs into two different output
directories:

```
$ sha256sum arena_assets/stadium_day/arena.json arena_assets/stadium_day/arena.bin \
            /tmp/rl_a5/run2/arena.json /tmp/rl_a5/run2/arena.bin
9f9625659da22794ca5543971f77a399c7f5c35d30db150d718f5f925d91d97d  arena_assets/stadium_day/arena.json
62d3cc3bd1136316847231c1fa697ac129d9db2876841c622f872cf779ddbe96  arena_assets/stadium_day/arena.bin
9f9625659da22794ca5543971f77a399c7f5c35d30db150d718f5f925d91d97d  /tmp/rl_a5/run2/arena.json
62d3cc3bd1136316847231c1fa697ac129d9db2876841c622f872cf779ddbe96  /tmp/rl_a5/run2/arena.bin
$ cmp arena_assets/stadium_day/arena.json /tmp/rl_a5/run2/arena.json && \
  cmp arena_assets/stadium_day/arena.bin /tmp/rl_a5/run2/arena.bin && echo identical
identical
```

### 4.1 Content proof (an independent re-read)

A pack that is byte-identical can still be wrong, so the content is re-read
by a script that does **not** use the writer and is checked against the
measured oracles of the earlier rounds:

```
PASS oracle DFH_Day_StadiumBase2: x=-31072.607..27472.021      (A1: -31073..27472)
PASS oracle Field_Center: r=706.899 z=-0.000..0.000             (A1: r=707)
PASS oracle Goal_STD_Collision: x=-2688.000..2688.043           (A1: +/-2688)
PASS normals unit length: 36975/36975 in [0.98,1.02]            (A2 decode map)
PASS matrices match the A4 handoff: 40 checked, 0 differ         (bit-identical)
PASS texture decodes from the blob: OOBCubeNegY 128x128 PF_DXT1 -> 65536 B
```

---

## 5. Versioning rule

* The format identifier is `"rl-arena-pack/<N>"`; `format_version` is `<N>`.
  **Any change a reader could notice** — a new field, a removed field, a
  changed offset/stride/type, a changed array order, a changed status
  string — bumps `<N>` and the identifier together.
* **Additive** changes (a new optional table, a new optional field) that a
  version-1 reader can ignore **do not** bump the version; the new field is
  documented as "version ≥ 2 readers only".
* The blob header carries the same `format_version`; a reader must refuse a
  blob whose header version it does not know.
* A reader must check `blob.sha256` before trusting any offset.

Current version: **`rl-arena-pack/1`**, `format_version = 1`.

---

## 6. Building and verifying a pack

```sh
# build (the pack may only land under arena_assets/ inside the repo)
python3 tools/arena/rip_arena.py \
    --pack ~/Games/Heroic/rocketleague/TAGame/CookedPCConsole/Stadium_Day_P.upk \
    --out arena_assets/stadium_day

# verify (re-reads the pack and checks every manifest/blob agreement)
python3 tools/arena/rip_arena.py --verify arena_assets/stadium_day
```

Options:

| flag | meaning |
|---|---|
| `--install <CookedPCConsole dir>` | the install root; the sibling-package index is built from it when it is missing |
| `--index <json>` | the sibling-package index (default `/tmp/rl_a3b/pkgindex.json`) |
| `--no-textures` | build the geometry-only pack (no texture payloads) |
| `--store rgba` | store the textures expanded to RGBA8 instead of compressed |
| `--quiet` | less chatter |

Exit codes: `0` success, `1` a verification check failed, `2` a fatal error
(missing package, refused output path, unreadable pack).

The verifier prints one line per check and a summary line; for DFH Stadium
(Day) it prints **19 checks, 19 PASS, 0 FAIL** (the list is in
`docs/ARENA_RIP_LOG.md`, round A5).

---

## 7. Known limits of version 1

* **UV channel 0 only.**  A mesh may declare up to 4 UV channels; only
  channel 0 is decoded and stored.
* **The mesh's own material array is not in the cook.**  The `StaticMesh`
  exports carry no `Materials` property (measured: 0 of 119), so the
  section's `material_index` cannot be resolved inside the mesh.  The
  binding therefore comes from the *component's* material array, which is
  the array the engine actually uses.
* **Extra LOD triangles are carried but not described.**  5 of 119 meshes
  carry a further LOD in the index buffer; the pack stores the whole index
  buffer and flags the extra triangles in `lod.extra_lod_triangles`, but it
  does not split the buffer per LOD.
* **The pack holds mip 0 only.**  `mip_count` is reported, the mips are not
  stored.
* **The diffuse is the only texture.**  Normal, specular, emissive and
  detail textures are not resolved (A3b measured them as unreachable or
  non-albedo); the pack is a diffuse-only pack.
* **The mesh table is per package.**  A pack holds one stadium package; the
  four external meshes come from sibling packages and are recorded with
  their source package.

---

## 8. Provenance

| fact | measured by | where |
|---|---|---|
| the vertex/section/index layout | `tools/arena/rlmesh.py` | `docs/ARENA_RIP.md`, `docs/ARENA_RIP_LOG.md` rounds A1/A2 |
| the material → diffuse resolution | `tools/arena/rlmat.py` | round A3 / A3b |
| the texture decoders | `tools/arena/rldds.py` | round A2/A3 (self-test 37 PASS) |
| the placements and the world matrices | `tools/arena/rlplace.py` | round A4 |
| the sibling-package index | `tools/arena/rlpkg.py` | round A3b |
| the pack itself | `tools/arena/rip_arena.py` | round A5 |
