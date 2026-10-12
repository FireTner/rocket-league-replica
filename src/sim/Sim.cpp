// A1 SIM CORE — deterministic 120 Hz RocketSim wrapper (implementation).
//
// Determinism rules enforced here:
//  - Fixed 120 Hz tick; no wall-clock anywhere in the sim path.
//  - Cars are placed with explicit SetState (fixed kickoff slots), never via
//    RocketSim's global-RNG Respawn.
//  - Bullet solver iterations pinned (m_numIterations = 10) so a Bullet
//    version drift cannot silently change results.
//  - Build with -ffp-contract=off -fno-fast-math (see root Makefile).
//  - Sim-owned RNG is seeded fixed (SetSeed to change); no rand()/time().
#include "Sim.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <new>
#include <random>
#include <string>
#include <vector>

#include "RocketSim.h"
#include "RLConst.h"
#include "Math/Math.h" // GetRandEngine: reseed for deterministic demo respawn
#include "CollisionMeshFile/CollisionMeshFile.h"
#include "../../../third_party/RocketSim/libsrc/bullet3-3.24/BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h"
#include "../../../third_party/RocketSim/libsrc/bullet3-3.24/BulletCollision/CollisionShapes/btTriangleMesh.h"

#include "../physics/CarPresets.h"
#include "../arena/ArenaGeometry.h"
#include "../arena/ArenaConstants.h"

namespace {
// Build a RocketSim CarConfig from the A3 single-source preset table.
// Values are verbatim vs third_party/RocketSim/src/Sim/Car/CarConfig/CarConfig.cpp:20-82
// (verified by src/physics/verify_vs_rocketsim.py).
RocketSim::CarConfig ConfigFromPreset(const Physics::CarPreset& p) {
	RocketSim::CarConfig cfg;
	cfg.hitboxSize = RocketSim::Vec(p.sizeX, p.sizeY, p.sizeZ);
	cfg.hitboxPosOffset = RocketSim::Vec(p.offX, p.offY, p.offZ);
	cfg.frontWheels.wheelRadius = p.frontWheels.wheelRadius;
	cfg.frontWheels.suspensionRestLength = p.frontWheels.suspensionRestLength;
	cfg.frontWheels.connectionPointOffset = RocketSim::Vec(
		p.frontWheels.connectX, p.frontWheels.connectY, p.frontWheels.connectZ);
	cfg.backWheels.wheelRadius = p.backWheels.wheelRadius;
	cfg.backWheels.suspensionRestLength = p.backWheels.suspensionRestLength;
	cfg.backWheels.connectionPointOffset = RocketSim::Vec(
		p.backWheels.connectX, p.backWheels.connectY, p.backWheels.connectZ);
	cfg.threeWheels = p.threeWheels;
	cfg.dodgeDeadzone = p.dodgeDeadzone;
	return cfg;
}
} // namespace

struct Sim::Impl {
	RocketSim::Arena* arena = nullptr;
	std::vector<RocketSim::Car*> cars;
	double accumulator = 0.0;
	uint64_t drops = 0;
	uint64_t ticks = 0;
	bool realSoccar = false;
	bool goalLatched = false;
	// RocketSim's goal callback fires on every tick the ball is past the line
	// (Arena.cpp:749). PollGoal() is once-per-goal, so we latch on the rising
	// edge: scoredThisTick is set by the callback, scoredLastTick is the
	// previous Step120's value.
	bool scoredThisTick = false;
	bool scoredLastTick = false;
	std::mt19937_64 rng{ 0xA15EEDULL };
	// Synthetic flat floor (fallback arena only; A1-owned geometry, 1 plane).
	btStaticPlaneShape* floorShape = nullptr;
	btRigidBody* floorBody = nullptr;
	// Fallback pad league (ONLY when !realSoccar): Soccar pad layout with a
	// rule transcribed from BoostPad.cpp (_CheckCollide cylinder branch +
	// _PostTickUpdate grant + _PreTickUpdate cooldown). RocketSim ticks pads
	// only when hasArenaStuff (Arena.cpp:704 — false for THE_VOID, and SOCCAR
	// without dumped meshes aborts in _SetupArenaCollisionShapes), so the
	// fallback owns this; when meshes land, RocketSim owns pads and this
	// vector stays empty. Order: 6 big (LOCS_BIG_SOCCAR) then 28 small.
	struct PadRt {
		float x, y, z;
		bool big;
		bool active = true;
		float cooldown = 0;
	};
	std::vector<PadRt> pads;
	// Demolition events (RocketSim CarBumpCallback, isDemo=true only). Queued
	// during Step120 in contact order; drained by PollDemo oldest-first.
	struct DemoEvt { int bumper, victim; uint64_t tick; };
	std::vector<DemoEvt> demoQueue;
};

namespace {

// A real dumped arena loads as a SET of meshes (16 for standard Soccar), and
// only a dumped mesh hashes into RocketSim's known set. We detect it from the
// .cmf FILES rather than from the loaded Bullet shapes, because
// CollisionMeshFile::UpdateHash() is ORDER-DEPENDENT over `tris`: replaying it
// from a btBvhTriangleMeshShape cannot reproduce it (the BVH reorders and
// deduplicates triangles). Parsing each file with RocketSim's own reader and
// reading its `hash` is exact.
// The 16 known SOCCAR collision-mesh hashes — verbatim from
// third_party/RocketSim/src/RocketSim.cpp:20-28. A CMF produced by
// RLArenaCollisionDumper on a standard Soccar arena hits one of these; a
// clean-room reconstruction cannot (and must not, since it is not a Psyonix
// asset).
constexpr uint32_t kKnownSoccarMeshHashes[16] = {
	0xA160BAF9, 0x2811EEE8, 0xB81AC8B9, 0x760358D3,
	0x73AE4940, 0x918F4A4E, 0x1F8EE550, 0x255BA8C1,
	0x14B84668, 0xEC759EBF, 0x94FB0D5C, 0xDEA07102,
	0xBD4FBEA8, 0x39A47F63, 0x3D79D25D, 0xD84C7A68,
};

bool DirectoryHasKnownSoccarMesh(const std::string& meshDir) {
	namespace fs = std::filesystem;
	if (meshDir.empty())
		return false;
	fs::path dir = fs::path(meshDir) / "soccar";
	std::error_code ec;
	if (!fs::is_directory(dir, ec))
		return false;
	for (const auto& entry : fs::directory_iterator(dir, ec)) {
		if (ec)
			break;
		if (entry.path().extension() != ".cmf")
			continue;
		try {
			RocketSim::CollisionMeshFile file;
			RocketSim::DataStreamIn reader = RocketSim::DataStreamIn(entry.path(), false);
			file.ReadFromStream(reader, /*silent=*/true);
			for (uint32_t known : kKnownSoccarMeshHashes)
				if (known == file.hash)
					return true;
		} catch (...) {
			// Unreadable/malformed file: not proof of a real dump.
		}
	}
	return false;
}

struct MeshInfo {
	int shapeCount = 0;
	int triCount = 0;
	int vertCount = 0;
	bool hashMatched = false; // set in Init from the .cmf hashes
};

// Computed once per process, right after RocketSim init (global mesh state).
MeshInfo g_meshInfo;
bool g_meshInfoValid = false;

// First Sim created in the process wins: RocketSim global init runs once.
void InitRocketSimOnce(const std::string& meshDir) {
	using namespace RocketSim;
	// Detect a real dump from the files BEFORE the Arena is built (the flag is
	// consumed by meshMode()/GetMeshReport()).
	g_meshInfo.hashMatched = DirectoryHasKnownSoccarMesh(meshDir);
	if (!meshDir.empty() && std::filesystem::exists(meshDir)) {
		RocketSim::Init(std::filesystem::path(meshDir), /*silent=*/true);
	} else {
		// No dumped meshes (Psyonix-owned, never vendored): init mesh-free.
		std::map<GameMode, std::vector<FileData>> empty;
		RocketSim::InitFromMem(empty, /*silent=*/true);
	}
}

float Clamp11(float v) {
	if (v > 1.0f) return 1.0f;
	if (v < -1.0f) return -1.0f;
	return v;
}

void ComputeMeshInfo() {
	using namespace RocketSim;
	if (g_meshInfoValid)
		return;
	g_meshInfoValid = true;
	const auto& shapes = RocketSim::GetArenaCollisionShapes(GameMode::SOCCAR);
	g_meshInfo.shapeCount = int(shapes.size());
	for (size_t i = 0; i < shapes.size(); i++) {
		btBvhTriangleMeshShape* shape = shapes[i];
		if (!shape)
			continue;
		const unsigned char* vertexBase = nullptr;
		const unsigned char* indexBase = nullptr;
		int numVerts = 0, vertexStride = 0, indexStride = 0, numFaces = 0;
		shape->getMeshInterface()->getLockedReadOnlyVertexIndexBase(
			&vertexBase, numVerts, vertexStride, &indexBase, indexStride, numFaces);
		g_meshInfo.vertCount += numVerts;
		g_meshInfo.triCount += numFaces;
		// (hashMatched is set in Init from the .cmf files, not from shapes.)
	}
}

} // namespace

Sim::Sim() : impl_(new (std::nothrow) Impl()) {}

Sim::~Sim() {
	if (!impl_)
		return;
	// Arena's destructor removes every collision object from the bullet
	// world first, so deleting our floor body/shape after is safe.
	delete impl_->arena;
	delete impl_->floorBody;
	delete impl_->floorShape;
	delete impl_;
}

bool Sim::Init(const char* meshDir) {
	using namespace RocketSim;
	// meshDir is consumed by InitRocketSimOnce (RocketSim's mesh registry is
	// global and initialized once per process); Init() only reads the result.
	(void)meshDir;
	if (!impl_ || !impl_->arena /*fresh*/) {
		if (!impl_)
			return false;
	}
	try {
		GameMode mode = GameMode::THE_VOID;
		if (!RocketSim::GetArenaCollisionShapes(GameMode::SOCCAR).empty())
			mode = GameMode::SOCCAR;

		// NOTE (Round 1 open blocker): RocketSim's btRSBroadphase decides which
		// grid cells a static mesh occupies by calling processAllTriangles
		// (btRSBroadphase.cpp:151-170), which reads the shape's optimized BVH.
		// Pre-building that BVH here via getAabb() gets the mesh registered in
		// the cell grid (verified: staticHandleRefs=6468), but loaded .cmf mesh
		// shapes still produce ZERO manifold contacts against any dynamic body
		// (ball, box, or car), so the mesh currently contributes no collision.
		// Kept because it is correct and harmless; the real blocker is recorded
		// in docs/PHYSICS_1TO1_LOG.md Round 1 with a reproduction.
		for (btBvhTriangleMeshShape* shape :
			RocketSim::GetArenaCollisionShapes(GameMode::SOCCAR)) {
			if (shape) {
				btVector3 aabbMin, aabbMax;
				shape->getAabb(btTransform::getIdentity(), aabbMin, aabbMax);
			}
		}

		impl_->arena = RocketSim::Arena::Create(mode, ArenaConfig(), float(kTickHz));
		impl_->realSoccar = (mode == GameMode::SOCCAR);
		ComputeMeshInfo();

		// Pin the constraint solver iteration count for determinism.
		impl_->arena->_bulletWorld.getSolverInfo().m_numIterations = 10;

		// RocketSim's global RNG (Math::GetRandEngine, thread-local) is seeded
		// from wall-clock ms + thread-id hash — nondeterministic across
		// processes. Car demo respawn consumes it (Car::_Respawn slot pick),
		// so pin it to our default seed here and on every SetSeed below.
		RocketSim::Math::GetRandEngine().seed(0xA15EEDULL);

		// Demolitions: RocketSim owns the trigger (Arena.cpp
		// _BtCallback_OnCarCarCollision → DemoMode::NORMAL = attacker
		// SUPERSONIC flag, enableTeamDemos=false) and the victim's 3 s
		// auto-respawn (Car::Demolish/RLConst::DEMO_RESPAWN_TIME). We only
		// record events for gameplay polling. Callback fires inside Step120
		// (single-threaded) — queueing here is deterministic.
		impl_->arena->SetCarBumpCallback(
			[](RocketSim::Arena* arena, RocketSim::Car* bumper, RocketSim::Car* victim,
				bool isDemo, void* userInfo) {
				(void)arena;
				if (!isDemo)
					return; // plain bumps are not surfaced
				Sim::Impl* im = (Sim::Impl*)userInfo;
				int bi = -1, vi = -1;
				for (size_t i = 0; i < im->cars.size(); i++) {
					if (im->cars[i] == bumper) bi = (int)i;
					if (im->cars[i] == victim) vi = (int)i;
				}
				im->demoQueue.push_back({ bi, vi, im->ticks });
			},
			impl_);

		// Goals: RocketSim owns the rule (Arena::IsBallScored,
		// Arena.cpp:893-900: |ballY| > goalBaseThresholdY + ballRadius) and
		// calls this every tick the ball is past the line. PollGoal() is
		// documented as once-per-goal, so record the scored flag and let
		// Step120 latch on the rising edge (see Impl::scoredLastTick). The
		// callback can only be registered on a real arena (THE_VOID asserts),
		// so the fallback path keeps latching from A2's analytic IsGoal.
		if (impl_->realSoccar) {
			impl_->arena->SetGoalScoreCallback(
				[](RocketSim::Arena* arena, RocketSim::Team scoringTeam, void* userInfo) {
					(void)arena;
					(void)scoringTeam;
					Sim::Impl* im = (Sim::Impl*)userInfo;
					im->scoredThisTick = true;
				},
				impl_);
		}

		if (!impl_->realSoccar) {
			// A1-owned synthetic ground plane at z=0 so wheels make contact
			// and throttle/ground physics works without dumped Psyonix meshes.
			// Mirrors Arena::_AddStaticCollisionShape conventions (userPointer
			// must be the Arena: the contact callback casts world bodies to
			// Arena* for ball+world hits).
			impl_->floorShape = new btStaticPlaneShape(btVector3(0, 0, 1), 0.0f);
			impl_->floorBody = new btRigidBody(0.0f, nullptr, impl_->floorShape);
			btTransform t;
			t.setIdentity();
			impl_->floorBody->setWorldTransform(t);
			impl_->floorBody->setUserPointer(impl_->arena);
			impl_->floorBody->setFriction(RLConst::ARENA_COLLISION_BASE_FRICTION);
			impl_->floorBody->setRestitution(RLConst::ARENA_COLLISION_BASE_RESTITUTION);
			impl_->arena->_bulletWorld.addRigidBody(impl_->floorBody);

			// Fallback pad league: Soccar layout, Sim-owned rule (see Impl).
			// Positions verbatim from RLConst.h:274-312 (UU).
			impl_->pads.clear();
			for (int i = 0; i < RLConst::BoostPads::LOCS_AMOUNT_BIG; i++) {
				const Vec& lp = RLConst::BoostPads::LOCS_BIG_SOCCAR[i];
				impl_->pads.push_back({lp.x, lp.y, lp.z, true, true, 0});
			}
			for (int i = 0; i < RLConst::BoostPads::LOCS_AMOUNT_SMALL_SOCCAR; i++) {
				const Vec& lp = RLConst::BoostPads::LOCS_SMALL_SOCCAR[i];
				impl_->pads.push_back({lp.x, lp.y, lp.z, false, true, 0});
			}
		}
	} catch (...) {
		delete impl_->arena;
		impl_->arena = nullptr;
		delete impl_->floorBody;
		impl_->floorBody = nullptr;
		delete impl_->floorShape;
		impl_->floorShape = nullptr;
		return false;
	}
	return impl_->arena != nullptr;
}

Sim* Sim::CreateSoccar(const char* meshDir) {
	std::string dir = (meshDir && meshDir[0] != '\0') ? meshDir : "";
	static std::once_flag initFlag;
	std::call_once(initFlag, [&]() { InitRocketSimOnce(dir); });

	Sim* sim = new (std::nothrow) Sim();
	if (!sim || !sim->impl_) {
		delete sim;
		return nullptr;
	}
	if (!sim->Init(dir.c_str())) {
		delete sim;
		return nullptr;
	}
	(void)meshDir;
	return sim;
}

int Sim::AddCar(int team) {
	using namespace RocketSim;
	if (!impl_ || !impl_->arena)
		return -1;
	if (team != kBlue && team != kOrange)
		return -1;
	RocketSim::Team rsTeam = (team == kOrange) ? RocketSim::Team::ORANGE : RocketSim::Team::BLUE;
	Car* car = nullptr;
	try {
		// Default preset is Octane (matches Arena::AddCar default arg
		// CAR_CONFIG_OCTANE). CarConfig() would be a zero hitbox — never use.
		car = impl_->arena->AddCar(rsTeam, ConfigFromPreset(Physics::kDefaultPreset));
	} catch (...) {
		return -1;
	}
	if (!car)
		return -1;

	// Deterministic placement: fixed slot per team, explicit state.
	// (AddCar internally Respawns via the global RNG; we overwrite that.)
	// Facing mirrors RLConst.h:356-364 back-spawn yaw (Blue +PI/2 -> +y toward
	// ball, Orange -PI/2; GetForwardVec(yaw)= (cos yaw, sin yaw, 0)).
	CarState s;
	s.pos = Vec(0.0f, (team == kBlue) ? -2300.0f : 2300.0f,
		RLConst::CAR_SPAWN_REST_Z);
	s.rotMat = Angle((team == kBlue) ? float(M_PI_2) : float(-M_PI_2), 0.0f, 0.0f).ToRotMat();
	s.vel = Vec(0.0f, 0.0f, 0.0f);
	s.angVel = Vec(0.0f, 0.0f, 0.0f);
	s.boost = impl_->arena->GetMutatorConfig().carSpawnBoostAmount;
	s.isOnGround = true;
	car->SetState(s);
	car->controls = CarControls();

	impl_->cars.push_back(car);
	return int(impl_->cars.size()) - 1;
}

bool Sim::SetCarPreset(int idx, const char* name) {
	using namespace RocketSim;
	if (!impl_ || !impl_->arena)
		return false;
	if (idx < 0 || idx >= int(impl_->cars.size()))
		return false;
	const Physics::CarPreset* p = Physics::FindPreset(name);
	if (!p)
		return false;
	Car* old = impl_->cars[size_t(idx)];
	if (!old)
		return false;
	// Preserve dynamic state + driver inputs across the rebuild. Mutating
	// old->config alone would NOT change collisions (Bullet bakes extents at
	// AddCar time), so remove + re-add with the new config at the same index.
	CarState st = old->GetState();
	CarControls cc = old->controls;
	RocketSim::Team rsTeam = old->team;
	impl_->arena->RemoveCar(old); // frees old, pointer now invalid
	Car* car = nullptr;
	try {
		car = impl_->arena->AddCar(rsTeam, ConfigFromPreset(*p));
	} catch (...) {
		return false;
	}
	if (!car)
		return false;
	car->SetState(st);
	car->controls = cc;
	impl_->cars[size_t(idx)] = car;
	return true;
}

void Sim::SetControls(int idx, float throttle, float steer, float pitch, float yaw,
	float roll, bool jump, bool boost, bool handbrake) {
	if (!impl_ || idx < 0 || idx >= int(impl_->cars.size()))
		return;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car)
		return;
	car->controls.throttle = Clamp11(throttle);
	car->controls.steer = Clamp11(steer);
	car->controls.pitch = Clamp11(pitch);
	car->controls.yaw = Clamp11(yaw);
	car->controls.roll = Clamp11(roll);
	car->controls.jump = jump;
	car->controls.boost = boost;
	car->controls.handbrake = handbrake;
}

void Sim::Step120() {
	if (!impl_ || !impl_->arena)
		return;
	impl_->scoredThisTick = false;
	impl_->arena->Step(1);
	impl_->ticks++;
	// Latch goals on the rising edge so PollGoal() is once-per-goal even
	// though RocketSim's callback fires every scored tick (Arena.cpp:749).
	if (impl_->scoredThisTick && !impl_->scoredLastTick)
		impl_->goalLatched = true;
	impl_->scoredLastTick = impl_->scoredThisTick;
	if (impl_->realSoccar) {
		// Mesh arena: RocketSim owns walls/pads/goals (incl. goal events).
		return;
	}
	if (!impl_->pads.empty())
		TickFallbackPads();
	ContainFallbackBodies();
}

// Fallback arena containment (ONLY when !realSoccar): the mesh-less plane is
// infinite, so analytic A2 walls keep ball+cars inside the 1:1 Soccar shell.
// Ball: A2 CollideSphere push-out + restitution reflect (mutator
// ballWorldRestitution), goal mouth/net handled by A2 (drivable) with
// IsGoal latching. Cars: push-out + normal-velocity removal (plastic wall,
// e=0.1) so players can't drive through rendered walls; full car-vs-wall
// solver waits on mesh arenas. Deterministic: fixed order, no RNG.
void Sim::ContainFallbackBodies() {
	using namespace RocketSim;
	if (!impl_ || !impl_->arena)
		return;
	const auto& mut = impl_->arena->GetMutatorConfig();
	// Ball first (it moves fastest).
	if (impl_->arena->ball) {
		BallState bs = impl_->arena->ball->GetState();
		Arena::Vec3 c{bs.pos.x, bs.pos.y, bs.pos.z};
		Arena::Vec3 n{0, 0, 1};
		if (Arena::CollideSphere(c, Arena::BALL_RADIUS, n)) {
			bs.pos = Vec(c.x, c.y, c.z);
			if (n.z < 0.9f) {
				// Walls/ceiling/nets: analytic reflect (no physical
				// counterpart on the infinite plane). Floor (n.z~+1) is
				// owned by Bullet (plane restitution); push-out only.
				float vn = bs.vel.x * n.x + bs.vel.y * n.y + bs.vel.z * n.z;
				float e = vn < 0 ? mut.ballWorldRestitution : 0;
				bs.vel = Vec(bs.vel.x - (1 + e) * vn * n.x,
					bs.vel.y - (1 + e) * vn * n.y,
					bs.vel.z - (1 + e) * vn * n.z);
			}
			impl_->arena->ball->SetState(bs);
		}
		if (Arena::IsGoal(Arena::Vec3{bs.pos.x, bs.pos.y, bs.pos.z}))
			impl_->goalLatched = true;
	}
	for (Car* car : impl_->cars) {
		if (!car)
			continue;
		CarState cs = car->GetState();
		Arena::Vec3 c{cs.pos.x, cs.pos.y, cs.pos.z};
		Arena::Vec3 n{0, 0, 1};
		// Car center probe radius ~50uu. Floor contacts (n.z~+1) are
		// skipped: the car rests at z=17 < 50 by design and the wheel
		// raycasts own the ground. Walls/ceiling get push-out + normal
		// velocity removal (plastic wall, e=0.1).
		if (Arena::CollideSphere(c, 50.0f, n) && n.z < 0.9f) {
			float vn = cs.vel.x * n.x + cs.vel.y * n.y + cs.vel.z * n.z;
			cs.pos = Vec(c.x, c.y, c.z);
			if (vn < 0)
				cs.vel = Vec(cs.vel.x - 1.1f * vn * n.x,
					cs.vel.y - 1.1f * vn * n.y,
					cs.vel.z - 1.1f * vn * n.z);
			car->SetState(cs);
		}
	}
}

void Sim::TickFallbackPads() {
	using namespace RocketSim;
	if (!impl_ || !impl_->arena)
		return;
	// Cooldowns (mirror BoostPad::_PreTickUpdate: cooldown -= dt, floor 0;
	// active = (cooldown == 0)).
	for (auto& p : impl_->pads) {
		if (p.cooldown > 0)
			p.cooldown = p.cooldown - float(kTickDt) > 0 ? p.cooldown - float(kTickDt) : 0;
		p.active = (p.cooldown == 0);
	}
	// Pickups (mirror BoostPad::_CheckCollide cylinder branch +
	// _PostTickUpdate grant). Fixed order: cars by index, pads by index.
	const auto& mut = impl_->arena->GetMutatorConfig();
	for (Car* car : impl_->cars) {
		if (!car)
			continue;
		CarState cs = car->GetState();
		for (auto& p : impl_->pads) {
			if (!p.active)
				continue;
			float rad = p.big ? float(RLConst::BoostPads::CYL_RAD_BIG)
							  : float(RLConst::BoostPads::CYL_RAD_SMALL);
			float dx = cs.pos.x - p.x, dy = cs.pos.y - p.y;
			if (dx * dx + dy * dy >= rad * rad)
				continue;
			float dz = cs.pos.z - p.z;
			if (dz >= float(RLConst::BoostPads::CYL_HEIGHT) ||
				dz <= -float(RLConst::BoostPads::CYL_HEIGHT))
				continue;
			CarState s2 = car->GetState();
			float add = p.big ? float(RLConst::BoostPads::BOOST_AMOUNT_BIG)
							  : float(RLConst::BoostPads::BOOST_AMOUNT_SMALL);
			s2.boost = s2.boost + add > RLConst::BOOST_MAX ? RLConst::BOOST_MAX : s2.boost + add;
			car->SetState(s2);
			p.active = false;
			p.cooldown = p.big ? mut.boostPadCooldown_Big : mut.boostPadCooldown_Small;
		}
	}
}

int Sim::StepAccumulator(double dtSeconds) {
	if (!impl_ || !(dtSeconds > 0.0))
		return 0;
	if (dtSeconds > 1.0) // clamp a pathological single dt
		dtSeconds = 1.0;
	impl_->accumulator += dtSeconds;
	int steps = 0;
	// Drain whole ticks, bounded per call so one frame cannot monopolise the
	// loop. Leftover stays in the accumulator and is drained next frame.
	while (impl_->accumulator >= kTickDt && steps < kMaxStepsPerFrame) {
		Step120();
		impl_->accumulator -= kTickDt;
		steps++;
	}
	// Hard ceiling: only a backlog larger than 1 s of sim is discarded (and
	// counted). This is the spiral-of-death guard; normal render hitches stay
	// well under it and are caught up without dropping a tick.
	const double kBacklogLimit = kMaxBacklogSteps * kTickDt;
	if (impl_->accumulator > kBacklogLimit) {
		uint64_t dropped = uint64_t((impl_->accumulator - kBacklogLimit) / kTickDt);
		impl_->drops += dropped;
		impl_->accumulator = kBacklogLimit;
	}
	return steps;
}

uint64_t Sim::tick() const {
	return impl_ ? impl_->ticks : 0;
}

uint64_t Sim::physicsDropCount() const {
	return impl_ ? impl_->drops : 0;
}

int Sim::numCars() const {
	return impl_ ? int(impl_->cars.size()) : 0;
}

bool Sim::usingRealSoccar() const {
	return impl_ && impl_->realSoccar;
}

Sim::MeshMode Sim::meshMode() const {
	if (!impl_ || !impl_->realSoccar)
		return kMeshFallback;
	return g_meshInfo.hashMatched ? kMeshDumped : kMeshCleanroom;
}

bool Sim::GetMeshReport(MeshReport* out) const {
	if (!out)
		return false;
	out->mode = meshMode();
	out->shapeCount = impl_ && impl_->realSoccar ? g_meshInfo.shapeCount : 0;
	out->triCount = impl_ && impl_->realSoccar ? g_meshInfo.triCount : 0;
	out->vertCount = impl_ && impl_->realSoccar ? g_meshInfo.vertCount : 0;
	out->hashMatched = impl_ && impl_->realSoccar && g_meshInfo.hashMatched;
	return true;
}

const char* Sim::arenaDescription() const {
	switch (meshMode()) {
	case kMeshDumped: return "dumped-mesh";
	case kMeshCleanroom: return "cleanroom-mesh";
	default: return "flat-floor-fallback";
	}
}

bool Sim::GetCar(int idx, BodySample* out) const {
	if (!impl_ || !out || idx < 0 || idx >= int(impl_->cars.size()))
		return false;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car)
		return false;
	RocketSim::CarState st = car->GetState();
	out->x = st.pos.x;
	out->y = st.pos.y;
	out->z = st.pos.z;
	out->vx = st.vel.x;
	out->vy = st.vel.y;
	out->vz = st.vel.z;
	return true;
}

bool Sim::GetBall(BodySample* out) const {
	if (!impl_ || !out || !impl_->arena || !impl_->arena->ball)
		return false;
	RocketSim::BallState st = impl_->arena->ball->GetState();
	out->x = st.pos.x;
	out->y = st.pos.y;
	out->z = st.pos.z;
	out->vx = st.vel.x;
	out->vy = st.vel.y;
	out->vz = st.vel.z;
	return true;
}

bool Sim::GetCarForward(int idx, float* fx, float* fy, float* fz) const {
	if (!impl_ || idx < 0 || idx >= int(impl_->cars.size()))
		return false;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car || !fx || !fy || !fz)
		return false;
	RocketSim::Vec f = car->GetForwardDir();
	*fx = f.x;
	*fy = f.y;
	*fz = f.z;
	return true;
}

bool Sim::GetCarUp(int idx, float* ux, float* uy, float* uz) const {
	if (!impl_ || idx < 0 || idx >= int(impl_->cars.size()))
		return false;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car || !ux || !uy || !uz)
		return false;
	RocketSim::Vec u = car->GetUpDir();
	*ux = u.x;
	*uy = u.y;
	*uz = u.z;
	return true;
}

void Sim::SetSeed(uint64_t seed) {
	if (impl_)
		impl_->rng.seed(seed);
	// Keep RocketSim's global engine (demo respawn slot pick) in sync — see
	// the seeding comment in Init().
	RocketSim::Math::GetRandEngine().seed(seed);
}

void Sim::SetBoost(int idx, float amount) {
	if (!impl_ || idx < 0 || idx >= int(impl_->cars.size()))
		return;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car)
		return;
	RocketSim::CarState st = car->GetState();
	st.boost = amount < 0.0f ? 0.0f : (amount > 100.0f ? 100.0f : amount);
	car->SetState(st);
}

float Sim::GetBoost(int idx) const {
	if (!impl_ || idx < 0 || idx >= int(impl_->cars.size()))
		return 0.0f;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car)
		return 0.0f;
	return car->GetState().boost;
}

bool Sim::IsBoosting(int idx) const {
	if (!impl_ || idx < 0 || idx >= int(impl_->cars.size()))
		return false;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car)
		return false;
	return car->GetState().isBoosting;
}

int Sim::NumPads() const {
	if (!impl_ || !impl_->arena)
		return 0;
	if (!impl_->realSoccar)
		return int(impl_->pads.size());
	return int(impl_->arena->GetBoostPads().size());
}

bool Sim::GetPad(int i, PadSample* out) const {
	if (!impl_ || !impl_->arena || !out)
		return false;
	if (!impl_->realSoccar) {
		if (i < 0 || i >= int(impl_->pads.size()))
			return false;
		const auto& p = impl_->pads[size_t(i)];
		out->x = p.x;
		out->y = p.y;
		out->big = p.big;
		out->active = p.active;
		out->cooldown = p.cooldown;
		return true;
	}
	const auto& pads = impl_->arena->GetBoostPads();
	if (i < 0 || i >= int(pads.size()) || !pads[size_t(i)])
		return false;
	RocketSim::BoostPad* p = pads[size_t(i)];
	RocketSim::BoostPadState st = p->GetState();
	out->x = p->_posBT.x * BT_TO_UU;
	out->y = p->_posBT.y * BT_TO_UU;
	out->big = p->config.isBig;
	out->active = st.isActive;
	out->cooldown = st.cooldown;
	return true;
}

void Sim::ResetCarToSpawn(int idx) {
	if (!impl_ || !impl_->arena || idx < 0 || idx >= int(impl_->cars.size()))
		return;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car)
		return;
	// Blue slots mirror AddCar: (0,-2300,+y); extra cars park down the line.
	int team = (car->team == RocketSim::Team::ORANGE) ? kOrange : kBlue;
	RocketSim::CarState s;
	if (team == kBlue && idx == 0) {
		s.pos = RocketSim::Vec(0.0f, -2300.0f, RocketSim::RLConst::CAR_SPAWN_REST_Z);
		s.rotMat = RocketSim::Angle(float(M_PI_2), 0.0f, 0.0f).ToRotMat();
	} else if (team == kOrange && idx == 1) {
		s.pos = RocketSim::Vec(0.0f, 2300.0f, RocketSim::RLConst::CAR_SPAWN_REST_Z);
		s.rotMat = RocketSim::Angle(float(-M_PI_2), 0.0f, 0.0f).ToRotMat();
	} else {
		float x = -1500.0f + 750.0f * float(idx);
		s.pos = RocketSim::Vec(x, (team == kBlue) ? -2300.0f : 2300.0f,
			RocketSim::RLConst::CAR_SPAWN_REST_Z);
		s.rotMat = RocketSim::Angle((team == kBlue) ? float(M_PI_2) : float(-M_PI_2),
			0.0f, 0.0f).ToRotMat();
	}
	s.vel = RocketSim::Vec(0.0f, 0.0f, 0.0f);
	s.angVel = RocketSim::Vec(0.0f, 0.0f, 0.0f);
	s.boost = impl_->arena->GetMutatorConfig().carSpawnBoostAmount;
	s.isOnGround = true;
	car->SetState(s);
	car->controls = RocketSim::CarControls();
}

void Sim::ResetBall() {
	if (!impl_ || !impl_->arena || !impl_->arena->ball)
		return;
	RocketSim::BallState s;
	s.pos = RocketSim::Vec(0.0f, 0.0f, RocketSim::RLConst::BALL_REST_Z);
	s.vel = RocketSim::Vec(0.0f, 0.0f, 0.0f);
	s.angVel = RocketSim::Vec(0.0f, 0.0f, 0.0f);
	impl_->arena->ball->SetState(s);
}

void Sim::SetBall(float x, float y, float z, float vx, float vy, float vz) {
	if (!impl_ || !impl_->arena || !impl_->arena->ball)
		return;
	RocketSim::BallState s;
	s.pos = RocketSim::Vec(x, y, z);
	s.vel = RocketSim::Vec(vx, vy, vz);
	// At-rest placement: Ball::SetState only writes ACTIVE_TAG when vel/angVel
	// are non-zero (Ball.cpp:46-47), and Arena::Step re-sleeps a zero-velocity
	// ball every tick (Arena.cpp:693-698). A fully zero state would therefore
	// never fall. A negligible angular kick keeps it awake; it is deterministic
	// and has no effect on translation or on the ball's spherical collision
	// shape. A non-zero linear placement is used verbatim.
	constexpr float kAtRestKick = 1e-4f; // rad/s
	s.angVel = (vx == 0.0f && vy == 0.0f && vz == 0.0f)
		? RocketSim::Vec(kAtRestKick, 0.0f, 0.0f)
		: RocketSim::Vec(0.0f, 0.0f, 0.0f);
	impl_->arena->ball->SetState(s);
}

void Sim::Teleport(int idx, float x, float y, float yawRad) {
	if (!impl_ || !impl_->arena || idx < 0 || idx >= int(impl_->cars.size()))
		return;
	RocketSim::Car* car = impl_->cars[size_t(idx)];
	if (!car)
		return;
	RocketSim::CarState s;
	s.pos = RocketSim::Vec(x, y, RocketSim::RLConst::CAR_SPAWN_REST_Z);
	s.rotMat = RocketSim::Angle(yawRad, 0.0f, 0.0f).ToRotMat();
	s.vel = RocketSim::Vec(0.0f, 0.0f, 0.0f);
	s.angVel = RocketSim::Vec(0.0f, 0.0f, 0.0f);
	s.isOnGround = true;
	car->SetState(s);
	car->controls = RocketSim::CarControls();
}

bool Sim::PollGoal() {
	if (!impl_ || !impl_->goalLatched)
		return false;
	impl_->goalLatched = false;
	return true;
}

bool Sim::PollDemo(DemoEvent* out) {
	if (!impl_ || impl_->demoQueue.empty())
		return false;
	if (out) {
		const auto& e = impl_->demoQueue.front();
		out->bumper = e.bumper;
		out->victim = e.victim;
		out->tick = e.tick;
	}
	impl_->demoQueue.erase(impl_->demoQueue.begin());
	return true;
}

bool Sim::IsSupersonic(int idx) const {
	if (!impl_ || idx < 0 || idx >= (int)impl_->cars.size())
		return false;
	return impl_->cars[idx]->GetState().isSupersonic;
}

bool Sim::IsDemoed(int idx) const {
	if (!impl_ || idx < 0 || idx >= (int)impl_->cars.size())
		return false;
	return impl_->cars[idx]->GetState().isDemoed;
}
