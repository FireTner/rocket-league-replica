# Third-party dependencies (Rule 4 log)

| Dep | Version / pin | License | Used for | Vendored? |
|-----|---------------|---------|----------|-----------|
| RocketSim (ZealanL) | commit `c2baacb8f4b441dd8505e63c2aeb5a1679b60b02` (see `third_party/RocketSim/A1_VENDOR.txt`) | MIT © 2022 ZealanL (`third_party/RocketSim/LICENSE`, UNMODIFIED) | 120 Hz physics authority (Bullet vehicle, ball, pads) | yes, pristine |
| Bullet Physics (via RocketSim `libsrc/bullet3-3.24`) | 3.24 (as vendored upstream) | zlib/libpng-style (Bullet license) | collision + raycast vehicle under RocketSim | yes, via RocketSim |
| SDL2 (system) | distro package (`sdl2-config`) | zlib | window, GL context, input, audio device | no (system lib) |
| OpenGL / Mesa (system) | GL 4.5 Compatibility required (hard target, no fallback; fixed-function is the default and programmable paths are permitted per `docs/RENDER_POLICY.md`) | MIT/X11 | rendering | no (system lib) |
| 5x7 bitmap font (`src/render/Font.h`) | authored in-tree | MIT (this project) | on-screen menu/HUD text | n/a (ours) |
| Fennec body mesh (`assets/models/fennec.bin`, converted from `fennec.glb` via `tools/convert_fennec.py`) | "Fennec - Rocket League Car" by Jako (sketchfab.com/fairlight51) — community recreation, **not** extracted game geometry | **CC-BY-4.0** (full text: `assets/models/fennec.LICENSE.txt`; attribution REQUIRED and given here + loader startup log) | player/bot car visual (Octane hitbox; physics unaffected) | yes (converted binary + license) |
| CarBaller camera reference (`docs/carballer-facts/rl_camera.{h,cpp}`) | master `e11180e51c88e01acf598e85525d86c171894b49` (2026-10-13), [Prankiman/CarBaller](https://github.com/Prankiman/CarBaller) | MIT © 2026 Arvid Kolsmyr (attribution in `docs/carballer-facts/README.md`) | camera feel reference only (ball-aim slew, air-velocity heading, stiffness lag, swivel orbit); never built, never shipped | yes (reference-only, off build path) |

Procedural: ball texture, turf/markings, glass hex-net, curbs, goal trims,
boost gauge, and all audio are generated in code. The single external
geometry is the CC-BY-4.0 community Fennec (above) — a fan recreation, never
a game rip. **No Psyonix assets ever** — no game models, textures, sounds,
dumps, or mesh files (mesh arenas load user-provided dumps at runtime only,
never vendored).

## Runtime-only inputs (never vendored, never committed)

| Item | Source / license | SHA-256 | Purpose |
|---|---|---|---|
| `RLArenaCollisionDumper.exe` v1.0.0 | [ZealanL/RLArenaCollisionDumper](https://github.com/ZealanL/RLArenaCollisionDumper), MIT | `91a40fc8d705a4c44b692c534f826253cf7b60b34bfacd4b90c6f6029875e5c5` | Dumps Rocket League's arena collision meshes |
| `collision_meshes/soccar/*.cmf` | **Psyonix-owned geometry** | — | Runtime arena collision input, produced by the dumper above from the user's own game install |
| `arena_assets/<map>/arena.json` + `arena.bin` (**our own pack format**, `rl-arena-pack/1`) | **Psyonix-owned art, user-extracted** from the user's own install by **our own** tools | `arena.json` `9f9625659da22794ca5543971f77a399c7f5c35d30db150d718f5f925d91d97d`, `arena.bin` `62d3cc3bd1136316847231c1fa697ac129d9db2876841c622f872cf779ddbe96` (DFH Stadium, Day) | Opt-in local-only render layer (`--arenadir DIR`, or `[arena] arena_dir`); the pack is read at runtime from a gitignored dir, the gated visuals always use the clean-room path |

None of these is in this repository. `collision_meshes/` and `arena_assets/`
are gitignored, and the dumper lives in the game's Wine prefix. See
`docs/MESH_SETUP.md` for the collision procedure and `docs/ARENA_RIP.md` +
`docs/ARENA_PACK.md` for the visual pack. Any future visual-asset loader
follows the same split: read the user's files at runtime, never copy their
bytes into this tree, never redistribute them. Facts learned from third-party
viewers (camera numbers, layout dims, algorithms) are treated like any other
cited source; their asset BYTES are not.

### The arena pack: our format, our readers, our writers, no external exporter

The ripped-arena pipeline is **entirely ours**.  No third-party exporter is
used, linked, vendored or shipped with it — **no UE Viewer, no UModel, no
UAssetAPI** (and none of their outputs).  The whole chain is in-tree and MIT:

| stage | our file | what it does |
|---|---|---|
| package reader | `tools/decomp/rlupk.py` | UE3 header/name/import/export table + AES-256-ECB name-table decryption + inflate |
| mesh layout | `tools/arena/rlmesh.py` | positions / indices / normals / tangents / UV0 / sections |
| materials | `tools/arena/rlmat.py` | the per-slot diffuse resolution and the reason for every unresolved slot |
| textures | `tools/arena/rldds.py` | DXT1 / DXT5 / `PF_*` → RGBA8 (self-test 37 PASS) |
| placements | `tools/arena/rlplace.py` | the world matrices, the material overrides, the 0-orphan census |
| sibling packages | `tools/arena/rlpkg.py` | the install-wide head index that resolves imported textures |
| pack writer | `tools/arena/rip_arena.py` | `arena.json` + `arena.bin` (`rl-arena-pack/1`), byte-deterministic |
| pack reader (C++) | `src/models/ArenaRip.{h,cpp}` | the same tables in memory; the CPU DXT decode is byte-identical to `rldds.py` (13/13) |
| draw path | `src/render/Renderer.*` | 96 merged world-space buckets, uploaded once, one draw each per frame |

The **inputs** are the user's own Rocket League install, read at runtime, and
the **outputs** are gitignored (`arena_assets/`) or live outside the tree.  The
package key is never in the repository: `~/.rl-decomp/upk.key` or
`$RL_UPK_KEY` only.  The pack *format* is ours and versioned
(`docs/ARENA_PACK.md`), so the reader is ours to change.

## Local tooling (not dependencies of the build)

`tools/meshgen/` (clean-room mesh generator) is ours, MIT. The
`wtsapi32.dll` shim used to make the dumper work under Wine lives in the game
prefix (`~/Games/Heroic/scripts/wtsapi_shim.c`) and is MIT; it uses only
documented Win32/NT APIs and Wine's own PE import libraries to link. It is not
part of this repo's build.

Camera ground truth (`docs/CAMERA_DECOMP.md`) uses these **local-only** tools;
none is vendored, linked, or needed by the build:

| Tool | License | Role |
|---|---|---|
| [EliotVU/Unreal-Library (UELib)](https://github.com/EliotVU/Unreal-Library) | MIT © Eliot van Uytfanghe | UE3 package object/decompiler model, built locally with the user-local .NET 10 SDK; used to confirm the licensee-32 ceiling (patched locally to 34 for the probe) |
| [AltimorTASDK/RLUPKTool](https://github.com/AltimorTASDK/RLUPKTool) | MIT © 2018 AltimorTASDK | provenance of Rocket League's AES-256 package key — the repo carries **no key**; a local `~/.rl-decomp/upk.key` or `$RL_UPK_KEY` is required, and is never committed |
| .NET 10 SDK | MIT © .NET Foundation | runs UELib; installed under `~/.dotnet`, not a build dependency |

`tools/decomp/rlupk.py` and `tools/decomp/camera_surface.py` are **ours** (MIT):
a UE3 header/name/import/export reader and a camera-symbol extractor. The data
they read (packages, decrypted images, decompiled text, `decomp/` fixtures) stays
on the user's machine and is gitignored.
