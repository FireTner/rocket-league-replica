// GoalBurst.h — presentation-only particle/ring shock on the goal latch.
// Does not move the car or the ball. Colour and timing are UNVERIFIED:
// no measurement of the real goal explosion is cited here.
#pragma once

namespace rl {

class Renderer;

// Short shock anchored at the ball position captured when the latch fires.
// The game resets the ball immediately after PollGoal(), so the draw must
// keep that point rather than reading the snapshot afterward.
class GoalBurst {
public:
	// Sim seconds. UNVERIFIED.
	static constexpr float kLifeSeconds = 0.80f;
	static constexpr int kParticleCount = 16;

	void Trigger(float x, float y, float z);
	void Update(float dtSeconds);

	bool IsActive() const { return life_ > 0.0f; }
	float Life() const { return life_; }
	float OriginX() const { return x_; }
	float OriginY() const { return y_; }
	float OriginZ() const { return z_; }

	void Draw(Renderer& renderer) const;

private:
	float life_ = 0.0f;
	float x_ = 0.0f;
	float y_ = 0.0f;
	float z_ = 0.0f;
};

} // namespace rl
