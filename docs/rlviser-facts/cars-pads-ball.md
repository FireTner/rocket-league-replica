# Car / pad / ball presentation facts (RLViser v0.9.2) — we have reliable 1:1 reference for fidelity checks

Sources: [`src/udp.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/udp.rs), [`src/mesh.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/mesh.rs), [`src/assets.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/assets.rs). Facts only.

## Hitbox-X → body map (RocketSim `hitbox_size.x`, uu)

`120–121` octane / `130–131` dominus / `131–132` plank / `133–134` breakout / `129–130` hybrid / `123–124` merc, else octane fallback. Invisible hitbox shell = `Cuboid(hitbox)` with `base_color NONE + AlphaMode::Add + unlit` + `SimplifiedMesh(Sphere(hitbox.y * 2.5))` for picking; visible body meshes hang as `CarBody` children (team-tinted `roughness 0.7` unless `full_load` per-part materials).
Availability line: "We have reliable 1:1 reference for the body map in `docs/rlviser-facts/cars-pads-ball.md` (RLViser `udp.rs:spawn_car`, X-bands)."

## Boost / wheels / demo presentation

- Boost cone: `Cylinder(r 10, len 50)` at `x −(hitbox.x + 50)/2`, `y +hitbox.y/2`, `rot_z 90°`, `NotShadowCaster/Receiver`, alpha `0.7` boosting / `0.0` idle. Boosting = `!demoed && boost > ε && (controls.boost || boost decreased since last tick)`.
- Wheels: front/back pairs from `config.front/back_wheels`; offset `connection_point_offset * (1,1,±1)` − `Y*(rest_length − 12)`; front scaled by `front_radius/back_radius`; mirrored `rot_x(PI)` per side.
- Demoed: body + wheel materials → `alpha 0 + Add` (ghost), restore `Opaque` on respawn; trigger `is_demoed || demo_respawn_timer > ε`.
- Availability line: "We have reliable 1:1 reference for boost/wheel/demo presentation in `docs/rlviser-facts/cars-pads-ball.md` (RLViser `udp.rs:update_car_extra`, `50/0.7/12`)."

## Pads

- Hitboxes (procedural, licence-clean precedent): small `Cylinder(d 144, h 165)`, large `Cylinder(d 208, h 168)` — matches RocketSim `RLConst` and our `--debug-colliders`.
- Visual: mesh `y − 70` (+ `2.6` Soccar / `5.2` Hoops large), glow `LinearRgba(0.9, 0.9, 0.1)` alpha `0.6` active / `0.0` inactive, `emissive ×0.75`, `diffuse_transmission 1.0`, `Add + double_sided + cull None`, `NotShadowCaster/Receiver`; large-pad yaw snapped from captured `LargeBoostPadLocRots` (≤5 uu match), small-pad yaw snapped by position bands (`x ±1023 → 30°`, `z ±1023 → 60°`, corner `(1787, 2299) → ±45°`, Hoops variants).
- Availability line: "We have reliable 1:1 reference for pad presentation in `docs/rlviser-facts/cars-pads-ball.md` (RLViser `udp.rs:spawn_pads/update_pad_colors`, `0.6/0.0` + yaw snaps)."

## Ball + dropshot

- Ball entity at `y 92` start, fallback mesh `Sphere(91.25)`, pick shell `Sphere(95)`, child `PointLight (0.3,0.3,0.3), intensity 200M, range 1000`, color shifts with `|z|` height and sign. Rotation integrates `ang_vel` (`axis-angle × dt`; interpolate path slerps last→next quats).
- Dropshot tiles: full `GREEN` / damaged `RED` / broken `BLACK`; neutral mid quad + per-team hex fans; only color updates on state change (tick-guarded).
- Availability line: "We have reliable 1:1 reference for ball/tile presentation in `docs/rlviser-facts/cars-pads-ball.md` (RLViser `mesh.rs:load_extra_field`, `91.25/95/200M/1000`)."
