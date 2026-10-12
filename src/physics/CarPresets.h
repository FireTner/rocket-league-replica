// CarPresets.h — A3 CAR & BALL PHYSICS
// 1:1 hitbox + wheel tables transcribed VERBATIM from RocketSim authority.
// MIT only. RocketSim (c) 2022 ZealanL — credited, see docs below.
//
// AUTHORITY (do NOT re-derive, do NOT use community GetLocalCollisionExtent table):
//   third_party/RocketSim/src/Sim/Car/CarConfig/CarConfig.cpp  (rev c2baacb)
//   third_party/RocketSim/src/Sim/Car/CarConfig/CarConfig.h
// RocketSim's header comment (CarConfig.cpp:7-18) explains why community tables
// disagree: RL's GetLocalCollisionExtent() returns slightly LARGER values than the
// ones actually used in simulation. The values below are the in-sim ones, validated
// by RocketSim against the real inertia matrix.
//
// UNITS: Unreal Units (uu). Hitbox size = FULL size (not half-extent).
//   Hitbox offset is from the car origin (center of mass stays at local 0,0,0).
//   Wheel connection Y is ALWAYS positive here; negate for the second wheel
//   (same convention as RocketSim WheelPairConfig, CarConfig.h:14).
#pragma once

#include <cstddef>
#include <cstring>

namespace Physics {

// Mirrors RocketSim WheelPairConfig (CarConfig.h:6-16).
struct WheelPairConfig {
    float wheelRadius;            // Radius of both wheels (uu)
    float suspensionRestLength;   // How far out the suspension rests (uu)
    float connectX, connectY, connectZ; // Suspension ray-start offset (uu), Y > 0
};

// One drivable body style. Mirrors RocketSim CarConfig (CarConfig.h:21-38)
// minus the serialization helpers (A1 Sim owns serialization).
struct CarPreset {
    const char* name;
    // Hitbox FULL size (uu). Cf. CarConfig.cpp:20-29 (HITBOX_SIZES).
    float sizeX, sizeY, sizeZ;
    // Hitbox offset from origin (uu). Cf. CarConfig.cpp:31-40 (HITBOX_OFFSETS).
    float offX, offY, offZ;
    WheelPairConfig frontWheels;
    WheelPairConfig backWheels;
    // Psyclops-only 3-wheel steering/friction/sticky path. Cf. CarConfig.cpp:100.
    bool threeWheels;
    // (|yaw|+|pitch|+|roll|) needed to flip. Cf. CarConfig.h:37 (default 0.5f).
    float dodgeDeadzone;
};

// ---------------------------------------------------------------------------
// Preset table — VERBATIM from CarConfig.cpp:20-82.
// Order matches HITBOX_SIZES / HITBOX_OFFSETS / wheel arrays (index 0-6).
// ---------------------------------------------------------------------------
inline constexpr CarPreset kCarPresets[] = {
    // OCTANE (index 0) — the default preset (kDefaultPreset).
    {"Octane", 120.507f, 86.6994f, 38.6591f, 13.8757f, 0.f, 20.755f,
     {12.50f, 38.755f, 51.25f, 25.90f, 20.755f},
     {15.00f, 37.055f, -33.75f, 29.50f, 20.755f}, false, 0.5f},
    // DOMINUS (index 1).
    {"Dominus", 130.427f, 85.7799f, 33.8f, 9.f, 0.f, 15.75f,
     {12.00f, 33.95f, 50.30f, 31.10f, 15.75f},
     {13.50f, 33.85f, -34.75f, 33.00f, 15.75f}, false, 0.5f},
    // PLANK / Batmobile (index 2).
    {"Plank", 131.32f, 87.1704f, 31.8944f, 9.00857f, 0.f, 12.0942f,
     {12.50f, 31.9242f, 49.97f, 27.80f, 12.0942f},
     {17.00f, 27.9242f, -35.43f, 20.28f, 12.0942f}, false, 0.5f},
    // BREAKOUT (index 3).
    {"Breakout", 133.992f, 83.021f, 32.8f, 12.5f, 0.f, 11.75f,
     {13.50f, 29.7f, 51.50f, 26.67f, 11.75f},
     {15.00f, 29.666f, -35.75f, 35.00f, 11.75f}, false, 0.5f},
    // HYBRID (index 4).
    {"Hybrid", 129.519f, 84.6879f, 36.6591f, 13.8757f, 0.f, 20.755f,
     {12.50f, 38.755f, 51.25f, 25.90f, 20.755f},
     {15.00f, 37.055f, -34.00f, 29.50f, 20.755f}, false, 0.5f},
    // MERC (index 5).
    {"Merc", 123.22f, 79.2103f, 44.1591f, 11.3757f, 0.f, 21.505f,
     {15.00f, 39.505f, 51.25f, 25.90f, 21.505f},
     {15.00f, 39.105f, -33.75f, 29.50f, 21.505f}, false, 0.5f},
    // PSYCLOPS (index 6) — hitbox is Octane + 0.134 uu per axis (CarConfig.cpp:28),
    // i.e. 120.641 x 86.8334 x 38.7931 (precomputed so the row stays flat).
    // front wheels merged (threeWheels path). NOTE: RocketSim Car.cpp:262
    // currently simulates 4 wheels regardless; threeWheels selects the alternate
    // steering/lateral-friction/sticky curves, NOT the wheel count.
    {"Psyclops", 120.641f, 86.8334f, 38.7931f,
     13.8757f, 0.f, 15.0f,
     {12.50f, 33.0f, 51.25f, 5.000f, 15.000f},
     {15.00f, 31.3f, -33.75f, 29.50f, 15.000f}, true, 0.5f},
};

inline constexpr std::size_t kNumCarPresets = sizeof(kCarPresets) / sizeof(kCarPresets[0]);

// Default preset: Octane (index 0).
inline constexpr const CarPreset& kDefaultPreset = kCarPresets[0];

// --- Lookup ---------------------------------------------------------------
inline const CarPreset* FindPreset(const char* name) {
    if (!name) return nullptr;
    for (std::size_t i = 0; i < kNumCarPresets; ++i) {
        const char* a = kCarPresets[i].name;
        const char* b = name;
        // ASCII case-insensitive compare (no libc dependency for freestanding sim).
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

// Switch the CURRENT preset pointer (menu / config path). Returns false on
// unknown name and leaves *current untouched.
inline bool SetPreset(const char* name, const CarPreset*& current) {
    const CarPreset* found = FindPreset(name);
    if (!found) return false;
    current = found;
    return true;
}

// --- Sim application -------------------------------------------------------
// Consume A1's SimAPI as authority. A1's concrete sim type is not present yet
// (src/sim/ is empty), so ApplyPreset is duck-typed: any sim object exposing
//   void ConfigureCar(const CarPreset&)
// works, including a stub. This keeps src/sim read-only for A3 (contract)
// while giving A1 a one-line hookup:
//
//   // A1 Sim side:
//   void MySim::ConfigureCar(const Physics::CarPreset& p) {
//       carConfig.hitboxSize = {p.sizeX, p.sizeY, p.sizeZ};
//       carConfig.hitboxPosOffset = {p.offX, p.offY, p.offZ};
//       ... wheels / threeWheels / dodgeDeadzone ...
//   }
//   Physics::ApplyPreset(sim, "Dominus");  // or Physics::kDefaultPreset
//
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
