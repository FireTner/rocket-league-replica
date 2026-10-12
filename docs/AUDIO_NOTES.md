# Audio notes — procedural synth, zero assets (MIT)

## Design
`src/audio/Audio.{h,cpp}` (game-owned; the sim never sees audio). SDL2 audio
callback synth, 44.1 kHz f32 mono, 1024-sample buffer. No samples, no files.

- **Engine**: saw-ish osc at 55 + 165·speed01 Hz + sub sine + breath noise,
  gain follows speed. Always on while the game runs.
- **Boost**: white-noise hiss while `IsBoosting`.
- **One-shots** (lock-guarded 8-slot queue, mixed in the callback):
  jump (220→440 Hz sweep, 0.10 s), small pad (880→1320, 0.12 s),
  big pad (660→990, 0.20 s), goal (square-ish 523→784 swell, 0.80 s).
- Events are game-detected: jump rising edge, boost-meter +5 jumps (pickups;
  suppressed under unlimited-boost top-up), goal latch. `M` mutes,
  `--no-audio` starts silent. Failure to open the device is never fatal.

## Verification
- Unit probe (`/tmp/audiotest.cpp`, dummy driver): all blips + mute path,
  63 callbacks / 64512 frames in 1.5 s, clean shutdown, exit 0.
- Game run (dummy driver): `audio_cb=215 audio_frames=220160` over 5 s at
  44.1 kHz — callback alive alongside render+physics, no underrun errors.
- On this box (no sound server under Xvfb) real output needs ALSA/Pulse;
  mixing correctness is driver-independent (same callback).

## Visual-pass status (Round 36)

Audio was deliberately **not touched** by the visual pass (MASTER_PROMPT_
VISUALS.md scopes it out; constraint 9 keeps it procedural). The only change
that reaches audio is indirect: FX now advance on sim time instead of wall
time, and audio still runs on its own wall-clock engine path, so the two are
independent. No samples, no assets, no new dependencies.

## Known approximations
- No doppler, no distance attenuation (ball), no crowd/ambient bed, no
  UI clicks. Engine timbre is a placeholder (saw+sub), not an RL recording.
