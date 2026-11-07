// Versioned free-play settings, loaded and saved as an INI file.
#pragma once

#include <string>
#include <vector>

namespace rl {

struct CameraSettings {
    float fov = 110.0f;
    float distance = 270.0f;
    float height = 100.0f;
    float angle = -3.0f;
    float stiffness = 0.45f;
    bool ballcam = true;
    float transitionSpeed = 1.5f;  // ball-cam blend, 1..2. Matches the on-disk profile.
    float swivelSpeed = 5.0f;      // how fast the stick offset eases, 1..10
    bool shake = false;            // off: the chase rig does not shake
    bool invertSwivel = false;     // flip swivel pitch
    bool snap = true;              // released stick returns the swivel; off holds it
    bool airHeading = false;      // airborne: blend nose heading toward velocity
};

// Key names are SDL names ("W", "Up", "LeftShift", "Space").
// An axis is slotA minus slotB, so slotA is the +1 side.
struct AxisBinding {
    std::vector<std::string> slotA;
    std::vector<std::string> slotB;
};

struct InputBindings {
    AxisBinding throttle;  // A forward, B back
    AxisBinding steer;     // A left, B right. Steer +1 is left.
    AxisBinding pitch;     // Same keys as throttle. The keyboard maps W to pitch -1 (nose down).
    AxisBinding yaw;       // A left, B right
    AxisBinding roll;      // A rolls left (Q), B rolls right (E)
    std::vector<std::string> jump = {"L"};
    std::vector<std::string> boost = {"K"};
    std::vector<std::string> handbrake = {"LeftShift", "RightShift"};
    std::vector<std::string> ballcamToggle = {"Space"};
    std::vector<std::string> resetCar = {"R"};
    std::vector<std::string> resetBall = {"C"};
    std::vector<std::string> unlimited = {"U"};
    std::vector<std::string> menu = {"Escape"};
    std::vector<std::string> kickoff[5];  // ball drills 0..4, keys 1..5
};

// Button names are SDL controller buttons ("A", "LeftShoulder", "DPadUp").
// Kept apart from the keyboard table so a rebind of one does not change the other.
struct GamepadBindings {
    // Left stick is fixed: X is steer, Y is throttle. Deadzone is radial, on the vector.
    float deadzone = 0.08f;
    std::string jump = "A";
    std::string boost = "B";
    std::string handbrake = "X";
    std::string ballcamToggle = "Y";
    std::string resetCar = "Back";
    std::string resetBall = "LeftStick";
    std::string unlimited = "RightStick";
    std::string menu = "Start";
    std::string kickoff[5];
};

struct PhysicsSettings {
    int tick = 120;
};

struct BoostSettings {
    bool unlimited = false;
    float drain = 33.33f;    // units per second while boosting
    float recharge = 10.0f;  // units per second
};

// The build writes collision_meshes/soccar/*.cmf. The stadium pack is local.
inline constexpr char kDefaultMeshDir[] = "collision_meshes";
inline constexpr char kDefaultArenaDir[] = "arena_assets/stadium_day";

struct ArenaSettings {
    bool soccar = true;
    // <mesh_dir>/soccar/*.cmf. An empty file value keeps kDefaultMeshDir.
    // A missing key uses that default. Dumps stay runtime-only.
    std::string meshDir = kDefaultMeshDir;
    // arena.json pack. A present empty value is the analytic shell.
    // A missing key uses the stadium pack. --arenadir still wins.
    std::string arenaDir = kDefaultArenaDir;
};

struct GameSettings {
    int kickoffIndex = 0;  // 0 center, 1 left, 2 right, 3 back-left, 4 back-right
};

struct RenderSettings {
    int msaa = 0;        // 0 = off, else sample count clamped to 0..16
    int maxFps = 0;      // 0 = off. A cap applies only while vsync is off.
    bool pacingLog = false;
    bool pbr = false;
    // Glass alpha. 0.40 matches the baked shell so a missing key is unchanged.
    float wallOpacity = 0.40f;
    // Pack-shader specular. Off until the ini turns it on.
    bool packSpec = false;
};

struct Settings {
    static constexpr int kSchemaVersion = 1;

    CameraSettings camera;
    InputBindings input;
    GamepadBindings gamepad;
    PhysicsSettings physics;
    BoostSettings boost;
    ArenaSettings arena;
    GameSettings game;
    RenderSettings render;

    static Settings Defaults();

    // Missing file or keys keep defaults. Returns false on I/O or a parse error.
    static bool Load(const std::string& path, Settings& out, std::string* err = nullptr);
    static bool Save(const std::string& path, const Settings& s, std::string* err = nullptr);

    // Fills keys added after fileSchema and clamps ranges.
    static void Migrate(Settings& s, int fileSchema);
};

}  // namespace rl
