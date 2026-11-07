// Free-play ball poses for keys 1–5. SetBall applies the pose and clears spin.
#pragma once

#include <cmath>

#include "arena/ArenaConstants.h"
#include "physics/CarPresets.h"
#include "physics/Tuning.h"

namespace rl {

// Units are uu and uu/s.
struct FreeplaySample {
	float x = 0.f, y = 0.f, z = 0.f;
	float vx = 0.f, vy = 0.f, vz = 0.f;
};

struct FreeplayCar {
	FreeplaySample body;
	float fx = 0.f, fy = 1.f, fz = 0.f;
	float ux = 0.f, uy = 0.f, uz = 1.f;
};

struct FreeplayCommand {
	float x = 0.f, y = 0.f, z = 0.f;
	float vx = 0.f, vy = 0.f, vz = 0.f;
};

// Past the hitbox front and the ball radius, so the sphere is not already inside the car.
inline constexpr float kPossessionClearance = 40.f;

// Horizontal speed when the car is slower than kStoppingForwardVel.
inline constexpr float kPassStoppedSpeed = 1200.f;

// Upward speed, uu/s. The hoops kickoff countdown is not applied. RLConst.h:50
inline constexpr float kHoopsTipoffZVel = 1000.f;

// Field-side distance from the goal line, start height, and horizontal speed.
inline constexpr float kDefendApproach = 3600.f;
inline constexpr float kDefendStartZ = 400.f;
inline constexpr float kDefendHorizSpeed = 2000.f;
// Below this horizontal distance the high-ball aim is skipped and the field placement is used.
inline constexpr float kDefendMinAimDist = 800.f;

// drill 0..4 is keys 1..5. Keys 3–5 do not read preset.
bool ComputeFreeplayBall(int drill, const FreeplayCar& car, const FreeplaySample& ball,
	const Physics::CarPreset& preset, FreeplayCommand* out);

} // namespace rl
