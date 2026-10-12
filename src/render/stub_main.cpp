// stub_main.cpp — A4 round-1 proof: 720p window + spinning box + camera + FX.
// Build: g++ $(sdl2-config --cflags) src/render/Renderer.cpp src/camera/Camera.cpp
//        src/fx/FX.cpp src/models/Models.cpp src/render/stub_main.cpp \
//        $(sdl2-config --libs) -lGL -o build/render_stub
// Run: ./build/render_stub   (B toggles ball-cam, ESC quits)
#include "../camera/Camera.h"
#include "../fx/FX.h"
#include "../models/Models.h"
#include "Renderer.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>

int main() {
    rl::Renderer renderer;
    if (!renderer.Init(1280, 720, true)) return 1;

    rl::Camera cam;
    bool ini = cam.LoadFromIni("config/settings.ini");
    std::fprintf(stderr, "[stub] camera %s (fov=%.0f dist=%.0f h=%.0f ang=%.0f stiff=%.2f ballcam=%d)\n",
        ini ? "loaded from config/settings.ini" : "compiled defaults",
        cam.Params().fov, cam.Params().distance, cam.Params().height,
        cam.Params().angle, cam.Params().stiffness, (int)cam.Params().ballCam);

    rl::FX fx;
    rl::Pad pads[4] = {
        { -1500, -1500, true, true, 0 }, { 1500, 1500, true, false, 6.0f },
        { 0, -2000, false, true, 0 },    { 0, 2000, false, false, 2.5f },
    };

    bool quit = false, ballCam = cam.Params().ballCam;
    uint64_t t0 = SDL_GetTicks();
    uint64_t last = t0;
    int frame = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) quit = true;
                if (e.key.keysym.sym == SDLK_b) { ballCam = !ballCam; }
            }
        }
        uint64_t now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f; last = now;
        if (dt > 0.1f) dt = 0.1f;
        float t = (now - t0) / 1000.0f;
        frame++;

        // Fake circling car + orbiting ball (stand-in for A1 sim Snapshot).
        rl::Snapshot s;
        s.carYaw = t * 0.5f;
        s.carPos[0] = std::cos(t * 0.3f) * 1200;
        s.carPos[1] = std::sin(t * 0.3f) * 1200;
        s.carPos[2] = 40;
        s.carSpeed = 2300; // force supersonic trail visible
        s.ballPos[0] = std::cos(t * 0.5f + 1.0f) * 500;
        s.ballPos[1] = std::sin(t * 0.5f + 1.0f) * 500;
        s.ballPos[2] = 100 + 60 * std::sin(t * 1.3f);
        s.boost = 50 + 50 * std::sin(t * 0.7f);
        s.boosting = (std::sin(t * 0.9f) > 0);
        s.ballCam = ballCam;
        s.tick = frame;

        // Pad cooldown tick + demo pickup/goal triggers for visual proof.
        for (auto& p : pads) if (!p.active) {
            p.cooldown -= dt;
            if (p.cooldown <= 0) { p.active = true; fx.OnPickup(p.big); }
        }
        if (frame == 300) fx.OnGoal();
        fx.Update(dt, s);

        cam.Update(dt, s.carPos, s.carYaw, s.ballPos, ballCam);

        // Physics-first policy: renderer only presents; never waits on sim.
        renderer.BeginFrame();
        cam.Apply(1280, 720);
        renderer.DrawGroundGrid(6000, 500);
        rl::DrawCar(renderer, s, true);
        rl::DrawBall(renderer, s);
        fx.DrawPads(renderer, pads, 4);
        fx.DrawBoostFlame(renderer, s);
        fx.DrawTrail(renderer, s);
        // Spinning proof box at center.
        renderer.DrawBox(0, 0, 200, 120, 120, 120, t, 0.9f, 0.3f, 0.2f);
        renderer.EndFrame();

        if (frame == 600) { // ~10s @60fps: auto-exit for headless proof
            auto st = renderer.Stats();
            std::fprintf(stderr, "[stub] 600 frames done, avg=%.2f p50=%.2f p99=%.2f — exiting OK\n",
                st.avg, st.p50, st.p99);
            break;
        }
    }
    renderer.Shutdown();
    SDL_Quit();
    return 0;
}
