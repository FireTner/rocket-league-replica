// A5-owned: SDL2 game controller -> Controls + InputEvents.
//
// Same shape as Keyboard: Poll() reads live hardware; PollFromState() applies
// the identical mapping over an injected controller state so gate tapes and
// contract tests run headless with no device and no SDL video init.
//
// Sign contract (unchanged, TAPE_FORMAT.md §2 / Controls.h): the mapping is a
// VERBATIM passthrough into Controls with ZERO negation. RL's left stick is
// +X = right, +Y = up, and RL's throttle is +1 = forward, steer is +1 = LEFT
// (MEASURED air_roll_300 / steer_left_360). So:
//     throttle = +stickY     (stick up  -> forward)
//     steer    = -stickX     (stick right -> -1 = right)
// Both axes pass through the SAME radial deadzone and rescale.
//
// MIT License. Depends on SDL2 + Controls.h + Settings.h.
#pragma once

#include <SDL2/SDL.h>

#include <string>

#include "Controls.h"
#include "../settings/Settings.h"

namespace rl {

class Gamepad {
   public:
    // Normalized stick/trigger sample. Raw SDL range is [-32768, 32767] for
    // axes and [0, 32767] for triggers.
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

    // Bindings are stored as SDL button enums, resolved from the INI/menu
    // names by ButtonForName() once per SetBindings (not per poll).
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

    // Radial deadzone parameters. Applied to the left stick as a VECTOR, not
    // per-axis, so a diagonal push is not clipped into a square.
    struct DeadzoneResult {
        float x = 0.0f;
        float y = 0.0f;
        float magnitude = 0.0f;  // raw stick magnitude, 0..1
    };
    // Pure function (unit-tested, deterministic):
    //  - reads the raw stick magnitude
    //  - returns (0,0) below `deadzone`
    //  - rescales the remainder so output magnitude runs 0..1 continuously
    //    (no jump at the deadzone edge), preserving the input DIRECTION
    //  - clamps the result to the unit disc
    static DeadzoneResult ApplyDeadzone(float x, float y, float deadzone);

    // Sample live hardware and produce controls+events in one call. Pumps
    // nothing; the main loop owns SDL_PumpEvents. Returns false when no
    // controller is attached (outputs are left neutral/cleared).
    bool Poll(Controls& controls, InputEvents& events);

    // Same mapping over an injected state (headless tests + synthetic tapes).
    void PollFromState(const State& st, Controls& controls, InputEvents& events);

    // Right-stick look (camera swivel). Abstract State convention: +X = right,
    // +Y = up (same convention as the left stick). Updated by both Poll()
    // (live hardware) and PollFromState() (headless). Pure addition: the
    // Controls passthrough above is untouched.
    void RightStick(float& x, float& y) const {
        x = lastRightX_;
        y = lastRightY_;
    }

    // Buttons that went down THIS poll (edge). Used by the menu to capture a
    // pad rebinding; the driving path consumes edges through InputEvents.
    // Returns a count; fills `out` with up to `max` button ids.
    int TakeNewPresses(SDL_GameControllerButton* out, int max) const;

    // Hot-plug bookkeeping. Returns true when the set of open controllers
    // changed (so the caller can log it). Never drops a tick: a disconnect
    // simply yields neutral controls.
    bool HandleDeviceEvent(const SDL_Event& e);
    bool connected() const { return state_.connected; }
    int openCount() const { return openCount_; }

    // "a" / "b" / "x" / "y" / "leftshoulder" / "rightshoulder" / "start" ...
    // -> SDL_GameControllerButton. Returns SDL_CONTROLLER_BUTTON_INVALID when
    // the name is unknown.
    static SDL_GameControllerButton ButtonForName(const std::string& name);
    static const char* NameForButton(SDL_GameControllerButton b);

   private:
    GamepadBindings bindings_;
    BoundButtons bound_;
    State state_;
    float lastRightX_ = 0.0f;  // swivel look, abstract convention (+X right)
    float lastRightY_ = 0.0f;  // (+Y up); refreshed every poll, both paths
    SDL_GameController* controllers_[4] = {nullptr, nullptr, nullptr, nullptr};
    int openCount_ = 0;
    // Previous button state for edge detection (toggle events).
    bool prevButtons_[SDL_CONTROLLER_BUTTON_MAX] = {};
    bool newPresses_[SDL_CONTROLLER_BUTTON_MAX] = {};
    bool firstPoll_ = true;
    void RefreshState();
};

}  // namespace rl
