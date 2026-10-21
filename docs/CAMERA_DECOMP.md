# Camera decomp: what came out of the real game, and what we can gate on it

Status: **pulled, local-only, gated as `camera_surface_gate.py` (CS1–CS5)**.
Started 2026-10-20 as Path A of [`carballer-facts/EXACT_PATHS.md`](carballer-facts/EXACT_PATHS.md)
("decompile TAGame camera classes"). This file is the record; the derived truth
surface lives in a gitignored fixture, never in this tree.

> ### ERRATUM (2026-10-20, this order) — read before trusting anything below
>
> Three statements further down are **superseded by measurement** and must not be
> acted on. Details: `docs/CAMERA_EXACT_LOG.md` Rounds 8 and 15.
>
> 1. **"tagged-property names are inline int32-length strings" is FALSE.** There is
>    no inline-string name encoding. Property tags are ordinary UE3
>    (`FName name`, `FName type`, `i32 size`, `i32 arrayIndex`, typeData, value).
>    The `04 00 00 00 "RPC\0"` records that appeared to confirm it were real strings
>    from the **wrong object** — reads were shifted by
>    `TotalHeaderSize - DependsOffset` (ProjectX `86500`, TAGame `562181`).
> 2. **The real blocker was `SerialOffset`.** For licensee ≥ 33 it is relative to
>    `Summary.DependsOffset` while the object stream starts at `TotalHeaderSize`.
>    With that bias applied, `UDefaultProperty.DeserializeTagUE3` and
>    `UField.Deserialize` **never throw**, every camera object decodes with
>    `serial == decoded`, and the class default objects yield real values
>    (`FOVLimits 60/110`, `AngleLimits -15/0`, `DistanceLimits 100/400`, …).
> 3. **§1's "native" list was mostly wrong.** Only `Camera_X.ClampPOV` (plus
>    `EventCameraStateChanged`, `OnViewTargetChanged`, `EventCameraTargetChanged`)
>    is a native shell — its body is exactly `04 0b 4c`.
>    `GetDesiredSwivel` (474 B), `UpdateSwivel` (560 B) and `UpdateFOV` (118 B) all
>    carry real bytecode.
>
> What is genuinely still open is the **expression layer**: the licensee-34
> opcode map is unsolved past its anchors (`EX_Return 0x04`, `EX_Nothing 0x0B`,
> `EX_EndOfScript 0x4C` — exactly **7** below stock UE3's `0x53`), so no function
> yields compilable UnrealScript yet. Objects, property tables, CDO values and
> validated bytecode blobs *are* available.

## VERDICT (2026-10-20, rev 3) — what is exact, what is not, and why

This order's goal was to replace the CarBaller-derived mixer with RL's own
decomposition and exact constants. Here is the honest split. Evidence for every
line is in `docs/CAMERA_EXACT_LOG.md` (Rounds 0–22), reproduced by the lead.

**EXACT — decompiled from the real game and verified**

- **The rig's range constants.** `FOVLimits 60/110`, `HeightLimits 40/200`
  (interval 10), `AngleLimits -15/0`, `DistanceLimits 100/400`,
  `StiffnessLimits 0/1` (interval 0.05), `SwivelSpeedLimits 1/10`,
  `TransitionSpeedLimits 1/2`, `FreeLookSpeedLimits 1/10`. Six of our seven
  shipped ranges were **already correct** (they now have proof instead of guides);
  **`angle` is wrong** — ours allows `-30..15`, RL allows `-15..0`. The fix is
  cross-owner (A5: `Settings.cpp` + `INI_SCHEMA.md` + menu, with a failing
  roundtrip test first) and is **reported, not applied**.
- **Free-look and clamp values.** `MaxFreeLookPitchDegrees 75.0`,
  `FreeLookSensitivityScale_Mouse 0.83`, `_AnalogStick 10000`,
  `_Digital 5000`, `GroundClampZOffset 10.0`, `bAllowRearCamera`.
- **The transition blend shape.** `BlendParams = (VTBlend_Cubic, BlendExp 2.0,
  bLockOutgoing false)`. Our blend is a **linear scalar step** — the current code
  comment claiming "upstream is linear too" is true of CarBaller and **false of
  Rocket League**. Ported to P4 with provenance, not yet landed.
- **The camera surface itself**: 6 classes / 43 functions / 38 properties, pinned
  by `camera_surface_gate.py` (CS1–CS5, 5 PASS).

**VERIFIED IN OUR CODE — structure, agreement, and cost**

- RL's **layer decomposition** is now our architecture (`src/camera/TruthPov.h` +
  12 named layers), behind an unchanged public API, proven **bit-identical**
  over 900 scripted frames and 4270 randomized frames, with VG4's measured radii
  unchanged and VG7 identical to the recorded values.
- **`Pov()`** exposes RL's `Focus/Rotation/Distance/FOV/Time`, with
  `eye_from_pov(Pov()) == Eye()` holding to 0.000977 uu (float32 only) *by
  construction*. `Distance` was otherwise unobservable — `|Eye()-Center()|` is
  the look-target distance (~1100–1600 uu), not the rig distance (270–315 uu).
- **Trace tooling + gate**: format v1 (magic/version/stride/crc32, loud negative
  tests), `selfcheck` exact, and `camera_trace_gate.py` CT1 PASS / CT2–CT3
  **SKIP** (no recorded trace).
- **Cost**: camera `Update` is **0.303 / 0.365 / 0.470 µs** mean (rest / driving /
  worst) on machine B, p99 ≤ 0.671 µs — **5.9% of the 8 µs budget**. No
  allocation in the path. Machine A is `NOT MEASURED` (a different laptop, not
  reachable from this session).

**NOT EXACT — and why, with no pretending otherwise**

- **The expression-level math is still unresolved.** The licensee-34 **opcode
  map** is unsolved past its anchors (`EX_Return 0x04`, `EX_Nothing 0x0B`,
  `EX_EndOfScript 0x4C` — exactly **7** below stock UE3's `0x53`). Objects,
  property/field tables, CDO values and 17 validated bytecode blobs are
  delivered; **no function yields a compiled body**. Consequence: `P3` could port
  constants but **not the algorithms**, so the per-frame math remains CarBaller's
  shape with our own tuning.
- **The dynamics constants do not exist in the CDOs.** `FocusInterp`,
  `DistanceInterp`, `DistanceSpeedScale`, `AirVelocityInfluence(MaxSpeed)`,
  `InterpTo{Ground,Air}Rate`, `MaxSpeedFOV`, `FOVInterpSpeed`, `SupersonicFOV`,
  `SwivelDieRate`, `SwivelFastSpeed` are **declared but not defaulted** —
  `CameraState_Car_TA` declares 149 properties and its CDO serializes 11. They
  are therefore native-side, computed from `CameraSettings`, or zero with the
  behaviour in the bytecode we cannot yet read.
- **No live recording was made.** P2's hook was correctly *not attempted*: the
  user's session was running throughout (pid 77117), `ptrace_scope=1` with no
  passwordless sudo rules out attaching, and there is no MSVC-ABI toolchain for a
  BakkesMod plugin. So **CT2/CT3 — the actual exact-feel bar — remain SKIP**, and
  our camera's real error against RL is **`NOT AVAILABLE`**, not "small".
- **UE handedness is unreconciled.** Our world is Z-up with `yaw=atan2(y,x)`;
  UE rotators are left-handed at 65536/360°. `rotationFromDir`/`forwardFromRotator`
  document the mapping, but without a trace the sign question stays
  `UNVERIFIED` — it was not "fixed" by nudging a sign until a number matched.
- **Deliberately not landed** (feel changes awaiting a trace): the cubic
  transition, symmetric `±75°` free-look pitch (ours is −45.8°/+40.1°),
  `GroundClampZOffset 10.0` (ours `kEyeFloorZ 16.0`), and swivel decay.

**Bottom line.** The *structure, surface and constants* are exact and evidenced;
the *per-frame dynamics* are still ours, with RL's real values for the parts we
could prove. "Exact feel" is **not delivered** and was not closeable by this
route alone: it needs the expression layer (a bounded search, not a blank page —
see the 7-opcode gap) or a recorded trace (prepared, gated, not yet possible on
this machine). Nothing in the code claims otherwise — every constant is marked
`// decomp:` or remains explicitly tuned.

## Verdict in one line

The real camera **structure** is recoverable today (`Camera_X`, `CameraState_Car_TA`,
`CameraState_BallCam_TA`, `CameraStateBlender_X` + 98 camera symbols, with every
mechanism-bearing function and property name); the **function bodies** are not —
this build's packages are `UE3 868 / licensee 34` and the public decompiler stack
(UELib) only knows up to licensee 32. Nothing is guessed into our code from names:
the fixture proves which mechanisms exist, not what they compute.

## What we actually pulled (MEASURED, 2026-10-20)

Source: the user's own install, `~/Games/Heroic/rocketleague/TAGame/CookedPCConsole/`
(`ProjectX.upk` 2.2 MB for the base rig, `TAGame.upk` 75 MB for the `_TA` states).

```
UE3 868 / licensee 34, EngineVersion 10897, CookerVersion 136
TAGame.upk GUID 66101E814F23F2BA77A621A269A5613B, 54429 names / 129850 exports
ProjectX.upk    18878 exports
→ 98 camera-named classes/structs/enums, 43 mechanism-bearing functions,
  38 mechanism-bearing properties (the gate pins that subset)
```

Key rig, with the symbols that matter to us:

| RL class (package) | fns | what its symbol names prove exists |
|---|---|---|
| `Camera_X` (ProjectX) | 37 | `UpdateCamera`, `ProcessCameraState`, `ClampPOV`, `BlendCameraOrientations`, `SnapTransition`, `UpdateCameraKnocks`, POV fields `FOV/Distance/Rotation/Focus/InterpRate/MaxDistance` |
| `Camera_TA` (TAGame) | 21 | `UpdateSwivel`, `GetDesiredSwivel`, `ApplySwivel`, `UpdateFOV`, `ClipToField`, `ApplyRearCameraView`; `SwivelDieRate`, `SwivelFastSpeed`, `SwivelExtentFast/Slow`, `CurrentSwivel`, `bGroundClampTrace`, `GroundClampZOffset`, `*Limits` structs |
| `CameraState_TA` (TAGame) | 17 | free look as its own axis system: `UpdateFreeLook`, `ApplyFreeLook`, `UpdateFreeLookInput`; `MaxFreeLookPitchDegrees`, `FreeLookRateYaw/Pitch`, `FreeLookOffset`, `bFreeLookActive`, `FreeLookSensitivityScale_{Digital,AnalogStick,Mouse}` |
| `CameraState_Car_TA` (TAGame) | 34 | `UpdateAirGroundBlend`, `UpdateGroundPOV`, `UpdateAirPOV`, `CalculateDesiredAirRotation`, `UpdateDistance`, `UpdateFOV`, `UpdateFocus`, `UpdateValidPOV`; `AirVelocityInfluence(MaxSpeed)`, `SupersonicFOV(InterpSpeed)`, `MaxSpeedFOV`, `FOVInterpSpeed`, `DistanceSpeedScale`, `DistanceOffsetMin`, `DistanceInterp`, `FocusInterp`, `GroundRotationInterpRate(Wall)`, `InterpToGroundRate`, `InterpToAirRate` |
| `CameraState_BallCam_TA` (TAGame) | 21 | target selection (`TargetSelect`, `ChooseTarget`, `FindBestTarget`, `ShouldExecute`) + pitch shaping: `PitchExtentMax/Min`, `PitchFocusZFactor`, `PitchScale`, `RotationRate` |
| `CameraStateBlender_X` (ProjectX) | 10 | `BlendCameraState`, `UpdatePOV`, `TransitionToState`, `Tick`, `IsTransitioning`; `SnapshotPOV`, `RemainingTime`, `BlendParams`, `TransitionDelta` |

## Why the function bodies are not here (yet)

Three independent blockers, all reproducible:

1. **Decompiler ceiling.** UELib's Rocket League build range is
   `[Build(867, 868, 9u, 32u)]` — our `licensee 34` is outside it. Forced in, the
   package summary loads (`Build: RocketLeague`) but `UClass`/`UFunction`
   deserialization throws in `UField.Deserialize` / `UDefaultProperty.DeserializeTagUE3`.
2. **Name encoding changed.** In this build, tagged-property names inside object
   data are **inline int32-length strings**, not FName indices — e.g. a class
   default object starts `04 00 00 00 "RPC\0"`, and function data carries
   `13 00 00 00 "LocalTeamMemberIDs\0"`, `08 00 00 00 "PlayerID\0"`. A reader that
   expects FName indices decodes the string length as an index and walks off.
3. **Native surface.** Several of the hooked entry points (`ClampPOV`,
   `GetDesiredSwivel`, `UpdateSwivel`, `UpdateFOV`) are the ones BakkesMod's
   `CameraWrapper` thunks into; the UnrealScript may be a thin shell over native
   code, which is exactly why the community records these by hooking live state.

So the honest split is: **structure now** (this file + the gate), **numbers via
Path B** (record the live camera) — the same conclusion `EXACT_PATHS.md` reached,
now with the blocker pinned instead of suspected.

## Our port vs the real rig (structure only)

Left = what `src/camera/Camera.{h,cpp}` implements; right = the RL symbols that
justify it. **"structure" = the mechanism exists in the real game; every number
stays tuned/UNVERIFIED until Path B measures it.**

| Ours (`Camera.h` mechanism) | Real RL symbols | Status |
|---|---|---|
| #1 ball-aim slew (`ballYaw_`, 20 rad/s) | `CameraState_BallCam_TA.UpdateFocus`, `.RotationRate`, `Camera_X.ProcessCameraState` | mechanism confirmed; rate UNVERIFIED |
| #2 air heading (`carYaw_`, 200→900 uu/s) | `CameraState_Car_TA.UpdateAirGroundBlend`, `.AirVelocityInfluence`, `.AirVelocityInfluenceMaxSpeed`, `.CalculateDesiredAirRotation` | mechanism confirmed; blend window UNVERIFIED |
| #3 stiffness world-space lag | `Camera_X.UpdateCamera`, `Camera_X.ProcessCameraState`, `.FocusInterp`, `.DistanceInterp` | mechanism confirmed; `followRate` UNVERIFIED |
| #4 swivel orbit + snap-back (2× rate, 0.05 s idle, 6 rad/s floor) | `CameraState_TA.UpdateFreeLook/.ApplyFreeLook`, `Camera_TA.UpdateSwivel/.ApplySwivel`, `.SwivelDieRate`, `.SwivelFastSpeed`, `.SwivelExtentFast/.Slow` | **semantics differ**: RL has a swivel *die rate* + fast/slow extents (decay), we snap at 2×; pitch clamp is `MaxFreeLookPitchDegrees` (symmetric?) vs our −0.8/+0.7 rad |
| #5 one-euro look-target filter | `CameraState_Car_TA.FocusInterp`, `Camera_X.ProcessCameraState` | mechanism plausible; a one-euro filter is OUR choice — no matching symbol, treat as approximation |
| #6 transition blend (`0.5+2.5*transitionSpeed`) | `CameraStateBlender_X.BlendCameraState/.TransitionToState`, `.BlendParams`, `.RemainingTime` | mechanism confirmed; curve UNVERIFIED |
| dynamic FOV/distance (+5/+5, +100 @0 stiffness, +114.7 supersonic) | `CameraState_Car_TA.UpdateFOV/.SupersonicFOV/.SupersonicFOVInterpSpeed/.MaxSpeedFOV/.FOVInterpSpeed`, `.DistanceSpeedScale`, `.DistanceOffsetMin` | mechanism confirmed; numbers from Rocket Science, not from this decomp |
| shake | `Camera_X.UpdateCameraKnocks/.ModifyCameraShakeScale`, `CameraModifier_CameraShake_X` | ours is a screen shake only; RL has location/rotation **knocks** — gap (feature parity item) |
| look-behind | `Camera_TA.ApplyRearCameraView` | confirmed |
| ground clamp / field clip | `Camera_TA.bGroundClampTrace`, `.GroundClampZOffset`, `.ClipToField`, `.ClipRate` | **different**: RL traces and clips to the field; ours clamps rig elevation — gap |
| ball-cam pitch shaping | `CameraState_BallCam_TA.PitchExtentMax/Min`, `.PitchFocusZFactor`, `.PitchScale` | **missing in ours**: no extent clamps, no focus-Z pitch scale |

Biggest real findings for our own interpretation: (a) RL splits **swivel decay**
(`SwivelDieRate`, fast/slow extents) from **free look** (`CameraState_TA`, with
per-device sensitivity and a max pitch in degrees) — we model one lumped orbit;
(b) ball-cam pitch is *scaled by focus height* (`PitchFocusZFactor`) and clamped
to an extent — we slew pitch without either; (c) `ChildCameraState` transitions
live in a dedicated blender with snapshot POV, not in our per-frame blend.

## The truth gate

Prerequisite — the package key is a **local input, never in the repo**: put your
own copy at `~/.rl-decomp/upk.key` (64 hex digits) or export `RL_UPK_KEY`. No game
build, launch, or binary scan is involved. Without a key the extractor prints
`SKIP` and exits 3, so a public clone is inert but runnable.

```sh
python3 tools/decomp/camera_surface.py --out decomp/camera_surface.json  # re-extract
python3 tools/verify/camera_surface_gate.py                              # CS1-CS5
python3 tools/verify/camera_surface_gate.py --regen                      # both
```

- `decomp/camera_surface.json` is **Psyonix-derived data → gitignored**. The gate
  exits 0 with a loud `SKIP` when it is absent (same contract as
  `mesh_gates.py` without a mesh dump), so CI/other machines stay green.
- Checks: `CS1` pinned package identity (engine 10897; a patch changes it →
  re-baseline this file), `CS2` 6 camera classes, `CS3` 43 functions, `CS4` 38
  properties, `CS5` every mechanism we ported still maps to a live RL symbol.
- Measured 2026-10-20: **5 PASS / 0 FAIL** on the local install, 98 symbols
  extracted. It is an advisory/local tier gate — it never blocks `make check` and
  never touches `gates*.json`.

## Legal boundary (Rule 1)

- **Tooling landed in-repo is ours** (`tools/decomp/rlupk.py`,
  `tools/decomp/camera_surface.py`, `tools/verify/camera_surface_gate.py`, MIT):
  a UE3 package reader + a symbol extractor. No Psyonix bytes.
- **No key in the repo.** `rlupk.py` reads `$RL_UPK_KEY` or `~/.rl-decomp/upk.key`
  (outside the tree). A published clone therefore carries no circumvention
  technology — it carries a parser that needs *your* local input. This is the
  deliberate publication boundary: reader/analysis may ship, the key may not.
- **Not in-repo**: package files, decrypted copies, decompiled text, the fixture
  (`decomp/`), and any `.upk` work dir. They stay on the user's machine.
- **Facts may land**: names, counts, package identity, and the mechanism mapping
  above are facts, cited like RocketSim line numbers.
- Third-party tools used locally (not vendored, not build deps): UELib (MIT),
  RLUPKTool key provenance (MIT), .NET 10 SDK — logged in `THIRD_PARTY.md`.

## If we want the bodies (next steps, cheapest first)

1. **Patch UELib's RL branch for licensee 34**: read tagged-property names as
   strings (`UDefaultProperty.DeserializeTagUE3`) and the inline `UField` list.
   Then `obj decompile Camera_X.UpdateCamera` yields UnrealScript for the rig.
   Best effort ~a session; validate against functions we can already identify by
   size (`BlendCameraState` 1181 B, `UpdateCameraKnocks` 818 B, `UpdateCamera` 695 B).
2. **Path B** ([`EXACT_PATHS.md`](carballer-facts/EXACT_PATHS.md) §Path B): hook
   `ProjectX.Camera_X.ClampPOV`, record Focus/Rotation/Distance/FOV + swivel over
   scripted scenarios, fit our constants. This is the only route to measured
   numbers for the native entry points, and it also validates whatever (1) yields.
3. Fill the gaps the structure already proves: swivel decay model, ball-cam pitch
   extent + `PitchFocusZFactor`, location/rotation knocks, field clip.

## Log

- 2026-10-20 (round 1): decrypt + inflate verified — `rlupk.py` reads TAGame.upk
  in 1.1 s (54429 names / 129850 exports, 78 chunks, 161 MB image) and
  ProjectX.upk (18878 exports). 98 camera symbols extracted to a 244 KB fixture
  (names/sizes only). UELib built on .NET 10, confirmed to stop at licensee 32;
  patched locally to 34 it parses the summary as `Build: RocketLeague` and then
  throws in `UField`/`UDefaultProperty` — the inline-string name encoding above.
  Gate landed: `camera_surface_gate.py --regen` = **CS1–CS5 5 PASS / 0 FAIL**
  (43 functions, 38 properties, all 10 ported mechanisms mapped). Not run:
  `make check` — no compiled source touched this round (docs + `tools/decomp/`
  + the advisory gate only).
