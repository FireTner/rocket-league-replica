# Exact camera: what exists, what is achievable, and the approximation ranking

Companion to [`camera.md`](camera.md) (what to port now) — this file answers
"is there anything closer to the real RL camera?" so agents stop re-searching.
Researched 2026-10-20; treat search output as leads, not gospel.

## Verdict

No public exact decompilation of the camera update math exists. The fullest
public approximation remains the vendored CarBaller `RLCamera`
([`camera.md`](camera.md)). Two paths to actual exactness both run on THIS
machine only, from the user's own install, never committed (repo Rule 1).

## What exists publicly (interface, not algorithm)

- **BakkesModSDK auto-generated wrappers** —
  [CameraWrapper.h](https://github.com/bakkesmodorg/BakkesModSDK/blob/master/include/bakkesmod/wrappers/GameObject/CameraWrapper.h),
  `CameraXWrapper.h`, `CameraSettingsActorWrapper.h`: field offsets + live-binary
  thunks (`GetDesiredSwivel`, `UpdateSwivel`, `UpdateFOV`, `ClampPOV`,
  `CameraStateBlender`). Exact signature surface of the real camera; none of the
  math inside. Already absorbed as our field set (`docs/CROSS_CHECK.md` §7).
- **Memory-layout reversing** —
  [UnknownCheats SDK/structs thread](https://www.unknowncheats.me/forum/other-fps-games/179108-rocket-league-sdk-reversing-information.html),
  [flaryx32/rocketleague-offsets](https://github.com/flaryx32/rocketleague-offsets)
  (GNames/GObjects scanner),
  [DRKn1ght/RocketLeague-Data-Dumper](https://github.com/DRKn1ght/RocketLeague-Data-Dumper)
  (`PlayerCamera` at offset `0x480`). Offsets/layouts only.
- **UE3 base camera source (free, exact foundation)** —
  [UT3 UnrealScript sources](https://unrealarchive.org/wikis/unreal-wiki/UnrealScript_source_code.html):
  `Engine.Camera` / `GameFramework.GamePlayerCamera` + camera-state blender,
  which RL's `TAGame` camera subclasses. Exact scaffolding; TAGame's BallCam
  overrides are the unknown part. Consult when the decomp (below) shows a
  `super.` call you need the body of.

## Approximation ranking (closest first)

1. **CarBaller `RLCamera`** (vendored here) — dual rigs, aim slew, air-velocity
   heading, stiffness lag, orbit+tilt, snap, shake. Tuned numbers, full behavior
   set. Port target: [`CAMERA_PORT_PROMPT.md`](CAMERA_PORT_PROMPT.md).
2. **`rocketsim_vis::CarCameraConfig`** (VirxEC, Rust) — single-idea tilt rig
   (`300/130`, tilt 100→500, `0.2/0.7/0.5`). Weaker; documented in
   `docs/rlviser-facts/camera.md`.
3. **CinderBlocc plugins** ([FIFA](https://github.com/CinderBlocc/FIFAplugin),
   [FirstPerson](https://github.com/CinderBlocc/FirstPersonPlugin), DollyCam,
   CinematicsBuddy) — contain NO chase math; they override Focus/Rotation/
   Distance/FOV through the parent `CameraControl` hook on
   `ProjectX.Camera_X.ClampPOV` (unreal rotation units `182.044449`/deg) and read
   live `ProfileCameraSettings`. Not an approximation — they are the RECORDING
   RIG for path B below. Representative source read: `SideViewPlugin.cpp`
   (`CreateValues`: Focus from ball/car + height floor, Rotation from angle
   slider, `Distance = camSettings.Distance * 6`, `FOV = camSettings.FOV`).
4. **RLViser `update_camera`** — hardcoded debug rig (`300/150` vs `280/110/−6°`,
   no stiffness/dynamics/transition/swivel/shake). Never gameplay defaults
   (`docs/CAMERA_NOTES.md` G7 verdict).
5. **Unity/Godot clones** (`uozuaho`, `lufemas`) — trivial `look_at` follows.
   Ignore.

## Path A — decompile TAGame camera classes (ground-truth algorithm)

- Tool: [EliotVU/Unreal-Library](https://github.com/EliotVU/Unreal-Library)
  (UE1/2/3 package decompiler; RL 10897 / 867/009 supported, decryption via
  [RLUPKTool](https://github.com/AltimorTASDK/RLUPKTool)).
- Input: the user's own install packages (same key-bot + decrypt workflow as
  `docs/MESH_SETUP.md`; game lives in the Heroic prefix per that doc).
- Targets: `CameraState_BallCam_TA`, `Camera_X`, `CameraStateBlender`,
  `PlayerCamera` subclasses — decompile to UnrealScript, translate the update
  math to C++ in `src/camera/` by hand.
- Caveat: the Unreal compiler strips `cpptext` (native) blocks to
  `// (cpptext)` comments on import, so native-heavy functions may come back
  hollow — that is the signal to switch to path B for those functions.
- Hygiene: decompiled TEXT stays on this machine as working notes; translated
  BEHAVIOR lands in our own implementation. Never commit package bytes,
  decrypted dumps, or verbatim decompiled files.

## Path B — record the live camera, fit our model (exact constants)

- Rig: CinderBlocc `CameraControl` parent pattern (hook
  `Function ProjectX.Camera_X.ClampPOV`; per-frame Focus/Rotation/Distance/FOV
  + swivel state + `ProfileCameraSettings`) plus car/ball poses, logged each
  frame over scripted scenarios mirroring the ported probe (overhead crossing,
  dribble teleport/hold, overhead drift, air flip, boost run, swivel sweeps,
  transition toggles).
- Fit: solve for our model parameters (aim slew rate, air-ease rate,
  follow/lag rates + cap, transition rate, snap timing) against the recordings;
  keep what's already measured (Rocket-Science dynamics) fixed.
- Needs: RL running (Proton/Heroic) + BakkesMod under Wine on this machine;
  online play must stay untouched (offline freeplay only — note FIFA's
  `canvas.Project` caveat and the template's private-match time restriction).
- Output: MEASURED constants replacing CarBaller's tuned ones, gated by the
  ported probe (`CONTRACTS.md` R1) + `visual_gates.py --only VG4`.

## Suggested order

1. Port mechanisms now ([`CAMERA_PORT_PROMPT.md`](CAMERA_PORT_PROMPT.md)) — feel today.
2. Path A in parallel — replaces tuned math with Psyonix's own where it decompiles.
3. Path B for whatever A leaves hollow (native blocks) — replaces tuned numbers
   with measured ones.
