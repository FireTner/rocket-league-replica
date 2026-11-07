// Hitbox and wheel presets transcribed from RocketSim. Sizes are full extents, uu.
// Values from RocketSim (MIT © 2022 ZealanL).
#pragma once

#include <cstddef>
#include <cstring>

namespace Physics {

// Wheel connection Y is positive; negate it for the other wheel.
struct WheelPairConfig {
    float wheelRadius;
    float suspensionRestLength;
    float connectX, connectY, connectZ;
};

struct CarPreset {
    const char* name;
    float sizeX, sizeY, sizeZ; // CarConfig.cpp:20-29
    float offX, offY, offZ; // CarConfig.cpp:31-40
    WheelPairConfig frontWheels;
    WheelPairConfig backWheels;
    bool threeWheels; // CarConfig.cpp:100
    // |yaw| + |pitch| + |roll| required to flip.
    float dodgeDeadzone;
};

inline constexpr CarPreset kCarPresets[] = {
    {"Octane", 120.507f, 86.6994f, 38.6591f, 13.8757f, 0.f, 20.755f,
     {12.50f, 38.755f, 51.25f, 25.90f, 20.755f},
     {15.00f, 37.055f, -33.75f, 29.50f, 20.755f}, false, 0.5f},
    {"Dominus", 130.427f, 85.7799f, 33.8f, 9.f, 0.f, 15.75f,
     {12.00f, 33.95f, 50.30f, 31.10f, 15.75f},
     {13.50f, 33.85f, -34.75f, 33.00f, 15.75f}, false, 0.5f},
    {"Plank", 131.32f, 87.1704f, 31.8944f, 9.00857f, 0.f, 12.0942f,
     {12.50f, 31.9242f, 49.97f, 27.80f, 12.0942f},
     {17.00f, 27.9242f, -35.43f, 20.28f, 12.0942f}, false, 0.5f},
    {"Breakout", 133.992f, 83.021f, 32.8f, 12.5f, 0.f, 11.75f,
     {13.50f, 29.7f, 51.50f, 26.67f, 11.75f},
     {15.00f, 29.666f, -35.75f, 35.00f, 11.75f}, false, 0.5f},
    {"Hybrid", 129.519f, 84.6879f, 36.6591f, 13.8757f, 0.f, 20.755f,
     {12.50f, 38.755f, 51.25f, 25.90f, 20.755f},
     {15.00f, 37.055f, -34.00f, 29.50f, 20.755f}, false, 0.5f},
    {"Merc", 123.22f, 79.2103f, 44.1591f, 11.3757f, 0.f, 21.505f,
     {15.00f, 39.505f, 51.25f, 25.90f, 21.505f},
     {15.00f, 39.105f, -33.75f, 29.50f, 21.505f}, false, 0.5f},
    // threeWheels selects the alternate steer and friction curves, not the wheel count.
    // CarConfig.cpp:28
    {"Psyclops", 120.641f, 86.8334f, 38.7931f,
     13.8757f, 0.f, 15.0f,
     {12.50f, 33.0f, 51.25f, 5.000f, 15.000f},
     {15.00f, 31.3f, -33.75f, 29.50f, 15.000f}, true, 0.5f},
};

inline constexpr std::size_t kNumCarPresets = sizeof(kCarPresets) / sizeof(kCarPresets[0]);

inline constexpr const CarPreset& kDefaultPreset = kCarPresets[0];

inline const CarPreset* FindPreset(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kNumCarPresets; ++i) {
        const char* a = kCarPresets[i].name;
        const char* b = name;
        // ASCII case-insensitive compare.
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
            if (ca != cb) break;
            ++a; ++b;
        }
        if (*a == '\0' && *b == '\0') return &kCarPresets[i];
    }
    return nullptr;
}

// Returns false on an unknown name and leaves *current untouched.
inline bool SetPreset(const char* name, const CarPreset*& current) {
    const CarPreset* found = FindPreset(name);
    if (!found) return false;
    current = found;
    return true;
}

template <typename SimT>
bool ApplyPreset(SimT& sim, const char* name) {
    const CarPreset* found = FindPreset(name);
    if (!found) return false;
    sim.ConfigureCar(*found);
    return true;
}

template <typename SimT>
void ApplyPreset(SimT& sim, const CarPreset& preset) {
    sim.ConfigureCar(preset);
}

} // namespace Physics
