// A5 contract test: gamepad mapping (headless — no device, no video init).
// Build: g++ -std=c++20 -Isrc tests/test_gamepad.cpp src/input/Gamepad.cpp
//        $(sdl2-config --cflags --libs) -o /tmp/t_gp && /tmp/t_gp
//
// Covers MASTER_PROMPT_PHYSICS.md §8 B1–B3 on the INPUT side:
//  - radial (not per-axis) deadzone with a continuous rescale
//  - sign table: stick up = throttle +1, stick right = steer -1, zero negation
//  - button map -> jump/boost/handbrake + edge-triggered events
//  - hot-plug: a disconnected pad yields neutral controls, never a dropped tick
//  - name<->button resolution used by the INI + menu
#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "input/Controls.h"
#include "input/Gamepad.h"
#include "settings/Settings.h"

static int failures = 0;
#define CHECK(cond, msg)                     \
    do {                                     \
        if (cond) {                          \
            std::printf("  ok   %s\n", msg); \
        } else {                             \
            std::printf("  FAIL %s\n", msg); \
            ++failures;                      \
        }                                    \
    } while (0)

static bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// Build a connected pad state with a left stick at (x,y) in -1..1.
static rl::Gamepad::State Stick(float x, float y, float deadzone = 0.08f) {
    rl::Gamepad::State s;
    s.connected = true;
    s.leftX = x;
    s.leftY = y;
    (void)deadzone;
    return s;
}

int main() {
    rl::Settings def = rl::Settings::Defaults();
    rl::Gamepad pad(def.gamepad);

    std::printf("[1] radial deadzone mathematics\n");
    {
        // Inside the deadzone -> exactly neutral, for every direction.
        auto r = rl::Gamepad::ApplyDeadzone(0.05f, 0.05f, 0.08f);
        CHECK(r.x == 0.0f && r.y == 0.0f, "inside deadzone -> (0,0)");
        r = rl::Gamepad::ApplyDeadzone(0.0f, 0.0f, 0.08f);
        CHECK(r.x == 0.0f && r.y == 0.0f, "centred -> (0,0)");

        // RADIAL, not per-axis: a diagonal push whose components are each
        // inside the deadzone can still be outside it as a vector.
        // x=y=0.06 -> magnitude 0.0849 > 0.08, so it must NOT be neutral.
        r = rl::Gamepad::ApplyDeadzone(0.06f, 0.06f, 0.08f);
        CHECK(r.x > 0.0f && r.y > 0.0f, "diagonal in-square is not dead-zoned");

        // Continuity: output magnitude must approach 0 as the input
        // magnitude approaches the deadzone (no jump at the edge).
        float magJustInside = rl::Gamepad::ApplyDeadzone(0.0799f, 0.0f, 0.08f).magnitude;
        r = rl::Gamepad::ApplyDeadzone(0.0801f, 0.0f, 0.08f);
        float outJustOutside = std::sqrt(r.x * r.x + r.y * r.y);
        CHECK(magJustInside < 0.08f && outJustOutside < 0.01f,
              "continuous at the deadzone edge (no jump)");
        r = rl::Gamepad::ApplyDeadzone(1.0f, 0.0f, 0.08f);
        float outFull = std::sqrt(r.x * r.x + r.y * r.y);
        CHECK(Near(outFull, 1.0f, 1e-3f), "full deflection -> magnitude 1");

        // Direction is preserved exactly.
        r = rl::Gamepad::ApplyDeadzone(0.6f, 0.8f, 0.08f);
        float inAng = std::atan2(0.8f, 0.6f), outAng = std::atan2(r.y, r.x);
        CHECK(Near(inAng, outAng, 1e-3f), "direction preserved through rescale");

        // Never leaves the unit disc.
        r = rl::Gamepad::ApplyDeadzone(1.0f, 1.0f, 0.08f);
        CHECK(std::sqrt(r.x * r.x + r.y * r.y) <= 1.0f + 1e-4f,
              "clamped inside the unit disc");
    }

    std::printf("[2] sign table (TAPE_FORMAT.md §2 / Controls.h)\n");
    {
        rl::Controls c;
        rl::InputEvents e;
        // Stick up -> throttle +1 (forward). Stick right -> steer -1 (right).
        pad.PollFromState(Stick(0.0f, 1.0f), c, e);
        CHECK(c.throttle > 0.99f, "stick up   -> throttle +1 (forward)");
        CHECK(Near(c.steer, 0.0f), "stick up   -> steer 0");
        pad.PollFromState(Stick(1.0f, 0.0f), c, e);
        CHECK(c.steer < -0.99f, "stick right-> steer -1 (RIGHT)");
        pad.PollFromState(Stick(-1.0f, 0.0f), c, e);
        CHECK(c.steer > 0.99f, "stick left -> steer +1 (LEFT)");
        pad.PollFromState(Stick(0.0f, -1.0f), c, e);
        CHECK(c.throttle < -0.99f, "stick down -> throttle -1 (reverse)");
    }

    std::printf("[3] buttons -> Controls (held) + edge events\n");
    {
        rl::Controls c;
        rl::InputEvents e;
        auto st = Stick(0.0f, 0.0f);
        st.buttons[SDL_CONTROLLER_BUTTON_A] = true;  // jump
        st.buttons[SDL_CONTROLLER_BUTTON_B] = true;  // boost
        st.buttons[SDL_CONTROLLER_BUTTON_X] = true;  // handbrake
        pad.PollFromState(st, c, e);
        CHECK(c.jump && c.boost && c.handbrake, "A/B/X -> jump/boost/handbrake");

        // Y is the ballcam toggle: an EDGE event, not a held state.
        auto st2 = Stick(0.0f, 0.0f);
        st2.buttons[SDL_CONTROLLER_BUTTON_Y] = true;
        pad.PollFromState(st2, c, e);
        CHECK(e.toggleBallcam, "Y press -> toggleBallcam edge");
        pad.PollFromState(st2, c, e);
        CHECK(!e.toggleBallcam, "Y held does not re-fire the edge");

        // Handbrake + steer mirrors steer onto roll (air-roll convention).
        auto st3 = Stick(-1.0f, 0.0f);
        st3.buttons[SDL_CONTROLLER_BUTTON_X] = true;
        pad.PollFromState(st3, c, e);
        CHECK(Near(c.steer, 1.0f) && Near(c.roll, 1.0f),
              "handbrake + stick-left -> roll +1 (roll-LEFT)");
    }

    std::printf("[4] hot-plug / no device\n");
    {
        rl::Gamepad fresh(def.gamepad);
        rl::Controls c;
        rl::InputEvents e;
        rl::Gamepad::State off;
        off.connected = false;
        c.throttle = 5.0f;  // sentinel: must be overwritten with neutral
        fresh.PollFromState(off, c, e);
        CHECK(c.throttle == 0.0f && c.steer == 0.0f && !c.jump && !c.boost,
              "disconnected pad -> neutral controls");
        CHECK(!fresh.connected(), "connected() reflects no device");
        CHECK(fresh.openCount() == 0, "openCount() is 0 with no device");
    }

    std::printf("[5] button-name resolution (INI + menu)\n");
    {
        CHECK(rl::Gamepad::ButtonForName("A") == SDL_CONTROLLER_BUTTON_A, "A");
        CHECK(rl::Gamepad::ButtonForName("b") == SDL_CONTROLLER_BUTTON_B, "b (case)");
        CHECK(rl::Gamepad::ButtonForName("LeftShoulder") == SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
              "LeftShoulder");
        CHECK(rl::Gamepad::ButtonForName("left_shoulder") == SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
              "left_shoulder (underscore)");
        CHECK(rl::Gamepad::ButtonForName("LB") == SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "LB alias");
        CHECK(rl::Gamepad::ButtonForName("DPadUp") == SDL_CONTROLLER_BUTTON_DPAD_UP, "DPadUp");
        CHECK(rl::Gamepad::ButtonForName("Start") == SDL_CONTROLLER_BUTTON_START, "Start");
        CHECK(rl::Gamepad::ButtonForName("nonsense") == SDL_CONTROLLER_BUTTON_INVALID,
              "unknown name -> INVALID");
        CHECK(std::strcmp(rl::Gamepad::NameForButton(SDL_CONTROLLER_BUTTON_A), "A") == 0,
              "NameForButton(A) round-trips");
    }

    std::printf("[6] defaults match the shipped INI\n");
    {
        CHECK(rl::Gamepad::ButtonForName(def.gamepad.jump) == SDL_CONTROLLER_BUTTON_A,
              "default jump = A");
        CHECK(rl::Gamepad::ButtonForName(def.gamepad.boost) == SDL_CONTROLLER_BUTTON_B,
              "default boost = B");
        CHECK(rl::Gamepad::ButtonForName(def.gamepad.handbrake) == SDL_CONTROLLER_BUTTON_X,
              "default handbrake = X");
        CHECK(Near(def.gamepad.deadzone, 0.08f), "default deadzone = 0.08");
    }

    if (failures == 0)
        std::printf("GAMEPAD PASS\n");
    else
        std::printf("GAMEPAD FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
