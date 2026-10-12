# A5 INPUT — build notes

## Round: gamepad + analog semantics (MASTER_PROMPT_PHYSICS.md §8 B1–B5)

### What landed

- **`Gamepad.{h,cpp}` — real SDL_GameController support** (was a
  `GamepadStub` that always reported neutral):
  - Left stick → `Controls`: `throttle = +stickY` (up = forward),
    `steer = -stickX` (right = −1 = right). Same verbatim sign contract as the
    keyboard; zero negation anywhere.
  - **Radial** deadzone (`ApplyDeadzone`), not per-axis: works on the stick
    magnitude, rescales `[deadzone,1] → [0,1]` so the response is continuous at
    the edge (no jump), preserves direction exactly, clamps to the unit disc.
    `(0.06,0.06)` has magnitude 0.0849 and is NOT neutral — that is the whole
    point of radial.
  - Buttons: A/B/X held → jump/boost/handbrake; Y/Back/Start/sticks/DPad are
    edge events (ballcam/reset/unlimited/menu/kickoffs). Handbrake mirrors the
    steer sign onto `roll`, matching the keyboard air-roll convention.
  - **Hot-plug** via `HandleDeviceEvent` on
    `SDL_CONTROLLERDEVICEADDED/REMOVED`; a disconnect yields neutral controls
    and never drops a physics tick.
  - `PollFromState` makes the whole mapping testable headless (no device, no
    SDL video init).
- **Settings**: `GamepadBindings` (`[input] deadzone` + `pad_*` keys),
  load/save/migrate complete. The table is deliberately separate from the
  keyboard table so neither rebinding pass can clobber the other.
- **`src/game/main.cpp`**: controller subsystem init, `gamecontrollerdb.txt`
  load (best-effort), device events, and an **additive merge** after
  `keyboard.Poll` — the pad owns throttle/steer when connected and its buttons
  are OR-ed with the keyboard. With no pad attached, keyboard behaviour is
  unchanged (frozen checksums hold).
- **Gates**: `tests/test_gamepad.cpp` (33 checks, `make tests` → `GAMEPAD
  PASS`) and `I1`–`I3` in `tools/verify/measured.py` (runner now **19 PASS,
  0 FAIL**).

### Citations

- Dodge threshold: `third_party/RocketSim/src/Sim/Car/CarConfig/CarConfig.h:37`
  `dodgeDeadzone = 0.5f`, applied at `Car.cpp:705-706` as
  `|yaw|+|pitch|+|roll| >= dodgeDeadzone`. Measured discontinuity sits exactly
  between 0.49 (216 deg/s, dodge-less double jump) and 0.5 (423 deg/s, flip).
- Timing windows (cited, exercised by existing G3/G4/G7):
  `third_party/RocketSim/src/RLConst.h:96-99` — `JUMP_MIN_TIME=0.025`,
  `JUMP_RESET_TIME_PAD=1/40`, `JUMP_MAX_TIME=0.2`,
  `DOUBLEJUMP_MAX_DELAY=1.25`.

### UNVERIFIED

- `deadzone` default 0.08 — no licensed source located for RL's exact stick
  deadzone. Flagged in `docs/INI_SCHEMA.md` and here. Change it by editing
  `[input] deadzone` (range 0..0.5).

### Menu rebinding — DONE

`Menu` now has an `ItemKind::PadBinding` and a second capture path:

- `AddPadBinding` rows for every `pad_*` schema key plus `[input] deadzone`
  (section "INPUT — GAMEPAD"). The menu lists every gamepad key, so it still
  never diverges from `settings.ini`.
- `HandleControllerButton(button)` handles BOTH capture and navigation:
  DPad up/down move the selection, DPad left/right adjust the row, A activates,
  B closes. The driving path is unaffected — these are extra bindings, not a
  replacement, and the same buttons still reach the car.
- Capture is typed: a `PadBinding` row waits for a pad button and **ignores key
  presses**; a keyboard `Binding` row ignores pad presses. Esc cancels either.
  `captureIsPad()` / `CapturePrompt()` say which one is pending.
- The game loop feeds freshly-pressed pad buttons into the open menu via
  `Gamepad::TakeNewPresses()` (edge list captured during `Poll`).

Gated by `tests/test_keyboard_menu.cpp` sections [7] and [8]: the `Pad: Jump`
row exists, Enter starts a pad capture, the prompt asks for a pad button, a key
press is ignored during it, `LeftShoulder` rebinds the row, Esc cancels, and
all nine gamepad schema rows are listed. `make tests` -> `KEYBOARD+MENU PASS`.

### TODO (next round)

- Independent tapes for the full jump/flip ladder beyond G3/G4/G7
  (`JUMP_MIN_TIME`, `JUMP_RESET_TIME_PAD`, `DOUBLEJUMP_MAX_DELAY` boundaries).

## Round 7 — M6 close-out (no input code changed)

- Re-verified after a clean rebuild: I1–I3 PASS in `measured.py` (dodge
  threshold discontinuity at `dodgeDeadzone` 0.5, monotonic analog response,
  threshold summing across pitch/yaw/roll); `tests/test_gamepad.cpp` ->
  `GAMEPAD PASS` (radial deadzone maths, sign table, buttons/edges, hot-plug,
  name resolution, INI defaults); `tests/test_keyboard_menu.cpp` ->
  `KEYBOARD+MENU PASS` (signs, air-roll/powerslide modifiers, rebinding,
  gamepad menu rows).
- Frozen input signs unchanged and still gated: steer+1 LEFT, pitch+1 UP,
  yaw+1 left, roll+1 LEFT, zero negation.
- Still UNVERIFIED by design: the gamepad `deadzone` default 0.08 (no licensed
  source) — see `docs/INI_SCHEMA.md`. The TODO above (tapes for the full
  jump/flip timing ladder beyond G3/G4/G7) remains the only open input item;
  it is not part of the §2 acceptance bar, which is met.
