# Audio

The default bed is a procedural synth (engine, boost hiss, jump, pickup, goal, demo). No assets.

`--sounds DIR` (or `sound_assets/manifest.txt` when that file is present) plays the pack through miniaudio. `--synth` ignores the pack. `--no-audio` stays silent. A missing default pack stays on the synth. A `--sounds` directory with no `manifest.txt` is a fatal error.

The device is 48 kHz with a requested period of 256 frames. A clip that is already 48 kHz is not resampled. Loop beds fade in 6 ms. With the pack loaded, the procedural saw and noise are silent, and the crowd bed is not started. Powerslide follows the held brake and fades out on release. miniaudio 0.11.22 (public domain or MIT-0) lives in `third_party/miniaudio/`.

The pack is built from the user's own install and is gitignored:

```sh
python3 tools/audio/rip_sounds.py --vgmstream /path/to/vgmstream-cli
./build/game
```

## Known approximations
- Distance and a light doppler are on. The ear is the published camera eye
  (`Camera::Eye()`, after shake). The engine (and car one-shots, and boost
  hiss) use car-to-ear distance; the goal horn uses ball-to-ear distance.
  Doppler scales only the engine oscillator from the car's radial speed
  versus that eye (positive = receding). Curves are UNVERIFIED: full volume
  inside 800 uu, inverse distance, floor 0.20; speed of sound 34300 uu/s;
  pitch ratio clamped to [0.85, 1.15].
- No crowd/ambient bed, no UI clicks. Engine timbre is a placeholder
  (saw+sub), not an RL recording. Still zero sample assets.

`vgmstream-cli` is only needed to decode the banks. Playback is miniaudio. The tool refuses to write the pack anywhere in the repo except `sound_assets/`.

Cues: jump, dodge, double jump, boost pad / full boost, boost loop, tires, air whoosh, landing, ball hit, ball roll / fly, slide, supersonic sting + loop, goal horn and explosion, demo. `--synth` is the procedural bed. No countdown (free play has no match clock) and no cosmetic boosts. Tires play at the recorded rate. Distance and a light doppler still apply: full volume inside 800 uu, inverse distance, floor 0.20; the engine oscillator uses radial speed versus the camera eye (ratio clamped to 0.85..1.15). Curves are UNVERIFIED.

`python3 tools/audio/test_wwise_bnk.py` checks the bank parser against a synthetic bank.
