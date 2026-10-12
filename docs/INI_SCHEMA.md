# INI Schema v1 — NORMATIVE (A5-owned)

File: `config/settings.ini`. Parser/loader: `src/settings/Settings.{h,cpp}`
(`Settings::Load/Save/Migrate`). The in-game menu (`src/menu/Menu.{h,cpp}`)
edits these same keys; the menu never diverges from this document.

Base: coordinator provisional schema in `CONTRACTS.md` (A5 kept every
provisional key name + meaning, extended only: arrow alternates, `LeftShift` /
`LeftCtrl` concreteness, `kickoff_0..4` bindings). Any future change bumps
`[version] schema` and migrates via `Settings::Migrate()`.

## Grammar

- `;` or `#` starts a comment (full-line, or trailing after `" "`).
- `[section]` headers; `key = value` pairs. Keys/sections case-insensitive.
- Unknown sections/keys are **ignored** on load (forward tolerance).
- Missing file or missing keys → built-in defaults (== shipped `.ini`).
- Types: `bool` accepts `true/false/1/0/yes/no/on/off` (any case);
  `int` is decimal; `float` is decimal (`%.4g` on save).
- **Binding grammar:** `KEY[,ALT...][/KEY[,ALT...]]`. `/` separates the two
  slots of a paired axis (slot A / slot B); `,` lists alternates (first =
  primary, shown in menu). Single-action keys use one slot (`a,b` = alternates).
- Key names are SDL key names, canonicalised on load:
  single letters uppercase (`w`→`W`); `up/down/left/right`→`Up/Down/Left/Right`;
  aliases `Shift`→`LeftShift`, `Ctrl`/`Control`→`LeftCtrl`, `Esc`→`Escape`,
  `Del`→`Delete`. Everything else passes through verbatim
  (`Space`, `LeftShift`, `LeftCtrl`, `Escape`, `Return`, `F1`…`F12`, …).

## [version]

| key    | type | default | range | notes                        |
|--------|------|---------|-------|------------------------------|
| schema | int  | 1       | 1..1  | File-format version. Older → `Migrate()` fills/clamps; newer → load fails safe to defaults. Read-only in menu. |

## [camera]

| key       | type  | default | range       | notes                                    |
|-----------|-------|---------|-------------|------------------------------------------|
| fov       | float | 110     | 60..130     | Vertical field of view, degrees.         |
| distance  | float | 270     | 100..400    | Chase-cam distance (uu). RL 1:1 range.   |
| height    | float | 100     | 40..200     | Chase-cam height (uu). RL 1:1 range.     |
| angle     | float | -3      | -30..15     | Pitch-down angle, degrees. RL 1:1 range. |
| stiffness | float | 0.45    | 0..1        | 0 = loose, 1 = rigid follow. RL default. |
| ballcam   | bool  | true    | —           | Start tracking ball; `C` toggles live.   |

## [input]

Paired axes — slot A / slot B meaning per row:

| key      | type    | default            | slot A (+)          | slot B (−)           |
|----------|---------|--------------------|---------------------|----------------------|
| throttle | axis    | `W,Up/S,Down`      | forward (+1)        | back (−1)            |
| steer    | axis    | `A,Left/D,Right`   | LEFT (+1)           | right (−1)           |
| pitch    | axis    | `W,Up/S,Down`      | nose UP (+1)        | nose-down (−1)       |
| yaw      | axis    | `A,Left/D,Right`   | left (+1)           | right (−1)           |
| roll     | axis    | `Q/E`              | roll-left (Q)       | roll-right (E)       |

Sign authority: `tools/tas/TAPE_FORMAT.md` §2 (coordinator ruling — tape
floats → `Controls` → RocketSim `CarControls` is a verbatim passthrough,
zero negation). Physical truth of the labels is UNVERIFIED until A1's
empirical turn-test; if measurement contradicts, this doc + keyboard flip
together.

Notes: air pitch/yaw intentionally reuse the drive keys (standard keyboard
layout). Holding `handbrake` in air mirrors the steer *direction* onto `roll`
(keyboard air-roll convention; ground frames ignore `roll`).

Single actions (one slot, comma = alternates):

| key            | type | default        | notes                                              |
|----------------|------|----------------|----------------------------------------------------|
| jump           | keys | `Space`        | Held state in `Controls.jump`; sim detects edge.   |
| boost          | keys | `LeftShift`    | Hold to boost (drains per `[boost]`).              |
| handbrake      | keys | `S,LeftCtrl`   | Ground = powerslide; air = roll modifier. `S` doubles as reverse+slide (common keyboard setup); `LeftCtrl` is the dedicated modifier. |
| ballcam_toggle | keys | `C`            | Press → toggle ballcam (edge event).               |
| reset_car      | keys | `R`            | Press → respawn car at `game.kickoff_index`.       |
| reset_ball     | keys | `B`            | Press → ball to center, zero velocity.             |
| unlimited      | keys | `U`            | Press → toggle `[boost] unlimited` live.           |
| menu           | keys | `Escape`       | Press → open/close settings menu.                  |
| kickoff_0..4   | keys | `F1`…`F5`      | Press → spawn kickoff 0=center 1=left 2=right 3=back-left 4=back-right; also sets `game.kickoff_index`. |

### Gamepad keys (`[input]`, same section, `pad_`-prefixed)

Gamepad bindings are a **separate table** from the keyboard so neither device's
rebinding can clobber the other. Values are `SDL_GameControllerButton` names,
resolved case- and separator-insensitively (`LeftShoulder`, `left_shoulder`,
`LB` all work — `src/input/Gamepad.cpp::ButtonForName`). The left stick is
**fixed** (X → steer, Y → throttle) and is not rebindable through the menu.

| key                 | type  | default      | notes |
|---------------------|-------|--------------|-------|
| `deadzone`          | float | 0.08         | Radial stick deadzone, 0..0.5. Applied to the stick **vector** (magnitude), then the remainder is rescaled continuously to 0..1 with direction preserved. **UNVERIFIED** against a licensed source — 0.08 is a common Steam/RL-class default. |
| `pad_jump`          | btn   | `A`          | Held; sim detects the edge. |
| `pad_boost`         | btn   | `B`          | Hold to boost. |
| `pad_handbrake`     | btn   | `X`          | Ground = powerslide; air = roll modifier (same convention as keyboard). |
| `pad_ballcam_toggle`| btn   | `Y`          | Press → toggle ballcam (edge event). |
| `pad_reset_car`     | btn   | `Back`       | Press → respawn at `game.kickoff_index`. |
| `pad_reset_ball`    | btn   | `LeftStick`  | Press → ball to center. |
| `pad_unlimited`     | btn   | `RightStick` | Press → toggle `[boost] unlimited`. |
| `pad_menu`          | btn   | `Start`      | Press → open/close settings menu. |
| `pad_kickoff_0..4`  | btn   | `DPadUp`…`DPadRight`, (empty) | Same kickoff spawns as the keyboard F1–F5. |

Device selection is **additive**: a connected pad owns throttle/steer (analog)
and its buttons are OR-ed with the keyboard, so either device can drive and
neither cancels the other. A disconnected pad leaves keyboard behaviour
bit-identical. Hot-plug (`SDL_CONTROLLERDEVICEADDED/REMOVED`) never drops a
physics tick — see `src/input/Gamepad.h`.

Default keyboard table (this phase is keyboard-only; gamepad mapping is a
stub — see `src/input/BUILD_NOTES.md`):

| action              | primary    | alternate |
|---------------------|------------|-----------|
| drive fwd/back      | W / S      | Up / Down |
| steer left/right    | A / D      | Left / Right |
| jump                | Space      | — |
| boost               | LeftShift  | — |
| powerslide/air-roll | LeftCtrl   | S (held with steer) |
| discrete roll L/R   | Q / E      | — |
| ballcam             | C          | — |
| reset car / ball    | R / B      | — |
| unlimited boost     | U          | — |
| menu                | Esc        | — |
| kickoff spawns      | F1–F5      | — |

## [physics]

| key  | type | default | range   | notes                                          |
|------|------|---------|---------|------------------------------------------------|
| tick | int  | 120     | 30..240 | Sim tick rate Hz. A1 runs a 120 Hz accumulator; renderer may repeat a frame, never stall physics. |

## [boost]

| key      | type  | default | range    | notes                                                   |
|----------|-------|---------|----------|---------------------------------------------------------|
| unlimited| bool  | false   | —        | Infinite boost (free-play cheat). `U` toggles live.     |
| drain    | float | 33.33   | 0..100   | Boost units drained per second while boosting (≈3 s tank). |
| recharge | float | 10      | 0..100   | Passive regen units/s (pads still grant 12 / 100 flat). |

## [arena]

| key    | type | default | range | notes                                   |
|--------|------|---------|-------|-----------------------------------------|
| soccar | bool | true    | —     | Standard soccar map (only map this phase; 34 pads fixed by A2). |

## [game]

| key           | type | default | range | notes                                                        |
|---------------|------|---------|-------|--------------------------------------------------------------|
| kickoff_index | int  | 0       | 0..4  | Spawn slot: 0=center 1=left 2=right 3=back-left 4=back-right. |

## Migration policy

- `schema` absent → treated as 1 (this document is the first version).
- `schema` < 1 (impossible today; reserved): `Migrate()` fills keys absent in
  the old version with the defaults above, then clamps every ranged key.
- `schema` > 1: `Load()` fails safe (keeps compiled-in defaults, reports
  `schema N newer than supported`), so an old binary never misreads a new file.
- Empty binding slots after parse are refilled from defaults (a key with zero
  recognised keys is a misconfiguration, not a valid unbind).
