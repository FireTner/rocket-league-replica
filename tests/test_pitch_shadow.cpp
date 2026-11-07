// Pitch-plane sun shadow: one key light, and the blob stays on the pitch.
// No GL. The draw path is pack-only and is not this test.
#include "render/PitchShadow.h"

#include <cmath>
#include <cstdio>

static int fails = 0;

static void Expect(bool ok, const char* msg) {
	if (!ok) {
		std::fprintf(stderr, "FAIL %s\n", msg);
		fails++;
	}
}

int main() {
	float ray[3];
	rl::PitchShadow::SunRay(ray);
	float len = std::sqrt(ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
	Expect(std::fabs(len - 1.f) < 1.0e-4f, "sun ray is unit");
	Expect(ray[2] < -0.8f, "sun ray goes down onto the pitch");
	Expect(ray[0] < 0.f && ray[1] > 0.f, "offset follows the key light, not the fill");
	float x = 0.f, y = 0.f;
	rl::PitchShadow::Project(0.f, 0.f, 100.f, &x, &y);
	Expect(x < -20.f && x > -40.f, "height 100 offset x");
	Expect(y > 35.f && y < 55.f, "height 100 offset y");
	float x2 = 0.f, y2 = 0.f;
	rl::PitchShadow::Project(0.f, 0.f, 200.f, &x2, &y2);
	Expect(std::fabs(x2 - 2.f * x) < 0.05f && std::fabs(y2 - 2.f * y) < 0.05f,
	       "offset scales with height");
	rl::PitchShadow::Project(5.f, -7.f, -4.f, &x, &y);
	Expect(x == 5.f && y == -7.f, "below the floor the shadow does not flip");
	Expect(rl::PitchShadow::PitchClearance(0.f, 0.f) > 4000.f, "centre of the pitch");
	Expect(rl::PitchShadow::PitchClearance(0.f, 5600.f) > 300.f, "goal floor is pitch");
	Expect(rl::PitchShadow::PitchClearance(0.f, 5110.f) > 800.f,
	       "goal line still has room into the mouth");
	Expect(rl::PitchShadow::PitchClearance(2000.f, 5110.f) < 20.f,
	       "beside the mouth the back wall stops the blob");
	Expect(rl::PitchShadow::PitchClearance(3000.f, 5600.f) < 0.f, "beside the goal is stands");
	Expect(rl::PitchShadow::PitchClearance(4500.f, 0.f) < 0.f, "past the side wall");
	Expect(rl::PitchShadow::PitchClearance(4000.f, 4200.f) < 0.f, "corner fillet is not pitch");
	rl::PitchShadow::ShadowBlob ball =
	    rl::PitchShadow::BallBlob(0.f, 0.f, 93.15f, 91.25f);
	Expect(ball.visible, "resting ball has a shadow");
	Expect(ball.x < 0.f && ball.y > 0.f, "resting ball shadow is sun-offset");
	Expect(ball.major + 0.1f >= ball.minor, "ball shadow stretches along the sun");
	Expect(std::fabs(ball.minor - 91.25f) < 0.2f, "ball minor axis is the radius");
	rl::PitchShadow::ShadowBlob off =
	    rl::PitchShadow::BallBlob(0.f, 7000.f, 93.f, 91.25f);
	Expect(!off.visible, "past the goal the blob is not drawn on the stands");
	float pos[3] = {0.f, 0.f, 40.f};
	float fwd[3] = {1.f, 0.f, 0.f};
	rl::PitchShadow::ShadowBlob car = rl::PitchShadow::CarBlob(pos, fwd, 72.f, 52.f);
	Expect(car.visible, "car has a shadow");
	Expect(car.x < 0.f && car.y > 0.f, "car shadow is sun-offset");
	Expect(std::fabs(car.angle) < 0.01f, "car shadow follows forward");
	float nose[3] = {0.f, 0.f, 1.f};
	rl::PitchShadow::ShadowBlob up = rl::PitchShadow::CarBlob(pos, nose, 72.f, 52.f);
	Expect(up.visible, "nose-up car still has a shadow");
	Expect(std::fabs(up.major - up.minor) < 0.1f, "nose-up shadow is round");
	if (fails) {
		std::fprintf(stderr, "PITCH_SHADOW FAIL %d\n", fails);
		return 1;
	}
	std::printf("PITCH_SHADOW PASS\n");
	return 0;
}
