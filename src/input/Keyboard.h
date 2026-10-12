// A5-owned: SDL2 keyboard -> Controls + InputEvents.
// Poll() reads live hardware state; PollFromState() implements the same
// mapping over an injected scancode array so tests run headless.
// Gamepad: a real Gamepad (src/input/Gamepad.h) can be attached; when it is
// connected its analog stick owns throttle/steer and its buttons are OR-ed
// with the keyboard. Keyboard-only behaviour is unchanged when none is set.
// MIT License. Depends on SDL2 + src/input/Controls.h + src/settings/Settings.h.
#pragma once

#include <SDL2/SDL.h>

#include <functional>
#include <string>

#include "Controls.h"
#include "../settings/Settings.h"

namespace rl {

class Keyboard {
   public:
    explicit Keyboard(const InputBindings& bindings = Settings::Defaults().input);

    void SetBindings(const InputBindings& b) { bindings_ = b; }
    const InputBindings& bindings() const { return bindings_; }

    // Sample live SDL keyboard state. Pumps events? No — the main loop owns
    // SDL_PumpEvents; Poll only reads SDL_GetKeyboardState. Edge-triggered
    // InputEvents are derived against the previous Poll's state.
    void Poll(Controls& controls, InputEvents& events);

    // Same mapping, testable headlessly. `state` is indexed by SDL_Scancode
    // (as returned by SDL_GetKeyboardState); `numKeys` is its length.
    void PollFromState(const Uint8* state, int numKeys, Controls& controls,
                       InputEvents& events);

    // "W" / "Up" / "LeftShift" / "Space" / "Escape" / "F1" -> SDL_Scancode.
    // Returns SDL_NUM_SCANCODES when unknown.
    static SDL_Scancode ScancodeForName(const std::string& name);

   private:
    bool Down(const Uint8* state, int numKeys, const std::vector<std::string>& keys) const;
    bool Pressed(const Uint8* prev, const Uint8* cur, int numKeys,
                 const std::vector<std::string>& keys) const;

    InputBindings bindings_;
    // Previous-frame snapshot for edge detection, sized SDL_NUM_SCANCODES.
    Uint8 prev_[SDL_NUM_SCANCODES] = {};
    bool first_ = true;
};

}  // namespace rl
