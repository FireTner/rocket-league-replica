# RLViser facts base — citable 1:1 reference (facts only, no bytes)

Provenance: `VirxEC/rlviser` v0.9.2 (`Bevy 0.18.1`, `bevy_framepace 0.21`), fetched 2026-10-17 via `web_fetch` of `Cargo.toml`, `src/camera.rs`, `src/spectator.rs`, `src/udp.rs`, `src/mesh.rs`, `src/assets.rs`, `src/settings/cache_handler.rs`, `.gitignore`, `README.md`, plus `docs.rs/rocketsim_vis` `CarCameraConfig`. No RLViser file was cloned; no game mesh/texture/JSON byte enters this tree.

Legal line (Rule 1, `AGENTS.md:43-54`): this folder holds **facts** — numbers, shapes, algorithms, techniques — ported as prose with source links, exactly like CarBaller/BakkesModSDK citations. It holds **zero Psyonix bytes**: no `.bin`, `.tga`, `.pskx`, `Stadium_P_MeshObjects.json` content, UV/normal arrays, or material graphs. What RLViser actually does (verified): `./assets/*` + `./cache/*` are **gitignored** (`.gitignore`: `/assets*`, `/cache`, `umodel*`); `src/assets.rs:umodel::uncook()` extracts `Stadium_P.upk` etc. from the user's **own** `rocketleague/TAGame/CookedPCConsole` via UModel; `src/settings/cache_handler.rs` builds `./cache/mesh/*.bin` **locally** (debug) or embeds the local `cache.zip` (release); only `stadiums/*.json` layout + code ship in the Git tree. Our repo follows the same split: facts here are citable, game bytes only ever live in a gitignored user-local dir and **gates always run the clean-room path**.

How to use: when the owner names a feature ("ball cam", "shadows", "pads", "PBR", …), the agent's first line for that feature MUST be the availability statement, e.g. "We have reliable 1:1 reference for X in `docs/rlviser-facts/<file>.md` (RLViser <file>:<symbol>, <value>)." Then verify our code against the fact, never paste RLViser code or data. If a phase cannot proceed without game bytes, SKIP loudly and stay clean-room.

## Files

| File | Covers | 1:1 refill for |
|---|---|---|
| `camera.md` | spectator setup, `update_camera` ballcam/carcam/director, `rocketsim_vis` tilt rig | any camera-accuracy question — verdict: ours beats theirs for gameplay |
| `pipeline.md` | spawn-once + transform-per-frame, PSKX chunks → mesh, cache paths, layout-JSON structure, skip lists, frame pacing | merged draws, VBOs, loader design, draw-call cuts |
| `lighting-materials.md` | PBR params, alpha/cull modes, cascades, sun/atmosphere/tonemap/exposure | G6 eye-candy options + 60 FPS verdicts |
| `cars-pads-ball.md` | hitbox→body map, boost/wheel/demo presentation, pad glow + yaw tweaks, ball light, dropshot tiles | car/pad/ball fidelity checks |

Related: `MASTER_PROMPT_GRAPHICS45.md` §3 (take/leave table), `docs/CAMERA_NOTES.md`, `docs/VISUAL_PARITY_LEDGER.md`, `docs/THIRD_PARTY.md`.
