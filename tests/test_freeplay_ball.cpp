// Free-play ball commands (keys 1-5). Pure checks plus one scripted sim run.
// Headless: no window. Prints positions and velocities.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "game/FreeplayBall.h"
#include "sim/Sim.h"

static int failures = 0;

#define CHECK(cond, msg)                         \
	do {                                         \
		if (cond) {                              \
			std::printf("  ok   %s\n", msg);     \
		} else {                                 \
			std::printf("  FAIL %s\n", msg);     \
			++failures;                          \
		}                                        \
	} while (0)

static bool Near(float a, float b, float eps) {
	return std::fabs(a - b) <= eps;
}

static float Speed(float vx, float vy, float vz) {
	return std::sqrt(vx * vx + vy * vy + vz * vz);
}

static float AngSpeed(Sim* sim) {
	Sim::BallSpin s;
	if (!sim->GetBallSpin(&s))
		return -1.f;
	return std::sqrt(s.angVel[0] * s.angVel[0] + s.angVel[1] * s.angVel[1] +
		s.angVel[2] * s.angVel[2]);
}

static rl::FreeplayCar CarFrom(const Sim::BodySample& s, float fx, float fy, float fz,
	float ux, float uy, float uz) {
	rl::FreeplayCar c;
	c.body = {s.x, s.y, s.z, s.vx, s.vy, s.vz};
	c.fx = fx;
	c.fy = fy;
	c.fz = fz;
	c.ux = ux;
	c.uy = uy;
	c.uz = uz;
	return c;
}

static void ReadCar(Sim* sim, int idx, rl::FreeplayCar* out) {
	Sim::BodySample s;
	sim->GetCar(idx, &s);
	float fx = 0.f, fy = 1.f, fz = 0.f, ux = 0.f, uy = 0.f, uz = 1.f;
	sim->GetCarForward(idx, &fx, &fy, &fz);
	sim->GetCarUp(idx, &ux, &uy, &uz);
	*out = CarFrom(s, fx, fy, fz, ux, uy, uz);
}

static rl::FreeplaySample ReadBall(Sim* sim) {
	Sim::BodySample s;
	sim->GetBall(&s);
	return {s.x, s.y, s.z, s.vx, s.vy, s.vz};
}

static void Apply(Sim* sim, const rl::FreeplayCommand& cmd) {
	sim->SetBall(cmd.x, cmd.y, cmd.z, cmd.vx, cmd.vy, cmd.vz);
}

static void Drive(Sim* sim, int car, int ticks) {
	// Park the ball off the boost line so the run-up is not a car-ball hit.
	sim->SetBall(3000.f, -2300.f, 400.f, 0.f, 0.f, 0.f);
	sim->SetBoost(car, 100.f);
	sim->SetControls(car, 1.f, 0.f, 0.f, 0.f, 0.f, false, true, false);
	for (int i = 0; i < ticks; ++i)
		sim->Step120();
	sim->SetControls(car, 0.f, 0.f, 0.f, 0.f, 0.f, false, false, false);
}


static void PureGeometry() {
	std::printf("[pure] cited placement\n");
	const Physics::CarPreset& oct = Physics::kDefaultPreset;
	rl::FreeplayCar car;
	car.body = {0.f, -2300.f, 17.f, 0.f, 1400.f, 0.f};
	car.fx = 0.f;
	car.fy = 1.f;
	car.fz = 0.f;
	car.ux = 0.f;
	car.uy = 0.f;
	car.uz = 1.f;
	rl::FreeplaySample ball{1234.f, -567.f, 250.f, 9.f, 8.f, 7.f};
	rl::FreeplayCommand cmd;

	CHECK(ComputeFreeplayBall(0, car, ball, oct, &cmd), "key 1 computes");
	float front = oct.offX + oct.sizeX * 0.5f;
	float dist = front + Physics::Tuning::kBallRadiusSoccar + rl::kPossessionClearance;
	CHECK(Near(cmd.x, 0.f, 1e-3f), "key 1 x stays on the forward line");
	CHECK(Near(cmd.y, -2300.f + dist, 1e-2f), "key 1 is the front-face clearance ahead");
	CHECK(Near(cmd.z, Physics::Tuning::kBallRestZ, 1e-3f), "key 1 rests at BALL_REST_Z");
	CHECK(Near(cmd.vx, 0.f, 1e-4f) && Near(cmd.vy, 1400.f, 1e-3f) && Near(cmd.vz, 0.f, 1e-4f),
		"key 1 copies the car linear velocity");
	std::printf("  key1 dist=%.4f (front %.4f + r %.4f + clearance %.4f) z=%.4f vel=(%.3f,%.3f,%.3f)\n",
		dist, front, Physics::Tuning::kBallRadiusSoccar, rl::kPossessionClearance,
		cmd.z, cmd.vx, cmd.vy, cmd.vz);

	CHECK(ComputeFreeplayBall(1, car, ball, oct, &cmd), "key 2 computes");
	float lz = oct.offZ + oct.sizeZ * 0.5f + Physics::Tuning::kBallRadiusSoccar;
	CHECK(Near(cmd.x, oct.offY * -1.f, 1e-3f), "key 2 right-offset is the hitbox offY");
	CHECK(Near(cmd.y, -2300.f + oct.offX, 1e-2f), "key 2 sits forward of the origin by offX");
	CHECK(Near(cmd.z, 17.f + lz, 1e-2f), "key 2 sits one radius above the hitbox roof");
	CHECK(Near(cmd.vy, 1400.f, 1e-3f) && Near(cmd.vz, 0.f, 1e-4f),
		"key 2 copies car velocity (vz included; here 0)");
	std::printf("  key2 hood local (octane) lx=%.4f ly=%.4f lz=%.4f world=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f)\n",
		oct.offX, oct.offY, lz, cmd.x, cmd.y, cmd.z, cmd.vx, cmd.vy, cmd.vz);
	std::printf("  key2 rule: world = car + offX*forward + offY*right + (offZ+sizeZ/2+radius)*up; vel = car vel; spin cleared by SetBall\n");
	std::printf("  key2 source: CarPresets Octane (CarConfig.cpp:20-40) radius %.4f (RLConst.h:117) rest car z is the live body, not a plugin\n",
		Physics::Tuning::kBallRadiusSoccar);

	CHECK(ComputeFreeplayBall(2, car, ball, oct, &cmd), "key 3 computes");
	CHECK(cmd.x == ball.x && cmd.y == ball.y && cmd.z == ball.z, "key 3 position is the ball's position");
	CHECK(Near(cmd.vx, 0.f, 1e-4f) && Near(cmd.vy, 1400.f, 1e-3f) && Near(cmd.vz, 0.f, 1e-4f),
		"key 3 moving car: velocity is the car's horizontal velocity");

	car.body.vx = 0.f;
	car.body.vy = 0.f;
	car.body.vz = 3.f;
	CHECK(ComputeFreeplayBall(2, car, ball, oct, &cmd), "key 3 stopped computes");
	CHECK(cmd.x == ball.x && cmd.y == ball.y && cmd.z == ball.z, "key 3 stopped still does not move the ball");
	CHECK(Near(cmd.vx, 0.f, 1e-3f) && Near(cmd.vy, rl::kPassStoppedSpeed, 1e-2f) && Near(cmd.vz, 0.f, 1e-4f),
		"key 3 stopped: speed along car forward, vz 0");
	std::printf("  key3 stopped speed %.3f UNVERIFIED (below %.3f uu/s uses forward)\n",
		rl::kPassStoppedSpeed, Physics::Tuning::kStoppingForwardVel);
}

static void Scripted(Sim* sim, int car) {
	const Physics::CarPreset& oct = Physics::kDefaultPreset;

	std::printf("[script] key 1 at speed\n");
	sim->ResetCarToSpawn(car);
	sim->ResetBall();
	Drive(sim, car, 220);
	rl::FreeplayCar pose;
	ReadCar(sim, car, &pose);
	rl::FreeplaySample before = ReadBall(sim);
	rl::FreeplayCommand cmd;
	CHECK(ComputeFreeplayBall(0, pose, before, oct, &cmd), "script key 1");
	Apply(sim, cmd);
	rl::FreeplaySample at = ReadBall(sim);
	float carSpeed = Speed(pose.body.vx, pose.body.vy, pose.body.vz);
	float ballSpeed = Speed(at.vx, at.vy, at.vz);
	std::printf("  car pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f) speed=%.3f fwd=(%.4f,%.4f,%.4f)\n",
		pose.body.x, pose.body.y, pose.body.z, pose.body.vx, pose.body.vy, pose.body.vz, carSpeed,
		pose.fx, pose.fy, pose.fz);
	float ang = AngSpeed(sim);
	std::printf("  ball at command pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f) speed=%.3f ang=%.6f\n",
		at.x, at.y, at.z, at.vx, at.vy, at.vz, ballSpeed, ang);
	CHECK(ang >= 0.f && ang < 1e-5f, "key 1 linear command clears angular velocity");
	float ahead = (at.x - pose.body.x) * pose.fx + (at.y - pose.body.y) * pose.fy;
	CHECK(ahead > 100.f, "key 1 ball is in front of the car");
	CHECK(Near(at.z, Physics::Tuning::kBallRestZ, 0.05f), "key 1 ball is at rest height");
	CHECK(ballSpeed > 1000.f, "key 1 ball keeps the car's speed (not dropped to 0)");
	CHECK(Near(at.vx, pose.body.vx, 0.05f) && Near(at.vy, pose.body.vy, 0.05f) &&
			Near(at.vz, pose.body.vz, 0.05f),
		"key 1 linear velocity matches the car");
	for (int i = 0; i < 36; ++i)
		sim->Step120();
	rl::FreeplaySample later = ReadBall(sim);
	float laterSpeed = Speed(later.vx, later.vy, later.vz);
	std::printf("  ball after 36 ticks pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f) speed=%.3f\n",
		later.x, later.y, later.z, later.vx, later.vy, later.vz, laterSpeed);
	CHECK(laterSpeed > 400.f, "key 1 ball is still moving 0.3 s later");

	std::printf("[script] key 2 at speed, throttle held\n");
	sim->ResetCarToSpawn(car);
	sim->ResetBall();
	sim->SetBall(3000.f, -2300.f, 400.f, 0.f, 0.f, 0.f);
	sim->SetBoost(car, 100.f);
	sim->SetControls(car, 1.f, 0.f, 0.f, 0.f, 0.f, false, true, false);
	for (int i = 0; i < 220; ++i)
		sim->Step120();
	// Throttle stays down. Releasing it applies the coasting brake and the car slows out from under the ball.
	sim->SetControls(car, 1.f, 0.f, 0.f, 0.f, 0.f, false, false, false);
	ReadCar(sim, car, &pose);
	before = ReadBall(sim);
	CHECK(ComputeFreeplayBall(1, pose, before, oct, &cmd), "script key 2");
	Apply(sim, cmd);
	at = ReadBall(sim);
	float hoodZ = pose.body.z + oct.offZ + oct.sizeZ * 0.5f + Physics::Tuning::kBallRadiusSoccar;
	std::printf("  car pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f) spd=%.3f\n",
		pose.body.x, pose.body.y, pose.body.z, pose.body.vx, pose.body.vy, pose.body.vz,
		Speed(pose.body.vx, pose.body.vy, pose.body.vz));
	ang = AngSpeed(sim);
	std::printf("  ball on hood pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f) roofCenterZ=%.3f ang=%.6f\n",
		at.x, at.y, at.z, at.vx, at.vy, at.vz, hoodZ, ang);
	CHECK(ang >= 0.f && ang < 1e-5f, "key 2 clears leftover spin");
	CHECK(at.z > pose.body.z + 80.f, "key 2 ball is above the car origin");
	CHECK(Near(at.z, hoodZ, 0.2f), "key 2 height is the hitbox roof + radius");
	float along = (at.x - pose.body.x) * pose.fx + (at.y - pose.body.y) * pose.fy +
		(at.z - pose.body.z) * pose.fz;
	CHECK(along > 5.f, "key 2 is forward of the origin (hitbox offX), not stacked on it");
	CHECK(Near(at.vx, pose.body.vx, 0.05f) && Near(at.vy, pose.body.vy, 0.05f),
		"key 2 horizontal velocity matches the car");
	const float radius = Physics::Tuning::kBallRadiusSoccar;
	float gapAt[3] = {};
	float relHAt[3] = {};
	float relZAt[3] = {};
	int saved = 0;
	for (int i = 0; i < 120; ++i) {
		sim->Step120();
		int t = i + 1;
		if (t != 24 && t != 48 && t != 120)
			continue;
		ReadCar(sim, car, &pose);
		later = ReadBall(sim);
		float roof = oct.offZ + oct.sizeZ * 0.5f;
		float hx = pose.body.x + pose.fx * oct.offX + pose.ux * roof;
		float hy = pose.body.y + pose.fy * oct.offX + pose.uy * roof;
		float hz = pose.body.z + pose.fz * oct.offX + pose.uz * roof;
		float gap = (later.x - hx) * pose.ux + (later.y - hy) * pose.uy + (later.z - hz) * pose.uz;
		float dvx = later.vx - pose.body.vx;
		float dvy = later.vy - pose.body.vy;
		float relH = std::sqrt(dvx * dvx + dvy * dvy);
		float relZ = later.vz - pose.body.vz;
		gapAt[saved] = gap;
		relHAt[saved] = relH;
		relZAt[saved] = relZ;
		++saved;
		std::printf("  t=%d gapUp=%.3f (radius %.3f) relH=%.3f relZ=%.3f ball pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f) carVel=(%.3f,%.3f,%.3f) carSpd=%.3f\n",
			t, gap, radius, relH, relZ,
			later.x, later.y, later.z, later.vx, later.vy, later.vz,
			pose.body.vx, pose.body.vy, pose.body.vz,
			Speed(pose.body.vx, pose.body.vy, pose.body.vz));
	}
	CHECK(saved == 3, "key 2 sampled 0.2 s, 0.4 s, and 1.0 s");
	CHECK(std::fabs(gapAt[0] - radius) < 0.5f, "key 2 still on the hood at 0.2 s");
	CHECK(std::fabs(relZAt[0]) < 2.f, "key 2 vertical speed matches the car at 0.2 s");
	CHECK(relHAt[0] < 15.f, "key 2 horizontal speed matches the car at 0.2 s");
	CHECK(std::fabs(gapAt[1] - radius) < 0.5f, "key 2 still on the hood at 0.4 s");
	CHECK(std::fabs(relZAt[1]) < 2.f, "key 2 vertical speed matches the car at 0.4 s");
	CHECK(relHAt[1] < 15.f, "key 2 horizontal speed matches the car at 0.4 s");
	CHECK(std::fabs(gapAt[2] - radius) < 1.f, "key 2 still on the hood at 1.0 s");
	CHECK(std::fabs(relZAt[2]) < 5.f, "key 2 vertical speed matches the car at 1.0 s");
	CHECK(relHAt[2] < 30.f, "key 2 horizontal speed matches the car at 1.0 s");

	std::printf("[script] key 3 position unchanged, velocity along travel\n");
	sim->ResetCarToSpawn(car);
	sim->ResetBall();
	Drive(sim, car, 220);
	sim->SetBall(1234.5f, -567.25f, 250.f, -40.f, 80.f, 15.f);
	rl::FreeplaySample marked = ReadBall(sim);
	ReadCar(sim, car, &pose);
	CHECK(ComputeFreeplayBall(2, pose, marked, oct, &cmd), "script key 3");
	std::printf("  BEFORE command ball pos=(%.6f,%.6f,%.6f) vel=(%.6f,%.6f,%.6f)\n",
		marked.x, marked.y, marked.z, marked.vx, marked.vy, marked.vz);
	std::printf("  car vel=(%.6f,%.6f,%.6f) fwd=(%.6f,%.6f,%.6f)\n",
		pose.body.vx, pose.body.vy, pose.body.vz, pose.fx, pose.fy, pose.fz);
	std::printf("  COMMAND writes pos=(%.6f,%.6f,%.6f) vel=(%.6f,%.6f,%.6f)\n",
		cmd.x, cmd.y, cmd.z, cmd.vx, cmd.vy, cmd.vz);
	Apply(sim, cmd);
	at = ReadBall(sim);
	std::printf("  AFTER command (no step) ball pos=(%.6f,%.6f,%.6f) vel=(%.6f,%.6f,%.6f)\n",
		at.x, at.y, at.z, at.vx, at.vy, at.vz);
	CHECK(Near(at.x, marked.x, 0.05f) && Near(at.y, marked.y, 0.05f) && Near(at.z, marked.z, 0.05f),
		"key 3 position unchanged on the command");
	float hs = std::sqrt(pose.body.vx * pose.body.vx + pose.body.vy * pose.body.vy);
	float dot = at.vx * pose.body.vx + at.vy * pose.body.vy;
	CHECK(hs > 500.f, "key 3 car is actually moving");
	CHECK(dot > 0.99f * hs * hs, "key 3 velocity points along the car's horizontal travel");
	CHECK(Near(at.vz, 0.f, 0.05f), "key 3 vertical velocity is 0");
	sim->Step120();
	later = ReadBall(sim);
	std::printf("  AFTER 1 tick ball pos=(%.6f,%.6f,%.6f) vel=(%.6f,%.6f,%.6f)\n",
		later.x, later.y, later.z, later.vx, later.vy, later.vz);
	CHECK(std::fabs(later.y - marked.y) > 1.f, "key 3 moves on the following tick (velocity, not a second teleport)");

	std::printf("[script] key 3 stopped car uses forward, still no teleport\n");
	sim->ResetCarToSpawn(car);
	sim->ResetBall();
	sim->SetBall(-800.f, 400.f, 180.f, 0.f, 0.f, 0.f);
	marked = ReadBall(sim);
	ReadCar(sim, car, &pose);
	ComputeFreeplayBall(2, pose, marked, oct, &cmd);
	Apply(sim, cmd);
	at = ReadBall(sim);
	std::printf("  stopped BEFORE pos=(%.4f,%.4f,%.4f) AFTER pos=(%.4f,%.4f,%.4f) vel=(%.3f,%.3f,%.3f) fwd=(%.3f,%.3f)\n",
		marked.x, marked.y, marked.z, at.x, at.y, at.z, at.vx, at.vy, at.vz, pose.fx, pose.fy);
	CHECK(Near(at.x, marked.x, 0.05f) && Near(at.y, marked.y, 0.05f) && Near(at.z, marked.z, 0.05f),
		"stopped key 3 does not relocate");
	CHECK(at.vy > 1000.f && std::fabs(at.vx) < 1.f, "stopped blue car passes along +Y forward");
}

static void Keys45(Sim* sim, int car) {
	const Physics::CarPreset& oct = Physics::kDefaultPreset;
	std::printf("[script] key 4 launch\n");
	sim->ResetCarToSpawn(car);
	sim->ResetBall();
	sim->SetBall(1500.f, -800.f, 120.f, 300.f, -100.f, 50.f);
	rl::FreeplaySample marked = ReadBall(sim);
	rl::FreeplayCar pose;
	ReadCar(sim, car, &pose);
	rl::FreeplayCommand cmd;
	CHECK(ComputeFreeplayBall(3, pose, marked, oct, &cmd), "key 4 computes");
	Apply(sim, cmd);
	rl::FreeplaySample at = ReadBall(sim);
	std::printf("  launch BEFORE pos=(%.4f,%.4f,%.4f) AFTER pos=(%.4f,%.4f,%.4f) vel=(%.3f,%.3f,%.3f)\n",
		marked.x, marked.y, marked.z, at.x, at.y, at.z, at.vx, at.vy, at.vz);
	CHECK(Near(at.x, marked.x, 0.05f) && Near(at.y, marked.y, 0.05f) && Near(at.z, marked.z, 0.05f),
		"key 4 does not teleport");
	CHECK(Near(at.vx, 0.f, 0.05f) && Near(at.vy, 0.f, 0.05f) && Near(at.vz, rl::kHoopsTipoffZVel, 0.05f),
		"key 4 upward speed is the hoops tip-off 1000 uu/s");

	std::printf("[script] key 5 closest goal, not the end the car faces\n");
	sim->ResetCarToSpawn(car);
	sim->ResetBall();
	// Orange half, nose pointed at the blue goal (the far one).
	sim->Teleport(car, 0.f, 2000.f, -1.5707963f);
	ReadCar(sim, car, &pose);
	rl::FreeplaySample ball = ReadBall(sim);
	CHECK(ComputeFreeplayBall(4, pose, ball, oct, &cmd), "key 5 computes");
	Apply(sim, cmd);
	at = ReadBall(sim);
	std::printf("  car y=%.3f fwd=(%.3f,%.3f,%.3f) ball pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f)\n",
		pose.body.y, pose.fx, pose.fy, pose.fz, at.x, at.y, at.z, at.vx, at.vy, at.vz);
	CHECK(pose.fy < -0.5f, "setup faces the blue goal");
	CHECK(at.y > 0.f, "placement is on the orange side (closest goal)");
	CHECK(at.vy > 500.f, "velocity goes toward the orange goal, not the facing end");
	CHECK(std::fabs(at.x) < 1.f, "shot is on the goal center line");

	std::printf("[script] key 5 after a high ball keeps that position\n");
	sim->SetBall(1800.f, 200.f, 1500.f, 0.f, 0.f, 10.f);
	marked = ReadBall(sim);
	ReadCar(sim, car, &pose);
	ComputeFreeplayBall(4, pose, marked, oct, &cmd);
	Apply(sim, cmd);
	at = ReadBall(sim);
	std::printf("  high BEFORE pos=(%.3f,%.3f,%.3f) AFTER pos=(%.3f,%.3f,%.3f) vel=(%.3f,%.3f,%.3f)\n",
		marked.x, marked.y, marked.z, at.x, at.y, at.z, at.vx, at.vy, at.vz);
	CHECK(Near(at.x, marked.x, 0.05f) && Near(at.y, marked.y, 0.05f) && Near(at.z, marked.z, 0.05f),
		"high defend keeps the ball where launch left it");
	CHECK(at.vy > 0.f, "high ball is still aimed at the closest (orange) goal");
}

int main() {
	PureGeometry();
	Sim* sim = Sim::CreateSoccar(nullptr);
	if (!sim) {
		std::printf("FAIL CreateSoccar\n");
		return 1;
	}
	int car = sim->AddCar(Sim::kBlue);
	if (car < 0) {
		std::printf("FAIL AddCar\n");
		delete sim;
		return 1;
	}
	Scripted(sim, car);
	if (failures) {
		std::printf("KEYS 1-3 FAIL %d (4-5 not asserted)\n", failures);
		delete sim;
		return 1;
	}
	std::printf("KEYS 1-3 PASS\n");
	Keys45(sim, car);
	delete sim;
	if (failures) {
		std::printf("FAIL %d\n", failures);
		return 1;
	}
	std::printf("PASS freeplay ball\n");
	return 0;
}
