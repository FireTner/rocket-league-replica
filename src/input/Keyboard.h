// SDL keyboard state mapped to Controls and InputEvents.
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

    // Reads SDL_GetKeyboardState. The caller pumps events.
    void Poll(Controls& controls, InputEvents& events);

    // Same mapping over an injected scancode array.
    void PollFromState(const Uint8* state, int numKeys, Controls& controls,
                       InputEvents& events);

    // Returns SDL_NUM_SCANCODES when the name is unknown.
    static SDL_Scancode ScancodeForName(const std::string& name);

   private:
    bool Down(const Uint8* state, int numKeys, const std::vector<std::string>& keys) const;
    bool Pressed(const Uint8* prev, const Uint8* cur, int numKeys,
                 const std::vector<std::string>& keys) const;

    InputBindings bindings_;
    Uint8 prev_[SDL_NUM_SCANCODES] = {};
    bool first_ = true;
};

}  // namespace rl
