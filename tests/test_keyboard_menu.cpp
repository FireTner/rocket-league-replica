// A5 contract test: headless keyboard mapping + menu behaviour.
// Build: g++ -std=c++20 -I src tests/test_keyboard_menu.cpp
//        src/settings/Settings.cpp src/input/Keyboard.cpp src/menu/Menu.cpp
//        $(pkg-config --cflags --libs sdl2) -o /tmp/t_km && /tmp/t_km
// Uses only SDL scancode/keycode constants + name lookups (no video init).
#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "input/Controls.h"
#include "input/Keyboard.h"
#include "menu/Menu.h"
#include "settings/Settings.h"

static int failures = 0;
#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (cond) {                                             \
            std::printf("  ok   %s\n", msg);                    \
        } else {                                                \
            std::printf("  FAIL %s\n", msg);                    \
            ++failures;                                         \
        }                                                       \
    } while (0)

// Fake SDL keyboard state buffer.
struct FakeKb {
    Uint8 st[SDL_NUM_SCANCODES];
    FakeKb() { std::memset(st, 0, sizeof(st)); }
    void set(const char* name) {
        SDL_Scancode sc = rl::Keyboard::ScancodeForName(name);
        if (sc >= 0 && sc < SDL_NUM_SCANCODES) st[(int)sc] = 1;
    }
    void clear() { std::memset(st, 0, sizeof(st)); }
};

int main() {
    rl::Settings def = rl::Settings::Defaults();
    rl::Keyboard kb(def.input);
    rl::Controls c;
    rl::InputEvents e;
    FakeKb k;

    std::printf("[1] drive axes\n");
    k.set("W");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);  // first poll: no edges
    CHECK(c.throttle == 1.0f, "W -> throttle +1");
    CHECK(c.pitch == -1.0f, "W -> pitch -1 (nose DOWN, RL-correct)");
    k.clear();
    k.set("S");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(c.throttle == -1.0f, "S -> throttle -1");
    CHECK(c.handbrake == false, "S no longer handbrake (Shift is powerslide)");
    k.clear();
    k.set("A");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(c.steer == 1.0f, "A -> steer +1 (LEFT, tape canonical)");
    CHECK(c.yaw == 1.0f, "A -> yaw +1 (left)");
    k.clear();
    k.set("Right");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(c.steer == -1.0f, "Right arrow -> steer -1 (right)");
    k.clear();
    k.set("Q");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(c.roll == 1.0f, "Q -> roll +1 (roll-LEFT, MEASURED air_roll_300)");
    k.clear();
    k.set("E");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(c.roll == -1.0f, "E -> roll -1 (roll-right)");
    k.clear();
    k.set("L");
    k.set("K");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(c.jump && c.boost, "L+K -> jump+boost held");
    CHECK(!c.handbrake, "handbrake released");
    k.clear();
    k.set("LeftShift");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(c.handbrake, "Shift -> handbrake held (powerslide)");

    std::printf("[2] air-roll modifier (handbrake mirrors steer onto roll)\n");
    k.clear();
    k.set("LeftShift");
    k.set("D");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(c.handbrake && c.steer == -1.0f && c.roll == -1.0f,
          "Shift+D -> handbrake + steer -1 (right) + roll -1 (roll follows steer sign)");

    std::printf("[3] edge-triggered events\n");
    k.clear();
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);  // settle: nothing pressed
    CHECK(!e.toggleBallcam && e.kickoffIndex == -1, "no press -> no events");
    k.set("Space");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(e.toggleBallcam, "Space press -> toggleBallcam");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(!e.toggleBallcam, "Space held -> no repeat event");
    k.clear();
    k.set("C");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(e.resetBall && !e.resetCar, "C press -> resetBall only");
    k.clear();
    k.set("3");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(e.kickoffIndex == 2, "3 press -> kickoffIndex 2 (ball drill)");
    k.clear();
    k.set("R");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(e.resetCar && !e.resetBall, "R press -> resetCar only");
    k.clear();
    k.set("U");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(e.toggleUnlimitedBoost, "U press -> toggleUnlimitedBoost");
    k.clear();
    k.set("Escape");
    kb.PollFromState(k.st, SDL_NUM_SCANCODES, c, e);
    CHECK(e.openMenu, "Escape press -> openMenu");

    std::printf("[4] ScancodeForName coverage of default bindings\n");
    const char* names[] = {"W", "A",   "S",   "D",   "Q",     "E",  "Up",   "Down",
                           "Left",     "Right", "Space", "LeftShift", "RightShift", "C",
                           "R",        "K",   "L",   "U",   "J",   "Escape", "1", "2",  "3", "4",
                           "5"};
    bool allKnown = true;
    for (auto n : names) {
        if (rl::Keyboard::ScancodeForName(n) == SDL_NUM_SCANCODES) {
            std::printf("  unknown key name: %s\n", n);
            allKnown = false;
        }
    }
    CHECK(allKnown, "all default key names resolve to scancodes");

    std::printf("[5] menu: navigation + bool toggle + float adjust\n");
    rl::Settings ms = rl::Settings::Defaults();
    ms.camera.fov = 100.0f;  // mid-range: FOV max is the RL-correct 110
    int resets = 0, spawns = -1;
    rl::FreePlayActions acts;
    acts.resetCar = [&]() { ++resets; };
    acts.spawnKickoff = [&](int i) { spawns = i; };
    rl::Menu menu(ms, acts);
    menu.Open();
    CHECK(menu.IsOpen(), "menu opens");
    // FOV item is index 1 (0 = version info, 1 = CAMERA header? no: 0 info, 1 header, 2 fov).
    // Walk down to ballcam bool and toggle it via Enter.
    int nulls = 0;
    for (auto& l : menu.RenderText()) {
        if (!l.empty()) ++nulls;
    }
    CHECK(nulls > 40, "menu renders >40 lines (all schema keys + actions)");
    bool sawFov = false, sawKickoff = false, sawUnlimited = false;
    for (auto& l : menu.RenderText()) {
        if (l.find("FOV") != std::string::npos) sawFov = true;
        if (l.find("Kickoff:back-right") != std::string::npos) sawKickoff = true;
        if (l.find("Unlimited boost") != std::string::npos) sawUnlimited = true;
    }
    CHECK(sawFov && sawKickoff && sawUnlimited, "menu lists camera/input/game keys");
    float fovBefore = ms.camera.fov;
    menu.HandleKey(SDLK_RIGHT);  // adjust first selectable (FOV +1)
    // selection starts on FOV row (index 2): verify.
    CHECK(ms.camera.fov == fovBefore + 1.0f, "Right on FOV row -> +1");
    menu.HandleKey(SDLK_LEFT);
    menu.HandleKey(SDLK_LEFT);
    CHECK(ms.camera.fov == fovBefore - 1.0f, "Left x2 -> net -1");

    std::printf("[6] menu: free-play shortcuts + capture mode\n");
    menu.HandleKey(SDLK_r);
    CHECK(resets == 1, "R in menu -> resetCar action");
    menu.HandleKey(SDLK_4);
    CHECK(spawns == 3 && ms.game.kickoffIndex == 3, "4 in menu -> drill 3 + kickoff_index=3");
    bool ballBefore = ms.camera.ballcam;
    // Space is the menu ACTIVATE key, so the ballcam bool row is toggled
    // via activation (gameplay Space-toggle is handled with menu closed).
    for (int i = 0; i < 60; ++i) {
        bool onBall = false;
        for (auto& l : menu.RenderText())
            if (l.rfind("> ", 0) == 0 && l.find("Ball cam") != std::string::npos) onBall = true;
        if (onBall) break;
        menu.HandleKey(SDLK_DOWN);
    }
    menu.HandleKey(SDLK_SPACE);
    CHECK(ms.camera.ballcam == !ballBefore, "Space on Ball-cam row flips settings ballcam");
    // Navigate to a Binding row (Jump) and capture a new key.
    // Walk: find "Jump" row by moving down until value line matches.
    for (int i = 0; i < 60; ++i) {
        bool onJump = false;
        auto lines = menu.RenderText();
        // selected line starts with "> "
        for (auto& l : lines)
            if (l.rfind("> ", 0) == 0 && l.find("Jump") != std::string::npos) onJump = true;
        if (onJump) break;
        menu.HandleKey(SDLK_DOWN);
    }
    menu.HandleKey(SDLK_RETURN);
    CHECK(menu.IsCapturing(), "Enter on Jump binding -> capturing");
    menu.HandleKey(SDLK_j);
    CHECK(!menu.IsCapturing() && ms.input.jump.size() == 1 && ms.input.jump[0] == "J",
          "pressing J rebinds jump primary to J");
    menu.HandleKey(SDLK_ESCAPE);
    CHECK(!menu.IsOpen(), "Esc closes menu");

    std::printf("[7] gamepad binding rows + capture\n");
    {
        menu.Open();
        // Walk to the "Pad: Jump" row.
        for (int i = 0; i < 90; ++i) {
            bool found = false;
            for (auto& l : menu.RenderText())
                if (l.rfind("> ", 0) == 0 && l.find("Pad: Jump") != std::string::npos)
                    found = true;
            if (found) break;
            menu.HandleKey(SDLK_DOWN);
        }
        bool onPadJump = false;
        for (auto& l : menu.RenderText())
            if (l.rfind("> ", 0) == 0 && l.find("Pad: Jump") != std::string::npos)
                onPadJump = true;
        CHECK(onPadJump, "menu exposes a 'Pad: Jump' row");

        menu.HandleKey(SDLK_RETURN);
        CHECK(menu.IsCapturing() && menu.captureIsPad(),
              "Enter on a pad row -> pad capture");
        CHECK(menu.CapturePrompt().find("pad button") != std::string::npos,
              "capture prompt asks for a pad button");
        // A key press must NOT satisfy a pad capture.
        menu.HandleKey(SDLK_k);
        CHECK(menu.IsCapturing(), "key press ignored during pad capture");
        menu.HandleControllerButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        CHECK(!menu.IsCapturing() &&
                  ms.gamepad.jump == "LeftShoulder",
              "pad button rebinds Pad: Jump to LeftShoulder");

        // And a pad capture is cancellable with Esc.
        menu.HandleKey(SDLK_RETURN);
        CHECK(menu.captureIsPad(), "re-enter pad capture");
        menu.HandleKey(SDLK_ESCAPE);
        CHECK(!menu.IsCapturing(), "Esc cancels a pad capture");
        menu.Close();
    }

    std::printf("[8] menu lists every gamepad schema key\n");
    {
        menu.Open();
        const char* want[] = {"Pad: Jump", "Pad: Boost", "Pad: Powerslide",
                              "Pad: Ballcam", "Pad: Reset car", "Pad: Reset ball",
                              "Pad: Unlimited boost", "Pad: Menu", "Stick deadzone"};
        auto lines = menu.RenderText();
        std::string all;
        for (auto& l : lines) all += l + "\n";
        for (const char* w : want)
            CHECK(all.find(w) != std::string::npos, w);
        menu.Close();
    }

    std::printf(failures == 0 ? "KEYBOARD+MENU PASS\n" : "KEYBOARD+MENU FAIL\n");
    return failures == 0 ? 0 : 1;
}
