# Third-party dependencies (Rule 4 log)

| Dep | Version / pin | License | Used for | Vendored? |
|-----|---------------|---------|----------|-----------|
| RocketSim (ZealanL) | commit `c2baacb8f4b441dd8505e63c2aeb5a1679b60b02` (see `third_party/RocketSim/A1_VENDOR.txt`) | MIT © 2022 ZealanL (`third_party/RocketSim/LICENSE`, UNMODIFIED) | 120 Hz physics authority (Bullet vehicle, ball, pads) | yes, pristine |
| Bullet Physics (via RocketSim `libsrc/bullet3-3.24`) | 3.24 (as vendored upstream) | zlib/libpng-style (Bullet license) | collision + raycast vehicle under RocketSim | yes, via RocketSim |
| SDL2 (system) | distro package (`sdl2-config`) | zlib | window, GL context, input, audio device | no (system lib) |
| OpenGL / Mesa (system) | GL 2.1 fixed-function | MIT/X11 | rendering | no (system lib) |
| 5x7 bitmap font (`src/render/Font.h`) | authored in-tree | MIT (this project) | on-screen menu/HUD text | n/a (ours) |
| Fennec body mesh (`assets/models/fennec.bin`, converted from `fennec.glb` via `tools/convert_fennec.py`) | "Fennec - Rocket League Car" by Jako (sketchfab.com/fairlight51) — community recreation, **not** extracted game geometry | **CC-BY-4.0** (full text: `assets/models/fennec.LICENSE.txt`; attribution REQUIRED and given here + loader startup log) | player/bot car visual (Octane hitbox; physics unaffected) | yes (converted binary + license) |

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
| visual asset packs, if/when supported (stadium/car/texture extracts) | **Psyonix-owned art, user-extracted** | — | Opt-in local-only render layer (RLViser-style accuracy on the user's own machine); loaded from a gitignored asset dir, gated visuals always use the clean-room path |

Neither is in this repository. `collision_meshes/` is gitignored, and the dumper
lives in the game's Wine prefix. See `docs/MESH_SETUP.md` for the procedure.
Any future visual-asset loader follows the same split: read the user's files at
runtime, never copy their bytes into this tree, never redistribute them. Facts
learned from third-party viewers (camera numbers, layout dims, algorithms) are
treated like any other cited source; their asset BYTES are not.

## Local tooling (not dependencies of the build)

`tools/meshgen/` (clean-room mesh generator) is ours, MIT. The
`wtsapi32.dll` shim used to make the dumper work under Wine lives in the game
prefix (`~/Games/Heroic/scripts/wtsapi_shim.c`) and is MIT; it uses only
documented Win32/NT APIs and Wine's own PE import libraries to link. It is not
part of this repo's build.
