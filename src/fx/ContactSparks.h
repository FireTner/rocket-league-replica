// Contact sparks decision. Presentation only: it reads the sim view and
// says where a scrape or an awkward ball touch should draw. No particles
// are stored here, and nothing in the boost plume, the supersonic mark,
// or the airborne spin circles is consulted.
#pragma once

#include <cmath>

#include "sim/Sim.h"

namespace rl {

// Wheel lateral speed RocketSim already calls significant slip is 5 uu/s
// (Car.cpp _UpdateWheels, baseFriction > 5). Sparks start above that
// chatter. 80 uu/s is UNVERIFIED as an RL visual threshold.
constexpr float kScrapeSlipMin = 80.0f;
// lat/(lat+lon) is RocketSim frictionCurveInput (Car.cpp:454).
// 0.35 is UNVERIFIED as the visual grind cutoff. A clean roll stays near 0.
constexpr float kScrapeSlipRatio = 0.35f;
// handbrakeVal is 0..1 and rises at POWERSLIDE_RISE_RATE 5/s (RLConst.h:81).
constexpr float kScrapeHandbrake = 0.5f;
// Body or wall scrape, uu/s of speed along the surface. UNVERIFIED.
// Higher than the wheel floor because a body has no rolling part to remove.
constexpr float kBodyScrapeMin = 250.0f;
// |normal.z| below this is a wall rather than a floor. UNVERIFIED; 0.7 is
// about 45 degrees off horizontal.
constexpr float kWallNormalZ = 0.7f;
// Clean hits stay quiet: no published source says a square touch sparks.
// Tangential part of (ball.vel - car.vel) must clear this floor. 400 uu/s
// is UNVERIFIED. A square boost hit in the contact-spark test measured
// 150 uu/s tangent; the 100 uu offset hit measured 437.
constexpr float kAwkwardTangentMin = 400.0f;
// |dot(contact normal, car forward)| below this is off the nose.
// Same test, seed 1, fallback: square hit aligned 0.91, offset hit 0.64.
// 0.75 sits in that gap. UNVERIFIED against a live capture. Post-step
// approach/tangent are too close to separate those two hits.
constexpr float kAwkwardAlignMax = 0.75f;
// Ball center at or under radius 91.25 (RLConst.h:117) plus 40 uu of
// UNVERIFIED slack counts as "against the floor" for a pinch.
constexpr float kPinchBallZ = 91.25f + 40.0f;
// Burst length in sim seconds. UNVERIFIED. Short on purpose.
constexpr float kBallBurstSeconds = 0.18f;
constexpr int kMaxSparkPatches = 5;

struct ContactSparkFrame {
	bool scrape = false;
	int count = 0;
	float x[kMaxSparkPatches] = {};
	float y[kMaxSparkPatches] = {};
	float z[kMaxSparkPatches] = {};
	float nx[kMaxSparkPatches] = {};
	float ny[kMaxSparkPatches] = {};
	float nz[kMaxSparkPatches] = {};
	float bx = 1, by = 0, bz = 0; // unit, opposite the slide
	bool awkward = false;
	float hx = 0, hy = 0, hz = 0;
};

struct ContactDiag {
	float maxLat = 0;
	float maxRatio = 0;
	float tangent = 0;
	float approach = 0;
	float align = 0;
};

inline ContactSparkFrame ClassifyContact(const Sim::CarScrape& car,
	const Sim::BallTouch& ball, ContactDiag* diag) {
	ContactSparkFrame frame;
	float maxLat = 0, maxRatio = 0;
	float horiz = std::sqrt(car.vx * car.vx + car.vy * car.vy);
	float speed = std::sqrt(horiz * horiz + car.vz * car.vz);
	float bx = 0, by = 0, bz = 0;
	bool haveBack = false;
	if (horiz > 20.0f) {
		bx = -car.vx / horiz;
		by = -car.vy / horiz;
		haveBack = true;
	} else if (speed > 20.0f) {
		bx = -car.vx / speed;
		by = -car.vy / speed;
		bz = -car.vz / speed;
		haveBack = true;
	}
	auto push = [&](float x, float y, float z, float nx, float ny, float nz) {
		if (frame.count >= kMaxSparkPatches)
			return;
		int i = frame.count++;
		frame.x[i] = x;
		frame.y[i] = y;
		frame.z[i] = z;
		frame.nx[i] = nx;
		frame.ny[i] = ny;
		frame.nz[i] = nz;
	};
	if (haveBack) {
		for (int i = 0; i < 4; ++i) {
			const Sim::WheelScrape& w = car.wheels[i];
			if (!w.touching)
				continue;
			float denom = w.lat + w.lon;
			float ratio = denom > 1.0f ? w.lat / denom : 0.0f;
			if (w.lat > maxLat)
				maxLat = w.lat;
			if (ratio > maxRatio)
				maxRatio = ratio;
			bool grinding = w.lat >= kScrapeSlipMin &&
				(ratio >= kScrapeSlipRatio || car.handbrake >= kScrapeHandbrake);
			if (grinding)
				push(w.x, w.y, w.z, w.nx, w.ny, w.nz);
		}
		auto body = [&](const Sim::BodyScrape& b) {
			if (!b.hit)
				return;
			float vn = car.vx * b.nx + car.vy * b.ny + car.vz * b.nz;
			float tx = car.vx - b.nx * vn;
			float ty = car.vy - b.ny * vn;
			float tz = car.vz - b.nz * vn;
			float ts = std::sqrt(tx * tx + ty * ty + tz * tz);
			bool wall = std::fabs(b.nz) < kWallNormalZ;
			bool belly = !wall && car.wheelsTouching < 2;
			if ((wall || belly) && ts >= kBodyScrapeMin)
				push(b.x, b.y, b.z, b.nx, b.ny, b.nz);
		};
		body(car.body);
		body(car.wall);
	}
	frame.scrape = frame.count > 0;
	frame.bx = bx;
	frame.by = by;
	frame.bz = bz;
	float tangent = 0, approach = 0, align = 0;
	if (ball.hitThisTick) {
		float nd = ball.relVx * ball.nx + ball.relVy * ball.ny + ball.relVz * ball.nz;
		approach = std::fabs(nd);
		float tx = ball.relVx - ball.nx * nd;
		float ty = ball.relVy - ball.ny * nd;
		float tz = ball.relVz - ball.nz * nd;
		tangent = std::sqrt(tx * tx + ty * ty + tz * tz);
		float fl = std::sqrt(ball.fx * ball.fx + ball.fy * ball.fy + ball.fz * ball.fz);
		if (fl > 1e-6f)
			align = std::fabs((ball.nx * ball.fx + ball.ny * ball.fy + ball.nz * ball.fz) / fl);
		bool glancing = tangent >= kAwkwardTangentMin && align < kAwkwardAlignMax;
		// Pinch: car on the ground, ball against the floor, contact on the
		// upper half (the car is coming down onto it). A square nose hit
		// stays under the tangent test and does not take this branch.
		// The 0.5 normal cutoff is UNVERIFIED.
		bool pinch = ball.carOnGround && ball.ballZ <= kPinchBallZ &&
			ball.nz > 0.5f && speed > 500.0f && tangent >= kAwkwardTangentMin;
		frame.awkward = glancing || pinch;
		frame.hx = ball.x;
		frame.hy = ball.y;
		frame.hz = ball.z;
	}
	if (diag) {
		diag->maxLat = maxLat;
		diag->maxRatio = maxRatio;
		diag->tangent = tangent;
		diag->approach = approach;
		diag->align = align;
	}
	return frame;
}

} // namespace rl
