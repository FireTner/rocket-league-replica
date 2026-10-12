// A5-owned: in-game settings menu + free-play flow control.
// The menu edits the SAME Settings struct that Settings::Load/Save persists,
// so the menu can never diverge from config/settings.ini (schema v1).
// Renderer-agnostic: RenderText() produces plain text lines; A4 draws them.
// Input: the main loop feeds SDL_KEYDOWN keycodes via HandleKey() while the
// menu is open. Gamepad: stubbed (no gamepad navigation this phase).
// MIT License. Depends on SDL2 (key constants only) + Settings.h.
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_gamecontroller.h>

#include <functional>
#include <string>
#include <vector>

#include "../settings/Settings.h"

namespace rl {

// Free-play side effects. The game wires these to sim/arena code (A1/A2);
// Menu never touches sim internals directly (CONTRACTS.md rule: no cross
// rewrites). Defaults are no-ops so the menu runs standalone in tests.
struct FreePlayActions {
    std::function<void()> resetCar = [] {};
    std::function<void()> resetBall = [] {};
    std::function<void(int)> spawnKickoff = [](int) {};
    std::function<void()> toggleBallcam = [] {};
    std::function<void()> toggleUnlimitedBoost = [] {};
};

class Menu {
   public:
    enum class ItemKind { Header, Bool, Float, Int, Choice, Binding, PadBinding, Action, Info };

    struct Item {
        std::string id;       // schema key path, e.g. "camera.fov" ("" for headers/actions)
        std::string label;    // shown text
        ItemKind kind = ItemKind::Action;
        std::function<std::string()> value;  // display string for value column
        std::function<void(int)> adjust;     // Left/Right, dir = -1/+1
        std::function<void()> activate;      // Enter/Space
    };

    Menu(Settings& settings, FreePlayActions actions = FreePlayActions());

    // -- state --
    bool IsOpen() const { return open_; }
    void Open() { open_ = true; }
    void Close() {
        open_ = false;
        capturing_ = false;
    }
    void Toggle() {
        if (open_)
            Close();
        else
            Open();
    }
    const std::vector<Item>& items() const { return items_; }
    int selected() const { return selected_; }

    // Feed one SDL key-down (keycode, e.g. SDLK_UP). Returns true while the
    // menu stays open. Handles: Up/Down navigate, Left/Right adjust,
    // Enter/Space activate, Esc close/cancel-capture, R/B/U/C/F1..F5
    // free-play shortcuts, and binding capture (next key becomes the binding).
    bool HandleKey(SDL_Keycode key);

    // Feed one SDL controller button-down. Returns true while the menu stays
    // open. Only pad bindings capture pad buttons; other rows ignore them.
    bool HandleControllerButton(SDL_GameControllerButton button);

    // Capture mode: after pressing Enter on a Binding item, the next key press
    // (or, for a PadBinding item, the next pad button) replaces that binding.
    // Esc cancels. captureIsPad() says which input the capture is waiting for.
    bool IsCapturing() const { return capturing_; }
    bool captureIsPad() const { return captureIsPad_; }
    std::string CapturePrompt() const;

    // Text view for the renderer: one line per row, "  label : value" with
    // "> " marking the selection. Section headers are "--- X ---".
    std::vector<std::string> RenderText() const;

    Settings& settings() { return settings_; }

   private:
    void BuildItems();
    void AddHeader(const std::string& title);
    void AddBool(const std::string& id, const std::string& label, bool& ref);
    void AddFloat(const std::string& id, const std::string& label, float& ref, float min,
                  float max, float step);
    void AddInt(const std::string& id, const std::string& label, int& ref, int min, int max,
                int step = 1);
    void AddBinding(const std::string& id, const std::string& label,
                    std::vector<std::string>& ref);
    // Gamepad button binding (stored as a single SDL button name).
    void AddPadBinding(const std::string& id, const std::string& label, std::string& ref);
    void AddAxisBinding(const std::string& id, const std::string& label, AxisBinding& ref);
    void AddAction(const std::string& label, std::function<void()> fn);
    void MoveSelection(int dir);
    static std::string KeyNameForScancode(SDL_Scancode sc);
    static SDL_Keycode ScancodeToKeycode(SDL_Scancode sc);

    Settings& settings_;
    FreePlayActions actions_;
    std::vector<Item> items_;
    int selected_ = 0;
    bool open_ = false;
    bool capturing_ = false;
    bool captureIsPad_ = false;
    std::vector<std::string>* captureTarget_ = nullptr;
    std::string* capturePadTarget_ = nullptr;
    std::string captureLabel_;
};

}  // namespace rl
