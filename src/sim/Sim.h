// Deterministic 120 Hz wrapper around RocketSim.
// Values from RocketSim (MIT © 2022 ZealanL).
#pragma once

#include <cstdint>


class Sim {
public:
	// One Step120() call is one tick of 1/120 s.
	static constexpr double kTickHz = 120.0;
	static constexpr double kTickDt = 1.0 / kTickHz;
	// At most kMaxStepsPerFrame ticks run per call; leftover time stays queued.
	// A backlog past kMaxBacklogSteps (1 s) is dropped and counted.
	static constexpr int kMaxStepsPerFrame = 16;
	static constexpr int kMaxBacklogSteps = 120; // 1 s at 120 Hz

	enum Team {
		kBlue = 0,   // defends -Y, attacks +Y
		kOrange = 1, // defends +Y, attacks -Y
	};

	// Position (uu) and linear velocity (uu/s).
	struct BodySample {
		float x = 0, y = 0, z = 0;
		float vx = 0, vy = 0, vz = 0;
		// Facing yaw in radians, atan2 of forward, wrapped to [-pi, pi].
		// Blue kickoff faces +Y, so heading is +pi/2. The ball stays 0.
		float heading = 0;
	};

	// Empty meshDir, or a directory with no Soccar mesh, uses the flat-floor fallback.
	// Returns nullptr on failure.
	static Sim* CreateSoccar(const char* meshDir = nullptr);

	~Sim();

	Sim(const Sim&) = delete;
	Sim& operator=(const Sim&) = delete;

	// Fixed kickoff slot per team. Returns the car index, or -1 on failure.
	int AddCar(int team);

	// Rebuilds the car so the new hitbox is used. Bullet bakes extents at AddCar time.
	// Unknown idx or name leaves the car unchanged.
	bool SetCarPreset(int idx, const char* name);

	// throttle/steer/pitch/yaw/roll are clamped to [-1, 1]. Unknown idx is ignored.
	void SetControls(int idx, float throttle, float steer, float pitch, float yaw,
		float roll, bool jump, bool boost, bool handbrake);

	void Step120();

	// Accumulates dtSeconds and runs whole ticks. Returns how many ran.
	int StepAccumulator(double dtSeconds);

	uint64_t tick() const;
	// Ticks discarded by StepAccumulator. Stays 0 when only Step120 is used.
	uint64_t physicsDropCount() const;

	int numCars() const;
	// True when a Soccar collision mesh is loaded.
	bool usingRealSoccar() const;

	// RocketSim initializes once per process, so the first Sim chooses the mesh.
	enum MeshMode {
		kMeshFallback = 0,  // flat floor and analytic containment
		kMeshCleanroom = 1,  // generated Soccar mesh
		kMeshDumped = 2,     // hash-matched .cmf dump
	};
	MeshMode meshMode() const;
	struct MeshReport {
		MeshMode mode = kMeshFallback;
		int shapeCount = 0;    // btBvhTriangleMeshShape count for Soccar
		int triCount = 0;
		int vertCount = 0;
		bool hashMatched = false; // a .cmf hash matched a known Soccar mesh
	};
	bool GetMeshReport(MeshReport* out) const;
	const char* arenaDescription() const;

	bool GetCar(int idx, BodySample* out) const;
	bool GetBall(BodySample* out) const;

	// Newest touch wins. An equal tick keeps the lower car index.
	struct LastTouch {
		int car = -1;
		int team = -1; // kBlue or kOrange
		uint64_t tick = 0;
	};
	bool GetLastTouch(LastTouch* out) const;

	// angVel is rad/s in world space. rot is the integral of that rate (column-major).
	// The physics basis stays identity, so rot is not the body's rotMat.
	// ResetBall and SetBall restart rot at identity.
	struct BallSpin {
		float angVel[3] = {0, 0, 0};
		float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	};
	bool GetBallSpin(BallSpin* out) const;

	bool GetCarForward(int idx, float* fx, float* fy, float* fz) const;
	bool GetCarUp(int idx, float* ux, float* uy, float* uz) const;

	// Demo respawn reads RocketSim's global RNG, so this reseeds that engine too.
	void SetSeed(uint64_t seed);

	// Clamped to 0..100.
	void SetBoost(int idx, float amount);

	float GetBoost(int idx) const; // 0..100
	bool IsBoosting(int idx) const;

	// On the flat-floor fallback the pads are owned here, because RocketSim
	// ticks pads only when the arena has collision meshes.
	struct PadSample { float x, y; bool big, active; float cooldown; };
	int NumPads() const;
	bool GetPad(int i, PadSample* out) const;

	void ResetCarToSpawn(int idx);
	void ResetBall();

	void Teleport(int idx, float x, float y, float yawRad);

	// Position in uu, velocity in uu/s.
	void SetBall(float x, float y, float z, float vx, float vy, float vz);

	// True once per goal, then clears.
	bool PollGoal();

	// Attacker must be supersonic. Teammates are exempt. Drained oldest-first.
	struct DemoEvent {
		int bumper = -1; // car that demolished
		int victim = -1; // car demolished
		uint64_t tick = 0;
	};
	bool PollDemo(DemoEvent* out);

	// RocketSim's flag: enter at 2200 uu/s, hold down to 2100 for up to 1 s.
	bool IsSupersonic(int idx) const;
	// Wheel contact, including a wall or the ceiling.
	bool IsOnGround(int idx) const;
	bool IsDemoed(int idx) const; // true until the 3 s respawn

	// RocketSim CarState::HasFlipReset (Car.cpp): airborne, still able to
	// flip, and hasJumped is false — the flip came from wheel contact, not
	// from a jump. A grounded car and a normal dodge window are false.
	// Read-only. Does not write car, ball, or world state.
	bool HasFlipReset(int idx) const;

	// Read-only contact for presentation. Does not write car, ball, or world
	// state. Wheel slip matches Car::_UpdateWheels (Car.cpp:436-454): lateral
	// and long speed at the contact, uu/s. World-body point is the hitbox
	// support along the stored normal (the manifold point is not retained).
	// `wall` is the fallback analytic shell only; mesh mode leaves it clear
	// and reports the bullet world contact in `body` instead.
	struct WheelScrape {
		bool touching = false;
		float x = 0, y = 0, z = 0;
		float nx = 0, ny = 0, nz = 1;
		float lat = 0, lon = 0;
	};
	struct BodyScrape {
		bool hit = false;
		float x = 0, y = 0, z = 0;
		float nx = 0, ny = 0, nz = 1;
	};
	struct CarScrape {
		WheelScrape wheels[4];
		int wheelsTouching = 0;
		float handbrake = 0; // 0..1
		float vx = 0, vy = 0, vz = 0;
		BodyScrape body;
		BodyScrape wall;
	};
	// hitThisTick is true only on the step the collision callback fired.
	struct BallTouch {
		bool hitThisTick = false;
		float x = 0, y = 0, z = 0;
		float nx = 0, ny = 0, nz = 1;
		float relVx = 0, relVy = 0, relVz = 0; // ball.vel - car.vel
		float fx = 1, fy = 0, fz = 0;
		float ballZ = 0;
		bool carOnGround = false;
	};
	bool GetCarScrape(int idx, CarScrape* out) const;
	bool GetBallTouch(int idx, BallTouch* out) const;

 private:
	void TickFallbackPads();
	void ContainFallbackBodies();
	void ClearBallOrientation();
	void AdvanceBallOrientation();

private:
	Sim();
	bool Init(const char* meshDir);

	struct Impl;
	Impl* impl_;
};
