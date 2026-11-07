# RocketLeagueLaptop

Free-play Rocket League replica. C++20, SDL2, OpenGL 4.5 compatibility. Physics is vendored [RocketSim](third_party/RocketSim) (MIT © 2022 ZealanL), unmodified.

## Build and run

From the repo root, with a C++20 compiler, CMake, SDL2, and OpenGL. The default build is Release with `-O2`. ccache is used when it is installed:

```
cmake -B build
cmake --build build
./build/game
```

`./build/game` loads `collision_meshes` and, when the pack is present, `arena_assets/stadium_day`. If `collision_meshes/soccar/` has no mesh, the build writes a clean-room one there (gitignored). `--meshdir` and `--arenadir` override those paths. `--shell` draws the analytic arena. `./build/game --help` lists the other flags and keys.

`./build/headless_sim --tape tools/tas/tapes/<tape>.csv --ticks N --seed S --dump out.csv` replays a tape. Tape CSV columns are `tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake` (120 Hz, signs passed through). `steer +1` is left. Pass `--meshdir collision_meshes` to replay against the collision mesh.

`cmake --build build --target check` runs the unit tests and the headless checks.

The sim objects are compiled with `-ffp-contract=off -fno-fast-math -fno-strict-aliasing`.

## Camera

The chase camera is `src/camera`. It follows the car in z-up units. The field-of-view slider is a horizontal angle, converted for the window.

`config/settings.ini` `[camera]`, also editable in the Esc menu:

| Key | Default | What it does |
| --- | --- | --- |
| `fov` | 110 | Horizontal field of view, degrees, 60–110 |
| `distance` | 270 | Rest distance, uu, 100–400. Forward speed lengthens it. |
| `height` | 100 | Focus height above the car, uu, 40–200 |
| `angle` | -3 | Rest pitch, degrees, −30–15. Negative looks down. |
| `stiffness` | 0.45 | How fast yaw and pitch catch the car or the ball, 0–1 |
| `ballcam` | true | Blend the view from the nose toward the ball |
| `transition_speed` | 1.5 | Ball-cam blend, 1–2. The view moves at 0.5 + 2.5× this per second and then stops. |
| `swivel_speed` | 5 | How fast the right-stick offset eases, 1–10 |
| `shake` | false | Offset applied after the rig, off unless selected. `--no-shake` forces it off. |
| `invert_swivel` | false | Flip stick pitch |
| `snap` | true | A released stick returns the swivel. Off holds the offset. |
| `air_heading` | false | In the air, blend heading from the nose toward velocity (200–900 uu/s) |

Supersonic widens the field of view. The horizon stays level: air roll, a wall, and the ceiling change yaw and pitch, not roll. The rig keeps the eye at least 10 uu off the floor. With snap on, a released stick returns the swivel.

Space toggles ball cam. Hold J to look behind. F is the free camera. The right stick swivels.

## Rules

- Do not commit Psyonix game assets. Collision dumps and arena packs stay on the machine that owns the install.
- Do not modify `third_party/RocketSim` or the Bullet sources under it.

## Credits

- RocketSim, MIT © 2022 ZealanL, commit `c2baacb`. Includes Bullet Physics 3.24 © Erwin Coumans (zlib).
- Fennec body mesh, CC-BY-4.0, "Fennec - Rocket League Car" by [Jako](https://sketchfab.com/fairlight51). Attribution: `assets/models/fennec.LICENSE.txt`.
- SDL2 (zlib) and OpenGL are system libraries. The 5×7 HUD font is original.
