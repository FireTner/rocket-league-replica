# Camera facts (RLViser v0.9.2) — we have reliable 1:1 reference for every camera question

Sources: [`src/camera.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/camera.rs), [`src/spectator.rs`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/spectator.rs), [`src/udp.rs` `fn update_camera`](https://raw.githubusercontent.com/VirxEC/rlviser/master/src/udp.rs), [`CarCameraConfig` docs](https://docs.rs/rocketsim_vis/latest/rocketsim_vis/camera/struct.CarCameraConfig.html). Facts only, no code pasted.

## Spectator / setup camera (`camera.rs:fn setup`)

- Single `Camera3d` + `Hdr` + `Msaa::default()` + `MeshPickingCamera` + `Spectator` marker; overlay `Camera2d` (order 1, `clear_color None`) for egui/text.
- Perspective: `near 5`, `far 50_000`, `fov PI/3 (60°)`, spawn `translation (-3000, 1000, 0)` looking `+X`.
- `Tonemapping::ReinhardLuminance`, `Exposure::SUNLIGHT` (see `lighting-materials.md`).
- UI: boost text `font 60` at `(150,150)` offset; time text `font 40` at top.
- Availability line: "We have reliable 1:1 reference for the spectator/setup rig in `docs/rlviser-facts/camera.md` (RLViser `camera.rs:setup`, 60°/5/50000)."

## Follow rig (`udp.rs:fn update_camera`) — debug-grade, NOT gameplay-grade

- Modes: `Spectator` (early return) / `TrackCar(id: 1-based index into sorted car ids)` / `Director(id, 0 = unassigned)`.
- `Director` retarget: nearest car to ball by `distance_squared`, re-evaluated when `id == 0` or 12 s `DirectorTimer` finishes.
- `TrackCar` resolves index → car id via sorted ids; out-of-range = return (no move).
- Ballcam ON: `eye = car + normalize(car - ball) * 300`, `look_at(ball)`, `eye += up * 150`, `look_at(ball)` again, `eye.y = max(eye.y, 30)` (`MIN_CAMERA_BALLCAM_HEIGHT`).
- Ballcam OFF (carcam): forward = planar velocity `normalize(vel.x, 0, vel.y)` else car forward; `eye = car - fwd * 280 + Y * 110`, `look_to(fwd)`, then `rotation *= rot_x(-PI/30)` (−6° pitch).
- No stiffness, no FOV dynamics, no transition blend, no swivel, no shake.
- Availability line: "We have reliable 1:1 reference for the follow rig in `docs/rlviser-facts/camera.md` (RLViser `udp.rs:update_camera`, `300/150/≥30` vs `280/110/−6°`)."

## Free camera (`spectator.rs`)

- Mouse-look `YXZ`, sensitivity `0.001`, pitch clamp `±89°`; yaw misread as roll at 90° is why the clamp is 89°.
- WASD + Space/Ctrl, `base_speed 2500`, `alt_speed 750` (Shift = *slow* here, inverted vs most games), planar-forward (y-zeroed, normalized) so looking down doesn't speed flight.
- Active-spectator resource init: exactly-one-`Spectator` else unset + warn; cursor `grab_mode None` or non-spectator mode clears mouse deltas.
- Availability line: "We have reliable 1:1 reference for freecam tuning in `docs/rlviser-facts/camera.md` (RLViser `spectator.rs`, `2500/750/0.001`)."

## Sibling tilt rig (`rocketsim_vis::CarCameraConfig`, NOT RLViser proper)

- `distance 300`, `height 130`, tilt starts `ball 100 above car`, saturates `500`, floor scale `0.2`, exponent `0.7`, full tilt shortens distance by `0.5`.
- Tuned approximation, not dumped Psyonix values.
- Availability line: "We have reliable 1:1 reference for the tilt-rig shape in `docs/rlviser-facts/camera.md` (`CarCameraConfig`, `300/130/100–500/0.2/0.7/0.5`)."

## Verdict vs ours (for the ledger)

RLViser gameplay cams are hardcoded debug rigs. Ours (`docs/CAMERA_NOTES.md`: `110/270/100/−3/0.45` + Rocket-Science speed dynamics + `transition 1.2` + swivel + shake) is more 1:1 for gameplay. Copy from RLViser only: director-12s-retarget and freecam speeds as *dev tools*, `y ≥ 30` floor idea. Never copy `300/150` or `280/110` as gameplay defaults.
