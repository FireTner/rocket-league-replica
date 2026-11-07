// SDL game controller mapped to Controls and InputEvents.
// Stick up is throttle +1; stick right is steer -1. Steer +1 is left.
#pragma once

#include <SDL2/SDL.h>

#include <string>

#include "Controls.h"
#include "../settings/Settings.h"

namespace rl {

class Gamepad {
   public:
    // Raw SDL sticks are [-32768, 32767]; triggers are [0, 32767].
    struct State {
        float leftX = 0.0f;   // -1..1, +1 = right
        float leftY = 0.0f;   // -1..1, +1 = up
        float rightX = 0.0f;
        float rightY = 0.0f;
        float leftTrigger = 0.0f;   // 0..1
        float rightTrigger = 0.0f;  // 0..1
        bool buttons[SDL_CONTROLLER_BUTTON_MAX] = {};
        bool connected = false;
    };

    Gamepad() = default;
    explicit Gamepad(const GamepadBindings& bindings) { SetBindings(bindings); }

    // Resolved once in SetBindings, not on each poll.
    struct BoundButtons {
        float deadzone = 0.08f;
        SDL_GameControllerButton jump = SDL_CONTROLLER_BUTTON_INVALID;
        SDL_GameControllerButton boost = SDL_CONTROLLER_BUTTON_INVALID;
        SDL_GameControllerButton handbrake = SDL_CONTROLLER_BUTTON_INVALID;
        SDL_GameControllerButton ballcamToggle = SDL_CONTROLLER_BUTTON_INVALID;
        SDL_GameControllerButton resetCar = SDL_CONTROLLER_BUTTON_INVALID;
        SDL_GameControllerButton resetBall = SDL_CONTROLLER_BUTTON_INVALID;
        SDL_GameControllerButton unlimited = SDL_CONTROLLER_BUTTON_INVALID;
        SDL_GameControllerButton menu = SDL_CONTROLLER_BUTTON_INVALID;
        SDL_GameControllerButton kickoff[5] = {};
    };

    void SetBindings(const GamepadBindings& b);
    const GamepadBindings& bindings() const { return bindings_; }
    const BoundButtons& bounds() const { return bound_; }

    // Radial deadzone on the stick vector, so a diagonal is not clipped to a square.
    struct DeadzoneResult {
        float x = 0.0f;
        float y = 0.0f;
        float magnitude = 0.0f;  // raw stick magnitude, 0..1
    };
    static DeadzoneResult ApplyDeadzone(float x, float y, float deadzone);

    // Returns false when no controller is attached. Outputs are then neutral.
    bool Poll(Controls& controls, InputEvents& events);

    void PollFromState(const State& st, Controls& controls, InputEvents& events);

    // Right stick, +X right and +Y up. Does not change Controls.
    void RightStick(float& x, float& y) const {
        x = lastRightX_;
        y = lastRightY_;
    }

    // Buttons that went down this poll. Returns how many were written to out.
    int TakeNewPresses(SDL_GameControllerButton* out, int max) const;

    // True when the set of open controllers changed. A disconnect yields neutral controls.
    bool HandleDeviceEvent(const SDL_Event& e);
    bool connected() const { return state_.connected; }
    int openCount() const { return openCount_; }

    // Returns SDL_CONTROLLER_BUTTON_INVALID when the name is unknown.
    static SDL_GameControllerButton ButtonForName(const std::string& name);
    static const char* NameForButton(SDL_GameControllerButton b);

   private:
    GamepadBindings bindings_;
    BoundButtons bound_;
    State state_;
    float lastRightX_ = 0.0f;  // +X right
    float lastRightY_ = 0.0f;  // +Y up
    SDL_GameController* controllers_[4] = {nullptr, nullptr, nullptr, nullptr};
    int openCount_ = 0;
    bool prevButtons_[SDL_CONTROLLER_BUTTON_MAX] = {};
    bool newPresses_[SDL_CONTROLLER_BUTTON_MAX] = {};
    bool firstPoll_ = true;
    void RefreshState();
};

}  // namespace rl
