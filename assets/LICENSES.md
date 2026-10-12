# assets/LICENSES.md — A4 asset licenses

**Round 1 decision: ZERO external assets. Everything is procedural.**

All visuals are generated in code (boxes/capsules/spheres via fixed-function
GL2.1) and all audio hooks are synth-only. This eliminates any risk of
shipping Psyonix geometry/textures/audio.

## Why no Kenney / community packs this round

- Any future pack must be MIT/CC0/CC-BY with URL + license file vendored
  here before use. Candidates (NOT yet vendored — do not treat as deps):
  - Kenney Prototype Textures (CC0, https://kenney.nl/assets/prototype-textures)
  - Kenney Particle Pack (CC0, https://kenney.nl/assets/particle-pack)
- **Hard rule: NO Psyonix / Rocket League rips, ever** — no game models,
  textures, sounds, or names beyond nominative "RL-like" references.

## Vendored assets

| Asset | Source | License |
|-------|--------|---------|
| `models/fennec.bin` + `models/fennec.LICENSE.txt` (converted from the CC-BY GLB via `tools/convert_fennec.py`, bake-verified: bbox 140.4×71.4 uu matches the reference port's measurements) | "Fennec - Rocket League Car" by **Jako** (https://sketchfab.com/fairlight51), https://sketchfab.com/3d-models/fennec-rocket-league-car-5b43b50b6eeb4a12a29671df3418f57a — community recreation, NOT extracted game geometry | **CC-BY-4.0** (attribution REQUIRED — reproduced in `models/fennec.LICENSE.txt`, `docs/THIRD_PARTY.md`, and the loader's startup log) |

## Audio

Synth-only hooks (see `src/audio/`). Ripped audio is forbidden.
Engine/boost/pickup/goal/demo sounds are procedural (sine/noise envelopes).
