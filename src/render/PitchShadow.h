// PitchShadow.h — one sun shadow of the cars and the ball on the pitch.
//
// The ripped stadium draws with live lighting off: its shade is the baked
// lightmap (Renderer.cpp DrawArenaPack). A shadow map would need a
// framebuffer this renderer does not have, so this is a pitch-plane blob
// projected along the key light. It is a stand-in, not a mesh silhouette.
// The analytic shell is not drawn here and its lighting is unchanged.
//
// The blob is clipped to the Soccar floor (ArenaGeometry.h: |x|<=4096,
// |y|<=5120, |x|+|y|<=8064, plus the goal mouths) so it cannot darken the
// stands. One light only: GL_LIGHT0 in Renderer::Init. The fill is not a
// second sun.
#pragma once

#include <cmath>

namespace rl {

class Renderer;
struct Snapshot;

struct PitchShadow {
	// Direction toward the key light. Same numbers as Renderer::Init GL_LIGHT0.
	static constexpr float kSunX = 0.25f;
	static constexpr float kSunY = -0.40f;
	static constexpr float kSunZ = 0.88f;
	// Soccar pitch, cited from src/arena/ArenaGeometry.h.
	static constexpr float kPitchHalfX = 4096.f;
	static constexpr float kPitchHalfY = 5120.f;
	static constexpr float kPitchCorner = 8064.f;
	static constexpr float kGoalHalfX = 892.755f;
	static constexpr float kGoalBack = 6000.f;

	struct ShadowBlob {
		float x = 0.f;
		float y = 0.f;
		float major = 0.f;
		float minor = 0.f;
		float angle = 0.f; // radians, major axis
		bool visible = false;
	};

	// Unit ray from a body toward the pitch (away from the key light).
	static void SunRay(float out[3]);
	// Hit of that ray on z = 0. A point already below the floor keeps its xy.
	static void Project(float x, float y, float z, float* ox, float* oy);
	// Signed room (uu) before the blob leaves the pitch. Negative is off it.
	static float PitchClearance(float x, float y);
	// Sphere stand-in. `radius` is the ball radius (Models.h kBallR).
	static ShadowBlob BallBlob(float x, float y, float z, float radius);
	// Car stand-in. `along` / `across` are the ground radii (uu).
	static ShadowBlob CarBlob(const float pos[3], const float fwd[3],
	                          float along, float across);

private:
	static ShadowBlob Finish(float px, float py, float major, float minor,
	                         float angle);
};

void DrawPitchSunShadows(Renderer& renderer, const Snapshot& player,
                         const Snapshot& opponent);

inline void PitchShadow::SunRay(float out[3]) {
	float x = -kSunX;
	float y = -kSunY;
	float z = -kSunZ;
	float len = std::sqrt(x * x + y * y + z * z);
	out[0] = x / len;
	out[1] = y / len;
	out[2] = z / len;
}

inline void PitchShadow::Project(float x, float y, float z, float* ox, float* oy) {
	float ray[3];
	SunRay(ray);
	if (z <= 0.f || ray[2] >= -1.0e-6f) {
		*ox = x;
		*oy = y;
		return;
	}
	float t = (0.f - z) / ray[2];
	*ox = x + t * ray[0];
	*oy = y + t * ray[1];
}

inline float PitchShadow::PitchClearance(float x, float y) {
	float ax = std::fabs(x);
	float ay = std::fabs(y);
	// The goal mouth is pitch too, so a blob on the goal line may cross
	// y = ±5120 without landing on the stands. Beside the mouth it may not.
	bool inMouth = ax <= kGoalHalfX;
	bool inGoal = inMouth && ay > kPitchHalfY && ay <= kGoalBack;
	bool inOct = ax <= kPitchHalfX && ay <= kPitchHalfY &&
	             (ax + ay) <= kPitchCorner;
	if (!inOct && !inGoal)
		return -1.f;
	if (!inOct) {
		float side = kGoalHalfX - ax;
		float back = kGoalBack - ay;
		return side < back ? side : back;
	}
	float side = kPitchHalfX - ax;
	float corner = (kPitchCorner - ax - ay) * 0.70710678f;
	float depth = inMouth ? (kGoalBack - ay) : (kPitchHalfY - ay);
	float room = side < corner ? side : corner;
	if (depth < room)
		room = depth;
	return room;
}

inline PitchShadow::ShadowBlob PitchShadow::Finish(float px, float py,
                                                   float major, float minor,
                                                   float angle) {
	ShadowBlob blob;
	blob.x = px;
	blob.y = py;
	blob.angle = angle;
	float contain = major > minor ? major : minor;
	float room = PitchClearance(px, py);
	if (room < 6.f || contain < 1.f)
		return blob;
	float scale = contain > room ? room / contain : 1.f;
	blob.major = major * scale;
	blob.minor = minor * scale;
	blob.visible = blob.major >= 6.f && blob.minor >= 6.f;
	return blob;
}

inline PitchShadow::ShadowBlob PitchShadow::BallBlob(float x, float y, float z,
                                                     float radius) {
	float px = 0.f;
	float py = 0.f;
	Project(x, y, z, &px, &py);
	float ray[3];
	SunRay(ray);
	float dz = ray[2] < -1.0e-4f ? -ray[2] : 1.0e-4f;
	float angle = std::atan2(ray[1], ray[0]);
	return Finish(px, py, radius / dz, radius, angle);
}

inline PitchShadow::ShadowBlob PitchShadow::CarBlob(const float pos[3],
                                                    const float fwd[3],
                                                    float along, float across) {
	float px = 0.f;
	float py = 0.f;
	Project(pos[0], pos[1], pos[2], &px, &py);
	float fx = fwd[0];
	float fy = fwd[1];
	float fl = std::sqrt(fx * fx + fy * fy);
	float angle = 0.f;
	if (fl < 0.2f) {
		float r = along > across ? along : across;
		along = r;
		across = r;
	} else {
		angle = std::atan2(fy, fx);
	}
	return Finish(px, py, along, across, angle);
}

} // namespace rl
