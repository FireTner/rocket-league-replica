// ContactSparks.h — where a scrape or an awkward ball touch should spark.
#pragma once

#include <cmath>

#include "sim/Sim.h"

namespace rl {

constexpr float kScrapeSlipMin = 80.0f; // uu/s, above Car.cpp _UpdateWheels slip of 5
constexpr float kScrapeSlipRatio = 0.35f; // Car.cpp:454 frictionCurveInput
constexpr float kScrapeHandbrake = 0.5f; // RLConst.h:81
constexpr float kBodyScrapeMin = 250.0f; // uu/s along the surface
constexpr float kWallNormalZ = 0.7f; // |n.z| below this is a wall (~45 deg)
constexpr float kAwkwardTangentMin = 400.0f; // tangent of (ball.vel - car.vel), uu/s
constexpr float kAwkwardAlignMax = 0.75f; // |dot(normal, forward)| off the nose
constexpr float kPinchBallZ = 91.25f + 40.0f; // RLConst.h:117
constexpr float kBallBurstSeconds = 0.18f; // sim seconds
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
		// Pinch: grounded car, ball on the floor, contact on the upper half.
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
