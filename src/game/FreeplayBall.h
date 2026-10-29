// Free-play ball commands for keys 1–5.
//
// Presentation chooses a ball pose and linear velocity. Sim::SetBall applies
// it and clears angular velocity (Sim.cpp SetBall: a non-zero linear velocity
// writes angVel = 0). This file does not step the simulation.
//
// Published behavior: Psyonix, "Using the New Free Play Controls" and patch
// notes v2.06 (2021-10-12). Pass Ball's "towards your car" sentence is NOT
// implemented: the owner decision keeps the ball where it is and sends it
// along travel (known conflict, left open).
//
// Numbers that are not a measurement or a published source are marked
// UNVERIFIED. BakkesMod plugin offsets are not used.
#pragma once

#include <cmath>

#include "arena/ArenaConstants.h"
#include "physics/CarPresets.h"
#include "physics/Tuning.h"

namespace rl {

// One rigid-body sample. Units: uu and uu/s.
struct FreeplaySample {
	float x = 0.f, y = 0.f, z = 0.f;
	float vx = 0.f, vy = 0.f, vz = 0.f;
};

// Car pose the five commands are allowed to read. Forward and up are the
// sim's unit axes (they need not be re-normalized by the caller).
struct FreeplayCar {
	FreeplaySample body;
	float fx = 0.f, fy = 1.f, fz = 0.f;
	float ux = 0.f, uy = 0.f, uz = 1.f;
};

// Replacement ball state. Angular velocity is not carried: SetBall clears it.
struct FreeplayCommand {
	float x = 0.f, y = 0.f, z = 0.f;
	float vx = 0.f, vy = 0.f, vz = 0.f;
};

// UNVERIFIED (I3): no published Take Possession distance. Added beyond the
// hitbox front face + soccar ball radius so the sphere is not already inside
// the car when the matched velocity is applied. Not a plugin offset.
inline constexpr float kPossessionClearance = 40.f;

// UNVERIFIED: Pass Ball speed is unpublished. Used only when the car's
// horizontal speed is below Physics::Tuning::kStoppingForwardVel (25 uu/s,
// RLConst.h:87). 1200 uu/s is the previous in-repo stand-in, kept as a scale
// and not claimed as the game. A moving car copies its own horizontal
// velocity instead, so this constant is not on that path.
inline constexpr float kPassStoppedSpeed = 1200.f;

// Hoops tip-off upward speed. RocketSim RLConst.h:50
// BALL_HOOPS_LAUNCH_Z_VEL = 1000 (uu/s impulse on a ball that was at rest).
// Free-play Launch Ball has no separate published speed; this is the cited
// tip-off number. The 0.265 s delay (RLConst.h:51) is the hoops kickoff
// countdown, not this button, and is not applied.
inline constexpr float kHoopsTipoffZVel = 1000.f;

// UNVERIFIED defend-shot placement and speed. Psyonix says the ball is shot
// toward the closest goal, and that Launch then Defend mimics a high shot,
// but publishes no coordinates. Approach is the field-side distance from the
// goal line (BACK_WALL_Y). The aim height is the mouth's geometric center
// (GOAL_HEIGHT / 2), which is a definition from the cited mouth, not a
// capture of the game's aim point. Horizontal speed is unpublished.
inline constexpr float kDefendApproach = 3600.f;
inline constexpr float kDefendStartZ = 400.f;
inline constexpr float kDefendHorizSpeed = 2000.f;
// Below this horizontal distance a "keep the high ball" aim is unstable, so
// the command uses the field placement instead. UNVERIFIED cutoff.
inline constexpr float kDefendMinAimDist = 800.f;

// drill 0..4 = keys 1..5. Returns false if drill is outside that range.
// preset is the car's hitbox (free play spawns Octane: Physics::kDefaultPreset,
// CarConfig.cpp:20-40 via CarPresets.h). Keys 3–5 do not read it.
bool ComputeFreeplayBall(int drill, const FreeplayCar& car, const FreeplaySample& ball,
	const Physics::CarPreset& preset, FreeplayCommand* out);

} // namespace rl
