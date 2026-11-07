// Keyboard mapping onto Controls.
#include "Keyboard.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace rl {

Keyboard::Keyboard(const InputBindings& bindings) : bindings_(bindings) {}

SDL_Scancode Keyboard::ScancodeForName(const std::string& name) {
    // Single letters (case-insensitive) map to SDL_SCANCODE_<X>.
    if (name.size() == 1 && std::isalpha((unsigned char)name[0])) {
        char up = (char)std::toupper((unsigned char)name[0]);
        if (up >= 'A' && up <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (up - 'A'));
    }
    if (name.size() == 1 && std::isdigit((unsigned char)name[0])) {
        if (name[0] == '0') return SDL_SCANCODE_0;
        return (SDL_Scancode)(SDL_SCANCODE_1 + (name[0] - '1'));  // '1'..'9'
    }
    std::string l = name;
    for (auto& c : l) c = (char)std::tolower((unsigned char)c);
    if (l == "up") return SDL_SCANCODE_UP;
    if (l == "down") return SDL_SCANCODE_DOWN;
    if (l == "left") return SDL_SCANCODE_LEFT;
    if (l == "right") return SDL_SCANCODE_RIGHT;
    if (l == "space") return SDL_SCANCODE_SPACE;
    if (l == "leftshift" || l == "shift") return SDL_SCANCODE_LSHIFT;
    if (l == "rightshift") return SDL_SCANCODE_RSHIFT;
    if (l == "leftctrl" || l == "ctrl" || l == "control") return SDL_SCANCODE_LCTRL;
    if (l == "rightctrl") return SDL_SCANCODE_RCTRL;
    if (l == "escape" || l == "esc") return SDL_SCANCODE_ESCAPE;
    if (l == "return" || l == "enter") return SDL_SCANCODE_RETURN;
    if (l == "tab") return SDL_SCANCODE_TAB;
    if (l == "delete" || l == "del") return SDL_SCANCODE_DELETE;
    if (l == "q") return SDL_SCANCODE_Q;
    if (l == "e") return SDL_SCANCODE_E;
    if ((l[0] == 'f' || l[0] == 'F') && l.size() <= 3) {
        int n = std::atoi(l.c_str() + 1);
        if (n >= 1 && n <= 12) return (SDL_Scancode)(SDL_SCANCODE_F1 + (n - 1));
    }
    // Fall back to SDL's own name lookup (handles the rest, e.g. "0".."9").
    SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    return sc;
}

bool Keyboard::Down(const Uint8* state, int numKeys, const std::vector<std::string>& keys) const {
    for (const auto& k : keys) {
        SDL_Scancode sc = ScancodeForName(k);
        if (sc != SDL_NUM_SCANCODES && sc >= 0 && sc < numKeys && state[sc]) return true;
    }
    return false;
}

bool Keyboard::Pressed(const Uint8* prev, const Uint8* cur, int numKeys,
                       const std::vector<std::string>& keys) const {
    for (const auto& k : keys) {
        SDL_Scancode sc = ScancodeForName(k);
        if (sc != SDL_NUM_SCANCODES && sc >= 0 && sc < numKeys && cur[sc] && !prev[sc]) return true;
    }
    return false;
}

void Keyboard::Poll(Controls& controls, InputEvents& events) {
    int numKeys = 0;
    const Uint8* state = SDL_GetKeyboardState(&numKeys);
    PollFromState(state, numKeys, controls, events);
}

void Keyboard::PollFromState(const Uint8* state, int numKeys, Controls& controls,
                             InputEvents& events) {
    if (!state || numKeys <= 0) {
        controls = Controls();
        events = InputEvents();
        return;
    }

    auto held = [&](const std::vector<std::string>& k) { return Down(state, numKeys, k); };

    // axis() = (slotA held) - (slotB held): +1 when slotA dominates.
    auto axis = [&](const AxisBinding& b) {
        float pos = Down(state, numKeys, b.slotA) ? 1.0f : 0.0f;
        float neg = Down(state, numKeys, b.slotB) ? 1.0f : 0.0f;
        return pos - neg;
    };

    controls.throttle = axis(bindings_.throttle);
    // W is throttle +1 and pitch -1 (nose down). Steer +1 is left.
    controls.steer = axis(bindings_.steer);
    controls.pitch = -axis(bindings_.pitch);
    controls.yaw = axis(bindings_.yaw);
    // Roll +1 rolls left. Q is left, E is right.
    controls.roll = axis(bindings_.roll);
    controls.jump = held(bindings_.jump);
    controls.boost = held(bindings_.boost);
    bool handbrakeHeld = held(bindings_.handbrake);
    controls.handbrake = handbrakeHeld;

    // Handbrake copies steer into roll. Steer +1 is left, so roll +1 rolls left.
    if (handbrakeHeld) controls.roll = controls.steer;

    // Edge-triggered events (skip on the very first poll: no previous state).
    events = InputEvents();
    if (!first_) {
        const Uint8* prev = prev_;
        auto pressed = [&](const std::vector<std::string>& k) {
            return Pressed(prev, state, numKeys, k);
        };
        events.toggleBallcam = pressed(bindings_.ballcamToggle);
        events.resetCar = pressed(bindings_.resetCar);
        events.resetBall = pressed(bindings_.resetBall);
        events.toggleUnlimitedBoost = pressed(bindings_.unlimited);
        events.openMenu = pressed(bindings_.menu);
        for (int i = 0; i < 5; ++i) {
            if (pressed(bindings_.kickoff[i])) {
                events.kickoffIndex = i;
                break;
            }
        }
    }
    // Snapshot current state for next frame's edge detection.
    int n = std::min(numKeys, (int)SDL_NUM_SCANCODES);
    std::memcpy(prev_, state, (size_t)n);
    first_ = false;

}

}  // namespace rl
