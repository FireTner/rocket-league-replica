#include "game/FreeplayBall.h"

namespace rl {
namespace {

struct V3 {
	float x, y, z;
};

float Dot(V3 a, V3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

float Len(V3 a) {
	return std::sqrt(Dot(a, a));
}

V3 Scale(V3 a, float s) {
	return {a.x * s, a.y * s, a.z * s};
}

V3 Add(V3 a, V3 b) {
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

V3 Cross(V3 a, V3 b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

V3 UnitOr(V3 a, V3 fallback) {
	float n = Len(a);
	if (n < 1e-6f)
		return fallback;
	return Scale(a, 1.f / n);
}

// Horizontal unit direction the command treats as "in front" / "forward".
// Prefers the car's forward axis. If that axis is vertical, uses horizontal
// velocity while the car is moving, then +Y (blue kickoff facing,
// RLConst.h spawn yaw +pi/2). The vertical fallback is degenerate only.
V3 HorizontalForward(const FreeplayCar& car) {
	V3 flat{car.fx, car.fy, 0.f};
	float n = Len(flat);
	if (n >= 1e-4f)
		return Scale(flat, 1.f / n);
	V3 hv{car.body.vx, car.body.vy, 0.f};
	n = Len(hv);
	if (n >= Physics::Tuning::kStoppingForwardVel)
		return Scale(hv, 1.f / n);
	return {0.f, 1.f, 0.f};
}

// +1 orange goal (+Y), -1 blue goal (-Y). Distance is to the goal-line
// center (0, ±BACK_WALL_Y), RLConst.h:15. A tie (the midfield line) breaks
// toward the goal the car faces, then blue. Facing is not the primary rule.
int ClosestGoalSign(const FreeplayCar& car) {
	float yBlue = -Arena::BACK_WALL_Y;
	float yOrange = Arena::BACK_WALL_Y;
	float dBlue = car.body.x * car.body.x + (car.body.y - yBlue) * (car.body.y - yBlue);
	float dOrange = car.body.x * car.body.x + (car.body.y - yOrange) * (car.body.y - yOrange);
	if (dOrange + 1.f < dBlue)
		return 1;
	if (dBlue + 1.f < dOrange)
		return -1;
	V3 f = HorizontalForward(car);
	if (f.y > 0.f)
		return 1;
	return -1;
}

void AimAtMouth(float x, float y, float z, int sign, float* vx, float* vy, float* vz) {
	float goalY = sign * Arena::BACK_WALL_Y;
	float targetZ = Arena::GOAL_HEIGHT * 0.5f;
	float dx = 0.f - x;
	float dy = goalY - y;
	float horiz = std::sqrt(dx * dx + dy * dy);
	if (horiz < 1.f)
		horiz = 1.f;
	float t = horiz / kDefendHorizSpeed;
	float g = Physics::Tuning::kGravityZ;
	*vx = dx / horiz * kDefendHorizSpeed;
	*vy = dy / horiz * kDefendHorizSpeed;
	// z(t) = z + vz t + 0.5 g t^2 lands on the mouth center. g is -650.
	*vz = (targetZ - z - 0.5f * g * t * t) / t;
}

FreeplayCommand TakePossession(const FreeplayCar& car, const Physics::CarPreset& preset) {
	// Local +X is forward (front wheels have connectX > 0, CarPresets.h).
	V3 f = HorizontalForward(car);
	float front = preset.offX + preset.sizeX * 0.5f;
	float dist = front + Physics::Tuning::kBallRadiusSoccar + kPossessionClearance;
	FreeplayCommand c;
	c.x = car.body.x + f.x * dist;
	c.y = car.body.y + f.y * dist;
	c.z = Physics::Tuning::kBallRestZ;
	// Owner: linear velocity equals the car's at this moment, including vz.
	c.vx = car.body.vx;
	c.vy = car.body.vy;
	c.vz = car.body.vz;
	return c;
}

FreeplayCommand StartDribble(const FreeplayCar& car, const Physics::CarPreset& preset) {
	V3 f = UnitOr({car.fx, car.fy, car.fz}, {0.f, 1.f, 0.f});
	V3 u = UnitOr({car.ux, car.uy, car.uz}, {0.f, 0.f, 1.f});
	float uf = Dot(u, f);
	u = UnitOr({u.x - f.x * uf, u.y - f.y * uf, u.z - f.z * uf}, {0.f, 0.f, 1.f});
	// up × forward = car-right for the RL basis (+X forward, +Y right, +Z up).
	V3 r = Cross(u, f);
	// Roof center of the hitbox, then one collision radius up. offX is already
	// forward of the origin (Octane 13.8757). Sitting on that point is the
	// hood the ball can rest on. I3: the visual-hood bias and the fraction of
	// vertical velocity are unpublished; vertical velocity is copied in full
	// so the ball is travelling with the car. Spin is cleared by SetBall.
	float lx = preset.offX;
	float ly = preset.offY;
	float lz = preset.offZ + preset.sizeZ * 0.5f + Physics::Tuning::kBallRadiusSoccar;
	V3 p = Add(Add(Scale(f, lx), Scale(r, ly)), Scale(u, lz));
	FreeplayCommand c;
	c.x = car.body.x + p.x;
	c.y = car.body.y + p.y;
	c.z = car.body.z + p.z;
	c.vx = car.body.vx;
	c.vy = car.body.vy;
	c.vz = car.body.vz;
	return c;
}

FreeplayCommand OwnerPass(const FreeplayCar& car, const FreeplaySample& ball) {
	// Position is the ball's current position. Not a spawn.
	FreeplayCommand c;
	c.x = ball.x;
	c.y = ball.y;
	c.z = ball.z;
	float hs = std::sqrt(car.body.vx * car.body.vx + car.body.vy * car.body.vy);
	if (hs >= Physics::Tuning::kStoppingForwardVel) {
		// "Horizontal velocity if the car is moving": the car's own (vx, vy).
		c.vx = car.body.vx;
		c.vy = car.body.vy;
		c.vz = 0.f;
	} else {
		V3 f = HorizontalForward(car);
		c.vx = f.x * kPassStoppedSpeed;
		c.vy = f.y * kPassStoppedSpeed;
		c.vz = 0.f;
	}
	return c;
}

FreeplayCommand LaunchBall(const FreeplaySample& ball) {
	// Hoops launches the ball that is already at its kickoff pose
	// (Ball.cpp: the impulse fires only when XY is the origin and velocity
	// is zero). Free play has no published launch coordinates. Position is
	// left alone so a ball on the wall stays there and goes up; v2.06 says
	// Launch then Defend mimics a shot from the ceiling or high on the wall,
	// which a teleport to field center would erase. UNVERIFIED vs a capture.
	FreeplayCommand c;
	c.x = ball.x;
	c.y = ball.y;
	c.z = ball.z;
	c.vx = 0.f;
	c.vy = 0.f;
	c.vz = kHoopsTipoffZVel;
	return c;
}

FreeplayCommand DefendShot(const FreeplayCar& car, const FreeplaySample& ball) {
	int sign = ClosestGoalSign(car);
	float goalY = sign * Arena::BACK_WALL_Y;
	float dx = 0.f - ball.x;
	float dy = goalY - ball.y;
	float horiz = std::sqrt(dx * dx + dy * dy);
	// Already above the crossbar and far enough to aim: keep the position.
	// That is the Launch-then-Defend high shot. GOAL_HEIGHT is the cited
	// mouth height (Arena.cpp APPROX_GOAL_HEIGHT); using it as the cutoff
	// is UNVERIFIED.
	if (ball.z >= Arena::GOAL_HEIGHT && horiz >= kDefendMinAimDist) {
		FreeplayCommand c;
		c.x = ball.x;
		c.y = ball.y;
		c.z = ball.z;
		AimAtMouth(c.x, c.y, c.z, sign, &c.vx, &c.vy, &c.vz);
		return c;
	}
	// Otherwise place a shot on the field side of the closest goal, on the
	// center line, travelling into that mouth. Not the end the car faces.
	FreeplayCommand c;
	c.x = 0.f;
	c.y = sign * (Arena::BACK_WALL_Y - kDefendApproach);
	c.z = kDefendStartZ;
	AimAtMouth(c.x, c.y, c.z, sign, &c.vx, &c.vy, &c.vz);
	return c;
}

} // namespace

bool ComputeFreeplayBall(int drill, const FreeplayCar& car, const FreeplaySample& ball,
	const Physics::CarPreset& preset, FreeplayCommand* out) {
	if (!out || drill < 0 || drill > 4)
		return false;
	if (drill == 0)
		*out = TakePossession(car, preset);
	else if (drill == 1)
		*out = StartDribble(car, preset);
	else if (drill == 2)
		*out = OwnerPass(car, ball);
	else if (drill == 3)
		*out = LaunchBall(ball);
	else
		*out = DefendShot(car, ball);
	return true;
}

} // namespace rl
