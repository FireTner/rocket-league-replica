// tools/meshgen — deterministic CLEAN-ROOM Soccar arena collision-mesh generator.
//
// Emits a RocketSim-loadable `.cmf` (CollisionMeshFile) built from published
// Soccar dimensions and our own geometry code. It is NOT a Psyonix asset and is
// NOT a dump: it will never hash-match one of RocketSim's 16 known SOCCAR mesh
// hashes (third_party/RocketSim/src/RocketSim.cpp:20-28). That is the point —
// repo rule 1 forbids vendoring Psyonix geometry, so the in-repo mesh is a
// from-scratch reconstruction; a user-supplied RLArenaCollisionDumper dump
// remains the only source of the *literal* mesh (docs/MESH_SETUP.md).
//
// What it does and does not model
// -------------------------------
// RocketSim's Arena::_SetupArenaCollisionShapes() ALWAYS adds analytic
// floor/ceiling/side-wall PLANES for SOCCAR
// (third_party/RocketSim/src/Sim/Arena/Arena.cpp:1030-1062): z=0, z=ARENA_HEIGHT,
// x=+/-ARENA_EXTENT_X. So the mesh only has to supply what those planes cannot:
//   * the two BACK walls (y = +/-5120) with a goal-mouth opening,
//   * the four 45-degree corner chamfers (|x| + |y| = 8064),
//   * the wall height profile: floor->wall fillet + wall + wall->ceiling fillet,
//   * the goal recess (side walls, back net, net roof) and sharp mouth edges.
// Duplicate side-wall or floor/ceiling triangles would fight the planes, so this
// generator deliberately omits them.
//
// Height profile (per footprint edge, "u" = inward distance from the wall):
//   0 <= u <= R   : concave floor fillet, center (R, R)
//   R <= u <= Rc  : vertical wall
//   u >= Rc       : concave ceiling fillet, center (Rc, H - Rc)
// with R = RAMP_RADIUS, Rc = CEIL_RADIUS, H = CEILING_Z.
//
// Fidelity caveat (recorded, not hidden): the real arena's bottom ramp is
// documented as "Approximately 256 (non-circular)"
// (https://wiki.rlbot.org/v4/botmaking/useful-game-values/). We model it as a
// circular fillet of that radius. Everything else in the footprint/goal comes
// from published dimensions. See docs/MESH_SETUP.md for the full ledger.
//
// Determinism: pure float math over a fixed lattice; no RNG, no clock, no
// iteration over unordered containers. Same input => byte-identical output.
//
// Usage: make_soccar_mesh <out.cmf> [--ramp-seg N] [--ceil-seg N]
// MIT License. No third-party code, no Psyonix data.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// --- published Soccar dimensions (uu) ---------------------------------------
// Kept in sync with src/arena/ArenaConstants.h by
// tools/verify/mesh_geometry.py (which parses both).
constexpr float SIDE_WALL_X = 4096.0f;  // RLConst.h:14 ARENA_EXTENT_X
constexpr float BACK_WALL_Y = 5120.0f;  // RLConst.h:15 ARENA_EXTENT_Y
constexpr float CEILING_Z   = 2048.0f;  // RLConst.h:16 ARENA_HEIGHT
constexpr float CORNER_SUM  = 8064.0f;  // RLBot wiki: corners at +/-8064, 45 deg
constexpr float GOAL_HALF_W = 892.755f; // RLBot wiki: goal center-to-post
constexpr float GOAL_HEIGHT = 642.775f; // RLBot wiki: goal height
constexpr float GOAL_DEPTH  = 880.0f;   // RLBot wiki: goal depth
constexpr float RAMP_RADIUS = 256.0f;   // RLBot wiki: wall bottom ramp ~256
constexpr float CEIL_RADIUS = 256.0f;   // symmetric with RAMP_RADIUS (UNVERIFIED)

struct V3 { float x, y, z; };

struct Tri { int a, b, c; };

// Face provenance, used only by the winding self-check.
enum FaceKind {
	kFaceFootprint = 0, // swept onto a footprint edge (chamfer or back wall)
	kFaceGoalSide,      // goal recess +X/-X wall
	kFaceGoalBack,      // goal recess back net
	kFaceGoalRoof,      // goal recess roof
};

struct Mesh {
	std::vector<V3> verts;
	std::vector<Tri> tris;
	std::vector<FaceKind> kinds; // parallel to tris

	int AddVert(const V3& v) {
		verts.push_back(v);
		return int(verts.size()) - 1;
	}
	// Adds quad a-b-c-d (in perimeter order) as two triangles: (a,b,c) and
	// (a,c,d). Each caller passes its perimeter in the order that makes this
	// winding face the free interior; CountWindingErrors enforces the result.
	void AddQuad(const V3& a, const V3& b, const V3& c, const V3& d, FaceKind kind) {
		int ia = AddVert(a), ib = AddVert(b), ic = AddVert(c), id = AddVert(d);
		tris.push_back({ia, ib, ic});
		kinds.push_back(kind);
		tris.push_back({ia, ic, id});
		kinds.push_back(kind);
	}
};

// --- footprint: 8 edges (4 axis walls, 4 chamfers) --------------------------
// Outward unit normals; the edge line is n.p = c.
constexpr float S = 0.70710678118654752f; // 1/sqrt(2)
constexpr float CH = CORNER_SUM * S;      // chamfer line offset 8064/sqrt(2)

struct Edge { float nx, ny, c; };
constexpr Edge kEdges[8] = {
	{ 1.0f,  0.0f, SIDE_WALL_X}, // 0 +X side wall
	{ S,     S,    CH},          // 1 +X/+Y chamfer
	{ 0.0f,  1.0f, BACK_WALL_Y}, // 2 +Y back wall
	{-S,     S,    CH},          // 3 -X/+Y chamfer
	{-1.0f,  0.0f, SIDE_WALL_X}, // 4 -X side wall
	{-S,    -S,    CH},          // 5 -X/-Y chamfer
	{ 0.0f, -1.0f, BACK_WALL_Y}, // 6 -Y back wall
	{ S,    -S,    CH},          // 7 +X/-Y chamfer
};

// Endpoints of edge e = intersections with its neighbours' lines.
void EdgeEndpoints(int e, V3& a, V3& b) {
	int ePrev = (e + 7) % 8, eNext = (e + 1) % 8;
	auto inter = [](int i, int j, float& x, float& y) {
		float det = kEdges[i].nx * kEdges[j].ny - kEdges[j].nx * kEdges[i].ny;
		x = (kEdges[i].c * kEdges[j].ny - kEdges[j].c * kEdges[i].ny) / det;
		y = (kEdges[i].nx * kEdges[j].c - kEdges[j].nx * kEdges[i].c) / det;
	};
	inter(ePrev, e, a.x, a.y);
	inter(e, eNext, b.x, b.y);
	a.z = b.z = 0.0f;
}

// --- height profile ---------------------------------------------------------
struct ProfPt { float u, v; };

// Polyline from the floor (u=R, v=0) up the wall to the ceiling (u=Rc, v=H).
std::vector<ProfPt> BuildProfile(int rampSeg, int ceilSeg) {
	std::vector<ProfPt> p;
	const float R = RAMP_RADIUS, Rc = CEIL_RADIUS, H = CEILING_Z;
	const float kHalfPi = 1.5707963267948966f;
	// Floor fillet: phi 0 (floor tangent, u=R, v=0) -> pi/2 (wall joint, u=0, v=R).
	for (int i = 0; i <= rampSeg; i++) {
		float phi = (float)i / (float)rampSeg * kHalfPi;
		p.push_back({R - R * std::sin(phi), R - R * std::cos(phi)});
	}
	p.push_back({0.0f, H - Rc}); // wall top joint
	// Ceiling fillet: psi 0 (wall joint) -> pi/2 (ceiling tangent, u=Rc, v=H).
	for (int i = 1; i <= ceilSeg; i++) {
		float psi = (float)i / (float)ceilSeg * kHalfPi;
		p.push_back({Rc - Rc * std::cos(psi), (H - Rc) + Rc * std::sin(psi)});
	}
	return p;
}

// Sweeps the profile along a straight footprint segment [p0,p1] with outward
// normal (nx,ny). Emits inward-facing quads.
void AddWallStrip(Mesh& m, const V3& p0, const V3& p1, float nx, float ny,
	const std::vector<ProfPt>& prof) {
	const float ix = -nx, iy = -ny;
	for (size_t r = 0; r + 1 < prof.size(); r++) {
		V3 A{p0.x + ix * prof[r].u, p0.y + iy * prof[r].u, prof[r].v};
		V3 B{p1.x + ix * prof[r].u, p1.y + iy * prof[r].u, prof[r].v};
		V3 C{p1.x + ix * prof[r + 1].u, p1.y + iy * prof[r + 1].u, prof[r + 1].v};
		V3 D{p0.x + ix * prof[r + 1].u, p0.y + iy * prof[r + 1].u, prof[r + 1].v};
		// Reversed perimeter: the profile runs CCW in (u,v), so the natural
		// (A,B,C,D) order faces OUTWARD; reversing makes it face the interior.
		m.AddQuad(D, C, B, A, kFaceFootprint);
	}
}

// Emit the solid runs of a back-wall segment [a,b] (both endpoints on the back
// wall line): everything OUTSIDE [-GOAL_HALF_W, +GOAL_HALF_W].
void AddBackWallSolidRuns(Mesh& m, const V3& a, const V3& b, float nx, float ny,
	const std::vector<ProfPt>& prof) {
	const float x0 = a.x, x1 = b.x;
	const float lo = (x0 < x1 ? x0 : x1), hi = (x0 < x1 ? x1 : x0);
	auto run = [&](float ra, float rb) {
		if (rb - ra <= 1e-4f)
			return;
		// Preserve the original a->b direction so winding stays consistent.
		V3 p0 = (x0 < x1) ? V3{ra, a.y, 0.0f} : V3{rb, a.y, 0.0f};
		V3 p1 = (x0 < x1) ? V3{rb, a.y, 0.0f} : V3{ra, a.y, 0.0f};
		AddWallStrip(m, p0, p1, nx, ny, prof);
	};
	run(lo, -GOAL_HALF_W);
	run(GOAL_HALF_W, hi);
}

// Goal recess for one end: sign = +1 (+Y net) or -1 (-Y net).
// Perimeter orders below are parameterised by `sign` because the winding that
// points a face into the net (or into the field, for the back net) flips with
// the end. Each order was derived from AddQuad's (a,b,c)+(a,c,d) triangulation
// and is enforced by CountWindingErrors.
void AddGoalRecess(Mesh& m, float sign) {
	const float yL = sign * BACK_WALL_Y;                // goal line
	const float yB = sign * (BACK_WALL_Y + GOAL_DEPTH); // back of net
	const float xL = -GOAL_HALF_W, xR = GOAL_HALF_W;
	const float zT = GOAL_HEIGHT;

	// Side wall at x = +GOAL_HALF_W. Interior is toward -X.
	if (sign > 0)
		m.AddQuad({xR, yL, 0}, {xR, yL, zT}, {xR, yB, zT}, {xR, yB, 0}, kFaceGoalSide);
	else
		m.AddQuad({xR, yL, 0}, {xR, yB, 0}, {xR, yB, zT}, {xR, yL, zT}, kFaceGoalSide);

	// Side wall at x = -GOAL_HALF_W. Interior is toward +X.
	if (sign > 0)
		m.AddQuad({xL, yL, zT}, {xL, yL, 0}, {xL, yB, 0}, {xL, yB, zT}, kFaceGoalSide);
	else
		m.AddQuad({xL, yL, 0}, {xL, yL, zT}, {xL, yB, zT}, {xL, yB, 0}, kFaceGoalSide);

	// Back net. Interior (the field side) is toward -sign * Y.
	if (sign > 0)
		m.AddQuad({xL, yB, 0}, {xR, yB, 0}, {xR, yB, zT}, {xL, yB, zT}, kFaceGoalBack);
	else
		m.AddQuad({xL, yB, 0}, {xL, yB, zT}, {xR, yB, zT}, {xR, yB, 0}, kFaceGoalBack);

	// Net roof. Interior is below.
	if (sign > 0)
		m.AddQuad({xR, yL, zT}, {xL, yL, zT}, {xL, yB, zT}, {xR, yB, zT}, kFaceGoalRoof);
	else
		m.AddQuad({xL, yL, zT}, {xR, yL, zT}, {xR, yB, zT}, {xL, yB, zT}, kFaceGoalRoof);
}

// --- physics hash (verbatim CollisionMeshFile::UpdateHash) ------------------
uint32_t UpdateHash(const Mesh& m) {
	constexpr uint32_t HASH_VAL_MUELLER = 0x45D9F3B;
	constexpr uint32_t HASH_VAL_SHIFT = 0x9E3779B9;
	uint32_t hash = uint32_t(m.verts.size()) +
		uint32_t(m.tris.size()) * uint32_t(m.verts.size());
	for (const Tri& t : m.tris) {
		int idx[3] = {t.a, t.b, t.c};
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				uint32_t curVal;
				float f = (&m.verts[size_t(idx[i])].x)[j];
				std::memcpy(&curVal, &f, sizeof(uint32_t));
				for (int k = 0; k < 2; k++)
					curVal = ((curVal >> 16) ^ curVal) * HASH_VAL_MUELLER;
				curVal = (curVal >> 16) ^ curVal;
				hash ^= curVal + HASH_VAL_SHIFT + (hash << 6) + (hash >> 2);
			}
		}
	}
	return hash;
}

// --- .cmf writer (int32 numTris, numVerts, tris, verts, all LE) -------------
bool WriteCmf(const std::string& path, const Mesh& m) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) {
		std::fprintf(stderr, "cannot open %s for writing\n", path.c_str());
		return false;
	}
	int32_t numTris = int32_t(m.tris.size());
	int32_t numVerts = int32_t(m.verts.size());
	std::fwrite(&numTris, sizeof(numTris), 1, f);
	std::fwrite(&numVerts, sizeof(numVerts), 1, f);
	for (const Tri& t : m.tris) {
		int32_t idx[3] = {t.a, t.b, t.c};
		std::fwrite(idx, sizeof(int32_t), 3, f);
	}
	for (const V3& v : m.verts) {
		float xyz[3] = {v.x, v.y, v.z};
		std::fwrite(xyz, sizeof(float), 3, f);
	}
	std::fclose(f);
	return true;
}

// --- winding self-check -----------------------------------------------------
// A face is correctly wound when its geometric normal points into the free
// side of the surface it belongs to. Each face carries its provenance, so we
// test against that family's known interior direction:
//   footprint walls -> the inward normal of their nearest footprint edge
//   goal side walls -> -X (never +X)
//   goal back net   -> toward the field (sign of y)
//   goal roof       -> -Z
// This is exact for our construction (walls always face the arena interior).
int CountWindingErrors(const Mesh& m) {
	int bad = 0;
	for (size_t ti = 0; ti < m.tris.size(); ti++) {
		const Tri& t = m.tris[ti];
		FaceKind kind = (ti < m.kinds.size()) ? m.kinds[ti] : kFaceFootprint;
		const V3& A = m.verts[size_t(t.a)];
		const V3& B = m.verts[size_t(t.b)];
		const V3& C = m.verts[size_t(t.c)];
		float e1x = B.x - A.x, e1y = B.y - A.y, e1z = B.z - A.z;
		float e2x = C.x - A.x, e2y = C.y - A.y, e2z = C.z - A.z;
		float nx = e1y * e2z - e1z * e2y;
		float ny = e1z * e2x - e1x * e2z;
		float nz = e1x * e2y - e1y * e2x;
		float len = std::sqrt(nx * nx + ny * ny + nz * nz);
		if (len < 1e-9f) { // degenerate
			bad++;
			continue;
		}
		nx /= len; ny /= len; nz /= len;
		float cx = (A.x + B.x + C.x) / 3.0f;
		float cy = (A.y + B.y + C.y) / 3.0f;

		float ix = 0.0f, iy = 0.0f, iz = 0.0f;
		switch (kind) {
		case kFaceGoalSide:
			// Goal sides are at the mouth X extremes; the net interior is
			// always toward x = 0.
			ix = (cx > 0.0f) ? -1.0f : 1.0f;
			break;
		case kFaceGoalBack:
			iy = (cy > 0.0f) ? -1.0f : 1.0f; // toward the field
			break;
		case kFaceGoalRoof:
			iz = -1.0f; // downward, into the net
			break;
		case kFaceFootprint:
		default: {
			// Nearest footprint edge by signed distance (largest = closest to
			// or inside that edge's boundary).
			float best = -1e30f;
			int bestE = 0;
			for (int e = 0; e < 8; e++) {
				float d = cx * kEdges[e].nx + cy * kEdges[e].ny - kEdges[e].c;
				if (d > best) {
					best = d;
					bestE = e;
				}
			}
			ix = -kEdges[bestE].nx;
			iy = -kEdges[bestE].ny;
			break;
		}
		}
		if (nx * ix + ny * iy + nz * iz <= 0.0f)
			bad++;
	}
	return bad;
}

// --- assembly ---------------------------------------------------------------
Mesh BuildSoccarMesh(int rampSeg, int ceilSeg) {
	Mesh m;
	const std::vector<ProfPt> prof = BuildProfile(rampSeg, ceilSeg);

	// Walls that RocketSim's analytic planes do NOT cover: the four corner
	// chamfers (1, 3, 5, 7) and the two back walls (2, 6). Side walls (0, 4)
	// are supplied by RLConst ARENA_EXTENT_X planes, so they are omitted.
	for (int e : {1, 3, 5, 7}) {
		V3 a, b;
		EdgeEndpoints(e, a, b);
		AddWallStrip(m, a, b, kEdges[e].nx, kEdges[e].ny, prof);
	}
	for (int e : {2, 6}) {
		V3 a, b;
		EdgeEndpoints(e, a, b);
		AddBackWallSolidRuns(m, a, b, kEdges[e].nx, kEdges[e].ny, prof);
	}

	// Goal recesses at both ends.
	AddGoalRecess(m, +1.0f);
	AddGoalRecess(m, -1.0f);
	return m;
}

void PrintUsage(const char* prog) {
	std::fprintf(stderr,
		"usage: %s <out.cmf> [--ramp-seg N] [--ceil-seg N]\n"
		"  Emits a clean-room Soccar collision mesh in RocketSim .cmf format.\n"
		"  Place as <meshdir>/soccar/<name>.cmf; see docs/MESH_SETUP.md.\n", prog);
}

} // namespace

int main(int argc, char** argv) {
	std::string out;
	int rampSeg = 8, ceilSeg = 6;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--ramp-seg" && i + 1 < argc) {
			rampSeg = std::atoi(argv[++i]);
		} else if (a == "--ceil-seg" && i + 1 < argc) {
			ceilSeg = std::atoi(argv[++i]);
		} else if (a == "--help" || a == "-h") {
			PrintUsage(argv[0]);
			return 0;
		} else if (!a.empty() && a[0] == '-') {
			std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
			PrintUsage(argv[0]);
			return 2;
		} else if (out.empty()) {
			out = a;
		} else {
			std::fprintf(stderr, "unexpected extra arg: %s\n", a.c_str());
			return 2;
		}
	}
	if (out.empty()) {
		PrintUsage(argv[0]);
		return 2;
	}
	if (rampSeg < 1 || rampSeg > 256 || ceilSeg < 1 || ceilSeg > 256) {
		std::fprintf(stderr, "ramp/ceil segments must be 1..256\n");
		return 2;
	}

	Mesh m = BuildSoccarMesh(rampSeg, ceilSeg);
	int windingErrors = CountWindingErrors(m);
	uint32_t hash = UpdateHash(m);

	if (!WriteCmf(out, m))
		return 1;

	// Deterministic, parseable report (the verifier reads these lines).
	std::printf("MESHGEN file=%s\n", out.c_str());
	std::printf("MESHGEN tris=%zu verts=%zu hash=0x%08X windingErrors=%d\n",
		m.tris.size(), m.verts.size(), hash, windingErrors);
	std::printf("MESHGEN rampSeg=%d ceilSeg=%d dims=%g,%g,%g,%g,%g\n",
		rampSeg, ceilSeg, double(SIDE_WALL_X), double(BACK_WALL_Y),
		double(CEILING_Z), double(CORNER_SUM), double(GOAL_HALF_W));
	return windingErrors == 0 ? 0 : 3;
}
