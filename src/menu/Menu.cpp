// A5-owned: menu implementation. MIT License.
#include "Menu.h"
#include "../input/Gamepad.h"

#include <cstdio>

namespace rl {

Menu::Menu(Settings& settings, FreePlayActions actions)
    : settings_(settings), actions_(actions) {
    BuildItems();
    MoveSelection(0);  // land on first selectable row
}

void Menu::AddHeader(const std::string& title) {
    items_.push_back(Item{"", title, ItemKind::Header, nullptr, nullptr, nullptr});
}

void Menu::AddBool(const std::string& id, const std::string& label, bool& ref) {
    Item it;
    it.id = id;
    it.label = label;
    it.kind = ItemKind::Bool;
    it.value = [&ref]() { return ref ? "On" : "Off"; };
    it.adjust = [&ref](int) { ref = !ref; };
    it.activate = [&ref]() { ref = !ref; };
    items_.push_back(it);
}

void Menu::AddFloat(const std::string& id, const std::string& label, float& ref, float min,
                    float max, float step) {
    Item it;
    it.id = id;
    it.label = label;
    it.kind = ItemKind::Float;
    it.value = [&ref]() {
        char b[32];
        std::snprintf(b, sizeof(b), "%.4g", (double)ref);
        return std::string(b);
    };
    it.adjust = [&ref, min, max, step](int dir) {
        ref += (float)dir * step;
        if (ref < min) ref = min;
        if (ref > max) ref = max;
    };
    items_.push_back(it);
}

void Menu::AddInt(const std::string& id, const std::string& label, int& ref, int min, int max,
                  int step) {
    Item it;
    it.id = id;
    it.label = label;
    it.kind = ItemKind::Int;
    it.value = [&ref]() { return std::to_string(ref); };
    it.adjust = [&ref, min, max, step](int dir) {
        ref += dir * step;
        if (ref < min) ref = min;
        if (ref > max) ref = max;
    };
    items_.push_back(it);
}

void Menu::AddBinding(const std::string& id, const std::string& label,
                      std::vector<std::string>& ref) {
    Item it;
    it.id = id;
    it.label = label;
    it.kind = ItemKind::Binding;
    it.value = [&ref]() {
        std::string s;
        for (size_t i = 0; i < ref.size(); ++i) {
            if (i) s += ",";
            s += ref[i];
        }
        return s.empty() ? "(none)" : s;
    };
    items_.push_back(it);
    // activate needs this item's index-independent target: capture by pointer.
    items_.back().activate = [this, &ref]() {
        captureTarget_ = &ref;
        capturePadTarget_ = nullptr;
        captureIsPad_ = false;
        captureLabel_ = items_.back().label;  // label of the activating row
        capturing_ = true;
    };
}

void Menu::AddAxisBinding(const std::string& id, const std::string& label, AxisBinding& ref) {
    AddBinding(id + ".A", label + " (-)", ref.slotA);
    AddBinding(id + ".B", label + " (+)", ref.slotB);
}

void Menu::AddPadBinding(const std::string& id, const std::string& label, std::string& ref) {
    Item it;
    it.id = id;
    it.label = label;
    it.kind = ItemKind::PadBinding;
    it.value = [&ref]() { return ref.empty() ? std::string("(none)") : ref; };
    items_.push_back(it);
    items_.back().activate = [this, &ref]() {
        capturePadTarget_ = &ref;
        captureTarget_ = nullptr;
        captureIsPad_ = true;
        captureLabel_ = items_.back().label;
        capturing_ = true;
    };
}

void Menu::AddAction(const std::string& label, std::function<void()> fn) {
    Item it;
    it.label = label;
    it.kind = ItemKind::Action;
    it.activate = fn;
    items_.push_back(it);
}

void Menu::BuildItems() {
    // Every schema key appears below (menu never diverges from settings.ini).
    char info[64];
    std::snprintf(info, sizeof(info), "schema v%d", Settings::kSchemaVersion);
    items_.push_back(Item{"version.schema", info, ItemKind::Info, nullptr, nullptr, nullptr});

    AddHeader("CAMERA (RL 1:1 ranges)");
    AddFloat("camera.fov", "FOV", settings_.camera.fov, 60, 130, 1);
    AddFloat("camera.distance", "Distance", settings_.camera.distance, 100, 400, 5);
    AddFloat("camera.height", "Height", settings_.camera.height, 40, 200, 5);
    AddFloat("camera.angle", "Angle", settings_.camera.angle, -30, 15, 1);
    AddFloat("camera.stiffness", "Stiffness", settings_.camera.stiffness, 0, 1, 0.05f);
    AddBool("camera.ballcam", "Ball cam (C)", settings_.camera.ballcam);

    AddHeader("INPUT");
    AddAxisBinding("input.throttle", "Throttle fwd/back", settings_.input.throttle);
    AddAxisBinding("input.steer", "Steer left/right", settings_.input.steer);
    AddAxisBinding("input.pitch", "Pitch", settings_.input.pitch);
    AddAxisBinding("input.yaw", "Yaw", settings_.input.yaw);
    AddAxisBinding("input.roll", "Roll Q/E", settings_.input.roll);
    AddBinding("input.jump", "Jump", settings_.input.jump);
    AddBinding("input.boost", "Boost", settings_.input.boost);
    AddBinding("input.handbrake", "Powerslide / air-roll", settings_.input.handbrake);
    AddBinding("input.ballcam_toggle", "Ballcam toggle", settings_.input.ballcamToggle);
    AddBinding("input.reset_car", "Reset car", settings_.input.resetCar);
    AddBinding("input.reset_ball", "Reset ball", settings_.input.resetBall);
    AddBinding("input.unlimited", "Unlimited boost", settings_.input.unlimited);
    AddBinding("input.menu", "Menu", settings_.input.menu);
    const char* knames[5] = {"kickoff_0", "kickoff_1", "kickoff_2", "kickoff_3", "kickoff_4"};
    const char* pnames[5] = {"pad_kickoff_0", "pad_kickoff_1", "pad_kickoff_2",
                             "pad_kickoff_3", "pad_kickoff_4"};
    const char* klabels[5] = {"Kickoff:center", "Kickoff:left", "Kickoff:right",
                              "Kickoff:back-left", "Kickoff:back-right"};
    for (int i = 0; i < 5; ++i)
        AddBinding(std::string("input.") + knames[i], klabels[i], settings_.input.kickoff[i]);

    AddHeader("INPUT — GAMEPAD");
    AddFloat("input.deadzone", "Stick deadzone", settings_.gamepad.deadzone, 0, 0.5f, 0.01f);
    AddPadBinding("input.pad_jump", "Pad: Jump", settings_.gamepad.jump);
    AddPadBinding("input.pad_boost", "Pad: Boost", settings_.gamepad.boost);
    AddPadBinding("input.pad_handbrake", "Pad: Powerslide", settings_.gamepad.handbrake);
    AddPadBinding("input.pad_ballcam_toggle", "Pad: Ballcam", settings_.gamepad.ballcamToggle);
    AddPadBinding("input.pad_reset_car", "Pad: Reset car", settings_.gamepad.resetCar);
    AddPadBinding("input.pad_reset_ball", "Pad: Reset ball", settings_.gamepad.resetBall);
    AddPadBinding("input.pad_unlimited", "Pad: Unlimited boost", settings_.gamepad.unlimited);
    AddPadBinding("input.pad_menu", "Pad: Menu", settings_.gamepad.menu);
    for (int i = 0; i < 5; ++i)
        AddPadBinding(std::string("input.") + pnames[i], std::string("Pad: ") + klabels[i],
                      settings_.gamepad.kickoff[i]);

    AddHeader("PHYSICS");
    AddInt("physics.tick", "Tick rate (Hz)", settings_.physics.tick, 30, 240, 10);

    AddHeader("BOOST");
    AddBool("boost.unlimited", "Unlimited boost (U)", settings_.boost.unlimited);
    AddFloat("boost.drain", "Drain /s", settings_.boost.drain, 0, 100, 1);
    AddFloat("boost.recharge", "Recharge /s", settings_.boost.recharge, 0, 100, 1);

    AddHeader("ARENA");
    AddBool("arena.soccar", "Soccar arena", settings_.arena.soccar);

    AddHeader("GAME");
    {
        Item it;
        it.id = "game.kickoff_index";
        it.label = "Kickoff spawn";
        it.kind = ItemKind::Choice;
        const char* names[5] = {"Center", "Left", "Right", "Back-Left", "Back-Right"};
        int* ref = &settings_.game.kickoffIndex;
        it.value = [ref, names]() { return names[*ref >= 0 && *ref < 5 ? *ref : 0]; };
        it.adjust = [ref](int dir) {
            *ref = (*ref + dir + 10 * 5) % 5;  // wrap around
        };
        it.activate = [this]() {
            settings_.game.kickoffIndex = (settings_.game.kickoffIndex + 1) % 5;
        };
        items_.push_back(it);
    }

    AddHeader("FREE PLAY");
    AddAction("Reset car (R)", [this]() { actions_.resetCar(); });
    AddAction("Reset ball (B)", [this]() { actions_.resetBall(); });
    AddAction("Spawn kickoff (F1..F5 = current)", [this]() {
        actions_.spawnKickoff(settings_.game.kickoffIndex);
    });
    AddAction("Toggle ballcam (C)", [this]() {
        settings_.camera.ballcam = !settings_.camera.ballcam;
        actions_.toggleBallcam();
    });
    AddAction("Toggle unlimited boost (U)", [this]() {
        settings_.boost.unlimited = !settings_.boost.unlimited;
        actions_.toggleUnlimitedBoost();
    });
    AddAction("Close menu (Esc)", [this]() { Close(); });
}

void Menu::MoveSelection(int dir) {
    if (items_.empty()) return;
    auto selectable = [&](int i) {
        ItemKind k = items_[(size_t)i].kind;
        return k != ItemKind::Header && k != ItemKind::Info;
    };
    if (dir == 0) {
        if (!selectable(selected_)) MoveSelection(1);
        return;
    }
    int n = (int)items_.size();
    for (int step = 0; step < n; ++step) {
        selected_ = (selected_ + dir + n) % n;
        if (selectable(selected_)) return;
    }
}

std::string Menu::KeyNameForScancode(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        return std::string(1, (char)('A' + (sc - SDL_SCANCODE_A)));
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        return std::string(1, (char)('1' + (sc - SDL_SCANCODE_1)));
    }
    if (sc == SDL_SCANCODE_0) return "0";
    switch (sc) {
        case SDL_SCANCODE_UP: return "Up";
        case SDL_SCANCODE_DOWN: return "Down";
        case SDL_SCANCODE_LEFT: return "Left";
        case SDL_SCANCODE_RIGHT: return "Right";
        case SDL_SCANCODE_SPACE: return "Space";
        case SDL_SCANCODE_LSHIFT: return "LeftShift";
        case SDL_SCANCODE_RSHIFT: return "RightShift";
        case SDL_SCANCODE_LCTRL: return "LeftCtrl";
        case SDL_SCANCODE_RCTRL: return "RightCtrl";
        case SDL_SCANCODE_ESCAPE: return "Escape";
        case SDL_SCANCODE_RETURN: return "Return";
        case SDL_SCANCODE_TAB: return "Tab";
        case SDL_SCANCODE_DELETE: return "Delete";
        default: break;
    }
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
        return "F" + std::to_string(1 + (sc - SDL_SCANCODE_F1));
    }
    const char* n = SDL_GetScancodeName(sc);
    return (n && *n) ? n : "Unknown";
}

bool Menu::HandleKey(SDL_Keycode key) {
    if (!open_) return false;
    if (capturing_) {
        if (key == SDLK_ESCAPE) {
            capturing_ = false;  // cancel, keep old binding
            return true;
        }
        if (captureIsPad_) return true;  // waiting for a pad button, not a key
        SDL_Scancode sc = SDL_GetScancodeFromKey(key);
        if (sc != SDL_SCANCODE_UNKNOWN && captureTarget_) {
            *captureTarget_ = {KeyNameForScancode(sc)};
            capturing_ = false;
        }
        return true;
    }
    switch (key) {
        case SDLK_UP: MoveSelection(-1); return true;
        case SDLK_DOWN: MoveSelection(1); return true;
        case SDLK_LEFT:
            if (items_[(size_t)selected_].adjust) items_[(size_t)selected_].adjust(-1);
            return true;
        case SDLK_RIGHT:
            if (items_[(size_t)selected_].adjust) items_[(size_t)selected_].adjust(+1);
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            if (items_[(size_t)selected_].activate) items_[(size_t)selected_].activate();
            return open_;
        case SDLK_ESCAPE: Close(); return false;
        default: break;
    }
    // Free-play shortcuts stay live while the menu is open.
    if (key == SDLK_r) {
        actions_.resetCar();
        return true;
    }
    if (key == SDLK_b) {
        actions_.resetBall();
        return true;
    }
    if (key == SDLK_c) {
        settings_.camera.ballcam = !settings_.camera.ballcam;
        actions_.toggleBallcam();
        return true;
    }
    if (key == SDLK_u) {
        settings_.boost.unlimited = !settings_.boost.unlimited;
        actions_.toggleUnlimitedBoost();
        return true;
    }
    if (key >= SDLK_F1 && key <= SDLK_F5) {
        int idx = (int)(key - SDLK_F1);
        settings_.game.kickoffIndex = idx;
        actions_.spawnKickoff(idx);
        return true;
    }
    return true;
}

bool Menu::HandleControllerButton(SDL_GameControllerButton button) {
    if (!open_) return false;
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) return true;
    if (capturing_) {
        if (captureIsPad_ && capturePadTarget_) {
            *capturePadTarget_ = Gamepad::NameForButton(button);
            capturing_ = false;
        }
        // A key capture ignores pad presses (and vice versa).
        return true;
    }
    // Menu navigation from the pad. The driving path still sees the raw
    // buttons; these are extra bindings, not a replacement.
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: MoveSelection(-1); return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: MoveSelection(1); return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (items_[(size_t)selected_].adjust) items_[(size_t)selected_].adjust(-1);
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (items_[(size_t)selected_].adjust) items_[(size_t)selected_].adjust(+1);
            return true;
        case SDL_CONTROLLER_BUTTON_A:
            if (items_[(size_t)selected_].activate) items_[(size_t)selected_].activate();
            return open_;
        case SDL_CONTROLLER_BUTTON_B:
            Close();
            return false;
        default:
            return true;
    }
}

std::string Menu::CapturePrompt() const {
    if (!capturing_) return "";
    return std::string(captureIsPad_ ? "Press a pad button for '" : "Press a key for '") +
           captureLabel_ + "' (Esc cancels)...";
}

std::vector<std::string> Menu::RenderText() const {
    std::vector<std::string> lines;
    if (!open_) return lines;
    lines.push_back("=== SETTINGS (schema v1) ===");
    if (capturing_) lines.push_back(CapturePrompt());
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        std::string prefix = ((int)i == selected_ && !capturing_) ? "> " : "  ";
        if (it.kind == ItemKind::Header) {
            lines.push_back("--- " + it.label + " ---");
        } else if (it.kind == ItemKind::Info) {
            lines.push_back("  [" + it.label + "]");
        } else if (it.kind == ItemKind::Action) {
            lines.push_back(prefix + it.label);
        } else {
            lines.push_back(prefix + it.label + " : " + (it.value ? it.value() : "?"));
        }
    }
    return lines;
}

}  // namespace rl
