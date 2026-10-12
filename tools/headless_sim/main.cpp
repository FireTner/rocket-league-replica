// A1 SIM CORE — headless_sim: run the deterministic sim without rendering.
//
// TAPE_FORMAT v1.0.0 conformant CLI:
//   headless_sim --tape <tape.csv> --ticks N --dump <states.csv|states.bin> [--seed S] [--body car0|ball]
//
// Tape: CSV per tools/tas/TAPE_FORMAT.md (header
//   tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake; `#` comments
//   BEFORE the header only; ticks sequential from 0; 120 Hz).
//   Floats out of [-1,1] are clamped (count reported to stderr).
//   Non-0/1 jump/boost/handbrake is a parse error (exit 2).
//   Sign passthrough is VERBATIM (tape -> CarControls, zero negation).
//
// Dump (CSV): header tick,x,y,z,vx,vy,vz,speed,heading,fx,fy,fz,ux,uy,uz,boost — superset of
//   the replay.py compare schema (tick,x,y,speed,heading) so
//   tools/tas/replay.py compare works unchanged. speed = |v| (3D, uu/s).
//   heading = atan2(vy,vx) (0 when speed ~ 0). (fx,fy,fz) = car forward unit
//   vector for TAS sign calibration (0,0,0 when --body ball).
//   Full 3D columns keep ballistics verifiable.
// Dump (.bin): packed LE records int32 tick + 9x float32
//   (x,y,z,vx,vy,vz,fx,fy,fz,ux,uy,uz,boost).
//
// CHECKSUM: FNV-1a-64 over the exact dump bytes, printed as
//   CHECKSUM <16 lowercase hex> on stdout (last line). Same binary + tape +
//   seed => byte-identical dump (zero divergence).
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "sim/Sim.h"

namespace {

struct TapeEntry {
	long tick = 0;
	float throttle = 0, steer = 0, pitch = 0, yaw = 0, roll = 0;
	bool jump = false, boost = false, handbrake = false;
};

constexpr char kHeader[] = "tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake";

std::string Trim(const std::string& s) {
	size_t a = 0;
	while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
		a++;
	size_t b = s.size();
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
		b--;
	return s.substr(a, b - a);
}

float Clamp11Count(float v, long& clamps) {
	if (v > 1.0f) {
		clamps++;
		return 1.0f;
	}
	if (v < -1.0f) {
		clamps++;
		return -1.0f;
	}
	return v;
}

// Strict TAPE_FORMAT parser. Returns false + err on any violation.
bool ParseTape(const std::string& path, std::vector<TapeEntry>& out, long& clampCount,
	std::string& err) {
	std::ifstream f(path);
	if (!f) {
		err = "cannot open tape file: " + path;
		return false;
	}
	std::string line;
	// Phase 1: skip `#` comments (before header only), then require header.
	bool headerSeen = false;
	long lineNo = 0;
	while (std::getline(f, line)) {
		lineNo++;
		std::string t = Trim(line);
		if (t.empty())
			continue; // tolerate blank lines before header
		if (!headerSeen && !t.empty() && t[0] == '#')
			continue;
		if (!headerSeen) {
			if (t != kHeader) {
				err = "bad header line " + std::to_string(lineNo) + ": '" + t +
					"' expected '" + kHeader + "'";
				return false;
			}
			headerSeen = true;
			continue;
		}
		if (t[0] == '#') {
			err = "comment after header at line " + std::to_string(lineNo) +
				" (comments allowed BEFORE header only)";
			return false;
		}
		// Data row: split on commas (exactly 9 fields).
		std::vector<std::string> cols;
		{
			std::string cur;
			std::istringstream ss(t);
			while (std::getline(ss, cur, ','))
				cols.push_back(Trim(cur));
		}
		if (cols.size() != 9) {
			err = "bad tape line " + std::to_string(lineNo) + ": expected 9 columns, got " +
				std::to_string(cols.size());
			return false;
		}
		TapeEntry e;
		try {
			size_t pos = 0;
			e.tick = std::stol(cols[0], &pos);
			if (pos != cols[0].size())
				throw std::invalid_argument("tick");
		} catch (...) {
			err = "bad tick at line " + std::to_string(lineNo) + ": '" + cols[0] + "'";
			return false;
		}
		if (e.tick != (long)out.size()) {
			err = "tick gap at line " + std::to_string(lineNo) + ": expected " +
				std::to_string(out.size()) + ", got " + std::to_string(e.tick);
			return false;
		}
		try {
			e.throttle = Clamp11Count(std::stof(cols[1]), clampCount);
			e.steer = Clamp11Count(std::stof(cols[2]), clampCount);
			e.pitch = Clamp11Count(std::stof(cols[3]), clampCount);
			e.yaw = Clamp11Count(std::stof(cols[4]), clampCount);
			e.roll = Clamp11Count(std::stof(cols[5]), clampCount);
		} catch (...) {
			err = "bad float at line " + std::to_string(lineNo);
			return false;
		}
		for (int i = 0; i < 3; i++) {
			if (cols[6 + i] != "0" && cols[6 + i] != "1") {
				err = "bad int col at line " + std::to_string(lineNo) + ": '" +
					cols[6 + i] + "' (must be 0 or 1)";
				return false;
			}
		}
		e.jump = cols[6] == "1";
		e.boost = cols[7] == "1";
		e.handbrake = cols[8] == "1";
		out.push_back(e);
	}
	if (!headerSeen) {
		err = "empty tape (no header): " + path;
		return false;
	}
	return true;
}

uint64_t Fnv1a64(const char* data, size_t len, uint64_t h = 0xcbf29ce484222325ULL) {
	for (size_t i = 0; i < len; i++) {
		h ^= (uint64_t)(uint8_t)data[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

char HexNibble(unsigned v) {
	return (char)(v < 10 ? '0' + v : 'a' + (v - 10));
}

std::string Hex16(uint64_t h) {
	std::string s(16, '0');
	for (int i = 15; i >= 0; i--) {
		s[size_t(i)] = HexNibble(unsigned(h & 0xF));
		h >>= 4;
	}
	return s;
}

void PrintUsage(const char* prog) {
	std::cerr << "usage: " << prog
			  << " --tape <tape.csv> --ticks N --dump <states.csv|states.bin> [--seed S] [--body car0|ball] [--boost Amt] [--padlog P] [--demolog P] [--place x,y,yawdeg] [--meshdir DIR] [--meshreport]\n"
			  << "  TAPE_FORMAT v1.0.0; signs passed VERBATIM (no negation).\n"
			  << "  --boost: test-harness tank prefill 0..100 (default: spawn 33.3).\n"
			  << "    For top-speed/supersonic gates on the pad-less fallback floor.\n"
			  << "  --meshdir DIR: load DIR/soccar/*.cmf as the arena collision mesh\n"
			  << "    (RocketSim-owned geometry). Omit for the analytic fallback arena.\n"
			  << "  --goallog P: write goal events (tick) to P (mesh gates).\n"
			  << "  --ballplace x,y,z,vx,vy,vz: test-harness ball placement (mesh gates).\n"
			  << "  --meshreport: print MESH lines describing the loaded arena on stderr.\n";
}

bool EndsWith(const std::string& s, const std::string& suf) {
	return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

} // namespace

int main(int argc, char** argv) {
	long numTicks = -1;
	std::string tapePath, dumpPath, body = "car0", padLogPath, demoLogPath, meshDir, goalLogPath;
	bool placeGiven = false;
	bool meshReport = false;
	float placeX = 0, placeY = 0, placeYaw = 0;
	bool ballPlaceGiven = false;
	float ballX = 0, ballY = 0, ballZ = 0, ballVX = 0, ballVY = 0, ballVZ = 0;
	uint64_t seed = 0xA15EEDULL;
	bool seedGiven = false;
	float boostPrefill = -1.0f; // <0 = no override (spawn amount)

	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto need = [&](std::string& out) {
			if (i + 1 >= argc) {
				std::cerr << "missing value for " << a << "\n";
				PrintUsage(argv[0]);
				return false;
			}
			out = argv[++i];
			return true;
		};
		if (a == "--ticks") {
			std::string v;
			if (!need(v))
				return 2;
			try {
				numTicks = std::stol(v);
			} catch (...) {
				std::cerr << "bad --ticks value\n";
				return 2;
			}
		} else if (a == "--tape") {
			if (!need(tapePath))
				return 2;
		} else if (a == "--dump") {
			if (!need(dumpPath))
				return 2;
		} else if (a == "--body") {
			if (!need(body))
				return 2;
		} else if (a == "--padlog") {
			if (!need(padLogPath))
				return 2;
		} else if (a == "--demolog") {
			if (!need(demoLogPath))
				return 2;
		} else if (a == "--goallog") {
			if (!need(goalLogPath))
				return 2;
		} else if (a == "--place") {
			std::string v;
			if (!need(v))
				return 2;
			try {
				size_t p1 = v.find(','), p2 = v.find(',', p1 + 1);
				placeX = std::stof(v.substr(0, p1));
				placeY = std::stof(v.substr(p1 + 1, p2 - p1 - 1));
				placeYaw = std::stof(v.substr(p2 + 1)) * 3.14159265f / 180.0f;
			} catch (...) {
				std::cerr << "bad --place value (want x,y,yawdeg)\n";
				return 2;
			}
			placeGiven = true;
		} else if (a == "--seed") {
			std::string v;
			if (!need(v))
				return 2;
			try {
				seed = std::stoull(v);
			} catch (...) {
				std::cerr << "bad --seed value\n";
				return 2;
			}
			seedGiven = true;
		} else if (a == "--boost") {
			std::string v;
			if (!need(v))
				return 2;
			try {
				boostPrefill = std::stof(v);
			} catch (...) {
				std::cerr << "bad --boost value\n";
				return 2;
			}
		} else if (a == "--ballplace") {
			// Test-harness ball placement: x,y,z,vx,vy,vz (UU, UU/s).
			std::string v;
			if (!need(v))
				return 2;
			if (std::sscanf(v.c_str(), "%f,%f,%f,%f,%f,%f", &ballX, &ballY, &ballZ,
					&ballVX, &ballVY, &ballVZ) != 6) {
				std::cerr << "bad --ballplace value (want x,y,z,vx,vy,vz)\n";
				return 2;
			}
			ballPlaceGiven = true;
		} else if (a == "--meshdir") {
			if (!need(meshDir))
				return 2;
		} else if (a == "--meshreport") {
			meshReport = true;
		} else if (a == "--help" || a == "-h") {
			PrintUsage(argv[0]);
			return 0;
		} else {
			std::cerr << "unknown arg: " << a << "\n";
			PrintUsage(argv[0]);
			return 2;
		}
	}
	if (tapePath.empty() || dumpPath.empty() || numTicks <= 0) {
		std::cerr << "--tape, --ticks N (>0) and --dump are all required\n";
		PrintUsage(argv[0]);
		return 2;
	}

	bool wantBall = (body == "ball");
	int carIdx = 0;
	if (!wantBall) {
		if (body.rfind("car", 0) != 0) {
			std::cerr << "--body must be carN or ball\n";
			return 2;
		}
		try {
			carIdx = std::stoi(body.substr(3));
		} catch (...) {
			std::cerr << "--body car index invalid\n";
			return 2;
		}
		if (carIdx < 0) {
			std::cerr << "--body car index must be >= 0\n";
			return 2;
		}
	}

	std::vector<TapeEntry> tape;
	long clampCount = 0;
	{
		std::string err;
		if (!ParseTape(tapePath, tape, clampCount, err)) {
			std::cerr << err << "\n";
			return 2;
		}
	}
	if (tape.empty()) {
		std::cerr << "tape has header but zero rows\n";
		return 2;
	}

	Sim* sim = Sim::CreateSoccar(meshDir.empty() ? nullptr : meshDir.c_str());
	if (!sim) {
		std::cerr << "failed to create sim\n";
		return 1;
	}
	if (seedGiven)
		sim->SetSeed(seed);

	// A1: a --meshdir that yields no Soccar mesh must fail loudly, never
	// silently fall back (MASTER_PROMPT_PHYSICS.md §7 A1).
	if (!meshDir.empty() && !sim->usingRealSoccar()) {
		std::cerr << "FATAL: --meshdir '" << meshDir
				  << "' loaded no Soccar collision mesh. Expected "
				  << meshDir << "/soccar/*.cmf (RocketSim COLLISION_MESH_BASE_PATH"
				  << " convention; see docs/MESH_SETUP.md). Refusing to silently "
				  << "run the analytic fallback arena.\n";
		delete sim;
		return 1;
	}

	if (meshReport || !meshDir.empty()) {
		Sim::MeshReport mr;
		sim->GetMeshReport(&mr);
		std::cerr << "MESH mode=" << sim->arenaDescription()
				  << " shapes=" << mr.shapeCount
				  << " tris=" << mr.triCount
				  << " verts=" << mr.vertCount
				  << " hashMatched=" << (mr.hashMatched ? 1 : 0)
				  << " pads=" << sim->NumPads() << "\n";
	}

	int blueIdx = sim->AddCar(Sim::kBlue);
	if (blueIdx < 0) {
		std::cerr << "failed to add car\n";
		delete sim;
		return 1;
	}
	for (int i = 1; i <= carIdx; i++)
		sim->AddCar(Sim::kOrange);
	if (placeGiven)
		sim->Teleport(blueIdx, placeX, placeY, placeYaw);
	if (boostPrefill >= 0.0f)
		sim->SetBoost(blueIdx, boostPrefill);
	if (ballPlaceGiven)
		sim->SetBall(ballX, ballY, ballZ, ballVX, ballVY, ballVZ);

	bool binMode = EndsWith(dumpPath, ".bin");
	std::ofstream dump(dumpPath, binMode ? (std::ios::binary | std::ios::trunc) : std::ios::trunc);
	if (!dump) {
		std::cerr << "cannot open --dump path: " << dumpPath << "\n";
		delete sim;
		return 1;
	}
	// Optional pad log (G9 verification): CSV tick,idx,x,y,big,active,cooldown.
	std::ofstream padlog;
	if (!padLogPath.empty()) {
		padlog.open(padLogPath, std::ios::trunc);
		if (!padlog) {
			std::cerr << "cannot open --padlog path: " << padLogPath << "\n";
			delete sim;
			return 1;
		}
		padlog << "tick,idx,x,y,big,active,cooldown\n";
	}
	// Optional demo log (G10 verification): CSV tick,bumper,victim — one row
	// per demolition event (RocketSim DemoMode::NORMAL, supersonic flag).
	std::ofstream demolog;
	if (!demoLogPath.empty()) {
		demolog.open(demoLogPath, std::ios::trunc);
		if (!demolog) {
			std::cerr << "cannot open --demolog path: " << demoLogPath << "\n";
			delete sim;
			return 1;
		}
		demolog << "tick,bumper,victim\n";
	}
	// Optional goal log (C2 verification): tick per scored goal.
	std::ofstream goallog;
	if (!goalLogPath.empty()) {
		goallog.open(goalLogPath, std::ios::trunc);
		if (!goallog) {
			std::cerr << "cannot open --goallog path: " << goalLogPath << "\n";
			delete sim;
			return 1;
		}
		goallog << "tick\n";
	}
	uint64_t hash = 0xcbf29ce484222325ULL;
	auto hashBytes = [&](const char* p, size_t n) { hash = Fnv1a64(p, n, hash); };

	if (!binMode) {
		const char* hdr = "tick,x,y,z,vx,vy,vz,speed,heading,fx,fy,fz,ux,uy,uz,boost\n";
		dump << hdr;
		hashBytes(hdr, strlen(hdr));
	}

	// Controls hold: row k applies at tick k; beyond tape end, hold last row.
	for (long t = 0; t < numTicks; t++) {
		const TapeEntry& e = tape[size_t(t < (long)tape.size() ? t : tape.size() - 1)];
		sim->SetControls(blueIdx, e.throttle, e.steer, e.pitch, e.yaw, e.roll, e.jump,
			e.boost, e.handbrake);
		sim->Step120();

		if (goallog.is_open()) {
			while (sim->PollGoal())
				goallog << t << "\n";
		}

		if (demolog.is_open()) {
			Sim::DemoEvent de;
			while (sim->PollDemo(&de)) {
				char db[64];
				int m = std::snprintf(db, sizeof(db), "%llu,%d,%d\n",
					(unsigned long long)de.tick, de.bumper, de.victim);
				demolog.write(db, size_t(m));
			}
		}

		Sim::BodySample s;
		bool ok = wantBall ? sim->GetBall(&s) : sim->GetCar(carIdx, &s);
		if (!ok) {
			std::cerr << "failed to sample " << body << " at tick " << t << "\n";
			delete sim;
			return 1;
		}
		double speed = std::sqrt(double(s.vx) * s.vx + double(s.vy) * s.vy + double(s.vz) * s.vz);
		double heading = (speed > 1e-6) ? std::atan2(double(s.vy), double(s.vx)) : 0.0;
		float fx = 0, fy = 0, fz = 0, ux = 0, uy = 0, uz = 0;
		if (!wantBall) {
			sim->GetCarForward(carIdx, &fx, &fy, &fz);
			sim->GetCarUp(carIdx, &ux, &uy, &uz);
		}
		if (binMode) {
			int32_t tick32 = (int32_t)t;
			float boostF = wantBall ? 0.0f : sim->GetBoost(carIdx);
			float vals[13] = {s.x, s.y, s.z, s.vx, s.vy, s.vz, fx, fy, fz, ux, uy, uz, boostF};
			dump.write((const char*)&tick32, sizeof(tick32));
			hashBytes((const char*)&tick32, sizeof(tick32));
			dump.write((const char*)vals, sizeof(vals));
			hashBytes((const char*)vals, sizeof(vals));
		} else {
			char buf[256];
			int n = std::snprintf(buf, sizeof(buf), "%ld,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.6f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.2f\n",
				t, double(s.x), double(s.y), double(s.z), double(s.vx), double(s.vy),
				double(s.vz), speed, heading, double(fx), double(fy), double(fz), double(ux), double(uy), double(uz), wantBall ? 0.0 : double(sim->GetBoost(carIdx)));
			dump.write(buf, size_t(n));
			hashBytes(buf, size_t(n));
		}
		if (padlog.is_open()) {
			int np = sim->NumPads();
			for (int i = 0; i < np; i++) {
				Sim::PadSample ps;
				if (!sim->GetPad(i, &ps))
					continue;
				char pb[128];
				int m = std::snprintf(pb, sizeof(pb), "%ld,%d,%.1f,%.1f,%d,%d,%.3f\n", t,
					i, double(ps.x), double(ps.y), ps.big ? 1 : 0,
					ps.active ? 1 : 0, double(ps.cooldown));
				padlog.write(pb, size_t(m));
			}
		}
	}
	dump.close();

	std::cout << "CHECKSUM " << Hex16(hash) << "\n";
	std::cerr << "ticks=" << sim->tick() << " drops=" << sim->physicsDropCount() << " arena="
			  << (sim->usingRealSoccar() ? "soccar-mesh" : "flat-floor-fallback")
			  << " clamps=" << clampCount << " body=" << body << "\n";

	delete sim;
	return 0;
}
