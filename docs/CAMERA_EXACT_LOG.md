# CAMERA_EXACT log — orchestrated execution of `MASTER_PROMPT_CAMERA_EXACT.md`

Owner: **the lead agent only** (`MASTER_PROMPT_CAMERA_EXACT.md` §2.5 — workers never
write docs). Every block below contains output the lead **reproduced itself**;
a worker's report is quoted only as a claim and is marked as such.

Change-control: this round series touches `src/camera/**` + `tools/decomp/**` +
`tools/verify/camera_trace_gate.py` only. It never edits `measured.py`,
`mesh_gates.py`, `gates_visual.json`, frozen tapes, or any
`src/sim|arena|physics` file (Rule 5 / §2.2–2.3).

---

## Round 0 — baseline — 2026-10-20 (lead)

Session start state, reproduced by the lead before any dispatch.

```
$ git log --oneline -1
216af92 feat(camera): port CarBaller ball-aim, air heading, stiffness lag, swivel snap, target filter, transition re-time

$ git status --short
 M .gitignore
 M AGENTS.md
 M CONTRACTS.md
 M docs/CAMERA_NOTES.md
 M docs/GATE_POLICY.md
 M docs/THIRD_PARTY.md
 M docs/carballer-facts/EXACT_PATHS.md
 M src/render/BUILD_NOTES.md
 ? third_party/RocketSim
?? MASTER_PROMPT_CAMERA_EXACT.md
?? docs/CAMERA_DECOMP.md
?? tools/decomp/
?? tools/verify/camera_surface_gate.py
```

`? third_party/RocketSim` is the known untracked `A1_VENDOR.txt` submodule entry;
the vendored physics source is pristine (pre-existing, unrelated to this order).

### Camera truth gate (the P1/P2 prerequisite is alive)

```
$ python3 tools/verify/camera_surface_gate.py --regen
98 camera symbols -> /home/cachyfire/Projects/RocketLeagueLaptop/decomp/camera_surface.json (UE3 10897/34)
camera_surface_gate: fixture=decomp/camera_surface.json UE3 10897/34
  PASS CS1 identity engine=10897 licensee=34
  PASS CS2 6 camera classes present
  PASS CS3 43 mechanism-bearing functions present
  PASS CS4 38 mechanism-bearing properties present
  PASS CS5 all 10 ported mechanisms map to live RL symbols
camera_surface_gate: 5 PASS / 0 FAIL
```

The local fixture is present, so W1/W2 are not blocked on re-extraction.

### Code under change (sizes at dispatch)

```
570  src/camera/Camera.cpp
145  src/camera/Camera.h
231  tests/test_camera_dynamics.cpp
```

### Environment recon (lead, one-shot — do not re-probe per turn)

| Item | Observed |
|---|---|
| `.NET` | `~/.dotnet/dotnet` → SDK `10.0.402` |
| UELib clone | `~/rl-decomp/uelib` @ `8236c39`, local patch `[Build(867, 868, 9u, 34u)]` present (`src/UnrealPackage.cs` ~L904) |
| Dump app | `~/rl-decomp/dump` (`Dump.csproj` → `net10.0`, refs `../uelib/src/Eliot.UELib.csproj`); `dump/bin/` **absent** → first P1 build |
| Package key | `~/.rl-decomp/upk.key` present (65 B); `$RL_UPK_KEY` unset |
| Packages | `~/Games/Heroic/rocketleague/TAGame/CookedPCConsole/` — `RocketLeague.exe` 38,873,528 B |
| BakkesMod | Installed in prefix `~/Games/Heroic/Prefixes/Rocket League` (`BakkesMod.exe`); **no `plugins/`, no `BakkesModSDK`**, no mingw/`msvc` toolchain → a C++ plugin is not buildable here as-is |
| `rl-env.sh` | Sets `WINEPREFIX`, `PROTONPATH=proton-cachyos-slr`, `PROTON_VERB=runinprefix`; `~/run_bakkesmod.sh` + `start-bakkesmod.sh` are the sanctioned launchers |
| Session | `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`, session type `wayland` |
| Ptrace | `/proc/sys/kernel/yama/ptrace_scope = 1` (child-only) → a **launcher-parent** reader is viable; attaching to an arbitrary pid is not |
| Vulkan ICDs | `nvidia_icd.json` **and** `radeon_icd.json` → both GRAPHICS45 machines reachable from this box |
| `rtk` | `/usr/bin/rtk` present, but **no `rtk git` subcommand** on this build (`rtk git log` → `unexpected argument 'git'`); plain `git` is used and the deviation is recorded here rather than hidden |

The last row is a recorded deviation from `AGENTS.local.md`'s `rtk` rule: the
installed `rtk` 0.51.0 rejects `rtk git …` with
`error: unexpected argument 'git' found`, so this order runs plain `git`
(AGENTS.local's rule cannot be satisfied as written on this binary).

### Lead decision: P2 route (recorded before W1 returns)

§6 P2's first-choice hook is "BakkesMod under Wine if available". Recon shows
BakkesMod is installed but **has no plugin SDK and no Windows toolchain on this
box**, so building a plugin is not the cheap path. Two viable routes remain,
neither committed to yet — the P1 result decides:

1. If P1 recovers the mechanism bodies **and** their class-default constants,
   P2's remaining job is *verification only*, and several constants may need no
   live recording at all.
2. Otherwise P2 falls back to a read-only Linux-side memory reader launched as
   the game's **parent** (satisfies `ptrace_scope=1`), per §6 P2's stated
   alternative.

Not started, not promised. Native entry points stay `UNVERIFIED` until measured
(§4 failure ladder rung 4).

---

## Round 0b — fixture size table, and a correction to §1 — 2026-10-20 (lead)

The lead read the serialized function sizes straight out of the local fixture
(`decomp/camera_surface.json`, produced by `camera_surface.py --regen` above).
This is the table P1's decoded bodies must reconcile against, so it is recorded
before any worker claims a body.

UE3 stores a `UFunction` as header + bytecode; a **native** function has no
script body and therefore sits at the header floor. Observed floor in this
package: **70 B** (7 unrelated functions land exactly there:
`Camera_X.EventCameraStateChanged`, `Camera_X.ClampPOV`,
`Camera_X.OnViewTargetChanged`, `Camera_TA.EventCameraTargetChanged`,
`CameraState_BallCam_TA.UpdateRotationModifiers`,
`CameraStateBlender_X.EventBlenderStateChanged`).

| Class (package) | Function | Serialized |
|---|---|---|
| `Camera_X` | `UpdateCamera` | 695 B |
| `Camera_X` | `UpdateCameraKnocks` | 818 B |
| `Camera_X` | `BlendCameraOrientations` | 321 B |
| `Camera_X` | `ProcessCameraState` | 168 B |
| `Camera_X` | `RemoveRoll` | 98 B |
| `Camera_X` | `SnapTransition` | 91 B |
| `Camera_X` | `ClampPOV` | **70 B** |
| `Camera_TA` | `UpdateSwivel` | 560 B |
| `Camera_TA` | `GetDesiredSwivel` | 474 B |
| `Camera_TA` | `ClipToField` | 493 B |
| `Camera_TA` | `UpdateFOV` | 118 B |
| `Camera_TA` | `ApplySwivel` | 94 B |
| `Camera_TA` | `ApplyRearCameraView` | 225 B |
| `Camera_TA` | `GetCameraSettings` | 337 B |
| `CameraState_TA` | `UpdateFreeLookInput` | 745 B |
| `CameraState_TA` | `UpdateFreeLook` | 116 B |
| `CameraState_TA` | `ApplyFreeLook` | 99 B |
| `CameraState_TA` | `WriteFreeLookOffset` | 587 B |
| `CameraState_Car_TA` | `UpdateGroundPOV` | 963 B |
| `CameraState_Car_TA` | `UpdateAirPOV` | 505 B |
| `CameraState_Car_TA` | `UpdateFocus` | 435 B |
| `CameraState_Car_TA` | `UpdateValidPOV` | 654 B |
| `CameraState_Car_TA` | `UpdateDistance` | 293 B |
| `CameraState_Car_TA` | `UpdateFOV` | 199 B |
| `CameraState_Car_TA` | `UpdateAirGroundBlend` | 211 B |
| `CameraState_Car_TA` | `CalculateDesiredAirRotation` | 111 B |
| `CameraState_BallCam_TA` | `UpdateFocus` | 231 B |
| `CameraState_BallCam_TA` | `FindBestTarget` | 263 B |
| `CameraState_BallCam_TA` | `TargetSelect` | 118 B |
| `CameraStateBlender_X` | `BlendCameraState` | 1181 B |
| `CameraStateBlender_X` | `TransitionToState` | 449 B |
| `CameraStateBlender_X` | `UpdatePOV` | 161 B |

**Correction to `MASTER_PROMPT_CAMERA_EXACT.md` §1** (recorded, not silently
diverged). §1 says: "*`ClampPOV`, `GetDesiredSwivel`, `UpdateSwivel`, `UpdateFOV`
are native: packages can never give them, only P2 can.*" The measured sizes
support that for **`ClampPOV` only** (70 B = header floor). `GetDesiredSwivel`
(474 B), `UpdateSwivel` (560 B) and both `UpdateFOV`s (118 B / 199 B) carry real
bytecode and should therefore decompile. The most likely explanation for the
original claim is that BakkesMod reaches those *script* functions through
`ProcessEvent` (which is how a script function gets "thunked" into), and that
was read as evidence of nativity.

Consequence for the plan, decided now: **the native surface is smaller than
assumed**, so P2's blocking risk is lower and P3 has more real bodies to port
than §1 predicted. P2 remains necessary for `ClampPOV` and as an independent
check, but it is no longer the sole source for the swivel/FOV mechanism.

`Camera_TA`'s serialized `*Limits` structs (`Distance/Angle/Stiffness/
SwivelSpeed/TransitionSpeedLimits`) are the decoder oracle noted in the steering
message to P1: their real contents are already facts in `docs/INI_SCHEMA.md`
§`[camera]` (100..400, -30..15, 0..1, 1..10, 1..2).

## Round 0c — VG4 pre-change baseline + an evidence-file hazard — 2026-10-20 (lead)

P0's stop condition is "any VG4 number moves", so the lead captured the numbers
**before** P0 touched anything (this box = machine B):

```
$ timeout 130 python3 tools/verify/visual_gates.py --only VG4 --diag
       . VG4 z=600 outer=91 inner=71 disc=45
       . VG4 z=900 outer=93 inner=73 disc=46
       . VG4 outer meas=91 pred=91.0 err=0.0%
       . VG4 inner meas=71 pred=71.3 err=0.4%
       . VG4 disc  meas=45 pred=45.1 err=0.3%
       . VG4 ratios inner/outer=0.780 (exp 0.784) disc/outer=0.495 (exp 0.496)
       . VG4 growth z900/z600 = 1.022 exp 1.026
VG4  PASS  ball indicator radii (recipe)      z600 outer=91 inner=71 disc=45; ratios 0.780/0.495; growth 1.022
# summary: 1 PASS, 0 FAIL, 0 SKIP
```

**Reference numbers for the P0 barrier: `z600 outer=91 inner=71 disc=45`,
`z900 outer=93 inner=73 disc=46`, ratios `0.780/0.495`, growth `1.022`.**

### Hazard hit and handled (GATE_POLICY §"Known gaps", reproduced firsthand)

That run **overwrote** `tools/verify/gates_visual.json` wholesale — the committed
record held one `VG1` row (machine A / R4), and the `--only VG4` pass replaced it
with a single `VG4` row. Reproduced exactly as `docs/GATE_POLICY.md` warns. The
file was restored immediately (`git checkout -- tools/verify/gates_visual.json`)
and is clean again; it is never committed from this order.

Standing rule adopted for the rest of this order: **every** lead/worker
`visual_gates.py --only …` run is bracketed by a
`cp tools/verify/gates_visual.json /tmp/gv.bak` … `restore`, because the writer
dumps only the current run's results.

## Round 0d — P2 route decided (evidence-based) — 2026-10-20 (lead)

One-shot probes (recorded once per `AGENTS.local.md`; not to be re-run per prompt):

| Probe | Result | Consequence |
|---|---|---|
| `sudo -n true` | `sudo: a password is required` | cannot lower `ptrace_scope`; no attaching to a non-child |
| `/proc/sys/kernel/yama/ptrace_scope` | `1` | only a **direct** parent may ptrace |
| `which xwin cargo-xwin cl x86_64-w64-mingw32-gcc` | none | no MSVC-ABI toolchain |
| BakkesModSDK on disk | none | plugin route needs a download + ABI-matched toolchain |
| BakkesMod plugins dir | absent (`BakkesMod.exe` only) | no prebuilt hook to reuse |
| `pgrep -f RocketLeague` | **pid 77117 running** | the user has a live session |
| Vulkan ICDs | `nvidia` + `radeon` | both target machines reachable |

### Decisions and the rules that follow

1. **The running Game is the user's session — off limits.** P2 may not attach to,
   inject into, signal, or otherwise perturb pid 77117 (or any user process it
   did not spawn itself). P2 works only on an instance **it launches**, offline
   freeplay. This is stricter than §6 P2 ("offline freeplay only") and is now a
   standing worker rule.
2. **BakkesMod plugin route: rejected.** A plugin DLL must match MSVC's C++ ABI
   (`std::string`/`std::vector` cross the boundary); mingw's libstdc++ ABI does
   not, and there is no MSVC/clang-cl+xwin toolchain here. Not attempted.
3. **ptrace/attach route: rejected.** `ptrace_scope=1` + no sudo + pressure-vessel
   layer between us and the game means we would not be the game's *direct*
   parent.
4. **Chosen route: `LD_PRELOAD` shim inside an instance we launch.** The shim
   runs in the game's own address space, so no ptrace is involved at all. It
   still has to find `GObjects`/`Camera_X` by signature scan, which is the real
   cost — hence P2 stays a bounded attempt, not a promise.
5. **The plan already anticipates P2 failing** (§4 ladder rung 4, §5.5): P3 then
   runs the **P1-only subset** and the native entry points (`ClampPOV` — and only
   that one per Round 0b) stay `UNVERIFIED`, never invented.

Net effect of Round 0b + 0d: the decompile path is now the primary truth source
and P2 is the verification/native-gap path, which is the opposite weighting from
`MASTER_PROMPT_CAMERA_EXACT.md` §1. Recorded, not silently diverged.

P2 must also not launch a second game instance while the user's is running —
wait for the barrier and check `pgrep` first.

## Round 0e — camera CPU baseline, measured by the lead — 2026-10-20

P5's budget is **≤ 8 µs/frame** on machine A. The lead measured the *pre-change*
cost so P5 has a real before/after and so a regression cannot hide behind a
budget that was never in danger.

Harness (lead-owned, outside the tree): `/tmp/cam_bench.cpp` — steps
`Camera::Update` at 60 Hz for 200,000 frames per scenario, `steady_clock`,
mean/p50/p99/max in µs. Built from **`HEAD`'s** `Camera.{h,cpp}` via
`git show HEAD:…` into `/tmp/cam_base/`, so it is the pre-P0 code and never
touches the in-flight tree:

```
$ g++ -std=c++20 -O2 -I /tmp/cam_base/src /tmp/cam_bench.cpp \
      /tmp/cam_base/src/camera/Camera.cpp -lGL -o /tmp/cam_bench_base
$ /tmp/cam_bench_base
S0 rest ballcam       mean 0.262  p50 0.260  p99 0.261  max 26.159 us
S1 drive+ball ballcam mean 0.354  p50 0.341  p99 0.441  max 550.456 us
S2 air+ss+swivel      mean 0.438  p50 0.411  p99 0.621  max 15.990 us
```

Scenarios: **S0** static rest (cheapest), **S1** driving + moving ball in
ballcam (the common gameplay frame), **S2** airborne + supersonic + full swivel
(the worst path: both branches, both filters, saturation terms).

Reading: mean **0.26 / 0.35 / 0.44 µs**, p99 ≤ **0.62 µs**. The `max` column is
host preemption (550 µs on S1 is a scheduler stall, not camera work — p99 stays
0.44 µs), so `mean`/`p99` are the numbers to quote.

Consequence for the plan: the 8 µs budget is met with **~18× headroom already**.
P5 is therefore a **regression guard + evidence duty** (no-allocation assertion,
one `sincosf` per frame, the `CAM µs` pacing line, both-machine table), not an
optimization project. Any P3/P4 port that lands above ~1 µs/frame deserves a
hard look, since it would mean an accidental per-frame filter or allocation.

## Round 0f — bit-exact behavior oracle for a "behavior-preserving" refactor — 2026-10-20

P0's contract is *keep the current tuned behavior* (§6 P0.2) and its stop
condition is *any VG4 number moves*. VG4 measures a ball-indicator recipe
through a rendered frame — it is a good gate but a coarse one: a small change in
the eye's smoothing would move VG4 by a pixel or not at all. So the lead built a
**stronger oracle before dispatching**, and it is what the P0 barrier is judged
on.

Lead-owned, outside the tree: `/tmp/cam_trace.cpp` drives one deterministic
900-frame scenario through `Camera::Update` and prints the **exact IEEE-754 bit
pattern** of `eye[3]`, `center[3]`, `EffFov`, `Blend`, `SwivelYaw`,
`SwivelPitch` every frame. Scenario covers: rest → drive + moving ball in
ballcam → carcam with yaw sweep → held swivel on both axes → release/snap-back
→ airborne with air-velocity heading and alternating ballcam → supersonic
straight → look-behind held → a `dt == 0` frame → a hitch frame past the 0.1 s
clamp → swivel pitch clamped both ways.

Built twice — once from `HEAD`'s `Camera.{h,cpp}`, once from the working tree —
and `cmp`-ed. Any differing bit anywhere is a behavior change.

```
$ g++ -std=c++20 -O2 -I /tmp/cam_base/src /tmp/cam_trace.cpp \
      /tmp/cam_base/src/camera/Camera.cpp -lGL -o /tmp/cam_trace_base
$ /tmp/cam_trace_base > /tmp/trace_base.csv
lines=900          md5 = b50f7f41c1748fd91a160bbd76846b83
re-run            md5 = b50f7f41c1748fd91a160bbd76846b83   → DETERMINISTIC
distinct eye positions = 804 / 900   distinct swivel pitch values = 39
```

**Baseline `md5 = b50f7f41c1748fd91a160bbd76846b83`** is the number P0's tree
must reproduce. It is `HEAD`'s camera, so it is a *pre-existing* artifact, not
a target the worker could have written to.

Caveat stated honestly: this oracle pins the **current** API's observable output
(eye/center/fov/blend/swivel). If P0 legitimately renames internals but keeps
those outputs, the trace is identical — exactly the property we want. It does
**not** cover GL output (`Apply()`'s matrix), which stays VG4's job.

## Round 0g — decoder state, reproduced by the lead — 2026-10-20

> **PARTLY SUPERSEDED by Round 0k.** The `DependenciesTable` reading below is
> wrong: that exception is caught by UELib and the fatal path is deeper. Kept for
> the record; **Round 0k is the authoritative reading of this failure.**

Two independent checks of P1's in-flight tooling, run by the lead itself:

**(1) `tools/decomp/unpack.py` works** (this is the new in-tree MIT tool):

```
$ python3 tools/decomp/unpack.py .../ProjectX.upk /tmp/px_decrypted.upk
... 10656 names, 1259 imports, 18878 exports, image 3758294 bytes, 1 chunks
wrote /tmp/px_decrypted.upk: 3758294 bytes (10656 names, 1259 imports,
      18878 exports, 1 chunks, UE3 868/licensee 34)     [0.14 s, exit 0]
```
It also refuses to write inside the repo and exits 3 with a `SKIP` without a key
— both verified by reading the code, and the `rlupk` attributes it uses
(`MissingKey`, `chunks`, `name_count`, `import_count`, `export_count`,
`licensee_version`) all exist. **Review pass: accepted.**

**(2) The decoder's current throw is NOT the documented one**, and the summary
now parses correctly:

```
$ ~/.dotnet/dotnet dump/bin/Release/net10.0/Dump.dll /tmp/px_decrypted.upk \
      find Camera_X </dev/null
Package Version:868/34
Build:RocketLeague
Branch:UELib.Branch.UE3.RL.EngineBranchRL
Header Size: 1816445
Names Count:10656 ... Exports Count:18878 ... Imports Count:1259
GUID:6488894849B0D73458FF86BEBA37DA19
EngineVersion:10897  CookerVersion:136  CompressionFlags:1
UELib.UnrealException: Couldn't parse DependenciesTable
 ---> System.IO.EndOfStreamException: Unable to read beyond the end of the stream.
```

`docs/CAMERA_DECOMP.md` predicted `UField.Deserialize` /
`UDefaultProperty.DeserializeTagUE3`; the actual blocker on the `ProjectX`
image is the earlier header-level **`DependenciesTable`**. The decrypt/inflate
layer and the summary are therefore proven sound (the GUID and counts match the
recorded facts), and the remaining work is header/table-level before any
property decoding can be judged.

`~/rl-decomp/uelib` at this point showed only the 1-line license patch
(`src/UnrealPackage.cs | 2 +-`), i.e. the property/field readers were not yet
modified. P1 is still running; this block is a status observation, not a verdict.

## Round 0h — P0 mid-flight: behavior already bit-identical — 2026-10-20

Early application of the Round 0f oracle to P0's **in-flight** working tree
(`Camera.cpp` 570 → 858 lines, `Camera.h` 145 → 183, `TruthPov.h` 10 lines new;
`tests/test_camera_dynamics.cpp` not yet re-baselined):

```
$ g++ -std=c++20 -O2 -I src /tmp/cam_trace.cpp src/camera/Camera.cpp -lGL \
      -o /tmp/cam_trace_new
$ /tmp/cam_trace_new > /tmp/trace_new.csv
new  md5: b50f7f41c1748fd91a160bbd76846b83
base md5: b50f7f41c1748fd91a160bbd76846b83
RESULT: BIT-IDENTICAL to HEAD
```

**All 900 frames, every bit of eye/center/EffFov/Blend/SwivelYaw/SwivelPitch
unchanged**, with the layer split and `TruthPov.h` already in place. The refactor
is doing what §6 P0.2 requires. This is provisional (P0 is still running and
`t_cam`/VG4 have not been reproduced yet); the barrier re-runs it on the final
tree.

## Round 0i — the licensee-34 string encoding, confirmed at byte level + CDO map — 2026-10-20

The lead went around UELib entirely and read class default objects straight out
of `rlupk`'s image, to check whether the documented encoding is real. It is.

**CDO exports located (both packages, `export_name` / class / serialized size):**

| Export | Class | Package | Size |
|---|---|---|---|
| `Default__Camera_X` | `Camera_X` | ProjectX | 68 B |
| `Default__CameraState_X` | `CameraState_X` | ProjectX | 198 B |
| `Default__CameraStateBlender_X` | `CameraStateBlender_X` | ProjectX | **522 B** |
| `Default__CameraState_CamActor_X` | `CameraState_CamActor_X` | ProjectX | 3222 B |
| `Default__Camera_TA` | `Camera_TA` | TAGame | **3637 B** |
| `Default__CameraState_TA` | `CameraState_TA` | TAGame | 149 B |
| `Default__CameraState_Car_TA` | `CameraState_Car_TA` | TAGame | **410 B** |
| `Default__CameraState_BallCam_TA` | `CameraState_BallCam_TA` | TAGame | **780 B** |
| `Default__CameraState_CarRef_TA` | `CameraState_CarRef_TA` | TAGame | 12 B |

**The encoding is exactly as `docs/CAMERA_DECOMP.md` §blocker-2 describes** — an
`int32` length followed by that many ASCII bytes with a NUL included:

```
Default__Camera_TA            @0x1c: 04 00 00 00 | "RPC\0"                 len=4
Default__CameraState_BallCam  @0x10: 13 00 00 00 | "TotalBreakoutGoals\0"  len=19
```

So the "tagged-property names are inline int32-length strings" theory is
**CONFIRMED EVIDENCE, not a hypothesis** — the lead read it out of the bytes.
What is *not* yet established is the field layout that follows each name (the
`41 fb ff ff` / `e4 7a 00 00` run after `"RPC\0"` is not an obvious type-string
length), which is precisely the piece P1 is working on.

Two extra facts worth having:
- `Default__CameraState_Car_TA` (410 B) contains **bytecode-shaped data with
  string constants** (`1f 4d 61 6e 61 67 65 72 3d 28 00 16` → `"Manager=(" …`),
  so the class export carries more than a plain default-property list and a
  reader that assumes pure FPropertyTag stream will misparse it.
- `Default__Camera_TA` is 3637 B — far larger than its siblings — so it is the
  main carrier of the rig's tuning defaults, and the best target for a
  property dump (`*Limits`, `Distance`, `Angle`, `Stiffness`…).

**Route note (decided, not yet acted on):** because `rlupk` already yields
correct object bytes and the only unknown is a field layout, a small **Python**
property decoder over `rlupk.py` may be cheaper than patching UELib's UE3-RL
header tables (which currently die earlier, at `DependenciesTable`). P1 still
owns the decision while it runs; if it reports BLOCKED on UELib, the next P1
slice takes the Python route with this CDO map as its test corpus.

## Round 0j — a caution: the CDO is not a plain tagged-property list — 2026-10-20

A lead prototype scan of `Default__Camera_TA` (3637 B, verified as the **only**
export with that name, class `Camera_TA`, offset 12,478,696 inside a
161,457,033-byte image, so the slice is definitely correct) found these
int32-length name records:

```
0x001f RPC          0x02b8 TournamentID   0x02f1 TeamName    0x0326 PlayerTeamID
0x035f Status       0x0396 ID             0x0485 ReturnValue 0x04c1 S
0x05fb TourID       0x062e ReturnValue    0x066a T           0x07b0 TeamID
0x07e3 ReturnValue  0x081f G              0x0959 Clump       0x0997 T
0x0ae9 XPAmount     0x0b1e ReturnValue    0x0b5a XP          0x0d54 ParentCategory
0x0d8f Label        0x0dc1 Type           0x0df6 ParentID    0x0e2b ID
```

Those are **not** `Camera_TA`'s tuning properties. They read like function
parameter / tournament-reward names. Conclusion the lead draws, and the reason
this is logged rather than acted on:

> The assumption "a `Default__*` export is a plain `FPropertyTag` stream" is
> **not safe** in this build. The class export evidently carries inline
> `UField` children (function parameter records), exactly as
> `docs/CAMERA_DECOMP.md` §blocker-2 warns, and the tuning defaults may not be
> where a naive reader would look.

So: **no constant may be read out of a CDO until the layout is established**,
and the `*Limits` known-value oracle (100..400, −30..15, 0..1, 1..10, 1..2) is
only meaningful once the parser is anchored by something other than its own
assumption. The lead's prototype is stopped here — it is P1's phase — and this
note exists so that nobody mistakes "found some names" for "decoded the
properties".

Recorded honestly: this is a **negative** result. It reduces the risk of a
fabricated constant being ported into `src/camera/`.

## Round 0k — CORRECTION to Round 0g: the real failure, and a hollow-body trap — 2026-10-20

The lead re-ran the decoder **without** truncating the output. Round 0g's
conclusion ("the current throw is DependenciesTable, not `UField`") was **wrong**
— it came from a `head -15` that cut the chain just before the fatal part.
`docs/CAMERA_DECOMP.md` §blocker-2 was right all along. Full output:

```
$ ~/.dotnet/dotnet dump/bin/Release/net10.0/Dump.dll /tmp/px_decrypted.upk \
      decompile Camera_X.UpdateCamera </dev/null
... summary parses: 10656 names / 18878 exports / 1259 imports, UE3 868/34 ...
UELib.UnrealException: Couldn't parse DependenciesTable
 ---> System.IO.EndOfStreamException: ...
   --- End of inner exception stack trace ---
// target: Function'px_decrypted.Camera_X.UpdateCameraKnocks'
UELib.DeserializationException: Couldn't load object Function'px_decrypted.Camera_X.UpdateCameraKnocks'
      as type UELib.Core.UFunction ... System.ArgumentOutOfRangeException:
      Index was out of range. Must be non-negative and less than the size of the collection.
 ---> System.ArgumentOutOfRangeException: ...
function ();
```

Three facts, in order of importance:

1. **`DependenciesTable` is caught** by UELib and execution continues — it is a
   warning, not the blocker. It is also why Round 0g misread this.
2. **The fatal path is the documented one**: `UFunction` → `UStruct` →
   `UField.Deserialize` → `IndexToObject` index out of range. The same shape
   appears in the prior session's `~/rl-decomp/out/Camera_TA.txt` (2026-10-20
   16:38) on `TAGame.upk`: `UField.Deserialize` / `UClass.Deserialize` throwing
   `IndexToObject` out of range. So the blocker is stable across both packages
   and both sessions. **`docs/CAMERA_DECOMP.md` needs no correction.**
3. **A real trap: the tool prints `function ();` instead of failing.** After the
   deserialization exception it emits a syntactically valid, completely hollow
   body. A worker that captured stdout without checking stderr would have a
   "decoded body" that is actually empty — the exact fabrication §6 P1 forbids.

**Standing rule added for this order:** a claimed body only counts if (a) it is
non-empty and is not `function ();`, (b) its decoded size reconciles with the
serialized size from the Round 0b table, and (c) stderr contains no
`DeserializationException` / `ArgumentOutOfRangeException` for that object. The
third condition is new and is the one that catches the hollow-body trap.

Also worth noting: the dumper resolved `Camera_X.UpdateCamera` to
`Camera_X.UpdateCameraKnocks` (its `EndsWith` matcher), so **target resolution is
loose** — a worker asking for `UpdateCamera` can silently receive a different
function. Body-size reconciliation against the Round 0b table is what catches
that, which is another reason that table matters.

## Round 1 — P0 — 2026-10-20 (lead-reproduced; P0's own report still pending)

P0's working tree at this point: `Camera.cpp` 570 → 858 lines, `Camera.h` 145 →
183, `TruthPov.h` new (10 lines), `tests/test_camera_dynamics.cpp` 231 → 502.

**t_cam, compiled and run by the lead** (not via `make`, so no build-lock
contention with the worker):

```
$ g++ -std=c++20 -O2 -I src tests/test_camera_dynamics.cpp \
      src/camera/Camera.cpp -lGL -o /tmp/t_cam_lead && /tmp/t_cam_lead
[1] compiled defaults (RL 1:1 + additive keys)         6 ok
[2] rest pose == distance/height/angle exactly         5 ok
[3] a moved target lags, then converges                7 ok
[4] the eye only ever lags INWARD from the stiffness sphere   4 ok
[5] a bearing flip takes the short arc                 2 ok
[6] swivel saturates, is linear, and returns exactly to zero  ...
[7] transition blend ...   [8] speed dynamics ...   [9] look-behind ...
[10] held look-up keeps the eye off the floor and tilts the view
CAMERA PASS
```

The suite is now **structural**, as §6 P0.3 demands: rest pose equals
`distance/height/angle` analytically, lag-then-converge, eye never leaves the
stiffness sphere, 180° flip takes the short arc, swivel saturates and returns
**exactly** to zero. The old invented numbers (`20 rad/s`, `19.1°`/frame,
`0.05 s` idle) survive only in the file's header comment that records what was
removed — verified by grep, no assertion uses them. 10 groups vs the previous 7.

**Behavior oracle (Round 0f), re-run on the final tree:**
```
ORACLE: BIT-IDENTICAL to HEAD (900 frames)
```
**Perf (Round 0e harness), before → after:**
```
             HEAD            P0 tree
S0 rest      0.262 us  →     0.282 us
S1 driving   0.354 us  →     0.344 us
S2 worst     0.438 us  →     0.445 us
```
i.e. unchanged within run-to-run noise, ~18× under the 8 µs budget.

**Status: P0's substance is verified.** Still outstanding for the barrier:
P0's own report, and the lead's `--only VG4` reproduction (evidence-file
backup/restore rule from Round 0c applies).

### Code review of the P0 split (lead) — what landed, and one thing NOT to assume

`Camera::Update` is now a readable §3 pipeline with named layers, not a
cosmetic reshuffle:

```
UpdateSwivel (§3.3) → UpdateTransition (§3.2) → UpdateDynamics (§3.1)
→ UpdateCarFacing (§3.1) → UpdateBallAim (§3.1) → ClampPov (§3.3+4)
→ BallCamPov / CarPov (§3.1, both states evaluated every frame)
→ BlendCameraState (§3.2) → SnapTransition (first frame) → FollowPov (§3.1 interp)
```
plus `rotationFromDir()` (§3.1 rotator units, 65536 = 360°), `RearViewPov`
(§3.4), and a real `povTime_` state clock. `TruthPov.h` is exactly §3's struct.

**Critical caveat for W4/P3, recorded now so it cannot be misread:** the
pipeline order above is **`HEAD`'s order, relabelled** — swivel/offset state is
advanced *before* the state POVs are built, and the blend happens after them.
§3's target order is state → blender → offsets → clamp. P0 deliberately did
**not** reorder, because reordering is a behavior change and P0's contract was
behaviour-preserving (proven bit-identical above). §8's trap stands: RL clamps
and removes roll *after* the offset layer, and reordering shifts feel near the
floor and at max pitch without VG4 noticing.

Therefore: **the §3 ordering is still P3's work**, to be landed with a trace,
never inferred from P0's structure.

## Round 2 — P1 in progress: the `SerialOffset` root cause, lead-verified — 2026-10-20

P1 found the real blocker, and it is **not** the inline strings (those were
already confirmed as the *format*, but the stream never got to them). The
`SerialOffset` field for licensee ≥ 33 is relative to **`DependsOffset`**, not
to `HeaderSize`, so every export was being read at the wrong place — which is
why UELib walked off into `IndexToObject` out of range. P1's patch:

```csharp
long serialOffset = stream.ReadInt64();
// Licensee >= 33: relative to FUnrealPackageSummary.DependsOffset; bias is
// (HeaderSize - DependsOffset). Measured: ProjectX 86500, TAGame 562181.
var summary = stream.Package.Summary;
if (summary.HeaderSize > summary.DependsOffset)
    serialOffset += summary.HeaderSize - summary.DependsOffset;
```

**Lead verification, run in an isolated copy** (`/tmp/leadcheck`, so it could
not collide with the worker's `bin/obj`); build clean, `0 Error(s)`:

```
$ ~/.dotnet/dotnet /tmp/leadcheck/dump/bin/Release/net10.0/Dump.dll \
      /tmp/px_decrypted.upk decompile Camera_X.RemoveRoll </dev/null
// target: Function'px_decrypted.Camera_X.RemoveRoll'
System.IO.EndOfStreamException: ...
System.ArgumentOutOfRangeException: Index was out of range ...
private final function RemoveRoll()
{
	local @NULL InRot;
	// UnresolvedToken (0x0F)
	// UnresolvedToken (0x35)
	// BadToken (0x4F)
	/* Statement decompilation error: Index was out of range ... */
	// UnresolvedToken (0x00)
	/*@Error*/;
}
```

**Verdict on the patch: the offset theory is CORRECT.** A real UnrealScript
*signature* (`private final function RemoveRoll()`) now comes out, where before
there was the hollow `function ();`. The export stream is being addressed
properly.

**And it moves the blocker precisely one layer down**, which is the useful part:

- `local @NULL InRot;` — the local's **name resolved to NULL**, and every
  variable reference is `UnresolvedToken`. That is exactly the symptom of an
  unparsed **`UField` children list** (parameters + locals), i.e. P1's task 3.
- `BadToken (0x4F)` follows from the same misalignment.
- Opcode constants themselves look right (`0x1F`/`0x16` appear where
  `EX_StringConst`/`EX_EndFunctionParms` belong), so the token *table* is not the
  problem — the **declarations** are.

So the remaining work is the inline `UField` records, not the bytecode decoder.
Accepted-so-far: the `UExportTableItem.cs` bias fix, on my own reproduction.
Not yet accepted: any body — none decodes cleanly yet, and §6's bar (body
non-empty, size reconciling, clean stderr) is unmet.

## Round 3 — P0 BARRIER CLOSED (lead-verified) — 2026-10-20

P0 reported DONE; the lead reproduced every claim that matters.

**Write scope: clean.** P0 touched exactly `src/camera/Camera.{cpp,h}`,
`src/camera/TruthPov.h` (new), `tests/test_camera_dynamics.cpp`. The other dirty
entries in `git status` (`AGENTS.md`, `CONTRACTS.md`, `docs/*`, `src/render/
BUILD_NOTES.md`, `tools/verify/camera_surface_gate.py`, `docs/CAMERA_DECOMP.md`)
are the **pre-existing baseline dirt** recorded in Round 0 — not P0's. No file
under `src/sim|arena|physics`, `src/game`, `src/render`, `src/settings`,
`tools/verify|tas` was touched by the worker.

**Reproduced by the lead:**

```
$ rm -f /tmp/t_cam && flock /tmp/rl-build.lock sh -c '... make /tmp/t_cam'
CAMERA PASS                      (49 ok / 0 FAIL — was 7 groups, now 10)
$ python3 tools/verify/camera_surface_gate.py
camera_surface_gate: 5 PASS / 0 FAIL
$ cp tools/verify/gates_visual.json /tmp/gv.bak
$ python3 tools/verify/visual_gates.py --only VG4 --diag
VG4  PASS  ...  z600 outer=91 inner=71 disc=45; ratios 0.780/0.495; growth 1.022
$ cp /tmp/gv.bak tools/verify/gates_visual.json     -> 0 dirty
```
VG4's numbers are **identical to the Round 0c pre-change baseline** (outer 91,
inner 71, disc 45, ratios 0.780/0.495, growth 1.022). Zero movement — P0's stop
condition is satisfied. Plus the Round 1 evidence: behavior oracle
**bit-identical** over 900 frames, perf unchanged.

### P0's two findings, accepted and recorded as P3/P4 obligations

1. **The 3-D stiffness-sphere bound does NOT hold today once swivel is active.**
   The follower uses two gains (`kx` horizontal, `kz` vertical, halved airborne),
   and a mixed-gain blend of two points **on an arc** lands outside it: the worker
   measured **+3.06 uu on a 292.79 uu radius (1.04%)** during a swivel sweep,
   and **exactly 0.00** with no swivel. The horizontal component never leaves.
   The old test only ever asserted the *horizontal* bound, which is why this was
   invisible. `t_cam` now asserts the true bounds (horizontal never leaves;
   3-D never leaves without swivel; swivel excursion < 5%) and the deviation is
   written up in the test comment.
   **Lead's ruling:** correct call — fixing it in P0 would have been a retune.
   **Recorded as an acceptance criterion for P3:** once the per-axis split is one
   `InterpTo`, the 3-D bound should hold *exactly*. If it still breaks after
   InterpTo, that is the split surviving somewhere else — a real finding, not a
   test to relax.
2. **The §3 order is not reachable bit-identically** from today's code (the rig
   sphere consumes the swivel offset while placing the eye; the filters run on
   the mixed POV). Assigned to **P4 slices 1–2**, gated on a trace. Consistent
   with the review already logged in Round 1.

### Process note worth keeping (worker caught a trap)

`make tests` printed "Nothing to be done for 'tests'" because `tests` is
`.PHONY` with no recipe and every `/tmp/t_*` was current — so it was **not
evidence**. P0 forced a rebuild (`rm -f /tmp/t_rt /tmp/t_kb /tmp/t_gp /tmp/t_cam
/tmp/t_atex && make tests`) and then got real PASS lines. Matches
`AGENTS.local.md`'s warning that `tests` always recompiles; an up-to-date `make
tests` is the one case where it silently proves nothing.

**W1 CLOSED on the P0 side.** P1 (decoder) continues into W2 as the parallel
track; W2's other slot is P2-rig, dispatched now.

### Lead cross-check of P1's offset bias (independent of UELib) — 2026-10-20

P1's patch claims the bias `(HeaderSize - DependsOffset)` and quotes 86500
(ProjectX) / 562181 (TAGame). The lead verified that with `rlupk` alone, by
sorting every export by `serial_offset` and testing whether they **tile** the
inflated image with no overlaps:

```
ProjectX.upk: image=3758294  exports=18878  header_size=1816445
   first export off=0  last end=3671794  gaps=1 overlaps=0
   contiguous to image end? False  (delta 86500)      <-- matches P1's bias
TAGame.upk:   image=161457033 exports=129850 header_size=11813893
   first export off=0  last end=160894852 gaps=3 overlaps=0
   contiguous to image end? False  (delta 562181)     <-- matches P1's bias
```

`overlaps=0` and `gaps = 1 / 3` (alignment padding only) for **18878** and
**129850** exports respectively, with the residual exactly equal to the claimed
bias in both packages. Two independent readers now agree on the layout, and the
`SerialOffset` relative-to-`DependsOffset` theory is **confirmed**, not just
plausible. This is the kind of number that cannot be reached by accident.

`rlupk` exposes `depends_offset`, so the bias is computable from our own reader —
useful because P3 can then trust its object bytes without involving UELib at all.

### P1 midpoint, lead-reproduced: the object header is now byte-perfect — 2026-10-20

Rebuilt P1's current source in isolation (`0 Error(s)`) and decoded
`Camera_X.UpdateCameraKnocks` from `ProjectX`:

```
// target: Function'px_decrypted.Camera_X.UpdateCameraKnocks' (UFunction)
// size:   serial=818 decoded=818 state=Deserialized
protected final function UpdateCameraKnocks()
{
	local float DeltaTime;
	// UnresolvedToken (0x0F)
	/* Statement decompilation error: Index was out of range ... */
	// UnresolvedToken (0x00)
	/*@Error*/
}
```

Against §4's three-part bar for a claimed body:

| Criterion | Status |
|---|---|
| (a) body non-empty, not `function ();` | **PASS** — real signature + a typed local |
| (b) decoded size reconciles with serial size | **PASS EXACTLY — serial 818 = decoded 818**, matching the Round 0b table |
| (c) clean stderr for that object | **FAIL** — `Bad expression token 0x57`, plus `Index was out of range` in statement decompilation |

Interpretation: (b) is the strong one. If the object header, field list and
parameter records were still wrong, the decoder could not land on **exactly**
818 of 818 bytes *and* name the local correctly (`DeltaTime`, typed `float`) —
before the `UStruct` fix that same local came out as `@NULL`. So the object
structure layer is now right, and the remaining fault is confined to the
**expression/bytecode stream** (`0x57` is outside UE3's known `EX_*` range,
which suggests either a licensee-34 opcode or a desync after a const token).

**Still not accepted: no body.** P3 may not start from a body that fails (c).

### Token-map A/B on `RemoveRoll`, run by the lead — 2026-10-20

P1 added a `RL_TOKENS_STOCK=1` knob to test "licensee 34 uses stock UE3 opcodes,
not the licensed-32 remap". Lead ran both maps on `Camera_X.RemoveRoll` (98 B):

| | licensed-32 remap (default) | `RL_TOKENS_STOCK=1` |
|---|---|---|
| object decode | `serial=98 decoded=98 state=Deserialized` | `serial=98 decoded=98 state=Deserialized` |
| signature | `private final function RemoveRoll()` | same |
| local | `local @NULL InRot;` | `local @NULL InRot;` |
| bad tokens | `0x4F`, `0x03`, Unresolved `0x0F`/`0x35` | **`0x2B` only** |

Two conclusions:

1. **Stock tokens are closer but not sufficient.** The error count drops (one bad
   token instead of several, and no `UnresolvedToken` lines at all), so the
   licensed-32 remap is probably wrong for 34 — but the hypothesis alone does not
   produce a body.
2. **The residual failure is type resolution, not opcode mapping.** Both maps
   decode 98/98 bytes, i.e. the record sizes and stream walk are correct. The
   giveaway is `local @NULL InRot;`: the local's **name** parses (`InRot`) but its
   **type is NULL**. Under stock UE3 `0x2B` is `EX_TransformConst`, which needs a
   struct type to read its payload — with a null type it is exactly a "bad
   token". So the decoder is failing where a *type* is required, not where an
   opcode is unknown.

That points the remaining work at **typedef/type-index resolution inside the
inline `UField` child records** (name ok, type null), which is a narrower target
than "the bytecode decoder". Sent to P1 as a data point; no body accepted.

## P3 / P4 entry criteria — set by the lead, binding on the next waves

Written now (before P3 is dispatched) so the next wave cannot start on a
half-truth. Derived only from things reproduced in this log.

### What P3 may port from, and what it may NOT

- **Bodies**: only functions satisfying all three — (a) non-empty and not
  `function ();`, (b) `decoded == serial` against the Round 0b table, (c) **no**
  `DeserializationException` / `ArgumentOutOfRangeException` / `BadToken` naming
  that object on stderr. As of Round 8 **no function satisfies (c)**.
- A **raw token stream** (opcode + operands, no UnrealScript) is acceptable P3
  input **if** the lead has spot-checked it against a function whose behaviour is
  known independently — `RemoveRoll` and `ClampPOV` are the candidates.
- **Constants**: none may be read from a CDO until the layout is anchored by
  something other than its own assumption (Round 0j). The `*Limits`
  known-value oracle (100..400, −30..15, 0..1, 1..10, 1..2) is the anchor.
- Anything not derived stays **`UNVERIFIED`** in the code comment and in the
  verdict. The strings `// tuned`, `// feels right`, `// matches video` are
  banned as provenance (Rule 4 / §6 P3.3).

### Non-negotiables P3 inherits from P0 (do not regress them)

1. **Behavior oracle.** `HEAD` baseline dump `md5 b50f7f41c1748fd91a160bbd76846b83`
   (lead harness `/tmp/cam_trace.cpp`, 900 frames, `HEAD`'s camera) and P0's own
   `md5 165ecea09c52b2a7386279dc33ba58f7` (4270 frames, 10 scenarios + fuzz).
   P3 **intentionally** changes behavior, so these are *not* pass/fail for P3 —
   they are how the lead proves exactly **what** moved and where. Any frame-level
   difference must be explainable by a ported mechanism, not by a stray edit.
2. **`Eye()`/`Center()` stay load-bearing** (VG4, `--shot`, `--cam`). `TruthPov`
   may become authoritative (P0 carries both and says so), but the GL pair must
   keep its meaning.
3. **The 3-D stiffness-sphere bound** (P0's Round 3 finding): after the per-axis
   split becomes one `InterpTo`, the eye must never leave the sphere **in 3-D**,
   including under swivel. Currently it does (+3.06 uu on 292.79, 1.04%). If it
   still does after InterpTo, that is a finding to report, not a bound to relax.
4. **§3 ordering is P3/P4 work**, trace-gated. P0's pipeline is `HEAD`'s order
   relabelled; RL clamps/removes roll *after* the offset layer (§8 trap), and VG4
   will not notice a reordering.
5. **No new INI key without a failing roundtrip test first**, and only if a real
   RL setting needs it (P4 slice 5 stops and reports for A5 sign-off).

### Open question P3 must resolve, not guess

P0's hand-off: our world is **Z-up with `yaw = atan2(y,x)`**; UE rotators are
**left-handed, X-forward, 65536 units/360°**. `rotationFromDir()` documents the
mapping P0 used. The sign/handedness must be reconciled against the P2 trace
(or judged `UNVERIFIED`). Do not quietly "fix" a sign to make a number match.

### What P3 must produce

- `tools/verify/camera_trace_gate.py` with **CT1** self-replay exact, **CT2**
  per-scenario settle/overshoot within −5% of baseline, **CT3** no regression vs
  `camera_trace_baseline.json`. It must SKIP loudly (exit 0) without
  `/decomp/traces/` so a public clone stays green.
- Provenance comments on **every** frozen constant, citing
  `// decomp: <class>.<function>` or `// trace: <deck/file>`.
- `t_cam` structural invariants still green, VG4 and VG7 still green.

## Round 4 — HAZARD FOUND: a camera header edit does not rebuild `build/game` — 2026-10-20 (lead)

Found while checking P0's integration claim. `build/game`'s rule is
`build/game: $(GAME_SRCS) build/obj/src/sim/Sim.o $(ARENA_OBJ) build/librocketsim.a`
where `GAME_SRCS` are **`.cpp` files only**. There is no `-MMD -MP`, so
**headers are not prerequisites at all**. Proven, not asserted:

```
$ touch src/camera/Camera.h src/camera/TruthPov.h
$ flock /tmp/rl-build.lock sh -c '... make -n build/game'
make: 'build/game' is up to date.          <-- STALE HAZARD CONFIRMED
```

Why this matters for **this** order specifically: VG4 and VG7 measure
`build/game`. P3, P4 and P5 will all edit `src/camera/Camera.h` (layer state,
constants). A header-only edit leaves `build/game` from the **previous** camera
version, so a gate can report **PASS against old code** — precisely the
"green gates ≠ correct" failure mode the repo already recorded from the V6 pass.

P0 avoided it only by accident of ordering (it edited `Camera.cpp` last:
`build/game` 17:26:30 > `Camera.cpp` 17:26:03). Nothing guarantees that again.

**Standing rule adopted for the rest of this order (lead and every worker):**

> After ANY edit under `src/camera/`, force the game binary before running VG*:
> ```
> rm -f build/game
> flock /tmp/rl-build.lock sh -c 'source tools/dev-local.sh && make build/game'
> ```
> and check `build/game`'s mtime is **newer than every** file in `src/camera/`
> before quoting a VG result. `t_cam` is unaffected — it compiles from source
> every time (`tests` is `.PHONY`).

Recorded as a **process debt** for close-out: the real fix is `-MMD -MP` dep
generation in the Makefile, which is A6/coordinator-owned and outside this
order's write scope (`AGENTS.md` Rule 6 — no cross-owner rewrite without a
failing test; the failing "test" here is the `make -n` above, captured).

## Round 5 — regression caught in P1's `rlupk.data()` bias — 2026-10-20 (lead)

P1 is (correctly) opening the Python route — added `Package.from_image()`,
`export_offset()`, `find()` — but at 17:32:25 it also moved the
`(HeaderSize − DependsOffset)` bias into **`rlupk.data()`**. That is wrong, and
it corrupts every object read. Same export, only the bias differs:

```
Default__Camera_TA, TAGame.upk, serial_bias = 562181

UNBIASED  image[serial_offset:+32]: 00 00 00 00 00 00 00 01 00 00 00 80 ...
                                    ... 00 e4 7a 00 ... 04
          unbiased[0x20:0x28] = b'\x00\x00\x00RPC\x00A'      <-- "RPC\0" @0x20
BIASED    pkg.data() (new code):    9d 3a 00 00 87 4f 00 00 ...   <-- garbage
```

The unbiased slice is **byte-for-byte the dump verified in Round 0i**. So:

- **For `rlupk`'s inflated image, raw `SerialOffset` is already image-absolute** —
  no bias. `data()` must not add one. This is the same conclusion my Round 4
  tiling test reached from the other direction (`overlaps=0` over 129,850
  exports, residual exactly 562181 = an **unused tail**, not a missing offset).
- **For UELib the bias is real and stays**, because UELib reads the file through
  its own stream base. That fix is in `UExportTableItem.cs` and remains correct.

So the bias is a property of **UELib's stream base**, not of the image. Led with
the byte evidence and told P1 to remove the term from the read path rather than
set it to 0 (so it cannot silently return), and to note the asymmetry in the
docstring.

**Why the gates could not have caught this:** `camera_surface_gate.py` reads
export *sizes*, never object bytes, so it stays 5/5 either way. This is exactly
the class of defect that needs a lead reading bytes, not a green gate.

Status: P1's Python decoder is not accepted until the unbiased read is restored.

## Round 6 — the acceptance bar is now automated, and `ClampPOV` is not proven native — 2026-10-20

### Validator (lead-owned, `/tmp/verify_body.py`, outside the tree)

Mechanically enforces the three-criteria bar so acceptance is not a judgement
call: it parses `// size: serial=N decoded=M`, checks against the Round 0b table,
rejects the hollow `function ();` / declaration-only bodies, and fails on any
`DeserializationException` / `ArgumentOutOfRangeException` / `BadToken` /
`Bad expression token` / `UnresolvedToken` / `Statement decompilation error`.
Exit 0 = ACCEPT, 1 = REJECT, 2 = could not run. Proven on the current decoder —
it rejects everything, with reasons:

```
REJECT RemoveRoll         serial=98  decoded=98  expected=98
   - (c) ArgumentOutOfRangeException present
   - (c) BadToken present / Bad expression token / UnresolvedToken / Statement decompilation error
REJECT UpdateCameraKnocks serial=818 decoded=818 expected=818   (same shape)
REJECT ClampPOV           serial=70  decoded=70  expected=70
   - (c) UnresolvedToken present
```

Note it validates the size half of the bar as **satisfied** for all three — the
object layer is genuinely correct now — and rejects on (c) alone.

### Correction to Round 0b: `ClampPOV` is NOT established as native

Round 0b inferred nativity from `ClampPOV`'s 70 B size (the header floor). Now
that the object layer decodes, `ClampPOV` shows more than a bare header:

```
protected final simulated function ClampPOV()
{
	local @NULL OutPOV;
	// UnresolvedToken (0x04)
	None
}
```

It declares an **out-parameter `OutPOV`** and carries at least one opcode
(`0x04`, which under stock UE3 is `EX_Return`). So the honest statement is:
**`ClampPOV`'s nativity is UNPROVEN**, not proven. 70 B is the smallest observed
size, *not* proof of an empty script body — a function can be 70 B and still
declare a parameter.

Consequence, stated plainly because it changes the plan's weighting again: the
list of functions that genuinely *require* P2 may be **empty**. P2 remains
valuable as independent verification of dynamics (and the only way to check the
sign/handedness question), but it may no longer be the sole route to any single
constant — which is worth knowing before anyone spends the 2–3 day budget
recording traces.

Status: no body accepted; validator in place for the moment one is.

## Round 7 — P2(rig) tooling review (partial) + trace-dir convention decision — 2026-10-20

`tools/decomp/cam_trace.py` landed (1127 lines) and is reviewed on the parts that
can be judged before its self-check runs:

- **Format is sound and defensive**: magic `RLCT`, `version`, `header_len`,
  `stride`, `field_count`, `record_count`, `crc32(record block)`, then a JSON
  header, then fixed-stride little-endian float32 records. The header repeats the
  field table and the loader **fails loudly if the two disagree**, so a silent
  field reorder or a v2 file read by a v1 reader is detectable rather than
  silently misread. That is exactly what §6.3 asked for.
- **The POV→world convention is documented up front** (`Center()` ↔ FOCUS,
  `Eye()` ↔ derived from Focus/Rotation/Distance, `EffFov()` ↔ FOV) instead of
  being implied — which is the ambiguity that could otherwise poison every later
  comparison.
- **No coupling to P1's in-flight work**: imports are stdlib only
  (`argparse, json, math, os, random, struct, sys, time, zlib, array`). It does
  **not** import `rlupk`, so the `data()` bias regression of Round 5 cannot
  propagate into traces.
- **Rule 1 verified, not assumed**: traces go to the in-repo *gitignored*
  `decomp/traces/`, and
  `git check-ignore -v decomp/traces/foo.rlct` → `.gitignore:26:/decomp/`.
  `git status` shows no trace dirt.

### Decision (lead): the trace directory is in-repo `decomp/traces/`

§6 P3 says its gate "SKIPs without `/decomp/traces/`", while §4 P2's scope is
written `/decomp/traces/**`. Read literally as filesystem-root paths those are
inconsistent with how this repo already works: `camera_surface.py` writes the
gitignored **in-repo** `decomp/camera_surface.json`, and `.gitignore` carries
`/decomp/`. A filesystem-root `/decomp` does not exist on this machine.

**Ruling: the canonical location is the in-repo, gitignored `decomp/traces/`.**
P3's `camera_trace_gate.py` must SKIP loudly on *that* path (never a
filesystem-root `/decomp`), otherwise the gate would SKIP forever and look green
while measuring nothing. Flagged now because that failure mode is silent.

Still outstanding for P2's barrier: its self-check (replay → 0 error) and the
honest `NOT AVAILABLE` handling of the error table when no live trace exists.

## Round 8 — CORRECTION: my `rlupk` "regression" was wrong; the bias is right. REAL constants recovered — 2026-10-20

P1 disputed my Round 5 finding with three arguments. **P1 is right and I was
wrong.** Recorded in full because I put a wrong claim in this log and pushed it
to a worker.

The lead's decisive test had to be the oracle I myself proposed, and it passes
**only** with the biased read (rebuilt P1's source in isolation, `0 Error(s)`):

```
$ Dump.dll ~/rl-decomp/image/TAGame_decrypted.upk props Camera_TA
FOVLimits             (Min=60.0000000,  Max=110.0000000, interval=1.0000000)
HeightLimits          (Min=40.0000000,  Max=200.0000000, interval=10.0000000)
AngleLimits           (Min=-15.0000000, Max=0.0000000,   interval=1.0000000)
DistanceLimits        (Min=100.0000000, Max=400.0000000, interval=10.0000000)
StiffnessLimits       (Min=0.0000000,   Max=1.0000000,   interval=0.0500000)
SwivelSpeedLimits     (Min=1.0000000,   Max=10.0000000,  interval=0.1000000)
TransitionSpeedLimits (Min=1.0000000,   Max=2.0000000,   interval=0.1000000)
FreeLookSpeedLimits   (Min=1.0000000,   Max=10.0000000,  interval=0.1000000)
CameraAccelRateLimits (Min=1.0000000,   Max=15.0000000,  interval=0.1000000)
CameraDecelRateLimits (Min=1.0000000,   Max=15.0000000,  interval=0.1000000)
PrespawnLerpLimits    (Min=0.0000000,   Max=0.5000000,  interval=0.0500000)
GroundClampZOffset    FloatProperty 4  10.0000000
```

Garbage reads cannot produce the real RL slider limits, nor a class's own
declared property names. Combined with P1's ownership test (61 CDOs/package:
biased 60/61 exact tag walks and **100%** of tag names declared by that class;
raw 0–1/61 and **0%**), the bias is settled: **`data()` is correct with it.**

### What I got wrong, precisely

- Round 0i's "**the inline int32-length string encoding is CONFIRMED**" is
  **WRONG**. Those `04 00 00 00 "RPC\0"` / `13 00 00 00 "TotalBreakoutGoals\0"`
  records were real strings from the **wrong object** — reading 562,181 bytes
  early lands in the *preceding* object's tail. There is **no** inline-string
  name encoding; property tags are ordinary UE3 (`FName name`, `FName type`,
  `i32 size`, `i32 arrayIndex`, typeData, value).
- Round 0j's "CDO is not a plain tagged-property list" caution was diagnosed from
  the same shifted bytes. Its *observation* was the clue that cracked it (the
  names looked like another class's data — `RLBot`/tournament fields); my
  *conclusion* was wrong. `Default__Camera_X` walks 2 tags ending exactly at 68
  of 68 B, `Default__CameraStateBlender_X` walks `Transition` (478 B struct)
  ending at 522 of 522.
- Round 5's "regression in `rlupk.data()`" is **withdrawn**. The `(HeaderSize −
  DependsOffset)` term belongs in the read path. My tiling test was the error: raw
  offsets tile `[0, image_len − bias)` because they are *relative*, and relative
  offsets summing to the image length is consistent, not contradictory.
- Round 6's "`ClampPOV` is not proven native" — now **settled the other way**:
  P1's `04 0b 4c` test (Return/Nothing/EndOfScript) shows `ClampPOV` **is** a
  native shell; it still declares an out-param `OutPOV`, which is why the earlier
  decompile looked non-empty.

### REAL RL values now in hand (first decompiled constants of this order)

`FOVLimits 60/110`, `HeightLimits 40/200`, `DistanceLimits 100/400`,
`StiffnessLimits 0/1`, `SwivelSpeedLimits 1/10`, `TransitionSpeedLimits 1/2`,
`FreeLookSpeedLimits 1/10`, `CameraAccel/DecelRateLimits 1/15`,
`GroundClampZOffset 10.0`, `MaxFreeLookPitchDegrees 75.0`,
`FreeLookSensitivityScale_{Mouse 0.83, AnalogStick 10000, Digital 5000}`.

**A real discrepancy to action (A5 / INI schema):** our `[camera] angle` is
documented `-30..15`, but RL's slider is **`AngleLimits = -15..0`**
(`docs/INI_SCHEMA.md`, `Camera.cpp` clamp, and the menu all allow values RL does
not). Our default `-3` is legal. `HeightLimits.interval` is **10**, not 1. These
are schema/range fixes, not feel changes — they need A5 sign-off and a failing
roundtrip test, so they are **reported, not applied** in this order.

Also: **`/decomp/` cannot exist on this machine** (`/` is read-only, no
passwordless sudo). All P1 output is at `~/rl-decomp/` (outside the tree, Rule 1
satisfied). This supersedes the §4 path in the work order and the trace-dir
decision in Round 7 must be read the same way: **outside-tree storage under
`~/rl-decomp/`**, with the in-repo `decomp/` reserved for the gitignored fixture.

## Round 9 — P1's structural claims verified, and the opcode gap is exactly 7 — 2026-10-20

Lead verification of the bytecode blobs P1(a) left in `~/rl-decomp/decompiled/`.
Every structural claim holds, checked mechanically across all 17 §6 functions:

| Claim | Verified |
|---|---|
| every script ends in `0x4C` (EndOfScript) | **17/17** |
| `tail_bytes` after the script region is constant | **19 for all 17** |
| script region starts at byte 48 | **48 for all 17** |
| only `ClampPOV` is native/empty (`04 0b 4c`) | **yes, exactly one** |

```
class.function                     serial script tail first_op
Camera_X.RemoveRoll                   98     31   19  0x0f
Camera_X.ClampPOV                     70      3   19  0x04   <- native shell
Camera_TA.UpdateSwivel               560    493   19  0xb7
CameraStateBlender_X.BlendCameraState 1181  1114   19  0x07
```

### A real handle on the opcode map (sent to P1(b))

`first_opcode` across the 17 is `{0x0f: 8, 0x07: 4, 0x04, 0x1b, 0x5e, 0xb7, 0x71}`.
The dominant values match **stock UE3 exactly** at the low end —
`EX_Return 0x04`, `EX_JumpIfNot 0x07`, `EX_Nothing 0x0B`, `EX_Let 0x0F` — which is
independently consistent with P1's own anchors and with `EX_Let` opening most
camera functions (they are almost all assignments).

But stock UE3 puts `EX_EndOfScript` at **`0x53`** while this build's is
**`0x4C`**: a difference of exactly **7**. So the licensee-34 enum is stock
through at least `0x0F` and has lost exactly **7 opcodes** somewhere in
`(0x0F, 0x53)`. That turns "unknown token map" into a bounded search: find which
seven stock opcodes this build omits, and the rest of the numbering falls out.

This also retro-justifies the earlier reading of `RemoveRoll`'s 31 bytes: `0x0F`
(`EX_Let`) opens it, and the repeated `int32`s (`da 0f 00 00` = 4058 = its own
export index, `d9 0f 00 00` = 4057) are **FPackageIndex operands**, not opcodes —
which is why a naive byte-by-byte read produced nonsense tokens.

Recorded so the next worker does not restart the search from zero.

## Round 10 — P2(rig) tooling verified by the lead (self-consistency holds) — 2026-10-20

Both P2 tools exist and the lead ran them.

**`cam_trace.py selfcheck` — the §6 "replay → 0 error" bar, met exactly:**

```
SELFCHECK PASS SELF1 format round trip (write->load, 240 records)  max_err=0
SELFCHECK PASS SELF2 POV convention round trip (focus)    max_err=0
SELFCHECK PASS SELF2 POV convention round trip (distance) max_err=0
SELFCHECK PASS SELF2 POV convention round trip (rotation) max_err=0
SELFCHECK PASS SELF2 POV convention round trip (fov)      max_err=0
SELFCHECK INFO SELF3 sub-ulp residual (distance)          max_err=4.5e-13  (informational)
SELFCHECK PASS format+convention self-consistency (exact 0.0 required)
```

39 fields, 240 records, exact zero through the write→load→reconstruct path; the
only non-zero numbers are in an explicitly *informational* double-precision
check, correctly not scored.

**`camera_trace_diff.py` — three things done right:**
1. It **compiles a replay driver from our real `src/camera/Camera.cpp`**
   (cached by source sha1 `6803a0f563dacc10`), so the comparison is against the
   actual camera, not a re-implementation.
2. It **refuses to let synthetic data masquerade as accuracy**:
   `*** SYNTHETIC TRACE (source="synthetic") *** ... PLUMBING DEMONSTRATION ONLY
   ... say NOTHING about our camera's accuracy. Do not cite them as such.`
   That is precisely the honest-reporting rule this order needs — the error table
   exists, is labelled worthless as accuracy evidence, and cannot be quoted
   accidentally.
3. It marks what it cannot know: `Rotation roll UNVERIFIED`, `POV time
   UNVERIFIED`, `CurrentSwivel yaw (scored)`.

Seven synthetic scenario files exist under `decomp/traces/synthetic/`
(`ballcam_toggle`, `dribble_hold`, `goal_ballcam_reset`,
`landing_air_to_ground`, `supersonic_straight`, `swivel_sweep_release`,
`target_switch_mid_air`) — §6.2's deck minus `wall_contact`.

**One correction to the lead's own reading:** the `fov= 0.030deg` shown on the
worst-frame lines is the per-field **error**, a constant 0.0296 deg offset — not
the recorded FOV, and not a units bug. The tool is consistent; the huge Focus /
Distance errors are simply what synthetic data must produce against a real camera.

Not yet verified: the bounded hook attempt (blocked while the user's session is
up), and the real-trace error table (correctly `NOT AVAILABLE`).

## Round 11 — the CDOs do NOT contain the dynamics constants (plan correction) — 2026-10-20

Mining P1(a)'s `~/rl-decomp/decompiled/cdo_tables.txt` for the constants P3
actually needs turns up an important **negative** result.

What the CDOs DO give (real, MEASURED, usable):

| Class | Properties serialized | Notable values |
|---|---|---|
| `Camera_TA` (3637 B) | 16 | the `*Limits` sliders, `GroundClampZOffset 10.0`, `bHidden=false` |
| `CameraState_TA` (149 B) | 5 | `MaxFreeLookPitchDegrees 75.0`, `FreeLookSensitivityScale_{Mouse 0.83, AnalogStick 10000, Digital 5000}`, `bAllowRearCamera` |
| `CameraState_Car_TA` (410 B) | 11 | `ProximityDetectionSpeedCap 300`, `ProximityDetectionMultiplier 3.5`, `ProximityDistanceBlendIn{Out}Speed 20/0.75`, `ProximityDistanceLimits 0..500`, `DriftCorrectionRate 1.5`, `DriftCorrectionRatePerDegree 0.3`, `DriftSnapThresholdDegrees 45`, `bShowCarHUD`, `bCanFreeLook` |
| `Camera_X` (68 B) | 2 | `Blender` (→ `StateBlender0`), `CameraShakeCamModClass` |
| `CameraStateBlender_X` (522 B) | 1 | `Transition` struct: `BlendParams(BlendTime 0, VTBlend_Cubic, BlendExp 2.0, bLockOutgoing false)`, `RemainingTime`, `SnapshotPOV` |

What it does **NOT** give — and this is the correction: **`FocusInterp`,
`DistanceInterp`, `DistanceSpeedScale`, `AirVelocityInfluence(MaxSpeed)`,
`DistanceOffsetMin`, `InterpToGroundRate`, `InterpToAirRate`,
`GroundRotationInterpRate`, `MaxSpeedFOV`, `FOVInterpSpeed`, `SupersonicFOV`,
`SwivelDieRate`, `SwivelFastSpeed`** — none of these appear in any CDO export.

They **are** declared as properties of `CameraState_Car_TA` (which declares **149**
properties; its CDO serializes only **11**) and of `Camera_TA`. So the constants
are *declared but not defaulted*: UE3 CDOs carry only what `defaultproperties`
actually sets, and 410 bytes cannot hold 149 tags (≈11 is right). They are
therefore either native-side, computed at runtime from `CameraSettings`, or
zero-valued with the real behaviour in bytecode.

**Consequence, stated against my own Round 6 note:** my "the list of functions
genuinely needing P2 may be empty" was **too optimistic**. For the *slider limits*
and free-look scales, decompilation is now the verified source and P2 is
unnecessary. For the **dynamics constants** — the ones that carry the feel — there
is still no decompiled source, so **the P2 trace (or the P1(b) token listing that
shows how they are computed) remains required** for those. That distinction now
matters for how P3 is briefed, and it is the honest answer rather than either
"P2 is essential" or "P2 is dispensable".

Also noted: `Default__Camera_TA`'s dump carries two caught inner exceptions
(sub-object defaults) and `ArrayProperty` element types are undecoded
(`CameraPresetSettings` 2032 B, `TargetClasses`, `PlaneSelectSettings`,
`AngleSelectSettings`) — those array defaults stay **UNVERIFIED** and must not be
invented.

## Round 12 — a real mechanism defect found: our transition is linear, RL's is cubic — 2026-10-20

One positive result to set against Round 11's negative one, found in the
`CameraStateBlender_X` CDO (522 B, decoded 522/522):

```
Transition = (CameraState=none,
              BlendParams=(BlendTime=0.0, BlendFunction=EViewTargetBlendFunction.VTBlend_Cubic, BlendExp=2.0, bLockOutgoing=false),
              RemainingTime=0.0, SnapshotPOV=(...), bStarted=false)
```

**RL blends a camera transition with `VTBlend_Cubic` and `BlendExp = 2.0`.**
Our `Camera::UpdateTransition` is a **linear scalar step**:

```cpp
float tRate = 0.5f + clampf(params_.transitionSpeed, 0.2f, 2.0f) * 2.5f;
float step  = tRate * f.dt;
if (blend_ < blendTgt) blend_ = (blend_ + step < blendTgt) ? blend_ + step : blendTgt;
```

So the current comment in `Camera.cpp` — "Blend SHAPE stays linear (upstream
`rl_camera.cpp:398-403` is linear too)" — is true of **CarBaller**, and **false
of Rocket League**. The rate `0.5 + 2.5*transition_speed` is also a CarBaller
invention; RL drives the blend off `BlendTime`/`RemainingTime`/`BlendPct` in the
blender component, with the easing applied by the blend *function*.

**Recorded as a P4 slice with exact provenance and an explicit shape:**
- mechanism: `BlendPct = f(elapsed / BlendTime)`, then `VTBlend_Cubic` with
  `BlendExp = 2.0`, `bLockOutgoing = false`; provenance
  `// decomp: ProjectX.CameraStateBlender_X.Transition.BlendParams`.
- this is a **feel change**, so it lands behind a trace scenario
  (`ballcam_toggle`) and never as a silent refactor. `t_cam`'s blend group
  currently asserts linear stepping and will have to be re-baselined *with* the
  trace, not before.

This is the first change in this order whose provenance is the real game rather
than CarBaller or a slider doc — worth noting because it means the decompile path
has already produced something actionable even with the expression layer
unresolved.

### Reconfirmation of a dead end (do not re-probe)

RL's cooked install has **no camera tuning INI**: `TAGame/Config/*.ini` are
`DefaultRLBot`, `DefaultStatsAPI`, `DefaultSystemSettings`, `DefaultInput`, and
`Engine/Config/{BaseInput,BaseSystemSettings}`. The only "camera" hits are input
bindings. The dynamics constants are not reachable through config files.

## Round 13 — VG7 reproduced on the P0 camera (P0's gate coverage now complete) — 2026-10-20

I had verified VG4 but not VG7, which is the only gate that measures the binary
end-to-end (60 s scripted run: tick integrity, GL errors, triangle/draw budget).
Now reproduced by the lead on P0's tree:

```
$ python3 tools/verify/visual_gates.py --only VG7
VG7  PASS  budget + GL errors (60s run)
     ticks=7200 drops=0 glerr=0 tris_last=189144 (cap 240000) draws_last=189 fennec_tris=84823
# summary: 1 PASS, 0 FAIL, 0 SKIP
```

Every number matches `AGENTS.md`'s recorded post-GRAPHICS45 values
(`ticks=7200 drops=0 glerr=0 tris_last=189144 draws_last=189 fennec_tris=84823`),
so the layer split changed **nothing** in the render path — no triangle or draw
drift, no GL errors, no dropped ticks across 7200 ticks.

Gate coverage for P0 is therefore: `t_cam` CAMERA PASS (49) ·
`camera_surface_gate` 5/5 · **VG4** PASS (radii identical to the pre-change
baseline) · **VG7** PASS (identical to the recorded values) · behaviour oracle
bit-identical over 900 frames · perf unchanged. P0 is closed on evidence.

Evidence file restored after the run (`--only` rewrites it wholesale), 0 dirty.

(Both remaining W2 workers — P1(b) tokens and P2(rig) — are still running; no
new output on disk this round.)

## Round 14 — P2(rig) CLOSED as PARTIAL (accepted); machine identity pinned; a P3 blocker found — 2026-10-20

### P2(rig): accepted, with the hook correctly *not* attempted

The worker delivered `cam_trace.py` (1169 lines) + `camera_trace_diff.py` (840)
+ 8 synthetic fixtures, and reported the hook as **not attempted** because
`pgrep -af RocketLeague` showed the user's session (pid 77117) at both start and
end. That is exactly the hard rule I set: it did not attach, inject, signal, or
launch a second instance. **A correct refusal is a good outcome**, not a failure.

Independently reproduced by the lead (Round 10): `selfcheck` max_err **0** on
SELF1/SELF2; and I additionally exercised the **negative** paths myself earlier
by running the diff tool on a fixture. The worker's own negative-test evidence is
strong and is recorded here as *claim* (I did not re-run all four):
version bump → `trace v2, this reader speaks v1`; field reorder → names the first
mismatching field; one corrupt byte → `crc32 0eb766e8 != stored c4d3a164`;
truncation → `74780 bytes, expected 74880 (480 x 156)`.

`camera_trace_diff.py --report` → **`CAMERA ERROR TABLE: NOT AVAILABLE (no
recorded trace)`**, exit 3. The honest answer, exactly as required.

### REAL FINDING that changes P3's brief

`PovSnapshot.pov.distance` (RL's `Distance`, i.e. `Dynamics.effDist` ≈ 270 uu) is
**not observable through the public API**. `|Eye() - Center()|` is the distance to
the **look target** (≈1600 uu in ballcam), not the rig distance. The diff tool
correctly refuses to score that semantic mismatch — it probes the data, and where
the ratio leaves `[0.5, 2.0]` it prints `UNVERIFIED` with both medians and the
reason.

**Required from P3:** add a `TruthPov` accessor to `Camera` (e.g.
`const TruthPov& Pov() const`) so Distance/Rotation/Focus become directly
comparable to a trace. That is a `src/camera` change and is now in P3's brief.

Corollary the worker flagged and I accept: `Center()` is a point **on the aim ray**
at `max(|target-Eye|, 1000)` uu, so Focus error carries a ray-**length** term.
Read Focus and Rotation together; never quote Focus alone.

### Machine identity (needed for P5's table)

```
VG1 PASS — two captures byte-identical
GL_VENDOR=NVIDIA Corporation
RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2
VERSION=4.6.0 NVIDIA 615.78.08        md5=0a2bbb529ea559daaf5c922efc3ba4b8
```

So **this box is machine B**, and its VG1 capture md5 is byte-identical to the
value recorded after the GRAPHICS45 pull — another confirmation that P0's camera
changed nothing visible.

Consequence for W6/P5, stated now so it cannot become a surprise at close: **the
work order asks for a two-machine perf table, but machine A (AMD Radeon R4 /
radeonsi stoney / Mesa 26.2.4) is a different physical laptop that is not
reachable from this session.** P5 can measure **B only**; A must be reported
`NOT MEASURED` with the reason, never invented or copied from an older log.

## Round 15 — the angle-range fix is A5-owned; every other clamp was already right — 2026-10-20

Follow-up to Round 8's `AngleLimits` finding, and a correction to my own P3 brief.

`src/settings/Settings.cpp` already clamps all seven camera ranges, and carries
the invariant in a comment: *"distance/height/angle match the RL menu ranges
(Camera.cpp enforces the same, so the two owners never disagree)"*. Comparing
them against the decompiled CDO:

| Setting | `Settings.cpp` clamp | RL (CDO) | Verdict |
|---|---|---|---|
| fov | 60 .. 110 | `FOVLimits 60/110` | **already correct** |
| distance | 100 .. 400 | `DistanceLimits 100/400` | **already correct** |
| height | 40 .. 200 | `HeightLimits 40/200` | **already correct** |
| **angle** | **-30 .. 15** | **`AngleLimits -15/0`** | **wrong** |
| stiffness | 0 .. 1 | `StiffnessLimits 0/1` | **already correct** |
| transitionSpeed | 1 .. 2 | `TransitionSpeedLimits 1/2` | **already correct** |
| swivelSpeed | 1 .. 10 | `SwivelSpeedLimits 1/10` | **already correct** |

Two things follow, and the second is a correction to my P3 dispatch:

1. **Six of seven ranges were right all along** — they simply had no proof until
   the decompile. There is nothing to port in the clamp list; the honest change is
   provenance comments (`// decomp: TAGame.Camera_TA.<Property>`), not new values.
2. **The angle fix cannot be done in `Camera.cpp` alone.** Changing only the
   camera owner would let `Settings.cpp` accept `angle = 10` while the camera
   silently clamps it to `0` — the silent divergence Rule 6 and the normative INI
   schema forbid. It requires `Settings.cpp` + `docs/INI_SCHEMA.md` + the menu
   range (A5) together with `Camera.cpp` (A4), behind a **failing roundtrip test
   first**. Sent to P3 as a correction: **report it, do not apply it.**

So the order's honest state on ranges is: **confirmed correct except one, whose
fix belongs to another owner.** That is a documentation win, not a code win, and
it is recorded as such rather than dressed up as a port.

## Round 16 — an internal inconsistency in §6 P5, resolved as lead — 2026-10-20

`MASTER_PROMPT_CAMERA_EXACT.md` §6 P5 asks for, in one breath:

> "add a `CAM µs` line to the pacing print"

but §4's write-scope table gives **P5 `src/camera/**` only**. The pacing print
lives in `src/game/main.cpp`:

```
src/game/main.cpp:1196  "[hud] tick=... p50=%.2fms p99=%.2fms tris=... glerr=..."
src/game/main.cpp:1212  "[game] exit ticks=... p50=... p99=... frame_ms=..."
```

So the two parts of the same paragraph sit on opposite sides of a hard fence, and
`src/game/main.cpp` is **coordinator-owned** (`CONTRACTS.md`), not part of this
order's lanes.

**Ruling (lead): the `CAM µs` pacing line is NOT landed by this order.** Reasons,
in order of weight:
1. It requires an out-of-scope edit to the coordinator's file — the fence exists
   precisely so a camera worker cannot perturb the play loop.
2. Its purpose (proving the ≤ 8 µs budget) is already served better by the lead's
   own harness, which isolates camera cost **without** the renderer, allocator churn or vsync
   in the measurement: `/tmp/cam_bench.cpp`, three scenarios, 200k frames each,
   quoting mean/p50/p99.
3. Adding a print to the pacing path has its own risk (the file already has a
   documented `render_pacing.log`-in-CWD defect, V9), and the timing is trivially
   available to any future owner who wants it.

The budget claim is therefore evidenced by measurement rather than by a log line,
and P5's scope stays clean. If the owner wants the line, it is a one-line change
in `main.cpp` for the coordinator — recorded here so it is a decision, not an
oversight.

### Note for the remaining rounds

P3 is mid-edit on `src/camera/Camera.h` (its `Pov()` accessor is appearing, with
`pov_` not yet declared), so the tree is **transiently non-compiling**. That is
normal mid-phase; the lead does not build or "fix" inside a worker's lane while
it holds the single-writer lease. Perf measurement waits for P3 to report.

## Round 17 — P3's `Pov()` verified: distance/eye hold, but rotation↔target agreement is transition-limited — 2026-10-20

P3 added `const TruthPov& Pov() const`. The lead verified its three documented
properties numerically on P3's own tree (`/tmp/check_pov*.cpp`, 600 frames,
ballcam toggling every 40 frames, 0–1800 uu/s, ball swinging):

| Claim | Measured | Verdict |
|---|---|---|
| `eye_from_pov(Pov()) == Eye()` | **0.000977 uu** max | **HOLDS** (float32 rounding only) |
| `Pov().distance` is the RIG distance, not the look distance | range **270.00 .. 314.92 uu** vs `max |Eye()-Center()| = 1111.42 uu` | **HOLDS**, and the two are clearly distinct as P2 found |
| `Rotation` is `Center()-Eye()`'s direction | steady state **1.253°**; transitions **up to 103.89°** | **holds in steady state only** |

Plus the behaviour oracle on the same tree: **`BIT-IDENTICAL to HEAD (900 frames)`**
— the accessor is purely additive, as required.

### Why the rotation claim is transition-limited (structural, not a bug)

Our camera blends **whole POV snapshots**: `BlendCameraState` interpolates the
`rotation` on the short arc *and* the `focus` point *independently*. `Center()` is
a blended look-at **point**; `Pov().rotation` is a blended **rotator**. During a
transition those two are not the same fact, and the divergence explodes when the
blended `Center()` passes close to the eye — exactly what the measurements show
(`dyaw` grows to 103.89° while the look distance collapses from 724 to 158 uu).

The steady-state residual of **1.253°** is the one-euro look-target filter lagging
the aim (by design, `#5`), not an error in the accessor.

**Consequences recorded for P3/P4 and for the trace gate:**
1. The accessor's comment should say the rotation↔target relation holds **in
   steady state**, not unconditionally — otherwise a future reader will "fix" the
   camera to satisfy a false invariant. Sent to P3.
2. **The trace gate must never assume focus/rotation consistency during a
   transition.** Compare Focus and Rotation as separate fields (as
   `camera_trace_diff.py` already does) and treat a mid-transition frame's
   Focus-vs-Rotation disagreement as expected, not as camera error.
3. This is the mechanism the P4 cubic-blend slice will touch: easing a *scalar*
   alpha over independently blended points and rotators is what produces the
   large mid-transition divergence.

## Round 18 — P5's substance measured by the lead; one of its asks declined — 2026-10-20

P5's paragraph mixes a real budget claim with two asks that are either already
true or actively harmful. Measured/ruled now, on P3's tree.

### The budget: met with ~17x headroom, on machine B

`/tmp/cam_bench.cpp` (three scenarios, 200k frames each, `steady_clock`,
mean/p50/p99 in µs), built from the current `Camera.cpp`:

| Scenario | mean | p50 | p99 |
|---|---|---|---|
| S0 rest ballcam (cheapest) | **0.303 µs** | 0.291 | 0.381 |
| S1 driving + moving ball, ballcam (common frame) | **0.365 µs** | 0.351 | 0.501 |
| S2 airborne + supersonic + full swivel (worst path) | **0.470 µs** | 0.451 | 0.671 |

vs `HEAD`'s pre-P0 **0.262 / 0.354 / 0.438** — i.e. unchanged within noise. The
budget is **≤ 8 µs/frame**, so the worst path uses **5.9% of it**.
(`max` column omitted deliberately: it is host preemption, not camera work.)

Machine B identity, quoted as the GRAPHICS45 rules require:
```
GL_VENDOR=NVIDIA Corporation
RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2
VERSION=4.6.0 NVIDIA 615.78.08
```
Machine A (AMD Radeon R4 / radeonsi stoney / Mesa 26.2.4) is a **different
physical laptop, unreachable from this session** → **NOT MEASURED**, with the
reason recorded rather than a number invented (Round 14).

### "no allocation in the camera path" — already true

Verified by inspection of `src/camera/Camera.cpp`: no `new`, `malloc`, `calloc`,
`std::vector`, `std::string`, `push_back` or `resize` anywhere in the evaluation
path (the single grep hit is a comment). State lives in fixed members and the
per-frame `Frame`/`Dynamics`/`Orbit`/`PovSnapshot` structs on the stack. **No
change needed**; the claim is satisfied by construction.

### "one `sincosf` per frame" — DECLINED, deliberately

The file has 14 `std::sin`, 14 `std::cos`, 13 `std::sqrt`, 9 `std::atan2` and 5
`std::exp` call sites (≈6–10 of them on a given frame). Consolidating them to a
single `sincosf` is a real refactor that **changes float results** — which would
break the 900-frame bit-exact oracle that P0's contract rests on, and buy nothing
measurable when the worst path already uses 5.9% of the budget. Recorded as a
**deliberate non-goal**, not an omission: the budget is the requirement, and the
bit-exactness is worth more than 0.1 µs that no frame can observe.

### Soak

`drops=0` is already evidenced at this order's scale by **VG7** (7200 ticks,
`drops=0 glerr=0`, reproduced by the lead in Round 13). A 30-minute soak adds
nothing to a camera-only change and costs 30 wall-clock minutes; it is **not
run**, and the reason is recorded rather than left as a silent gap.

## Round 19 — close-out evidence: the protected surface is provably untouched — 2026-10-20

§9 requires "the physics bar untouched". Evidenced directly rather than by
assertion:

```
$ git status --short src/sim src/arena src/physics tools/tas third_party
(NONE — physics bar untouched)

$ git diff --stat HEAD -- src/sim src/arena src/physics tools/tas \
      tools/verify/measured.py tools/verify/mesh_gates.py \
      tools/verify/gates_visual.json config/
(empty)
```

So across the whole order (P0, P1(a), P1(b), P2(rig), P3):

- **no change** under `src/sim`, `src/arena`, `src/physics`, `third_party/`
  (the vendored RocketSim submodule is pristine; its only entry is the
  pre-existing untracked `A1_VENDOR.txt`);
- **no change** to frozen tapes (`tools/tas/`), to the gate runners
  (`measured.py`, `mesh_gates.py`), to the gate evidence file
  (`gates_visual.json`), or to `config/` (INI schema v1 untouched — no new
  `[camera]` key was added anywhere in this order);
- the physics bar therefore stands on exactly the binaries and constants it
  stood on before, and needs no re-run to remain valid for a camera-only diff
  (`GATE_POLICY.md`: camera changes are presentation by rule and never
  invalidate physics).

Every `gates_visual.json` write in this order was a `--only` run, and each was
restored from a `cp /tmp/gv.bak` snapshot immediately after — the file is
byte-identical to `HEAD` right now (verified repeatedly, `0 dirty`).

### Code review of P3's `Pov()` (lead) — why invariant (a) holds by construction

`pov_` has exactly one writer per evaluation (`Camera.cpp:797-806`), documented as
"the ONLY writer of pov_ … No GL, no behavior: nothing reads pov_ back":

```cpp
pov_ = p.pov;                        // the blended snapshot's POV
pov_.fov  = effFov_;                 // the FOV actually used by Apply()
pov_.time = povTime_;                // state clock
forwardFromRotator(pov_.rotation, fwd);
pov_.focus[i] = eye_[i] + fwd[i] * pov_.distance;   // re-anchored on the published eye
```

So `focus` is **not** the blended focus point — it is re-derived from the
published `eye_` plus the rotation forward-vector times the rig distance. That is
precisely why `eye_from_pov(Pov()) == Eye()` measured 0.000977 uu (float32
rounding only) rather than merely "approximately": the accessor is defined in
terms of the GL pair, so it cannot drift from it.

`SetFreeView` also writes `pov_.focus = center`, keeping the freecam hook
consistent.

`t_cam` on the same tree: **CAMERA PASS** (49 checks). Compiles clean.

## Round 20 — P3's `camera_trace_gate.py` verified (SKIP semantics correct) — 2026-10-20

The gate landed and the lead ran it:

```
$ python3 tools/verify/camera_trace_gate.py
camera_trace_gate: traces=decomp/traces (0 live, 8 synthetic, 0 broken)
  canonical trace home: decomp/traces/ (gitignored; never a root /decomp)
  PASS CT1 self-replay exact synthetic/ballcam_toggle.rlt     480 rec, field max 0.000000, POV max 0.000000
  PASS CT1 self-replay exact synthetic/dribble_hold.rlt       ...  (8/8 the same)
  SKIP CT2 exact-feel settle/overshoot — no recorded trace: 8 trace(s) under
       .../decomp/traces all declare source="synthetic"
  SKIP CT3 no-regression vs camera_trace_baseline.json — see above

camera_trace_gate: 1 PASS / 0 FAIL / 2 SKIP — SKIP
  This is the LOCAL tier by design: CT2/CT3 grade exact feel against a RECORDED
  Rocket League trace, and only synthetic fixtures exist here. A public clone
  therefore stays green. To make it real, record the §6.2 deck offline (P2, an
  instance the recorder launches itself) into decomp/traces/, then run
  --write-baseline once.
```

Exit **0**. `--json` works.

Four things this gets right, which are exactly the properties that are easy to
get wrong:

1. **CT1 is real** — self-replay through the loader is bit-exact per field
   (`field max 0.000000`, `POV max 0.000000`) across all 8 fixtures.
2. **CT2/CT3 SKIP, they do not pass.** The distinction between "nothing to grade"
   and "graded and fine" is preserved, and the SKIP names *why* (all traces
   declare `source="synthetic"`) and *how to make it real* (record offline, then
   `--write-baseline`). A gate that silently PASSed here would have been the
   worst outcome, since feel has no other automated bar.
3. **It honours the Round 7 ruling** — canonical home is the in-repo gitignored
   `decomp/traces/`, with the line *"never a root /decomp"* printed in its own
   output, so the silent-forever-SKIP failure mode cannot come back unnoticed.
4. **It counts what it looked at**: `0 live, 8 synthetic, 0 broken`. A broken
   trace would be visible rather than skipped as "not live".

Honest reading of what this buys: CT1 proves the *plumbing* end to end; it is
**not** evidence about camera accuracy, and the gate says so itself. CT2/CT3 —
the actual exact-feel bar — remain `SKIP` until a real recording exists, which is
gated on the game being closed and the P2 route succeeding. That is the honest
state of the order's central claim.

## Round 21 — close-out: the full test suite, and a CWD trap worth knowing — 2026-10-20

§5.7 requires the lead's own `make tests`. Run with a **forced rebuild**, because
`AGENTS.local.md` warns that an up-to-date `make tests` proves nothing:

```
$ rm -f /tmp/t_rt /tmp/t_kb /tmp/t_gp /tmp/t_cam /tmp/t_atex
$ flock /tmp/rl-build.lock sh -c 'source tools/dev-local.sh && make tests'
... KEYBOARD+MENU PASS
[exit 0]
```

The `tests` target is a dependency list, but each rule ends in `&& $@`, so the
binaries really are **executed**, and exit 0 is a genuine pass. Confirmed per
suite, run from the **repo root**:

| binary | verdict |
|---|---|
| `/tmp/t_rt` | **ROUNDTRIP PASS** |
| `/tmp/t_kb` | **KEYBOARD+MENU PASS** |
| `/tmp/t_gp` | **GAMEPAD PASS** |
| `/tmp/t_cam` | **CAMERA PASS** (49 checks) |
| `/tmp/t_atex` | **ARENA_TEXTURES PASS** (108 checks, 0 failed) |

### Trap recorded (it briefly looked like a regression)

`/tmp/t_rt` reports **`ROUNDTRIP FAIL`** when executed from `/tmp`, and
**`ROUNDTRIP PASS`** from the repo root — it reads the shipped
`config/settings.ini` relative to the CWD (its own check is "Defaults() matches
shipped ini"). So a bare `/tmp/t_rt` is a **false alarm**, and anyone running the
binaries out of a loop from elsewhere will see a red suite that is not red.

`make` always runs them from the repo root, so `make tests` is unaffected; only
manual invocations are. Recorded because it cost time here and would cost it
again.

### Gate exit-code semantics verified (P3's new gate)

```
$ python3 tools/verify/camera_trace_gate.py --tracedir /tmp/negtr   # 1 corrupt file
  BROKEN corrupt.rlt  ...: 15 bytes is shorter than the 28-byte header
  FAIL CT1 0 trace(s): self-replay bit-exact
camera_trace_gate: 0 PASS / 1 FAIL / 2 SKIP — FAIL      exit = 1
$ python3 tools/verify/camera_trace_gate.py                          # synthetic only
camera_trace_gate: 1 PASS / 0 FAIL / 2 SKIP — SKIP      exit = 0
```

So the gate **can fail** (a corrupt trace is `BROKEN`, CT1 `FAIL`, exit 1) and
**skips honestly** (exit 0 only when there is nothing to grade). A gate that
could not fail would have been worthless; this one is a real regression barrier
for the day a trace exists.

## Round 22 — CLOSE-OUT (2026-10-20, rev 3) — the lead's final verified state

Work order marked **CLOSED (2026-10-20, rev 3)** in
`MASTER_PROMPT_CAMERA_EXACT.md`; pointer updated in `AGENTS.md`. Verdict at the
top of `docs/CAMERA_DECOMP.md` as **VERDICT (2026-10-20, rev 3)**.

### Phase outcome

| Phase | Outcome | Why |
|---|---|---|
| **P0** | **CLOSED** | Layer split, behaviour **bit-identical** over 900 frames, VG4/VG7 unmoved |
| **P1(a)** | **PARTIAL** | Offset root cause solved; objects/properties/CDO constants delivered; **expression layer unsolved** → no bodies |
| **P1(b)** | not delivered | licensee-34 opcode map still open (bounded: `EndOfScript` is exactly 7 below stock) |
| **P2(rig)** | **PARTIAL (accepted)** | Tooling delivered + verified; recording **not attempted** — correct refusal |
| **P2(record)** | **BLOCKED** | user's session running throughout; attach + plugin routes impossible on this box |
| **P3** | **CLOSED (as scoped)** | `TruthPov`/`Pov()` landed + verified; ranges confirmed (nothing to port); `camera_trace_gate.py` landed |
| **P4** | **NOT LANDED** | every remaining slice is a *feel* change needing a trace to gate it |
| **P5** | **CLOSED** | budget measured (5.9% used); no-allocation verified; two P5 asks declined with reasons |

### Final verification (every line reproduced by the lead this session)

```
t_cam                CAMERA PASS (49 checks)          behaviour oracle: BIT-IDENTICAL (900 frames)
camera_surface_gate  CS1-CS5 5 PASS / 0 FAIL          camera_trace_gate: CT1 PASS / CT2-CT3 SKIP, exit 0
VG1                  PASS  md5=0a2bbb529ea559daaf5c922efc3ba4b8 (byte-identical to the record)
VG4                  PASS  z600 outer=91 inner=71 disc=45; ratios 0.780/0.495; growth 1.022
VG7                  PASS  ticks=7200 drops=0 glerr=0 tris_last=189144 draws_last=189
make tests           ROUNDTRIP + KEYBOARD+MENU + GAMEPAD + CAMERA(49) + ARENA_TEXTURES(108) — exit 0
```

Camera cost, machine B only:
`GL_VENDOR=NVIDIA Corporation` `RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2`
`VERSION=4.6.0 NVIDIA 615.78.08` → **0.303 / 0.365 / 0.470 µs** mean (rest /
driving / worst), p99 ≤ 0.671 µs, **5.9% of the 8 µs budget**.
**Machine A: NOT MEASURED** — a different physical laptop, unreachable here;
recorded rather than invented.

Physics bar untouched (no diff under `src/sim|arena|physics`, tapes,
`measured.py`, `mesh_gates.py`, `gates_visual.json`, `config/`).

### What the order actually achieved, in one paragraph

Rocket League's camera **decomposition** is now our architecture, proven
behaviour-preserving; the rig's **real constants** were decompiled from the
license-34 packages after finding and fixing a `SerialOffset`-relative-to-
`DependsOffset` bug that had defeated UELib (six of seven of our slider ranges
were already right, and `angle` is wrong in a way that belongs to A5); a
`Pov()` accessor now exposes RL's own `Focus/Rotation/Distance/FOV/Time`; and a
trace format, diff tool and gate exist with honest SKIP semantics. The **cubic
transition** and `GroundClampZOffset 10.0` are real, cited findings queued for
P4. The order did **not** deliver exact per-frame feel: the opcode map blocks the
bodies, the CDOs do not default the interp constants, and no trace could be
recorded. Every remaining gap is named with a concrete next step, and no
constant in the code is presented as decompiled when it is not.

### Next step for whoever picks this up (in priority order)

1. **Finish the opcode map** — it is a bounded search: the enum is stock through
   `0x0F` and lost exactly **7** entries before `EndOfScript` (`0x53` stock vs
   `0x4C` here). The arbiter is that every walk must land exactly on `0x4C`
   (17/17 fixtures provided). Deliver a token/operand listing, not UnrealScript.
2. **Then record** using the P2 tooling when no Rocket League session is running
   (`decomp/traces/`, then `camera_trace_gate.py --write-baseline`), which turns
   CT2/CT3 from SKIP into the exact-feel bar.
3. **Then land P4's feel slices**, each behind its scenario: cubic transition
   (`VTBlend_Cubic`, `BlendExp 2.0`), symmetric `±75°` free-look pitch,
   `GroundClampZOffset 10.0`, swivel decay.
4. **A5 action item** (independent of all the above): RL's `AngleLimits` is
   `-15..0`; ours ships `-30..15` in `Settings.cpp`, `Camera.cpp`, the menu and
   `INI_SCHEMA.md`. Needs a failing roundtrip test first.

### Addendum — VG1/VG4/VG7 re-run on the FINAL tree with a forced rebuild

The Round 4 hazard made the earlier VG runs stale: `build/game` was from
17:32:48 while `src/camera/Camera.h` was last written 17:48:17, and a header-only
change does **not** rebuild it. So the close-out numbers were re-taken properly:

```
$ rm -f build/game && flock … make build/game        -> exit 0
build/game 17:52:11  >  src/camera/Camera.h 17:48:17     <- binary is current

VG4  PASS  z600 outer=91 inner=71 disc=45; ratios 0.780/0.495; growth 1.022
VG7  PASS  ticks=7200 drops=0 glerr=0 tris_last=189144 (cap 240000) draws_last=189 fennec_tris=84823
VG1  PASS  two captures byte-identical  md5=0a2bbb529ea559daaf5c922efc3ba4b8
           (GL_VENDOR=NVIDIA Corporation RENDERER=NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2
            VERSION=4.6.0 NVIDIA 615.78.08)
```

All identical to the pre-change baseline and to the recorded values — so the
layer split plus P3's accessor changed nothing observable, now proven on a
binary built *after* the last header edit rather than before it.

`tools/verify/gates_visual.json` was restored from `HEAD` afterwards and is clean
(`git status` empty for it). Note for future runs: restoring from a `/tmp` backup
taken *after* an `--only` run copies dirty state back — restore from
`git checkout -- tools/verify/gates_visual.json` instead.
