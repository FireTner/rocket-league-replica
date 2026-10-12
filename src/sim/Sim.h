// A1 SIM CORE — deterministic 120 Hz RocketSim wrapper.
//
// Lightweight public header: other streams code against THIS file. It does not
// include any RocketSim headers, so consumers never pay the Bullet/RocketSim
// compile cost and never depend on vendored internals.
//
// Ownership: native C++ (A1). MIT-only, no Psyonix assets, headless.
#pragma once

#include <cstdint>


class Sim {
public:
	// Fixed physics rate. One Step120() call == exactly one tick of 1/120 s.
	static constexpr double kTickHz = 120.0;
	static constexpr double kTickDt = 1.0 / kTickHz;
	// Accumulator catch-up limits.
	//
	// Per StepAccumulator() call we run at most kMaxStepsPerFrame ticks. This
	// is NOT a tick-dropping guard: leftover time stays in the accumulator and
	// is drained by later calls, so a render hitch delays physics by a frame
	// but never skips it (the renderer repeats frames; physics may not).
	//
	// A hard ceiling still exists for genuinely pathological input (a debugger
	// pause, laptop suspend, or a multi-second stall): if the backlog exceeds
	// kMaxBacklogSteps ticks (1 s of sim), that backlog is dropped and counted
	// in physicsDropCount(). Under normal play this never triggers.
	static constexpr int kMaxStepsPerFrame = 16;
	static constexpr int kMaxBacklogSteps = 120; // 1 s at 120 Hz

	enum Team {
		kBlue = 0,   // defends -Y, attacks +Y
		kOrange = 1, // defends +Y, attacks -Y
	};

	// Position (UU) + linear velocity (UU/s) sample, shared by cars and ball.
	struct BodySample {
		float x = 0, y = 0, z = 0;
		float vx = 0, vy = 0, vz = 0;
		// Facing yaw (radians) = atan2 of the car's forward axis, wrapped to
		// [-pi, pi]. Blue kickoff spawn faces +Y, so heading = +pi/2 there.
		// Always 0 for the ball (no facing). Added for the TAS dump format.
		float heading = 0;
	};

	// Create a Soccar-rules simulation.
	//
	// meshDir: optional directory of dumped Rocket League arena collision
	//   meshes (Psyonix-owned, never vendored). If null/empty, or if the
	//   directory holds no Soccar meshes, the sim falls back to a
	//   deterministic A1-owned flat-floor test arena (same car/ball physics,
	//   same 120 Hz tick, no walls/goals/pads). See usingRealSoccar().
	// Returns nullptr only on fatal failure (never throws).
	static Sim* CreateSoccar(const char* meshDir = nullptr);

	~Sim();

	Sim(const Sim&) = delete;
	Sim& operator=(const Sim&) = delete;

	// Add a car for team (kBlue/kOrange). The car is placed deterministically
	// (fixed kickoff slot per team, explicit state — no global-RNG spawn), so
	// identical call sequences produce identical simulations.
	// Returns the car index (>= 0) or -1 on failure (bad team / alloc fail).
	int AddCar(int team);

	// Switch car idx to a named body style ("Octane", "Dominus", "Plank",
	// "Breakout", "Hybrid", "Merc", "Psyclops"; ASCII case-insensitive).
	// Lookup via Physics::FindPreset (src/physics/CarPresets.h, A3-owned,
	// header-only). The car is rebuilt with the new hitbox/wheels: same
	// index, dynamic state and controls preserved (mutating the config alone
	// would NOT change collisions — Bullet bakes extents at AddCar time).
	// Returns false on unknown idx/name (car left untouched).
	bool SetCarPreset(int idx, const char* name);

	// Set driver inputs for car idx. Ranges match RocketSim CarControls:
	// throttle/steer/pitch/yaw/roll in [-1, 1]; jump/boost/handbrake booleans.
	// Out-of-range floats are clamped. Unknown idx is ignored (no crash).
	void SetControls(int idx, float throttle, float steer, float pitch, float yaw,
		float roll, bool jump, bool boost, bool handbrake);

	// Advance the simulation by exactly one 120 Hz tick.
	void Step120();

	// Variable-dt driver: accumulate dtSeconds and run whole ticks.
	// Runs at most kMaxStepsPerFrame ticks; leftover beyond that is dropped
	// (spiral-of-death guard) and counted. Returns ticks actually simulated.
	int StepAccumulator(double dtSeconds);

	// Total ticks simulated since creation.
	uint64_t tick() const;
	// Ticks discarded by the StepAccumulator guard (0 when Step120 is used).
	uint64_t physicsDropCount() const;

	int numCars() const;
	// True when running on real dumped Soccar meshes, false for the A1
	// flat-floor fallback arena.
	bool usingRealSoccar() const;

	// Which collision world the sim actually got. RocketSim is initialized
	// once per process, so the FIRST Sim created decides for every later one
	// (see InitRocketSimOnce / Sim.cpp).
	enum MeshMode {
		kMeshFallback = 0,  // THE_VOID + A1 plane + analytic containment
		kMeshCleanroom = 1,  // A1-generated clean-room Soccar mesh (no Psyonix asset)
		kMeshDumped = 2,     // user-supplied Psyonix .cmf dump, hash-matched
	};
	MeshMode meshMode() const;
	// Flat geometry report for the mesh-mode headless assertion / gates.
	struct MeshReport {
		MeshMode mode = kMeshFallback;
		int shapeCount = 0;    // RocketSim btBvhTriangleMeshShape count for SOCCAR
		int triCount = 0;      // triangles across those shapes
		int vertCount = 0;     // vertices across those shapes
		bool hashMatched = false; // clean-room/dumped: matched a known Psyonix hash
	};
	bool GetMeshReport(MeshReport* out) const;
	// Best-effort human-readable arena description ("fallback", "cleanroom-mesh",
	// "dumped-mesh") for banners and logs.
	const char* arenaDescription() const;

	bool GetCar(int idx, BodySample* out) const;
	bool GetBall(BodySample* out) const;

	// Car forward + up unit vectors (for TAS sign calibration + camera).
	// Returns false on unknown idx. Deterministic (pure function of car state).
	bool GetCarForward(int idx, float* fx, float* fy, float* fz) const;
	bool GetCarUp(int idx, float* ux, float* uy, float* uz) const;

	// Reseed the Sim-owned RNG (default fixed seed). Currently spawn placement
	// is fully explicit, so this only affects future randomized helpers; it is
	// provided so no caller ever needs a global/wall-clock RNG near the sim.
	void SetSeed(uint64_t seed);

	// Test-harness helper: set car idx boost meter (0..100, clamped).
	// For TAS gate runs (unlimited-boost top-speed / supersonic).
	void SetBoost(int idx, float amount);

	// Gameplay/HUD reads (pure state functions, false on bad idx).
	float GetBoost(int idx) const; // 0..100
	bool IsBoosting(int idx) const;

	// Boost pads: Soccar layout (6 big + 28 small). On real Soccar meshes
	// these mirror RocketSim's live pads; on the fallback floor they are
	// Sim-owned (transcribed rule, see Sim.cpp) since RocketSim only ticks
	// pads when hasArenaStuff. For HUD/render + G9 verification.
	struct PadSample { float x, y; bool big, active; float cooldown; };
	int NumPads() const;
	bool GetPad(int i, PadSample* out) const;

	// Free-play resets: deterministic kickoff spawn / center ball drop.
	void ResetCarToSpawn(int idx);
	void ResetBall();

	// Test-harness teleport (deterministic placement for gate tapes, e.g.
	// big-pad runs). Not gameplay.
	void Teleport(int idx, float x, float y, float yawRad);

	// Test-harness ball placement (position UU + velocity UU/s), for
	// mesh-mode collision gates (bounce/containment/goal-mouth probes).
	// Not gameplay.
	void SetBall(float x, float y, float z, float vx, float vy, float vz);

	// Goal latch (fallback: A2 IsGoal; mesh: RocketSim events — polled the
	// same way). Returns true once per scored goal, then clears.
	bool PollGoal();

	// Demolitions (RocketSim CarCarContact → Car::Demolish, DemoMode::NORMAL:
	// attacker SUPERSONIC flag at contact, teammates exempt). Events queue
	// during Step120 and are drained oldest-first — deterministic order.
	struct DemoEvent {
		int bumper = -1; // car index that caused the demolition
		int victim = -1; // car index demolished
		uint64_t tick = 0; // tick the event fired on
	};
	// Returns true once per queued demolition event, then clears it.
	bool PollDemo(DemoEvent* out);

	// Car state reads (pure functions, false on bad idx). Supersonic uses
	// RocketSim's flag with maintain semantics (start 2200, hold to 2100 for
	// up to 1 s) — NOT a bare speed check (RLConst.h:68-76).
	bool IsSupersonic(int idx) const;
	bool IsDemoed(int idx) const; // true from demolition until auto-respawn (3 s)

 private:
	void TickFallbackPads();
	void ContainFallbackBodies();

private:
	Sim();
	bool Init(const char* meshDir);

	struct Impl;
	Impl* impl_;
};
