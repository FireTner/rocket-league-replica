// A5-owned: gamepad mapping implementation. MIT License.
#include "Gamepad.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace rl {
namespace {

// SDL axis range: signed sticks are [-32768, 32767]; triggers [0, 32767].
constexpr float kAxisMax = 32767.0f;
constexpr float kAxisMin = 32768.0f;  // magnitude of the negative extreme

float NormSigned(Sint16 v) {
    float f = (v < 0) ? (float(v) / kAxisMin) : (float(v) / kAxisMax);
    return std::max(-1.0f, std::min(1.0f, f));
}

float NormTrigger(Sint16 v) {
    if (v <= 0) return 0.0f;
    return std::min(1.0f, float(v) / kAxisMax);
}

}  // namespace

Gamepad::DeadzoneResult Gamepad::ApplyDeadzone(float x, float y, float deadzone) {
    DeadzoneResult r;
    // Clamp the raw inputs to the unit square first so a device that reports
    // slightly out-of-range values cannot push the direction off.
    x = std::max(-1.0f, std::min(1.0f, x));
    y = std::max(-1.0f, std::min(1.0f, y));
    float mag = std::sqrt(x * x + y * y);
    r.magnitude = mag;
    if (deadzone < 0.0f) deadzone = 0.0f;
    if (deadzone > 0.999f) deadzone = 0.999f;
    if (mag <= deadzone || mag <= 1e-6f) {
        // Inside the deadzone (or exactly centred): neutral, no direction.
        r.x = 0.0f;
        r.y = 0.0f;
        return r;
    }
    // Rescale the magnitude linearly from [deadzone, 1] onto [0, 1] so the
    // response is CONTINUOUS at the deadzone edge (no jump) and reaches full
    // deflection at the stick's edge. Direction is preserved exactly.
    float scaled = (mag - deadzone) / (1.0f - deadzone);
    if (scaled > 1.0f) scaled = 1.0f;
    float inv = scaled / mag;
    r.x = x * inv;
    r.y = y * inv;
    // Guard against floating-point drift outside the unit disc.
    float outMag = std::sqrt(r.x * r.x + r.y * r.y);
    if (outMag > 1.0f) {
        r.x /= outMag;
        r.y /= outMag;
    }
    return r;
}

void Gamepad::SetBindings(const GamepadBindings& b) {
    bindings_ = b;
    bound_ = BoundButtons();
    bound_.deadzone = b.deadzone;
    bound_.jump = ButtonForName(b.jump);
    bound_.boost = ButtonForName(b.boost);
    bound_.handbrake = ButtonForName(b.handbrake);
    bound_.ballcamToggle = ButtonForName(b.ballcamToggle);
    bound_.resetCar = ButtonForName(b.resetCar);
    bound_.resetBall = ButtonForName(b.resetBall);
    bound_.unlimited = ButtonForName(b.unlimited);
    bound_.menu = ButtonForName(b.menu);
    for (int i = 0; i < 5; ++i) bound_.kickoff[i] = ButtonForName(b.kickoff[i]);
}

void Gamepad::RefreshState() {
    State s;
    s.connected = false;
    for (int i = 0; i < 4; i++) {
        SDL_GameController* gc = controllers_[i];
        if (!gc || !SDL_GameControllerGetAttached(gc))
            continue;
        s.connected = true;
        // First attached controller wins for the shared per-axis values; all
        // attached controllers contribute buttons (any-pad-is-pressed).
        if (i == 0 || !s.connected) {
            s.leftX = NormSigned(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX));
            s.leftY = NormSigned(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY));
            s.rightX = NormSigned(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
            s.rightY = NormSigned(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
            s.leftTrigger = NormTrigger(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
            s.rightTrigger = NormTrigger(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        }
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b))
                s.buttons[b] = true;
        }
    }
    state_ = s;
}

bool Gamepad::HandleDeviceEvent(const SDL_Event& e) {
    auto open = [&](int deviceIndex) {
        if (deviceIndex < 0 || deviceIndex >= 4 || controllers_[deviceIndex])
            return;
        SDL_GameController* gc = SDL_GameControllerOpen(deviceIndex);
        if (!gc)
            return;
        controllers_[deviceIndex] = gc;
        openCount_++;
    };
    auto close = [&](SDL_JoystickID id) {
        for (int i = 0; i < 4; i++) {
            SDL_GameController* gc = controllers_[i];
            if (!gc)
                continue;
            SDL_Joystick* j = SDL_GameControllerGetJoystick(gc);
            if (j && SDL_JoystickInstanceID(j) == id) {
                SDL_GameControllerClose(gc);
                controllers_[i] = nullptr;
                openCount_--;
                return;
            }
        }
    };
    bool changed = false;
    switch (e.type) {
    case SDL_CONTROLLERDEVICEADDED:
        open(e.cdevice.which);
        changed = true;
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        close(e.cdevice.which);
        changed = true;
        break;
    default:
        break;
    }
    if (changed)
        RefreshState();
    return changed;
}

bool Gamepad::Poll(Controls& controls, InputEvents& events) {
    RefreshState();
    PollFromState(state_, controls, events);
    return state_.connected;
}

void Gamepad::PollFromState(const State& st, Controls& controls, InputEvents& events) {
    Controls c;
    if (!st.connected) {
        // No device: neutral controls. The caller decides whether the keyboard
        // or the pad owns the frame (device selection is additive).
        controls = c;
        std::memset(prevButtons_, 0, sizeof(prevButtons_));
        std::memset(newPresses_, 0, sizeof(newPresses_));
        firstPoll_ = false;
        events = InputEvents();
        return;
    }

    const float dz = bound_.deadzone;
    DeadzoneResult stick = ApplyDeadzone(st.leftX, st.leftY, dz);
    // Sign contract: throttle +1 = forward (stick up = +Y), steer +1 = LEFT
    // (stick right = +X, so steer is the negated X). Zero negation elsewhere.
    c.throttle = stick.y;
    c.steer = -stick.x;

    auto held = [&](SDL_GameControllerButton b) {
        return b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX && st.buttons[b];
    };
    c.jump = held(bound_.jump);
    c.boost = held(bound_.boost);
    c.handbrake = held(bound_.handbrake);

    // Air roll modifier (handbrake + steer -> roll), mirroring the keyboard
    // convention: roll follows the steer SIGN (steer +1 = LEFT, roll +1 =
    // roll-LEFT, MEASURED air_roll_300). Ground frames ignore roll.
    if (c.handbrake)
        c.roll = c.steer;

    events = InputEvents();
    if (!firstPoll_) {
        auto pressed = [&](SDL_GameControllerButton b) {
            return b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX && st.buttons[b] &&
                   !prevButtons_[b];
        };
        events.toggleBallcam = pressed(bound_.ballcamToggle);
        events.resetCar = pressed(bound_.resetCar);
        events.resetBall = pressed(bound_.resetBall);
        events.toggleUnlimitedBoost = pressed(bound_.unlimited);
        events.openMenu = pressed(bound_.menu);
        for (int i = 0; i < 5; ++i) {
            if (pressed(bound_.kickoff[i])) {
                events.kickoffIndex = i;
                break;
            }
        }
    }
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        newPresses_[b] = st.buttons[b] && !prevButtons_[b];
        prevButtons_[b] = st.buttons[b];
    }
    firstPoll_ = false;
    controls = c;
}

int Gamepad::TakeNewPresses(SDL_GameControllerButton* out, int max) const {
    int n = 0;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX && n < max; b++)
        if (newPresses_[b]) out[n++] = (SDL_GameControllerButton)b;
    return n;
}

SDL_GameControllerButton Gamepad::ButtonForName(const std::string& name) {
    std::string l = name;
    for (auto& ch : l) ch = (char)std::tolower((unsigned char)ch);
    // Strip separators so "left_shoulder", "left-shoulder", "LeftShoulder"
    // all resolve identically in the INI.
    std::string k;
    for (char ch : l)
        if (std::isalnum((unsigned char)ch)) k.push_back(ch);
    if (k == "a") return SDL_CONTROLLER_BUTTON_A;
    if (k == "b") return SDL_CONTROLLER_BUTTON_B;
    if (k == "x") return SDL_CONTROLLER_BUTTON_X;
    if (k == "y") return SDL_CONTROLLER_BUTTON_Y;
    if (k == "back" || k == "select") return SDL_CONTROLLER_BUTTON_BACK;
    if (k == "guide") return SDL_CONTROLLER_BUTTON_GUIDE;
    if (k == "start") return SDL_CONTROLLER_BUTTON_START;
    if (k == "leftstick" || k == "l3") return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    if (k == "rightstick" || k == "r3") return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    if (k == "leftshoulder" || k == "lb" || k == "l1") return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if (k == "rightshoulder" || k == "rb" || k == "r1") return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if (k == "dpup" || k == "dpadup") return SDL_CONTROLLER_BUTTON_DPAD_UP;
    if (k == "dpdown" || k == "dpaddown") return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if (k == "dpleft" || k == "dpadleft") return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if (k == "dpright" || k == "dpadright") return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    return SDL_CONTROLLER_BUTTON_INVALID;
}

const char* Gamepad::NameForButton(SDL_GameControllerButton b) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return "A";
    case SDL_CONTROLLER_BUTTON_B: return "B";
    case SDL_CONTROLLER_BUTTON_X: return "X";
    case SDL_CONTROLLER_BUTTON_Y: return "Y";
    case SDL_CONTROLLER_BUTTON_BACK: return "Back";
    case SDL_CONTROLLER_BUTTON_GUIDE: return "Guide";
    case SDL_CONTROLLER_BUTTON_START: return "Start";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "LeftStick";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "RightStick";
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "LeftShoulder";
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "RightShoulder";
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return "DPadUp";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "DPadDown";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "DPadLeft";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "DPadRight";
    default: return "?";
    }
}

}  // namespace rl
