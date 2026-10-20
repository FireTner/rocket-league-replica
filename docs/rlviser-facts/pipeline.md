# Pipeline facts (RLViser v0.9.2) — we have reliable 1:1 reference for spawn-once, mesh build, and pacing

Sources: [`src/mesh.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/mesh.rs), [`src/assets.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/assets.rs), [`src/settings/cache_handler.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/settings/cache_handler.rs), [`.gitignore`](https://raw.githubusercontent.com/VirxEC/rlviser/master/.gitignore), [`src/camera.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/camera.rs). Facts only.

## Spawn-once + transform-per-frame

- Field meshes: `Mesh::new(TriangleList, RenderAssetUsages::default())` built once in `load_field`/`process_info_node` from the layout JSON, spawned as `Mesh3d + MeshMaterial3d + Transform + StaticFieldEntity`. Per frame only `Transform.translation/rotation = packet pos/rot_mat.to_bevy()` move (`update_ball`, `update_car` in `udp.rs`). Cars/ball/pads never rebuild geometry per frame.
- Coordinate flip: RL `(x, y, z)` → Bevy `(x, z, y)` via `ToBevyVec` (`[x, z, y]`); rotation Euler `ZYX (z, −y, x)` in degrees→radians; matrix→quat keeps `(x, z, y, −w)`. Our Z-up port must apply the equivalent once at load, never per frame.
- Availability line: "We have reliable 1:1 reference for spawn-once in `docs/rlviser-facts/pipeline.md` (RLViser `mesh.rs:load_field`, `RenderAssetUsages` + per-frame `Transform` only)."

## PSKX → mesh build (`assets.rs` + `mesh.rs:MeshBuilder`)

- Chunk walk: `PNTS` (verts, y-negated on load) / `VTXW` (wedge → vertex id + uv + material index) / `FACE` (3×u16 wedge idx + mat/aux/smoothing, wound `[1,0,2]`) / `MATT` (count only) / `VERTEXCO` (only for `Goal_STD_Trim`, `CrowdSpawnerMesh`, else warn+skip) / `EXTRAUVS` (per-material UV override via `process_materials`) / unknown chunk = error log in debug.
- Header must be `ACTRHEAD`; `MeshBuilder { ids, verts, uvs, colors, num_materials, mat_ids }` (bincode legacy) → `build_mesh` (single) / `build_meshes` (per-material split, shared verts, per-mat index filter) → `ATTRIBUTE_POSITION` + optional `ATTRIBUTE_COLOR` + `compute_smooth_normals`, then optional `ATTRIBUTE_UV_0` + `generate_tangents`.
- Dropshot-only procedural meshes (mid quad + hex tiles) use `compute_normals`, not smooth — precedent for mixing smooth/procedural in one scene.
- Availability line: "We have reliable 1:1 reference for the PSKX chunk map in `docs/rlviser-facts/pipeline.md` (RLViser `assets.rs:read_*`, `PNTS/VTXW/FACE/MATT/VERTEXCO/EXTRAUVS`)."

## Asset/cache paths (why it stays up on GitHub)

- Git tree ships code + `stadiums/*.json` (embedded via `include_flate!`) only. `./assets/*`, `./cache/*`, `umodel*`, `*.tfc` are gitignored. `umodel::uncook()` finds `RocketLeague.exe`, reads `assets.path` → `…/TAGame/CookedPCConsole`, runs `umodel -game=rocketleague -export -nooverwrite -nolightmap -uncook -uc` over 11 `.upk` (`Startup`, `MENU_Main_p`, `Stadium_P`, `HoopsStadium_P`, `ShatterShot_P`, 6 car bodies) into `./assets/`, writes `./assets/files.txt`.
- Debug: `get_mesh_cache(cache, asset, …)` → memory → `./cache/mesh/<name>.bin` → `./assets/<path>.pskx`; same two-level pattern for materials (`./cache/material`) and textures (`./cache/textures/*.tga` via `WalkDir` copy). Release (`not(debug_assertions)`): embeds the **locally built** `cache.zip` via `flate!(CACHED_ASSETS from "cache.zip")` and `unreachable!` on miss. So the 77 `.bin`s you saw were **locally generated**, not vendored — the repo's accuracy comes from runtime extraction, same split as our `--meshdir`.
- Availability line: "We have reliable 1:1 reference for the legal asset split in `docs/rlviser-facts/pipeline.md` (RLViser `.gitignore` + `assets.rs:uncook` + `cache_handler.rs`)."

## Layout-JSON structure (structural facts, no values copied)

- `STADIUM_P / HOOPS / SHATTERSHOT` layouts embedded via `flate!`; TheVoid = no field. Soccar/Hoops parse `(Pickup_Boost, structures, TheWorld)`; Dropshot parses `(TheWorld,)` only.
- `Node { subNodes: [Section] }`, `Section/ObjectNode { Location/Rotation/Scale + subNodes: [InfoNode] }`, `InfoNode { Translation/Rotation/Scale, StaticMesh, Materials?, InvisiTekMaterials? }`. Single-transform `ObjectNode.get_info_node()` vs multi-node fan-out; empty `StaticMesh` skipped with warn on missing materials.
- Grass lift `+10 y`, large-pad loc/rot capture for later pad alignment, `BreakOut`-only exception to the material blacklist.
- Availability line: "We have reliable 1:1 reference for the layout-graph shape in `docs/rlviser-facts/pipeline.md` (RLViser `mesh.rs:InfoNode/ObjectNode`)."

## Skip lists + static flags

- Mesh blocks: `CollisionMeshes`, `DecalBlocker`, `FieldCollision_Standard`, `Goal_STD_Outer`, `SkySphere01`, `Glow`, `Fog`, `FX_General`, `Collision_Plane` (+ `Side_Trim` unless `full_load`; non-`full_load` whitelist is `Field_STD/FF/BoostPads/BoostPad_Small/BoostPad_Large/Goal/AdvertStrip/Field_Center/Field_Mid/Body/Side/Floor/Lattice/FoamAd/Lines_Basketball/Net_Collision/BBall_*…`; `full_load` loads all).
- Material blacklist (8): `Collision_Mat`, `Grass_LOD_Team1_MIC`, `Glass_Projected_V2{,_Team2}`, `TreeBark_Mat`, `TrimLight_None_Mat`, `Asphalt_Simple_MAT`, `NetNonmove_Mat`. No-shadow mesh: `Proto_BBall.SM.Net_Collision` gets `NotShadowCaster + NotShadowReceiver`.
- Availability line: "We have reliable 1:1 reference for skip lists in `docs/rlviser-facts/pipeline.md` (RLViser `assets.rs:BLOCK_MESHES`, `mesh.rs:BLACKLIST/NO_SHADOWS`)."

## Frame pacing

- `FramepaceSettings { limiter: from_framerate(60) }` + `FramepacePlugin`; daylight `CycleTimer` at 60 Hz repeating. Per-frame work stays transform + material-alpha updates; state sets throttled to 30 Hz (`StateSetTime`).
- Availability line: "We have reliable 1:1 reference for pacing in `docs/rlviser-facts/pipeline.md` (RLViser `camera.rs:CameraPlugin`, 60 Hz limiter + 30 Hz state throttle)."
