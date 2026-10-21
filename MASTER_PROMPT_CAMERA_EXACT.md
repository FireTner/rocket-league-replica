# MASTER_PROMPT_CAMERA_EXACT.md — exact-feel camera, orchestrated handoff

**Status: CLOSED (2026-10-20, rev 3 — orchestrated).** Extends `src/camera/` only.

> **Close-out:** P0 CLOSED (verified). P1(a) PARTIAL — objects, properties and CDO
> constants delivered; the expression layer (licensee-34 opcode map) is UNSOLVED,
> so no compiled bodies. P1(b) tokens not delivered. P2(rig) PARTIAL — tooling
> delivered and verified, live recording NOT ATTEMPTED (correctly: the user's
> session was running throughout, and the attach/plugin routes are impossible on
> this machine). P3 — `TruthPov`/`Pov()` accessor landed and verified, limit
> constants confirmed correct (angle is A5's to fix), `camera_trace_gate.py`
> CT1 PASS / CT2-CT3 SKIP. P4 NOT LANDED (feel changes need a trace). P5 measured
> on machine B only. Verdict: `docs/CAMERA_DECOMP.md` → **VERDICT (2026-10-20,
> rev 3)**; round-by-round evidence: `docs/CAMERA_EXACT_LOG.md`.

**How to run this: open ONE agent and paste §5 (the lead prompt).** The lead then
needs nothing from you: it dispatches the §6 worker prompts to subagents, wave by
wave, verifies every worker claim itself against the repo, writes the docs and
closes the work order — in parallel where the file scopes allow, serially where
they do not. (A single §6 block is still a standalone prompt if you would rather
drive one phase at a time.)

Goal: replace our CarBaller-derived camera mixer with Rocket League's own
**decomposition** (state POV → blender → free-look/swivel offset → clamp/knocks),
in RL's units and space, constants either **decompiled** (P1) or **measured live**
(P2), verified on a replayable trace (P3), at **≤ 8 µs/frame** on the R4 (P5).
"Exact feel" = per-scenario step-response metrics against the P2 baseline, not
vibes.

| Wave | Workers | Why that parallel is safe |
|---|---|---|
| W1 | **P0** ∥ **P1(a)** decoder | `src/camera/**` + `tests/**` vs `tools/decomp/**` + `~/rl-decomp/**` — disjoint |
| W2 | **P1(b)** dumps ∥ **P2** rig | P1 writes only `/decomp/decompiled/**`; P2 writes new trace tooling |
| W3 | **P2** record | needs the game + P1's names; single-threaded by nature |
| W4 | **P3** | needs P1 + P2; sole owner of `src/camera/**` |
| W5 | **P4** slices, one worker per gap | serial after P3 — same files, single-writer lane |
| W6 | **P5** | serial close-out |

---

## §1 Ground truth (do not re-derive; measured 2026-10-20)

| Fact | Value |
|---|---|
| Package identity | `UE3 868 / licensee 34`, EngineVersion `10897`, CookerVersion `136` |
| `TAGame.upk` | GUID `66101E814F23F2BA77A621A269A5613B`, 54429 names / 129850 exports, 78 chunks |
| `ProjectX.upk` | 18878 exports — **the base rig lives here**, not in TAGame |
| Camera symbols | 98 extracted; the gate pins 6 classes / 43 functions / 38 properties |
| Our gate today | `python3 tools/verify/camera_surface_gate.py --regen` → **CS1–CS5 5 PASS / 0 FAIL** |
| Full record | `docs/CAMERA_DECOMP.md` — read before dispatching |

Real rig, the parts that carry the feel:

| RL class (package) | fns | symbols that matter |
|---|---|---|
| `Camera_X` (ProjectX) | 37 | `UpdateCamera`, `ProcessCameraState`, `ClampPOV`, `BlendCameraOrientations`, `RemoveRoll`, `SnapTransition`, `UpdateCameraKnocks` |
| `Camera_TA` (TAGame) | 21 | `UpdateSwivel`, `GetDesiredSwivel`, `ApplySwivel`, `UpdateFOV`, `ClipToField`, `ApplyRearCameraView`, `SwivelDieRate`, `SwivelFastSpeed`, `SwivelExtentFast/Slow`, `bGroundClampTrace`, `GroundClampZOffset` |
| `CameraState_TA` (TAGame) | 17 | `UpdateFreeLook`, `ApplyFreeLook`, `UpdateFreeLookInput`, `MaxFreeLookPitchDegrees`, `FreeLookRateYaw/Pitch`, `FreeLookOffset`, `bFreeLookActive` |
| `CameraState_Car_TA` (TAGame) | 34 | `UpdateGroundPOV/AirPOV`, `UpdateAirGroundBlend`, `CalculateDesiredAirRotation`, `UpdateDistance`, `UpdateFOV`, `UpdateFocus`, `UpdateValidPOV`, `AirVelocityInfluence(MaxSpeed)`, `SupersonicFOV(InterpSpeed)`, `MaxSpeedFOV`, `FOVInterpSpeed`, `DistanceSpeedScale`, `DistanceOffsetMin`, `DistanceInterp`, `FocusInterp`, `GroundRotationInterpRate(Wall)`, `InterpToGroundRate`, `InterpToAirRate` |
| `CameraState_BallCam_TA` (TAGame) | 21 | target selection + `PitchExtentMax/Min`, `PitchFocusZFactor`, `PitchScale`, `RotationRate` |
| `CameraStateBlender_X` (ProjectX) | 10 | `BlendCameraState`, `UpdatePOV`, `TransitionToState`, `SnapshotPOV`, `RemainingTime`, `BlendParams`, `TransitionDelta` |

**Proven blockers:** UELib's RL range is `[Build(867, 868, 9u, 32u)]`; licensee-34
object data stores tagged-property names as **inline int32-length strings**
(`04 00 00 00 "RPC\0"`, `13 00 00 00 "LocalTeamMemberIDs\0"`) with the `UField`
children list inline, so UELib throws in `UDefaultProperty.DeserializeTagUE3` /
`UField.Deserialize`. `ClampPOV`, `GetDesiredSwivel`, `UpdateSwivel`, `UpdateFOV`
are native: packages can never give them, only P2 can.

## §2 Non-negotiables (lead and every worker)

1. **No Psyonix data in the tree** (Rule 1). Packages, decrypted images,
   decompiled text, traces, fixtures → `/decomp/` (gitignored) or `~/rl-decomp/`.
   In-tree: our code + facts (names, numbers) + docs.
2. **Feel changes never touch gate values** (Rule 5). `measured.py`,
   `mesh_gates.py`, `gates_visual.json`, frozen tapes: read-only, always.
3. **Physics untouched.** No edits under `src/sim|arena|physics`, `Tuning.h`,
   `tools/tas/tapes`. Camera stays read-only on `Sim` — no camera→input feedback.
4. **INI schema v1 is normative** (`docs/INI_SCHEMA.md`, A5-owned). New
   `[camera]` keys are additive and need the failing roundtrip test first
   (`tests/test_settings_roundtrip.cpp`, `src/settings/Settings.cpp`). A worker
   that needs one stops and reports; the **lead** decides and logs it.
5. **Docs are written by the LEAD only.** Workers return a report block; the lead
   appends to `docs/CAMERA_EXACT_LOG.md` / `docs/CAMERA_DECOMP.md`. This is what
   makes parallel workers safe.
6. **One build at a time.** 2 cores, shared `build/`: every build/compile a
   worker runs is wrapped —
   `flock /tmp/rl-build.lock sh -c 'source tools/dev-local.sh && make /tmp/t_cam'`.
   Workers never run `make check` or the full visual suite (the lead does, once,
   at close).
7. **Cheapest proof first** (AGENTS.local): single target, single gate
   (`--only VG4`), `check_touched.py` before anything big. No `timeout 1800`
   foreground step; long runs are background jobs that the owning worker collects
   or kills in the same step.
8. **Nothing is accepted without reproduced output.** A worker's "PASS" is a
   claim; the lead re-runs that phase's cheapest proof and pastes *its own*
   output into the log.

## §3 Target architecture (the spec every worker implements)

```cpp
// src/camera/TruthPov.h — RL-shaped output of one camera evaluation.
struct TruthPov {
    float focus[3];      // world point the camera looks at (uu)
    float rotation[3];   // UE rotator units: pitch, yaw, roll (65536 = 360 deg)
    float distance;      // uu behind focus
    float fov;           // degrees
    float time;          // seconds (state-local clock, RL keeps one)
};
```

Per frame, in this order (order is feel-critical — do not reorder casually):

1. `state.UpdatePOV(out, dt)` — car or ball-cam state writes its **desired** POV
   (air/ground branch, focus interp, distance/FOV interp, air-velocity heading).
2. `blender.BlendCameraState(prevSnapshot, nextState, transition, dt)` — blend
   whole **POV snapshots** across a `Transition` (`Alpha`, `InterpRate`,
   `RemainingTime`); `SnapTransition` skips the blend.
3. free-look/swivel offset (`ApplyFreeLook` + swivel **decay**: `SwivelDieRate`,
   fast/slow extents), applied to `rotation`, then clamped
   (`MaxFreeLookPitchDegrees`).
4. `ClampPOV` + `RemoveRoll` + field clip (`ClipToField`, `bGroundClampTrace`,
   `GroundClampZOffset`) + knock/shake offsets (`LocationKnocks`,
   `RotationKnocks`, `ShakeFOVOffset`).
5. Convert once into eye/center for GL in `Camera::Apply()`; `Eye()/Center()`
   semantics unchanged (VG4, `--shot`, `--cam` depend on them).

Units: internal math in **rotator units** (65536/360) and **left-handed UE space**
(X fwd, Y right, Z up); degrees only at the INI/menu boundary, GL only in
`Apply()`. Interpolators are RL-style `InterpTo` (`1 - exp(-rate*dt)`), never a
stack of EMA/one-euro filters.

## §4 Orchestration contract

### Write scopes (hard fences — a worker writes nothing outside its scope)

| Phase | Writes | Reads | Blocked by | Est |
|---|---|---|---|---|
| P0 | `src/camera/**`, `tests/test_camera_dynamics.cpp`, `Makefile` (new TU only) | `docs/CAMERA_DECOMP.md`, `docs/CAMERA_NOTES.md` | — | ½ d |
| P1 | `tools/decomp/rlupk.py`, `tools/decomp/unpack.py` (new), `~/rl-decomp/**` | `docs/CAMERA_DECOMP.md`, `tools/decomp/camera_surface.py` | — | 1–2 d |
| P2 | `tools/decomp/cam_trace.py`, `tools/decomp/camera_trace_diff.py`, `/decomp/traces/**` | P1 output, `docs/MESH_SETUP.md` | P1 names (soft) | 2–3 d |
| P3 | `src/camera/**`, `tools/verify/camera_trace_gate.py` | P1 bodies, P2 traces | P1 + P2 | 2–3 d |
| P4 | `src/camera/**` (one slice per worker) | P3 constants, decomp gap table | P3 | 2 d |
| P5 | `src/camera/**` | everything | P4 | ½ d |

The lead alone owns `docs/**` and `AGENTS.md`; no worker writes docs.

### Worker report format (required — the lead parses it)

```
STATUS: DONE | PARTIAL | BLOCKED
PHASE: P0..P5 (+ slice name for P4)
FILES: path — created|modified        (one line each)
COMMANDS: verbatim command + last 5 lines of output   (one block per command)
EVIDENCE: the gate/test lines that prove the phase goal
DEVIATIONS: anything done differently, and why
NEXT-NEEDS: exact symbols/constants/files the next phase must know
RISKS: open items, or "none"
```

`BLOCKED` must include the failing command, its output, and the stop condition it
hit. Workers do not improvise past a stop condition.

### Lead verification duties (per wave, non-negotiable)

1. `git status --short` — reject out-of-scope writes; confirm no package/trace
   bytes or `decomp/` content entered the tree.
2. Re-run that phase's **cheapest proof** yourself; paste your own output.
3. For P1: reconcile decoded body sizes against the serialized sizes
   (`BlendCameraState` 1181 B, `UpdateCameraKnocks` 818 B, `UpdateCamera` 695 B,
   `GetCameraSettings` 337 B). Mismatch → the decode is wrong, send it back.
4. Append the verified block to `docs/CAMERA_EXACT_LOG.md` (create on first use)
   as `## Round N — Pk — <date>`; touch `docs/CAMERA_DECOMP.md` only at close.
5. `todo_write` the wave state after each wave so a resumed session knows where
   it stands.

### Failure ladder (same failure twice → climb one rung)

1. Re-dispatch the same worker prompt, quoting the verified discrepancy.
2. Re-dispatch with a narrower slice (one function instead of six).
3. Lead takes the phase over directly (allowed only after two failed workers; log
   it as `lead-takeover` with the reason).
4. Declare the phase `UNVERIFIED` and continue with what is provable (e.g. P2
   blocked → P3 runs the P1-only subset; native entry points stay `UNVERIFIED`,
   never invented).

### Concurrency rules

- Max **2** workers at once (2 cores + one build lock).
- `src/camera/**` is a **single-writer lane**: P0 → P3 → P4 slices → P5, always
  serial. Never dispatch two workers into it.
- Never two workers building at once — the `flock` rule in §2.6.
- A wave barrier means *all* workers in the wave reported **and** were verified
  before the next wave dispatches.

## §5 LEAD PROMPT — paste this into one agent

> You are the **lead** for `MASTER_PROMPT_CAMERA_EXACT.md` (rev 2) in
> `~/Projects/RocketLeagueLaptop`. You do not implement phases yourself unless
> §4's failure ladder says to take one over. Your job: dispatch workers, verify
> their claims against the repo, keep the docs, close the work order.
>
> **1. Read first, in order:** `AGENTS.md`, `AGENTS.local.md` (cheap proofs, build
> lock, `rtk`), this whole file, `docs/CAMERA_DECOMP.md`, `docs/GATE_POLICY.md`,
> `CONTRACTS.md` (camera + settings ownership), `docs/INI_SCHEMA.md` §`[camera]`.
>
> **2. Establish the baseline yourself (~1 min):**
> ```sh
> source tools/dev-local.sh
> python3 tools/verify/camera_surface_gate.py --regen   # expect CS1-CS5 5 PASS / 0 FAIL
> git status --short
> ```
> If it SKIPs, the local fixture is gone — re-extract per `docs/CAMERA_DECOMP.md`
> before dispatching P1/P2 (P1's self-check needs it).
>
> **3. Prepare wave state:** if your harness has goal tools, `create_goal` this
> objective once (it spans days and many workers) and `update_goal resume` after
> any session resume/fork. Then `todo_write` with one item per phase (P0…P5), all
> `pending`; mark the current wave `in_progress`; keep it current every wave.
>
> **4. Dispatch waves per the table at the top.** For each worker send exactly:
> - this file's §1–§4 (invariants, spec, fences, report format),
> - the target phase's §6 block verbatim,
> - its **write scope**, its **timebox**, and this line:
>   "You are one worker in a parallel wave. Write only inside your scope. Do not
>   write docs. Serialize every build with
>   `flock /tmp/rl-build.lock sh -c 'source tools/dev-local.sh && <make target>'`.
>   Report in the §4 format. If your stop condition triggers, report BLOCKED with
>   the failing command — do not improvise."
>
> Start **W1 = P0 ∥ P1(a)** as background jobs; do not duplicate their work while
> they run. As each settles: verify per §4 (git status, re-run its cheapest
> proof, reconcile P1's body sizes), then append the log round.
>
> **5. Wave sequence:** W1 (P0 ∥ P1a) → W2 (P1b ∥ P2-rig) → W3 (P2-record) →
> W4 (P3) → W5 (P4 slices, one worker each, serial) → W6 (P5). Never start a wave
> before the previous barrier is verified. P1 may be dispatched whole in W1
> (decoder + dumps) if you would rather run fewer waves — then W2 is P2-rig alone
> and nothing else changes. Adapt freely, but never break the two
> invariants: one writer per file, one build at a time. If P2 blocks on hooking the game:
> continue with P3 on the P1-only subset and record the native entry points as
> `UNVERIFIED`. Never let a worker guess a constant it could not derive.
>
> **6. At every barrier** also run `python3 tools/verify/check_touched.py` and
> confirm the tier it names matches what actually changed; if a worker touched a
> file outside its scope, restore that file to the last verified state and
> re-dispatch.
>
> **7. Close-out (W6):** P5's report + your independent run of `make tests`,
> `visual_gates.py --only VG1/VG4/VG7`, `camera_surface_gate.py`,
> `camera_trace_gate.py` (if traces exist), and the two-machine perf table
> (quote `GL_VENDOR/RENDERER/VERSION` for both). Write the verdict at the top of
> `docs/CAMERA_DECOMP.md` (exact / `UNVERIFIED` / stays approximate + why), the
> final round in `docs/CAMERA_EXACT_LOG.md`, mark this work order **CLOSED (date,
> rev 3)** and update the `AGENTS.md` pointer. Report to the human: phases
> landed, evidence lines, remaining gaps, measured µs table.
>
> **8. Rules that override any worker:** no Psyonix bytes in the tree; never edit
> gate values, `measured.py`, `mesh_gates.py`, `gates_visual.json`, frozen tapes,
> or physics/arena/sim sources; docs are yours alone; every claim you record must
> be output you reproduced yourself.

## §6 Worker prompts

Shared preamble for every worker: "Read `AGENTS.md`, `AGENTS.local.md`,
`MASTER_PROMPT_CAMERA_EXACT.md` §1–§4, and `docs/CAMERA_DECOMP.md`. Stay inside
your write scope. Report in §4's format."

### P0 — split the camera into RL's layers (behavior-preserving)

> **Write scope:** `src/camera/**`, `tests/test_camera_dynamics.cpp`, `Makefile`
> (only if a new TU must be added). Timebox ½ d.
> **Do:**
> 1. Add `src/camera/TruthPov.h` exactly as in §3.
> 2. Restructure `Camera.cpp` into the layers of §3 behind the **existing** API
>    (`Update`, `Apply`, `Eye`, `Center`, `EffFov`, `Blend`, `SwivelYaw/Pitch`,
>    `Params`, `SetFreeView`, `SetLookBehind`). One TU is fine; the point is named
>    layer functions. **Keep the current tuned behavior** in each layer — this
>    phase is a refactor, not a retune, so VG4 stays byte-comparable.
> 3. Re-baseline `tests/test_camera_dynamics.cpp` from CarBaller constants to
>    **structural invariants**: rest pose equals `distance/height/angle` exactly;
>    a moved target lags then converges; the eye never leaves the stiffness
>    sphere; a 180° flip takes the short arc; swivel clamps and returns to zero.
>    Delete assertions hard-coding invented numbers (`20 rad/s`, `19.1°`/frame,
>    `0.05 s` idle) — numeric fidelity belongs to P3's trace gate.
> 4. Keep `t_cam` headless (no GL context; `-lGL` only to link).
> **Prove (paste verbatim):**
> ```sh
> flock /tmp/rl-build.lock sh -c 'source tools/dev-local.sh && make /tmp/t_cam'
> flock /tmp/rl-build.lock sh -c 'source tools/dev-local.sh && make tests'
> python3 tools/verify/visual_gates.py --only VG4 --diag
> python3 tools/verify/camera_surface_gate.py
> ```
> **Stop if:** any VG4 number moves, `t_cam` needs a GL context, or the change
> leaks into `src/game/main.cpp` beyond the existing `camera.Params()` sync.

### P1 — get the function bodies (licensee-34 decoder)

> **Write scope:** `tools/decomp/rlupk.py`, `tools/decomp/unpack.py` (new),
> `~/rl-decomp/**`, `/decomp/decompiled/**`. **Never** package bytes in-tree.
> Timebox 1–2 d.
> **Environment (already provisioned — verify, do not reinstall):**
> `~/.dotnet/dotnet` (SDK 10.0.402), `~/rl-decomp/uelib` (UELib clone,
> unshallowed, local patch `[Build(867, 868, 9u, 34u)]` in
> `src/UnrealPackage.cs` ~L904), `~/rl-decomp/dump` (console app referencing
> `../uelib/src/Eliot.UELib.csproj`).
> **Do:**
> 1. Keep the local license patch; rebuild
>    `~/.dotnet/dotnet build ~/rl-decomp/dump -c Release -f net10.0`.
> 2. Teach licensee 34 to read tagged-property **names/types as int32-length
>    strings**: `src/Core/Classes/UDefaultProperty.cs` `DeserializeTagUE3()` —
>    try `ReadNameReference()`, on failure rewind and read an FString, then
>    `new UName(text)`; same for `typeName`.
> 3. Decode the inline `UField` children list (`"LocalTeamMemberIDs"`-style
>    records) in `src/Core/Classes/UStruct.cs` / `UField.cs` for RL ≥ 33
>    (name string, then type/outer indices, then size + flag words).
> 4. Add `tools/decomp/unpack.py` (ours, MIT) writing the decrypted + inflated
>    package (`rlupk.Package(...).image`) for UELib to read.
> 5. Dump to `/decomp/decompiled/` (gitignored):
>    `Camera_X.{UpdateCamera,ProcessCameraState,ClampPOV,BlendCameraOrientations,RemoveRoll,UpdateCameraKnocks,SnapTransition}`,
>    `Camera_TA.{UpdateSwivel,GetDesiredSwivel,ApplySwivel,UpdateFOV,ClipToField,ApplyRearCameraView}`,
>    `CameraState_TA.{UpdateFreeLook,ApplyFreeLook,UpdateFreeLookInput}`,
>    `CameraState_Car_TA.{UpdateGroundPOV,UpdateAirPOV,UpdateAirGroundBlend,CalculateDesiredAirRotation,UpdateDistance,UpdateFOV,UpdateFocus,UpdateValidPOV}`,
>    `CameraState_BallCam_TA.{UpdateFocus,UpdateRotationModifiers,TargetSelect,FindBestTarget}`,
>    `CameraStateBlender_X.{BlendCameraState,UpdatePOV,TransitionToState}`.
> **Self-check (the guard against a wrong decode):** bodies must reconcile with
> the serialized sizes — `BlendCameraState` 1181 B, `UpdateCameraKnocks` 818 B,
> `UpdateCamera` 695 B, `GetCameraSettings` 337 B — and every opcode stream ends
> in `EX_Return` + `EX_EndFunctionParms`. If they do not reconcile, fix the
> decoder; never fabricate a body.
> **Prove:** per-function decoded-size table, `camera_surface_gate.py` still 5/5,
> `git status --short` showing no package/decompiled bytes in-tree.
> **Stop if:** a function is native (empty / `// (cpptext)`) — record "P2 must
> measure" and continue.

### P2 — record the live camera (the only source of native truth)

> **Write scope:** `tools/decomp/cam_trace.py`, `tools/decomp/camera_trace_diff.py`,
> `/decomp/traces/**`. Timebox 2–3 d.
> **Do:**
> 1. Hook `ProjectX.Camera_X.ClampPOV` (BakkesMod under Wine if available; else a
>    read-only memory reader against `<HeroicPrefix>/pfx`) — **offline freeplay
>    only, never online play**. Log per frame: `OutPOV` (Focus, Rotation in
>    rotator units, Distance, FOV, Time), `ProfileCameraSettings`, car/ball poses,
>    inputs, `dt`, `CurrentSwivel`.
> 2. Scenario deck (one file each): ball-cam toggle, target switch mid-air,
>    swivel sweep + release, landing (air→ground), supersonic straight, wall
>    contact, dribble hold, goal→ball-cam reset. Seed-pin anything sim-driven.
> 3. Trace format + loader in `tools/decomp/cam_trace.py` (header: package
>    identity, settings, tick rate; records: fixed-stride float32).
> 4. `tools/decomp/camera_trace_diff.py`: replay a trace through our camera, print
>    max/median per-field error (uu for Focus/Distance, rotator units for
>    Rotation, degrees for FOV) and the 10 worst frames with inputs.
> **Prove:** self-consistency (replay the recorded POV through the loader → 0
> error), then our camera's current error table (expected large), then
> `git status` clean of trace bytes.
> **Stop if:** the game cannot be hooked after a bounded attempt (≤ 5-min steps,
> no retry loops) — report where it failed so P3 can run decompile-only.

### P3 — port the math and fit constants against the trace

> **Write scope:** `src/camera/**`, `tools/verify/camera_trace_gate.py`.
> Timebox 2–3 d.
> **Do:**
> 1. Implement each layer from P1's decoded bodies (P1 wins where it decoded; the
>    P2 trace wins where they disagree or the body is native).
> 2. Map sliders → internal values through the real limits (`StiffnessLimits`,
>    `DistanceLimits`, `AngleLimits`, `SwivelSpeedLimits`,
>    `TransitionSpeedLimits`), replacing our invented
>    `followRate = 1.5+14*stiffness`.
> 3. Fit remaining free constants per scenario, then freeze them in `Camera.cpp`
>    with provenance comments: `// decomp: CameraState_Car_TA.FocusInterp` or
>    `// trace: deck/swivel-release-01` — never "tuned".
> 4. Land `tools/verify/camera_trace_gate.py` (local tier, SKIPs without
>    `/decomp/traces/`): CT1 self-replay exact, CT2 per-scenario
>    settle/overshoot within −5% of baseline, CT3 no regression vs the stored
>    `camera_trace_baseline.json` (gitignored, beside the traces).
> 5. Keep P0's structural invariants green (`t_cam`) and VG4/VG7 green.
> **Prove:** `camera_trace_gate.py` output (CT1–CT3), before/after error tables,
> `make tests`, `--only VG4`.
> **Stop if:** a scenario diverges *structurally* (wrong branch, not a constant) —
> that is a missing mechanism: log it for P4, keep the gate at baseline, never fit
> around it.

### P4 — close the proven gaps (one slice per dispatch)

> **Write scope:** `src/camera/**`. Timebox ≤ ½ d per slice.
> **Slices, in order, each its own worker with its own trace scenario:**
> 1. Swivel **decay** (`SwivelDieRate`, `SwivelFastSpeed`,
>    `SwivelExtentFast/Slow`) replacing our 2× snap; map `snap = true` onto
>    `SnapTransition`/die-rate, not our idle timer.
> 2. Free look as its own layer: `FreeLookSensitivityScale_{Digital,AnalogStick,Mouse}`,
>    `FreeLookRateYaw/Pitch`, symmetric `MaxFreeLookPitchDegrees` (ours is
>    −0.8/+0.7 rad).
> 3. Ball-cam pitch shaping: `PitchExtentMax/Min`, `PitchFocusZFactor`, `PitchScale`.
> 4. Knocks (`LocationKnocks`, `RotationKnocks`, `UpdateCameraKnocks`) + field clip
>    (`ClipToField`, `bGroundClampTrace`, `GroundClampZOffset`) replacing our
>    elevation clamp.
> 5. Only if a real setting needs it: new `[camera]` INI keys — **stop and report**
>    for A5 sign-off (failing roundtrip test first, schema stays 1).
> **Prove (per slice):** its scenario in `camera_trace_gate.py`, `make tests`,
> `--only VG4`. Report mechanism + RL symbol(s) + before/after trace delta.

### P5 — performance + close-out

> **Write scope:** `src/camera/**`. Timebox ½ d. (Docs are the lead's.)
> **Do:**
> 1. Perf: no allocation in the camera path (assert in debug), float32 only, one
>    `sincosf` per frame, `InterpTo` factors shared per frame, single pass
>    (state → offsets → clamp → shake); add a `CAM µs` line to the pacing print.
> 2. Measure **both** machines per GRAPHICS45 rules (quote
>    `GL_VENDOR/RENDERER/VERSION`): A = R4 / radeonsi stoney (the 60 FPS
>    question), B = RTX 4060. Budget **≤ 8 µs/frame** worst case; 30-min soak
>    `drops=0`.
> 3. Re-run `make tests`, `--only VG1/VG4/VG7`, `camera_surface_gate.py`,
>    `camera_trace_gate.py`, `check_touched.py`.
> **Prove:** measured µs table per machine, allocation count (0), gate lines, and
> the exact/`UNVERIFIED` split.

## §7 Verification cheat sheet

```sh
python3 tools/verify/check_touched.py                    # tier selector, run first
flock /tmp/rl-build.lock sh -c 'source tools/dev-local.sh && make /tmp/t_cam'
flock /tmp/rl-build.lock sh -c 'source tools/dev-local.sh && make tests'
python3 tools/verify/visual_gates.py --only VG4 --diag    # ball-indicator recipe
python3 tools/verify/visual_gates.py --only VG7          # 60 s tri/draw budget
python3 tools/verify/camera_surface_gate.py              # CS1-CS5 (SKIPs w/o fixture)
python3 tools/verify/camera_trace_gate.py                # CT1-CT3 (P3+, SKIPs w/o traces)
python3 tools/decomp/camera_surface.py --out decomp/camera_surface.json  # re-extract
```

Local-only decomp toolchain (never in-tree):

```sh
# Dump.dll = the console dumper in ~/rl-decomp/dump (ours, not in-tree); UELib's
# CLI keeps reading stdin after the command, so always close it.
~/.dotnet/dotnet build ~/rl-decomp/dump -c Release -f net10.0
~/.dotnet/dotnet ~/rl-decomp/dump/bin/Release/net10.0/Dump.dll \
    /decomp/TAGame_decrypted.upk decompile Camera_X.UpdateCamera </dev/null
```

## §8 Traps (each one already bit someone)

- **`tests/test_camera_dynamics.cpp` encodes CarBaller's constants.** Leave them
  and the correct model fails its own test. P0 converts them to structural
  invariants.
- **`Eye()/Center()` are load-bearing**: VG4's ball-indicator recipe, `--shot`,
  `--cam eye...,target...`, the free-cam hook.
- **Blend order**: RL clamps/removes roll *after* the offset layer. Reordering
  changes feel near the floor and at max pitch — and VG4 will not notice.
- **Two filters is not one**: our one-euro + EMA stack is a different dynamic
  system from RL's `InterpTo`; replacing it is a behavior change, so it belongs in
  P3 with a trace, never in P0.
- **INI writes are A5-owned**: additive keys need the roundtrip test first.
- **Don't tune against video**: 60 Hz, motion blur, unknown `dt`.
- **Package drift**: if `camera_surface_gate.py` CS1 fails, RL patched — re-extract
  the fixture and re-baseline `docs/CAMERA_DECOMP.md` before anything else.
- **Parallel builds corrupt `build/`**: only the `flock`-wrapped targets above.
- **A subagent's "PASS" is a claim, not evidence**: the lead reproduces it.

## §9 What closes this work order

P0–P5 verified by the lead, `docs/CAMERA_EXACT_LOG.md` holding the baseline and
final residual tables, `docs/CAMERA_DECOMP.md` carrying the verdict (exact vs
`UNVERIFIED`), camera cost measured on both machines, the physics bar untouched,
and this header marked **CLOSED (date, rev 3)** plus the `AGENTS.md` pointer
updated.
