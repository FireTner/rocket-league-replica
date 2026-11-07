// In-game settings menu. Edits the same Settings struct that Load and Save use.
#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_gamecontroller.h>

#include <functional>
#include <string>
#include <vector>

#include "../settings/Settings.h"

namespace rl {

// Callbacks the game wires up. Defaults do nothing, so the menu can run alone.
struct FreePlayActions {
    std::function<void()> resetCar = [] {};
    std::function<void()> resetBall = [] {};
    std::function<void(int)> spawnKickoff = [](int) {};
    std::function<void()> toggleBallcam = [] {};
    std::function<void()> toggleUnlimitedBoost = [] {};
    std::function<void()> saveSettings = [] {};
};

class Menu {
   public:
    enum class ItemKind { Header, Bool, Float, Int, Choice, Binding, PadBinding, Action, Info };

    struct Item {
        std::string id;       // settings path, e.g. "camera.fov"; empty for headers and actions
        std::string label;
        ItemKind kind = ItemKind::Action;
        std::function<std::string()> value;
        std::function<void(int)> adjust;     // Left/Right, dir = -1 or +1
        std::function<void()> activate;
    };

    Menu(Settings& settings, FreePlayActions actions = FreePlayActions());

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

    // Returns true while the menu stays open.
    bool HandleKey(SDL_Keycode key);

    // Pad rows capture pad buttons. Other rows ignore them.
    bool HandleControllerButton(SDL_GameControllerButton button);

    // After Enter on a binding, the next key or pad button replaces it. Esc cancels.
    bool IsCapturing() const { return capturing_; }
    bool captureIsPad() const { return captureIsPad_; }
    std::string CapturePrompt() const;

    // One line per row. "> " marks the selection. Headers are "--- X ---".
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
