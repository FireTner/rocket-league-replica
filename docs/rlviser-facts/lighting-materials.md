# Lighting/material facts (RLViser v0.9.2) — we have reliable 1:1 reference for every eye-candy flag

Sources: [`src/camera.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/camera.rs), [`src/assets.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/assets.rs), [`src/mesh.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/mesh.rs), [`src/udp.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/udp.rs). Bevy/RLViser engine technique is MIT — re-implement the technique in our GLSL, never paste engine code; Psyonix material art stays out.

## Base PBR params

- Field/car fallback: `perceptual_roughness 0.7`, `reflectance 0.25`; unknown-material fallback adds `metallic 0.1`, `double_sided`, `cull_mode None`. Clean-room defaults in `get_default_material`: `roughness 0.6`, `reflectance 0.2`. Ball: `roughness 0.7`, `reflectance 0.25`, `base_color WHITE` (or `DARK_GRAY` when texture missing; sphere fallback `r 91.25`).
- `ForcefieldHex` special-case: `base_color (0.3, 0.3, 0.3, 0.3)` + texture; pad glow uses `diffuse_transmission 1.0` + emissive `0.75×`.
- Availability line: "We have reliable 1:1 reference for PBR baselines in `docs/rlviser-facts/lighting-materials.md` (RLViser `assets.rs:retreive_material`, `0.25/0.7`)."

## Alpha / cull technique (31 double-sided, 2 transparent, 15 additive)

- Transparent: LombardyPoplar bark/leaf → `Blend`. Additive: `ForceField_HexGage`, `HexGlass_WithArrows_{Team1,Team2}`, `LightCone_Simple`, `StadiumLight_Flare`, fog sheets/cylinders, `Glass_Projected_V2*`, `NetNonmove`, `BBall_Net` → `Add` (+ `double_sided`, `cull_mode None` for the 31-list). Non-`full_load` material whitelist is only 4 forcefield/hex entries; everything else falls back to the clean-room color ladder (primary `45,49,66`; secondary blue `86,136,199` / orange `222,145,81`; tertiary `55,30,48`; silver/advert/collision ramps — technique reference, tune our own hexes).
- Availability line: "We have reliable 1:1 reference for blend/cull treatment in `docs/rlviser-facts/lighting-materials.md` (RLViser `assets.rs:DOUBLE_SIDED/TRANSPARENT/ADD_MATS`)."

## Sun + cascades + atmosphere (G6 technique source)

- `GlobalAmbientLight FULL_DAYLIGHT`; `DirectionalLight` + `Hardware2x2` filtering + `SunDisk::EARTH`; shadow map default; cascade builder `minimum 20 / first_far 2000 / maximum 30_000`; camera-side `ShadowFilteringMethod::Gaussian`.
- `Atmosphere::earthlike`, `scene_units_to_m 0.01` (1 uu = 1 cm), `aerial_view_lut_max_distance 320`; `Exposure::SUNLIGHT`; `Hdr` on both 3D and overlay cameras.
- Daylight cycle (60 Hz): `t = (offset + secs) / (200 / day_speed)` (paused `stop_day` freezes `secs` at 0); `sun_pos = (−cos t, sin t, 0) * 100_000`, `look_at(ZERO)`; `illuminance = max(sin t, 0)² × RAW_SUNLIGHT`.
- Availability line: "We have reliable 1:1 reference for sun/cascade/atmosphere wiring in `docs/rlviser-facts/lighting-materials.md` (RLViser `camera.rs:setup/daylight_cycle`, `20/2000/30000` + `0.01/320`)."

## What to build from this (our GLSL, our numbers)

G6a PBR-lite (spec + roughness + ambient), G6b 2-cascade (R4) / 3–4-cascade (4060) shadows with glass/pad skips, G6c atmosphere/aerial tint + sun-disk + daylight + Reinhard — each behind `[render]` flags, default OFF, each with an R4-60FPS verdict. Verify against these facts; never import a `.tga`/`.mat`/MIC graph.
