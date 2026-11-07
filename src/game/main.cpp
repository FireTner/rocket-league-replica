// Free-play loop: step the 120 Hz sim, then draw the latest snapshot.
#include <SDL2/SDL.h>
#include <GL/gl.h> // --calibrate 3D-path probe draws raw fixed-function quads

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "bot/BotStub.h"
#include "audio/Audio.h"
#include "camera/Camera.h"
#include "fx/FX.h"
#include "fx/GoalBurst.h"
#include "input/Keyboard.h"
#include "input/Gamepad.h"
#include "menu/Menu.h"
#include "models/Fennec.h"
#include "models/Models.h"
#include "models/ArenaRip.h"
#include "models/ArenaTextures.h"
#include "render/Renderer.h"
#include "render/MatchHud.h"
#include "render/PitchShadow.h"
#include "settings/Settings.h"
#include "sim/Sim.h"
#include "game/FreeplayBall.h"

namespace {

rlbot::StubMode ParseBot(const char* s) {
	if (!s || !strcmp(s, "stationary"))
		return rlbot::StubMode::Stationary;
	if (!strcmp(s, "mirror"))
		return rlbot::StubMode::Mirror;
	if (!strcmp(s, "slow-chase") || !strcmp(s, "chase"))
		return rlbot::StubMode::SlowChase;
	std::fprintf(stderr, "[game] unknown --bot '%s', using stationary\n", s);
	return rlbot::StubMode::Stationary;
}

uint64_t NowMs() {
	return (uint64_t)SDL_GetTicks();
}

// Keys 1–5. Both the keydown path and the rebindable poll path call this
// so they cannot drift. SetBall clears leftover spin.
void RunBallDrill(Sim* sim, int player, int drill) {
	Sim::BodySample cc;
	Sim::BodySample ball;
	if (!sim->GetCar(player, &cc) || !sim->GetBall(&ball))
		return;
	float fx = 0.f, fy = 1.f, fz = 0.f;
	float ux = 0.f, uy = 0.f, uz = 1.f;
	sim->GetCarForward(player, &fx, &fy, &fz);
	sim->GetCarUp(player, &ux, &uy, &uz);
	rl::FreeplayCar car;
	car.body = {cc.x, cc.y, cc.z, cc.vx, cc.vy, cc.vz};
	car.fx = fx;
	car.fy = fy;
	car.fz = fz;
	car.ux = ux;
	car.uy = uy;
	car.uz = uz;
	rl::FreeplaySample b{ball.x, ball.y, ball.z, ball.vx, ball.vy, ball.vz};
	rl::FreeplayCommand cmd;
	if (!rl::ComputeFreeplayBall(drill, car, b, Physics::kDefaultPreset, &cmd))
		return;
	sim->SetBall(cmd.x, cmd.y, cmd.z, cmd.vx, cmd.vy, cmd.vz);
	std::fprintf(stderr, "[game] drill %d ball spawn\n", drill + 1);
}

} // namespace

int main(int argc, char** argv) {
	const char* botName = "stationary";
	double soakSecs = 0;
	long long tickBudget = -1;
	uint64_t seed = 0xA15EEDULL;
	bool seedGiven = false, noVsync = false, menuAtStart = false, noAudio = false, flatShading = false;
	bool synthOnly = false;
	std::string soundsDir;
	bool soundsFlag = false;
	bool calibrate = false;
	bool shell = false; // --shell draws the analytic arena
	std::string meshDir; // --meshdir override; empty = settings [arena] mesh_dir
	// --arenadir override; empty reads [arena] arena_dir, then the stadium pack.
	std::string arenaDir;
	bool arenaDirFlag = false;
	float camEye[3] = {0, 0, 0}, camTarget[3] = {0, 0, 0};
	bool camSet = false;
	long long shotAtTick = -1, shotEvery = 0, shotCount = 0;
	std::string shotPath, shotDir;
	bool ballPosSet = false;
	float ballPos[3] = {0, 0, 0};
	float forcedBoost = -1.0f; // <0 = off
	bool noFennec = false;
	bool debugColliders = false;
	bool debugGrid = false;
	bool noHexNet = false;
	bool noShake = false;
	bool driveOverride = false;
	rl::Controls driveCtl;
	std::string fxProbe;
	long long fxProbeTick = 120;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		auto need = [&](const char*& out) {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "missing value for %s\n", a.c_str());
				return false;
			}
			out = argv[++i];
			return true;
		};
		const char* v = nullptr;
		if (a == "--bot") {
			if (!need(v))
				return 2;
			botName = v;
		} else if (a == "--soak") {
			if (!need(v))
				return 2;
			soakSecs = atof(v);
		} else if (a == "--ticks") {
			if (!need(v))
				return 2;
			tickBudget = atoll(v);
		} else if (a == "--seed") {
			if (!need(v))
				return 2;
			seed = (uint64_t)strtoull(v, nullptr, 0);
			seedGiven = true;
		} else if (a == "--no-vsync") {
			noVsync = true;
		} else if (a == "--flat") {
			flatShading = true; // unlit: glColor only
		} else if (a == "--debug-grid") {
			debugGrid = true;
		} else if (a == "--no-hexnet") {
			noHexNet = true;
		} else if (a == "--no-audio") {
			noAudio = true;
		} else if (a == "--synth") {
			synthOnly = true;
		} else if (a == "--sounds") {
			if (!need(v))
				return 2;
			soundsDir = v;
			soundsFlag = true;
		} else if (a == "--menu") {
			menuAtStart = true;
		} else if (a == "--shot-at-tick") {
			if (!need(v))
				return 2;
			shotAtTick = atoll(v);
		} else if (a == "--shot-path") {
			if (!need(v))
				return 2;
			shotPath = v;
		} else if (a == "--shot-every") {
			if (!need(v))
				return 2;
			shotEvery = atoll(v);
		} else if (a == "--shot-dir") {
			if (!need(v))
				return 2;
			shotDir = v;
		} else if (a == "--cam") {
			if (!need(v))
				return 2;
			if (std::sscanf(v, "%f,%f,%f,%f,%f,%f", &camEye[0], &camEye[1],
					&camEye[2], &camTarget[0], &camTarget[1], &camTarget[2]) != 6) {
				std::fprintf(stderr, "bad --cam (want eye x,y,z, target x,y,z)\n");
				return 2;
			}
			camSet = true;
		} else if (a == "--meshdir") {
			if (!need(v))
				return 2;
			meshDir = v;
		} else if (a == "--arenadir") {
			if (!need(v))
				return 2;
			arenaDir = v;
			arenaDirFlag = true;
		} else if (a == "--shell") {
			shell = true;
		} else if (a == "--ballpos") {
			if (!need(v))
				return 2;
			if (std::sscanf(v, "%f,%f,%f", &ballPos[0], &ballPos[1], &ballPos[2]) != 3) {
				std::fprintf(stderr, "bad --ballpos (want x,y,z)\n");
				return 2;
			}
			ballPosSet = true;
		} else if (a == "--boost") {
			if (!need(v))
				return 2;
			forcedBoost = (float)atof(v);
		} else if (a == "--nofennec") {
			noFennec = true; // draw the box car
		} else if (a == "--debug-colliders") {
			debugColliders = true;
		} else if (a == "--no-shake") {
			noShake = true;
		} else if (a == "--drive") {
			// throttle,steer,pitch,yaw,roll,jump,boost,handbrake
			if (!need(v))
				return 2;
			float f[8] = {0, 0, 0, 0, 0, 0, 0, 0};
			if (std::sscanf(v, "%f,%f,%f,%f,%f,%f,%f,%f", &f[0], &f[1], &f[2],
					&f[3], &f[4], &f[5], &f[6], &f[7]) < 5) {
				std::fprintf(stderr, "bad --drive (want t,s,p,y,r[,j,b,h])\n");
				return 2;
			}
			driveCtl.throttle = f[0];
			driveCtl.steer = f[1];
			driveCtl.pitch = f[2];
			driveCtl.yaw = f[3];
			driveCtl.roll = f[4];
			driveCtl.jump = f[5] != 0;
			driveCtl.boost = f[6] != 0;
			driveCtl.handbrake = f[7] != 0;
			driveOverride = true;
		} else if (a == "--fx-probe") {
			if (!need(v))
				return 2;
			fxProbe = v;
		} else if (a == "--fx-probe-tick") {
			if (!need(v))
				return 2;
			fxProbeTick = atoll(v);
		} else if (a == "--calibrate") {
			calibrate = true; // palette quads, one screenshot, then exit
		} else if (a == "--help" || a == "-h") {
			std::fprintf(stderr,
				"usage: %s [--bot stationary|mirror|slow-chase] [--soak SECS] [--ticks N] [--seed S]\n"
				"       [--menu] [--shot-at-tick N --shot-path P] [--shot-every N --shot-dir D]\n"
				"       [--no-vsync] [--no-audio] [--synth] [--sounds DIR] [--meshdir DIR] [--arenadir DIR] [--shell]\n"
				"       --sounds DIR plays a local wav pack (default sound_assets/ when present); --synth keeps the procedural bed\n"
				"       defaults: mesh collision_meshes, arena arena_assets/stadium_day\n"
				"       --shell draws the analytic arena instead of the pack\n"
				"visual-harness (debug): [--ballpos x,y,z] [--boost N] [--drive t,s,p,y,r,j,b,h]\n"
				"       [--nofennec] [--debug-colliders] [--debug-grid] [--no-hexnet] [--no-shake]\n"
				"       [--fx-probe goal|demo|pickup --fx-probe-tick N]\n"
				"keys: WASD drive, L jump, K boost, Shift slide, Q/E roll, Space ball cam,\n"
				"      J look-behind, F freecam, Esc menu, R reset car, C reset ball, 1-5 kickoffs, M mute, F12 screenshot\n",
				argv[0]);
			return 0;
		} else {
			std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
			return 2;
		}
	}

	rl::Settings settings = rl::Settings::Defaults();
	{
		std::string err;
		if (!rl::Settings::Load("config/settings.ini", settings, &err))
			std::fprintf(stderr, "[game] settings load: %s (defaults)\n", err.c_str());
	}

	// --arenadir, else [arena] arena_dir. A blank value draws the shell.
	// A missing key uses the stadium pack. A missing default pack draws the
	// shell; any other directory with no pack is fatal.
	rl::ArenaPack arenaPack;
	bool useArenaPack = false;
	if (!shell) {
		if (!arenaDirFlag) {
			std::string iniArena;
			if (rl::ReadIniKey("config/settings.ini", "arena", "arena_dir", &iniArena))
				arenaDir = iniArena;
			else
				arenaDir = rl::kDefaultArenaDir;
		}
		if (!arenaDir.empty()) {
			const bool manifest = std::filesystem::is_regular_file(
				std::filesystem::path(arenaDir) / "arena.json");
			if (!manifest && !arenaDirFlag && arenaDir == rl::kDefaultArenaDir) {
				std::fprintf(stderr,
					"[game] arena pack not found at %s; drawing the shell\n",
					arenaDir.c_str());
			} else if (!rl::LoadArenaPack(arenaDir, arenaPack)) {
				std::fprintf(stderr,
					"[game] FATAL: arena dir '%s' holds no loadable arena pack "
					"(expected the manifest arena.json): %s\n",
					arenaDir.c_str(), rl::ArenaPackError(arenaPack));
				return 1;
			} else {
				useArenaPack = true;
				std::fprintf(stderr,
					"[game] arena pack: dir=%s source=%s meshes=%zu(+%zu external) "
					"placements=%zu(+%zu external) materials=%zu textures=%zu\n",
					arenaDir.c_str(), arenaPack.sourcePackage.c_str(),
					arenaPack.meshes.size(), arenaPack.externalMeshes.size(),
					arenaPack.instances.size(), arenaPack.externalInstances.size(),
					arenaPack.materials.size(), arenaPack.textures.size());
			}
		}
	}

	// --meshdir, else [arena] mesh_dir, else collision_meshes. A directory
	// that loads no Soccar mesh is a hard error.
	if (meshDir.empty())
		meshDir = settings.arena.meshDir;
	if (meshDir.empty())
		meshDir = rl::kDefaultMeshDir;
	Sim* sim = Sim::CreateSoccar(meshDir.empty() ? nullptr : meshDir.c_str());
	if (!sim) {
		std::fprintf(stderr, "[game] failed to create sim\n");
		return 1;
	}
	if (!meshDir.empty() && !sim->usingRealSoccar()) {
		std::fprintf(stderr,
			"[game] FATAL: mesh dir '%s' loaded no Soccar collision mesh "
			"(expected %s/soccar/*.cmf)\n",
			meshDir.c_str(), meshDir.c_str());
		delete sim;
		return 1;
	}
	if (seedGiven)
		sim->SetSeed(seed);
	int player = sim->AddCar(Sim::kBlue);
	int botIdx = sim->AddCar(Sim::kOrange);
	if (player < 0 || botIdx < 0) {
		std::fprintf(stderr, "[game] failed to add cars\n");
		delete sim;
		return 1;
	}
	sim->ResetBall();
	{
		Sim::MeshReport mr;
		sim->GetMeshReport(&mr);
		std::fprintf(stderr,
			"[game] pads=%d arena=%s shapes=%d tris=%d verts=%d hashMatched=%d\n",
			sim->NumPads(), sim->arenaDescription(), mr.shapeCount, mr.triCount,
			mr.vertCount, mr.hashMatched ? 1 : 0);
	}
	rl::FennecLoad(); // CC-BY-4.0 community Fennec body (optional asset)
	if (ballPosSet) {
		sim->SetBall(ballPos[0], ballPos[1], ballPos[2], 0, 0, 0);
		std::fprintf(stderr, "[game] ballpos=%.1f,%.1f,%.1f (harness)\n",
			(double)ballPos[0], (double)ballPos[1], (double)ballPos[2]);
	}

	// Initialize the game-controller subsystem BEFORE opening devices; the
	// renderer init below only brings up video+timer.
	if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0 &&
		SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
		std::fprintf(stderr, "[game] SDL_INIT_GAMECONTROLLER failed: %s\n", SDL_GetError());

	rl::Renderer renderer;
	// 4.5 compatibility. Init aborts if that context cannot be created.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
	if (settings.render.msaa > 0) {
		int msaa = settings.render.msaa < 0 ? 0 : (settings.render.msaa > 16 ? 16 : settings.render.msaa);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa);
	} else {
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	}
	if (!renderer.Init(1280, 720, !noVsync)) {
		std::fprintf(stderr, "[game] FATAL: renderer init failed (GL 4.5 compatibility required, no fallback)\n");
		delete sim;
		return 1;
	}
	// Upload the pack once. The GL context is live; the frame loop does not rebuild it.
	if (useArenaPack) {
		if (!renderer.BuildArenaPack(arenaPack, arenaDir)) {
			std::fprintf(stderr,
				"[game] FATAL: the arena pack in '%s' holds no drawable geometry\n",
				arenaDir.c_str());
			delete sim;
			return 1;
		}
		const rl::Renderer::ArenaPackStats& as = renderer.ArenaPackInfo();
		std::fprintf(stderr,
			"[arena] draw path: buckets=%u visible(last)=%u culled(last)=%u "
			"textured=%u fallback=%u tris=%llu\n",
			as.buckets, as.visible, as.culled, as.texturedBuckets,
			as.untexturedBuckets, (unsigned long long)as.tris);
	}
	rl::Camera camera;
	camera.LoadFromIni("config/settings.ini");
	if (flatShading)
		renderer.SetLight(false);
	// Compiled .ini values win over compiled defaults (menu edits live in
	// settings; synced to the camera every frame below).
	camera.Params().fov = settings.camera.fov;
	camera.Params().distance = settings.camera.distance;
	camera.Params().height = settings.camera.height;
	camera.Params().angle = settings.camera.angle;
	camera.Params().stiffness = settings.camera.stiffness;
	camera.Params().transitionSpeed = settings.camera.transitionSpeed;
	camera.Params().swivelSpeed = settings.camera.swivelSpeed;
	camera.Params().shake = settings.camera.shake;
		camera.Params().invertSwivel = settings.camera.invertSwivel;
		camera.Params().snap = settings.camera.snap;
		camera.Params().airHeading = settings.camera.airHeading;
	if (!settings.camera.ballcam && camera.BallCam())
		camera.ToggleBallCam();
	if (settings.camera.ballcam && !camera.BallCam())
		camera.ToggleBallCam();
	rl::Keyboard keyboard(settings.input);
	// Gamepad: additive device. Real SDL_GameController with hot-plug; the
	// left stick owns throttle/steer, buttons are OR-ed with the keyboard.
	rl::Gamepad gamepad(settings.gamepad);
	{
		// Optional community mapping DB (SDL ships one at this path on most
		// distros); absence is not an error.
		if (SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt") == -1)
			SDL_GameControllerAddMappingsFromFile(
				"/usr/share/gamecontrollerdb/gamecontrollerdb.txt");
		std::fprintf(stderr, "[game] gamepad: %d controller(s) attached at start\n",
			gamepad.openCount());
	}
	rl::FX fx;
	rl::GoalBurst goalBurst;
	rlbot::BotStub bot(ParseBot(botName));
	bool unlimitedBoost = false;
	if (calibrate) {
		renderer.BeginFrame(0, 0, 0);
		const float pal[5][3] = {{0.14f, 0.44f, 0.19f}, {0.25f, 0.5f, 1.0f},
			{0.10f, 0.35f, 0.90f}, {1.0f, 0.55f, 0.2f}, {0.9f, 0.9f, 0.9f}};
		// Band order: turf, wall, carblue, goalorange, white.
		for (int i = 0; i < 5; i++)
			renderer.FillRect2D(i * 256, 0, 256, 720, pal[i][0], pal[i][1], pal[i][2], 1.0f);
		renderer.DrawText2D(28, 686, "CALIBRATE 0123456789", 2, 0.9f, 0.95f, 1.0f);
		// 3D-path probe: one lit perspective blue box like the arena walls.
		{
			float aspect = 1280.0f / 720.0f;
			float fovY = 77.0f * 3.14159265f / 180.0f;
			glMatrixMode(GL_PROJECTION);
			glPushMatrix();
			glLoadIdentity();
			float zn = 10.0f, zf = 40000.0f;
			float t = zn * std::tan(fovY * 0.5f);
			float r = t * aspect;
			glFrustum(-r, r, -t, t, zn, zf);
			glMatrixMode(GL_MODELVIEW);
			glPushMatrix();
			glLoadIdentity();
			glTranslatef(0, 0, -3000);
			glScalef(800, 800, 800);
			glColor4f(0.25f, 0.5f, 1.0f, 0.3f);
			glNormal3f(0, 0, 1);
			glBegin(GL_QUADS);
			glVertex3f(-0.5f, -0.5f, 0.5f);
			glVertex3f(0.5f, -0.5f, 0.5f);
			glVertex3f(0.5f, 0.5f, 0.5f);
			glVertex3f(-0.5f, 0.5f, 0.5f);
			glEnd();
			glPopMatrix();
			glMatrixMode(GL_PROJECTION);
			glPopMatrix();
			glMatrixMode(GL_MODELVIEW);
		}
		renderer.SaveScreenshot("calibrate.ppm");
		std::fprintf(stderr, "[game] calibrate.ppm written\n");
		delete sim;
		return 0;
	}
	rl::Audio audio;
	if (!noAudio && !synthOnly) {
		const char* dir = soundsFlag ? soundsDir.c_str() : "sound_assets";
		bool manifest = std::filesystem::is_regular_file(
			std::filesystem::path(dir) / "manifest.txt");
		if (soundsFlag && !manifest) {
			std::fprintf(stderr,
				"[game] FATAL: --sounds '%s' has no manifest.txt\n", dir);
			delete sim;
			return 1;
		}
		if (manifest && !audio.LoadPack(dir)) {
			std::fprintf(stderr,
				"[game] FATAL: sound pack '%s' did not load a cue\n", dir);
			delete sim;
			return 1;
		}
	}
	bool audioOn = !noAudio && audio.Init();
	float prevBoostHud = 0;
	float prevBoostFx = 0; // pickup-burst edge, independent of audio
	bool prevJumpHeld = false;
	// Cosmetic 3-2-1. Rearmed on spawn; the sim keeps stepping.
	uint64_t kickoffTick = 0;
	auto armKickoff = [&]() { kickoffTick = sim->tick(); };
	bool prevGround = true;
	bool prevSuper = false;

	rl::Menu menu(settings, rl::FreePlayActions{
		[&]() { sim->ResetCarToSpawn(player); sim->ResetCarToSpawn(botIdx); armKickoff(); },
		[&]() { sim->ResetBall(); },
		[&](int) { sim->ResetCarToSpawn(player); sim->ResetCarToSpawn(botIdx); sim->ResetBall(); armKickoff(); },
		[&]() { camera.ToggleBallCam(); },
		[&]() { unlimitedBoost = !unlimitedBoost; },
		[&]() {
			std::string err;
			if (!rl::Settings::Save("config/settings.ini", settings, &err))
				std::fprintf(stderr, "[menu] save failed: %s\n", err.c_str());
			else
				std::fprintf(stderr, "[menu] settings saved\n");
		},
	});

	bool running = true, menuLogged = false;
	bool freeCam = false;
	float lastSpeed = 0;
	float prevEar[3] = {0, 0, 0};
	bool haveEar = false;
	float freeEye[3] = {-270, -2600, 400}, freeCenter[3] = {0, -2000, 100};
	if (camSet) {
		freeCam = true;
		freeEye[0] = camEye[0]; freeEye[1] = camEye[1]; freeEye[2] = camEye[2];
		freeCenter[0] = camTarget[0]; freeCenter[1] = camTarget[1];
		freeCenter[2] = camTarget[2];
	}
	if (menuAtStart)
		menu.Open();
	uint64_t shotTaken = 0;
	uint64_t lastFxTick = 0;
	uint64_t t0 = NowMs(), lastMs = t0, lastHudMs = 0;
	uint64_t frames = 0;
	rl::Controls ctl;
	rl::InputEvents ev;

	while (running) {
		SDL_PumpEvents();
		// Set when the event path below already toggled the menu for an
		// Escape keydown this frame. keyboard.Poll() still sees the held
		// Escape as an ev.openMenu edge afterwards, which would toggle the
		// menu right back (Esc looked dead). The poll edge is ignored once.
		bool escToggledMenu = false;
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_CONTROLLERDEVICEADDED ||
				e.type == SDL_CONTROLLERDEVICEREMOVED) {
				bool changed = gamepad.HandleDeviceEvent(e);
				// Never drop a tick on a hot-plug: just report and continue.
				if (changed)
					std::fprintf(stderr, "[game] gamepad %s (%d attached)\n",
						e.type == SDL_CONTROLLERDEVICEADDED ? "connected" : "disconnected",
						gamepad.openCount());
				continue;
			}
			if (e.type == SDL_QUIT) {
				if (soakSecs > 0) {
					// A soak keeps running if the window is closed; the timer ends it.
					std::fprintf(stderr, "[game] SDL_QUIT ignored (--soak harness mode)\n");
				} else {
					std::fprintf(stderr, "[game] SDL_QUIT (window close / quit request)\n");
					running = false;
				}
			}
			else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
				SDL_Keycode k = e.key.keysym.sym;
				if (menu.IsOpen()) {
					if (k == SDLK_ESCAPE) {
						menu.Close();
						escToggledMenu = true;
					} else {
						menu.HandleKey(k);
						for (auto& line : menu.RenderText())
							std::fprintf(stderr, "[menu] %s\n", line.c_str());
					}
				} else if (k == SDLK_ESCAPE) {
					menu.Open();
					escToggledMenu = true;
					for (auto& line : menu.RenderText())
						std::fprintf(stderr, "[menu] %s\n", line.c_str());
				} else if (k == SDLK_c) {
					sim->ResetBall();
				} else if (k == SDLK_m) {
					audio.SetMuted(!audio.Muted());
					std::fprintf(stderr, "[game] audio %s\n",
						audio.Muted() ? "muted" : "on");
				} else if (k == SDLK_f) {
					freeCam = !freeCam;
					std::fprintf(stderr, "[game] freecam %s (W/S dolly, A/D orbit, Q/E or Space/Shift height, R rejoin car)\n",
						freeCam ? "ON" : "OFF");
				} else if (k == SDLK_F12) {
					shotTaken = 2; // capture before EndFrame below
				} else if (k == SDLK_r) {
					sim->ResetCarToSpawn(player);
					armKickoff();
				} else if (k >= SDLK_1 && k <= SDLK_5) {
					RunBallDrill(sim, player, (int)(k - SDLK_1));
				}
			}
		}

		keyboard.Poll(ctl, ev);
		// When a pad is connected its stick replaces throttle and steer.
		// Buttons are OR-ed with the keyboard. No pad leaves the keyboard values alone.
		{
			rl::Controls gp;
			rl::InputEvents ge;
			bool padConnected = gamepad.Poll(gp, ge);
			// Menu: feed freshly-pressed pad buttons so pad bindings can be
			// captured and the menu navigated from the controller. The driving
			// path below is unaffected (menu and play share the same buttons).
			if (menu.IsOpen()) {
				SDL_GameControllerButton pressed[SDL_CONTROLLER_BUTTON_MAX];
				int n = gamepad.TakeNewPresses(pressed, SDL_CONTROLLER_BUTTON_MAX);
				for (int i = 0; i < n; ++i) {
					menu.HandleControllerButton(pressed[i]);
					for (auto& line : menu.RenderText())
						std::fprintf(stderr, "[menu] %s\n", line.c_str());
				}
			}
			if (padConnected) {
				ctl.throttle = gp.throttle;
				ctl.steer = gp.steer;
				ctl.pitch = gp.pitch;
				ctl.yaw = gp.yaw;
				ctl.jump = ctl.jump || gp.jump;
				ctl.boost = ctl.boost || gp.boost;
				ctl.handbrake = ctl.handbrake || gp.handbrake;
				if (ctl.handbrake)
					ctl.roll = ctl.steer;
				ev.toggleBallcam = ev.toggleBallcam || ge.toggleBallcam;
				ev.resetCar = ev.resetCar || ge.resetCar;
				ev.resetBall = ev.resetBall || ge.resetBall;
				ev.toggleUnlimitedBoost = ev.toggleUnlimitedBoost || ge.toggleUnlimitedBoost;
				ev.openMenu = ev.openMenu || ge.openMenu;
				if (ev.kickoffIndex < 0 && ge.kickoffIndex >= 0)
					ev.kickoffIndex = ge.kickoffIndex;
			}
		}
		// Live settings -> devices (menu rebinds apply instantly, no restart).
		keyboard.SetBindings(settings.input);
		gamepad.SetBindings(settings.gamepad);
		if (menu.IsOpen()) {
			if (!menuLogged) {
				menuLogged = true;
			}
			if (ev.openMenu && !escToggledMenu)
				menu.Close();
			ctl = rl::Controls(); // menu open: no driving
		} else {
			menuLogged = false;
			if (ev.openMenu && !escToggledMenu)
				menu.Open();
			if (ev.toggleBallcam)
				camera.ToggleBallCam();
			if (ev.resetCar) {
				sim->ResetCarToSpawn(player);
				armKickoff();
			}
			if (ev.resetBall)
				sim->ResetBall();
			if (ev.toggleUnlimitedBoost)
				unlimitedBoost = !unlimitedBoost;
			if (ev.kickoffIndex >= 0) {
				// Ball drills 1..5 (rebindable via kickoff_0..4): ball-only.
				RunBallDrill(sim, player, ev.kickoffIndex);
			}
			{
				// J held = look behind (rear view). Fixed key, like F freecam.
				int nKeys = 0;
				const Uint8* ks = SDL_GetKeyboardState(&nKeys);
				bool behind = ks && SDL_SCANCODE_J < nKeys && ks[SDL_SCANCODE_J];
				camera.SetLookBehind(behind && !menu.IsOpen());
			}
		}

		// Camera swivel (right-stick look): gamepad-only, deadzoned with the
		// same radial deadzone as the left stick. Zero while the menu is
		// open so menu navigation never moves the camera.
		float swivelX = 0.0f, swivelY = 0.0f;
		if (!menu.IsOpen() && gamepad.connected()) {
			float rx = 0.0f, ry = 0.0f;
			gamepad.RightStick(rx, ry);
			rl::Gamepad::DeadzoneResult dz =
				rl::Gamepad::ApplyDeadzone(rx, ry, settings.gamepad.deadzone);
			swivelX = dz.x;
			swivelY = dz.y;
		}

		// Player -> sim (verbatim; Keyboard owns canonical signs).
		// In freecam the car parks (flight uses ctl after this).
		rl::Controls drive = ctl;
		if (freeCam)
			drive = rl::Controls();
		if (driveOverride)
			drive = driveCtl; // scripted controls replace the player
		sim->SetControls(player, drive.throttle, drive.steer, drive.pitch, drive.yaw,
			drive.roll, drive.jump, drive.boost, drive.handbrake);
		{
			Sim::BodySample cs, bs;
			sim->GetCar(botIdx, &cs);
			sim->GetBall(&bs);
			float bfx = 0, bfy = 0, bfz = 0;
			sim->GetCarForward(botIdx, &bfx, &bfy, &bfz);
			rlbot::BotView view;
			view.carX = cs.x;
			view.carY = cs.y;
			view.carZ = cs.z;
			view.carYaw = atan2f(bfy, bfx);
			view.ballX = bs.x;
			view.ballY = bs.y;
			view.ballZ = bs.z;
			rlbot::BotInput pi;
			pi.throttle = ctl.throttle;
			pi.steer = ctl.steer;
			pi.pitch = ctl.pitch;
			pi.yaw = ctl.yaw;
			pi.roll = ctl.roll;
			pi.jump = ctl.jump;
			pi.boost = ctl.boost;
			pi.handbrake = ctl.handbrake;
			bot.setPlayerInput(pi);
			rlbot::BotInput bo = bot.tick(view);
			sim->SetControls(botIdx, bo.throttle, bo.steer, bo.pitch, bo.yaw,
				bo.roll, bo.jump, bo.boost, bo.handbrake);
		}
		if (unlimitedBoost) {
			sim->SetBoost(player, 100.0f);
			sim->SetBoost(botIdx, 100.0f);
		}
		if (forcedBoost >= 0.0f) {
			sim->SetBoost(player, forcedBoost);
		}
		if (audioOn) {
			if (drive.jump && !prevJumpHeld) {
				if (sim->IsOnGround(player))
					audio.PlayJump();
				else if (drive.pitch != 0 || drive.yaw != 0 || drive.roll != 0 || drive.steer != 0)
					audio.PlayDodge();
				else
					audio.PlayDoubleJump();
			}
			float pb = sim->GetBoost(player);
			if (!unlimitedBoost && pb - prevBoostHud >= 5.0f)
				audio.PlayPickup(pb >= 99.0f);
			prevBoostHud = pb;
			prevJumpHeld = drive.jump;
		}
		// Same boost-rise edge as the pickup blip, whether or not audio is on.
		{
			float pb = sim->GetBoost(player);
			if (!unlimitedBoost && forcedBoost < 0 && pb - prevBoostFx >= 5.0f)
				fx.OnPickup(pb >= 99.0f);
			prevBoostFx = pb;
		}

		uint64_t now = NowMs();
		double dt = double(now - lastMs) / 1000.0;
		lastMs = now;
		if (dt < 0)
			dt = 0;
		sim->StepAccumulator(dt);
		if (tickBudget > 0 && (long long)sim->tick() >= tickBudget)
			running = false;
		if (soakSecs > 0 && double(now - t0) / 1000.0 >= soakSecs)
			running = false;
		if (sim->PollGoal()) {
			fx.OnGoal();
			if (audioOn)
				audio.PlayGoal();
			Sim::BodySample gb;
			sim->GetBall(&gb);
			// Visual burst only. The horn and the shockwave stay in FX.
			goalBurst.Trigger(gb.x, gb.y, gb.z);
			std::fprintf(stderr, "[game] GOAL tick=%llu ball=(%.0f,%.0f,%.0f)\n",
				(unsigned long long)sim->tick(), (double)gb.x, (double)gb.y, (double)gb.z);
			sim->ResetBall();
		}
		// Demolitions: RocketSim demolishes the victim on supersonic contact
		// (DemoMode::NORMAL) and auto-respawns it after 3 s (RLConst
		// DEMO_RESPAWN_TIME). We surface the event (FX + boom + log).
		Sim::DemoEvent de;
		while (sim->PollDemo(&de)) {
			fx.OnDemo();
			if (audioOn)
				audio.PlayDemo();
			std::fprintf(stderr, "[game] DEMO tick=%llu car%d demolished car%d\n",
				(unsigned long long)sim->tick(), de.bumper, de.victim);
		}

		Sim::BodySample cs, bl;
		sim->GetCar(player, &cs);
		sim->GetBall(&bl);
		float fwdx = 0, fwdy = 1, fwdz = 0;
		sim->GetCarForward(player, &fwdx, &fwdy, &fwdz);
		float upx = 0, upy = 0, upz = 1;
		sim->GetCarUp(player, &upx, &upy, &upz);
		float speed = sqrtf(cs.vx * cs.vx + cs.vy * cs.vy + cs.vz * cs.vz);
		lastSpeed = speed;
		rl::Snapshot snap;
		snap.carPos[0] = cs.x;
		snap.carPos[1] = cs.y;
		snap.carPos[2] = cs.z;
		snap.carYaw = atan2f(fwdy, fwdx);
		snap.carSpeed = speed;
		snap.carForward[0] = fwdx; snap.carForward[1] = fwdy; snap.carForward[2] = fwdz;
		snap.carUp[0] = upx; snap.carUp[1] = upy; snap.carUp[2] = upz;
		snap.carVel[0] = cs.vx; snap.carVel[1] = cs.vy; snap.carVel[2] = cs.vz;
		snap.ballPos[0] = bl.x;
		snap.ballPos[1] = bl.y;
		snap.ballPos[2] = bl.z;
		snap.ballSpeed = sqrtf(bl.vx * bl.vx + bl.vy * bl.vy + bl.vz * bl.vz);
		{
			Sim::LastTouch lt;
			if (sim->GetLastTouch(&lt)) {
				snap.ballTouched = true;
				snap.ballTeam = lt.team;
			}
		}
		{
			Sim::BallSpin spin;
			if (sim->GetBallSpin(&spin)) {
				snap.ballAngVel[0] = spin.angVel[0];
				snap.ballAngVel[1] = spin.angVel[1];
				snap.ballAngVel[2] = spin.angVel[2];
				for (int i = 0; i < 9; i++)
					snap.ballRot[i] = spin.rot[i];
			}
		}
		snap.boost = sim->GetBoost(player);
		snap.boosting = sim->IsBoosting(player);
		snap.supersonic = sim->IsSupersonic(player);
		snap.flipReset = sim->HasFlipReset(player);
		snap.handbrake = ctl.handbrake;
		if (audioOn) {
			bool ground = sim->IsOnGround(player);
			float s01 = speed / 2300.0f;
			if (s01 > 1)
				s01 = 1;
			bool sliding = drive.handbrake && ground && speed > 300.0f;
			float tires = (ground && !sliding && speed > 200.0f) ? s01 * 0.65f : 0;
			float whoosh = (!ground && speed > 400.0f) ? s01 * 0.45f : 0;
			float roll = (bl.z < 120.0f && snap.ballSpeed > 800.0f)
				? std::min(0.3f, snap.ballSpeed / 5000.0f) : 0;
			float fly = (bl.z > 200.0f && snap.ballSpeed > 900.0f)
				? std::min(0.35f, snap.ballSpeed / 4000.0f) : 0;
			if (fly > 0)
				roll = 0;
			audio.Loops(snap.boosting ? 0.8f : 0, tires, whoosh,
				snap.supersonic ? 0.55f : 0, roll, fly);
			if (ground && !prevGround)
				audio.PlayLand();
			if (snap.supersonic && !prevSuper)
				audio.PlaySuper();
			audio.SetSlide(sliding);
			prevGround = ground;
			prevSuper = snap.supersonic;
		}
		// Off the ground, or moving up or down faster than 250 uu/s.
		snap.airborne = (cs.z > 45.0f) || (cs.vz > 250.0f) || (cs.vz < -250.0f);
		{
			// Angular speed from the change in forward and up since the previous tick.
			static float pF[3] = {1,0,0}, pU[3] = {0,0,1};
			static uint64_t pT = 0;
			uint64_t tk = sim->tick();
			if (pT != 0 && tk > pT) {
				float dt = float(tk - pT) / 120.0f;
				float df = (fwdx-pF[0])*(fwdx-pF[0])+(fwdy-pF[1])*(fwdy-pF[1])+(fwdz-pF[2])*(fwdz-pF[2]);
				float du = (upx-pU[0])*(upx-pU[0])+(upy-pU[1])*(upy-pU[1])+(upz-pU[2])*(upz-pU[2]);
				snap.angSpeed = (std::sqrt(df) + std::sqrt(du)) / (dt > 0 ? dt : 1.0f);
				if (snap.angSpeed > 30.0f) snap.angSpeed = 30.0f;
			}
			pF[0]=fwdx; pF[1]=fwdy; pF[2]=fwdz;
			pU[0]=upx; pU[1]=upy; pU[2]=upz; pT = tk;
		}
		snap.ballCam = camera.BallCam();
		snap.demoed = sim->IsDemoed(player); // FX respawn flash
		snap.tick = (uint32_t)sim->tick();
		rl::ContactSparkFrame contactSparks;
		{
			Sim::CarScrape scrape;
			Sim::BallTouch touch;
			bool playerHit = false;
			if (sim->GetCarScrape(player, &scrape) && sim->GetBallTouch(player, &touch)) {
				contactSparks = rl::ClassifyContact(scrape, touch, nullptr);
				playerHit = touch.hitThisTick;
			}
			if (audioOn) {
				if (playerHit)
					audio.PlayBall();
				else {
					Sim::BallTouch botTouch;
					if (sim->GetBallTouch(botIdx, &botTouch) && botTouch.hitThisTick)
						audio.PlayBall();
				}
			}
		}

		Sim::BodySample os;
		sim->GetCar(botIdx, &os);
		float obfx = 0, obfy = 1, obfz = 0;
		sim->GetCarForward(botIdx, &obfx, &obfy, &obfz);
		float oux = 0, ouy = 0, ouz = 1;
		sim->GetCarUp(botIdx, &oux, &ouy, &ouz);
		rl::Snapshot osnap = snap;
		osnap.carPos[0] = os.x;
		osnap.carPos[1] = os.y;
		osnap.carPos[2] = os.z;
		osnap.carYaw = atan2f(obfy, obfx);
		osnap.carForward[0] = obfx; osnap.carForward[1] = obfy; osnap.carForward[2] = obfz;
		osnap.carUp[0] = oux; osnap.carUp[1] = ouy; osnap.carUp[2] = ouz;
		osnap.carVel[0] = os.vx; osnap.carVel[1] = os.vy; osnap.carVel[2] = os.vz;
		osnap.boost = sim->GetBoost(botIdx);
		osnap.demoed = sim->IsDemoed(botIdx);
		osnap.supersonic = sim->IsSupersonic(botIdx);

		float dtf = float(dt > 0.1 ? 0.1 : dt);
		// Live settings -> camera (menu edits apply instantly, no restart).
		camera.Params().fov = settings.camera.fov;
		camera.Params().distance = settings.camera.distance;
		camera.Params().height = settings.camera.height;
		camera.Params().angle = settings.camera.angle;
		camera.Params().stiffness = settings.camera.stiffness;
		camera.Params().transitionSpeed = settings.camera.transitionSpeed;
		camera.Params().swivelSpeed = settings.camera.swivelSpeed;
		camera.Params().shake = settings.camera.shake;
		camera.Params().invertSwivel = settings.camera.invertSwivel;
		camera.Params().snap = settings.camera.snap;
		camera.Params().airHeading = settings.camera.airHeading;
		if (freeCam) {
			// Free flight: throttle dolly, steer orbit, jump/boost height.
			float fvx = freeCenter[0] - freeEye[0], fvy = freeCenter[1] - freeEye[1],
				  fvz = freeCenter[2] - freeEye[2];
			float fl = sqrtf(fvx * fvx + fvy * fvy + fvz * fvz);
			if (fl < 1e-3f) {
				fvx = 0;
				fvy = 1;
				fvz = 0;
				fl = 1;
			}
			fvx /= fl;
			fvy /= fl;
			fvz /= fl;
			float step = 1500.0f * dtf;
			freeEye[0] += fvx * ctl.throttle * step;
			freeEye[1] += fvy * ctl.throttle * step;
			freeEye[2] += fvz * ctl.throttle * step;
			freeCenter[0] += fvx * ctl.throttle * step;
			freeCenter[1] += fvy * ctl.throttle * step;
			freeCenter[2] += fvz * ctl.throttle * step;
			float orb = 1.2f * dtf * ctl.steer; // orbit eye around target
			float ox = freeEye[0] - freeCenter[0], oy = freeEye[1] - freeCenter[1];
			float co = cosf(orb), so = sinf(orb);
			freeEye[0] = freeCenter[0] + ox * co - oy * so;
			freeEye[1] = freeCenter[1] + ox * so + oy * co;
			float lift = (ctl.jump ? 1.0f : 0.0f) - (ctl.boost ? 1.0f : 0.0f);
			freeEye[2] += lift * step;
			freeCenter[2] += lift * step;
			if (freeEye[2] < 20)
				freeEye[2] = 20;
			camera.SetFreeView(freeEye, freeCenter);
		} else {
			// The chase rig owns distance and field of view. 2200 uu/s is supersonic.
			// Wheel contact, so a wall or the ceiling is a surface. The height check
			// on snap.airborne is only for effects.
			camera.Update(dtf, snap.carPos, snap.carYaw, snap.ballPos,
				camera.BallCam(), snap.carSpeed, snap.carSpeed >= 2200.0f,
				swivelX, swivelY, snap.carForward, snap.carVel,
				!sim->IsOnGround(player), snap.carUp);
		}
		// Shake offsets the eye. Off unless selected, and skipped in freecam or with --no-shake.
		if (!freeCam && !noShake && settings.camera.shake) {
			float sh[2];
			fx.ShakeOffset(sh);
			if (sh[0] != 0.0f || sh[1] != 0.0f) {
				float eye[3], ctr[3];
				camera.Eye(eye);
				camera.Center(ctr);
				eye[0] += sh[0]; eye[1] += sh[1];
				ctr[0] += sh[0] * 0.5f; ctr[1] += sh[1] * 0.5f;
				camera.SetRenderedPose(eye, ctr);
			}
		}
		// Ear is the published camera eye (after shake). Engine and the goal
		// horn attenuate from it; doppler uses the car's radial speed vs that
		// eye. Constants are UNVERIFIED (src/audio/Audio.cpp).
		if (audioOn) {
			float eye[3];
			camera.Eye(eye);
			float s01 = speed / 2300.0f;
			if (s01 > 1)
				s01 = 1;
			float cdx = cs.x - eye[0], cdy = cs.y - eye[1], cdz = cs.z - eye[2];
			float carDist = sqrtf(cdx * cdx + cdy * cdy + cdz * cdz);
			float bdx = bl.x - eye[0], bdy = bl.y - eye[1], bdz = bl.z - eye[2];
			float ballDist = sqrtf(bdx * bdx + bdy * bdy + bdz * bdz);
			float radial = 0;
			if (haveEar && dtf > 1e-4f && carDist > 1.0f) {
				float evx = (eye[0] - prevEar[0]) / dtf;
				float evy = (eye[1] - prevEar[1]) / dtf;
				float evz = (eye[2] - prevEar[2]) / dtf;
				float inv = 1.0f / carDist;
				radial = ((cs.vx - evx) * cdx + (cs.vy - evy) * cdy + (cs.vz - evz) * cdz) * inv;
			}
			prevEar[0] = eye[0];
			prevEar[1] = eye[1];
			prevEar[2] = eye[2];
			haveEar = true;
			audio.Engine(s01, sim->IsBoosting(player), carDist, ballDist, radial);
		}
		// FX advance on SIM time (tick delta), not wall time: FX timers, pulses
		// and the slow-mo beat are deterministic for capture and independent
		// of frame pacing. A repeated render frame does not advance FX.
		{
			uint64_t tk = sim->tick();
			float fdt = tk > lastFxTick
				? float(tk - lastFxTick) * float(Sim::kTickDt) : 0.0f;
			lastFxTick = tk;
			fx.Update(fdt, snap);
			goalBurst.Update(fdt);
		}
		if (!fxProbe.empty() && (long long)sim->tick() == fxProbeTick) {
			if (fxProbe == "goal") {
				fx.OnGoal();
				goalBurst.Trigger(snap.ballPos[0], snap.ballPos[1], snap.ballPos[2]);
			}
			else if (fxProbe == "demo")
				fx.OnDemo();
			else if (fxProbe == "pickup")
				fx.OnPickup(true);
		}

		renderer.BeginFrame();
		camera.Apply(1280, 720);
		// Debug grid goes under the turf so the turf covers it.
		if (debugGrid)
			renderer.DrawGroundGrid(5120.0f, 512.0f);
		// A loaded pack replaces the analytic shell. Drawing both z-fights the floors.
		if (renderer.HasArenaPack())
			renderer.DrawArenaPack();
		else
			renderer.DrawArenaBuckets(sim->usingRealSoccar(), noHexNet,
			                          settings.render.wallOpacity);
		// Pads.
		{
			int n = sim->NumPads();
			std::vector<rl::Pad> pads;
			pads.reserve(size_t(n));
			for (int i = 0; i < n; i++) {
				Sim::PadSample ps;
				if (!sim->GetPad(i, &ps))
					continue;
				rl::Pad p;
				p.x = ps.x;
				p.y = ps.y;
				p.big = ps.big;
				p.active = ps.active;
				p.cooldown = ps.cooldown;
				pads.push_back(p);
			}
			// The ripped stadium already has the real pad meshes. The procedural
			// discs would sit on top of them as a second set of pads.
			if (!pads.empty() && !renderer.HasArenaPack())
				fx.DrawPads(renderer, pads.data(), int(pads.size()));
		}
		// Loaded-pack only. The bowl's sun is already in the lightmap, so this
		// is a pitch-plane blob under the cars and the ball. No call, and no
		// extra GL, on the analytic shell.
		if (renderer.HasArenaPack())
			rl::DrawPitchSunShadows(renderer, snap, osnap);
		// Cars: Fennec GLB body when available (CC-BY-4.0), box fallback else.
		// Orientation now carries pitch/roll (carForward/carUp), so rolls and
		// flips are visible instead of being flattened to a 2D yaw.
		if (rl::FennecAvailable() && !noFennec) {
			rl::WheelMotion blueW = rl::UpdateWheelMotion(snap, 0);
			rl::WheelMotion orangeW = rl::UpdateWheelMotion(osnap, 1);
			rl::FennecDraw(renderer, snap.carPos, snap.carForward, snap.carUp, false,
			               blueW.steer, blueW.spin);
			rl::FennecDraw(renderer, osnap.carPos, osnap.carForward, osnap.carUp, true,
			               orangeW.steer, orangeW.spin);
		} else {
			rl::DrawCar(renderer, snap, true);
			rl::DrawCar(renderer, osnap, false);
		}
		rl::DrawBall(renderer, snap);
		// After the cars, so the goal glass blends over them. Skipped when a pack is loaded.
		if (!renderer.HasArenaPack())
			renderer.DrawArenaGoalInteriors(sim->usingRealSoccar());
		if (debugColliders) {
			// Over the shaded scene, with the depth test off.
			glDisable(GL_DEPTH_TEST);
			rl::DrawDebugColliders(renderer, snap, osnap);
			// Pad pickup cylinders (real radii 208 big / 144 small).
			{
				int n = sim->NumPads();
				for (int i = 0; i < n; i++) {
					Sim::PadSample ps;
					if (!sim->GetPad(i, &ps))
						continue;
					float rad = ps.big ? 208.0f : 144.0f;
					renderer.DrawWireCylinder(ps.x, ps.y, 0, rad, ps.big ? 168.0f : 120.0f,
						16, 1.0f, 0.85f, 0.2f, 0.6f);
				}
			}
			// Shell, then both goal mouths.
			renderer.DrawWireBox(0, 0, 1022, 8192, 10240, 2044, 0.3f, 0.7f, 1.0f, 0.8f);
			for (int g = 0; g < 2; g++) {
				float ys = g == 0 ? 5120.0f : -5120.0f;
				renderer.DrawWireBox(0, ys, 321.4f, 1785.51f, 24, 642.775f,
					0.6f, 1.0f, 0.6f, 0.9f);
			}
			glEnable(GL_DEPTH_TEST);
		}
		if (debugColliders)
			renderer.DrawText2D(28, 30, "DEBUG COLLIDERS: car hitbox / ball r91.25 / pads / shell",
				1, 1.0f, 0.9f, 0.3f);
		fx.DrawBallIndicator(renderer, snap);
		// Path streak only. Does not use the indicator or DrawTrail (car mark).
		fx.DrawBallTrail(renderer, snap);
		fx.DrawBoostFlame(renderer, snap);
		fx.DrawDrift(renderer, snap);
		fx.DrawAir(renderer, snap);
		fx.DrawTrail(renderer, snap);
		goalBurst.Draw(renderer);
		fx.DrawWheelEmbers(renderer, snap);
		fx.DrawContactSparks(renderer, contactSparks);
		if (menu.IsOpen()) {
			const auto& items = menu.items();
			int sel = menu.selected();
			std::vector<std::string> lines = menu.RenderText();
			// Duplicate keyboard bindings get an orange value as a conflict
			// hint (gamepad names live in a separate namespace: skipped).
			std::map<std::string, int> bindUse;
			for (const auto& it : items)
				if (it.kind == rl::Menu::ItemKind::Binding && it.value) {
					std::string v = it.value();
					if (!v.empty() && v != "(none)") bindUse[v]++;
				}
			// NOTE: lines[0] is the "=== SETTINGS ===" title; items[n]
			// renders as lines[n+1]. All index math below accounts for it.
			const int kRows = 22;  // visible item rows under the title
			int first = 1;
			int selLine = sel + 1;
			if ((int)lines.size() > kRows + 1) {
				first = selLine - kRows / 2;
				if (first < 1) first = 1;
				int maxFirst = (int)lines.size() - kRows;
				if (first > maxFirst) first = maxFirst;
			}
			int last = std::min((int)lines.size(), first + kRows);
			const int px = 190, pw = 900;
			const int rowH = 22, topH = 44, footH = 30;
			int ph = topH + (last - first) * rowH + footH + 14;
			int py = (720 - ph) / 2;
			if (py < 8) py = 8;
			renderer.FillRect2D(0, 0, 1280, 720, 0.01f, 0.02f, 0.04f, 0.72f);
			renderer.FillRect2D(px, py, pw, ph, 0.05f, 0.08f, 0.14f, 0.96f);
			renderer.FillRect2D(px, py, pw, topH, 0.07f, 0.28f, 0.55f, 1.0f);
			renderer.FillRect2D(px, py + topH - 3, pw, 3, 0.15f, 0.75f, 0.95f, 1.0f);
			renderer.DrawText2D(px + 24, py + 10, "SETTINGS", 3, 1, 1, 1);
			renderer.DrawText2D(px + pw - 320, py + 14, "ROCKET LEAGUE-STYLE", 1,
				0.75f, 0.85f, 0.95f);
			if (menu.IsCapturing())
				renderer.DrawText2D(px + 24, py + 28, menu.CapturePrompt().c_str(), 1, 1.0f,
					0.9f, 0.4f);
			auto itemAt = [&](int line) -> const rl::Menu::Item* {
				int n = line - 1;  // lines[0] is the title, not an item
				if (n < 0 || n >= (int)items.size()) return nullptr;
				return &items[size_t(n)];
			};
			auto isHeader = [&](int line) {
				const auto* it = itemAt(line);
				return it && it->kind == rl::Menu::ItemKind::Header;
			};
			auto isBind = [&](int line) {
				const auto* it = itemAt(line);
				return it && (it->kind == rl::Menu::ItemKind::Binding ||
							  it->kind == rl::Menu::ItemKind::PadBinding);
			};
			auto isKeyBind = [&](int line) {
				const auto* it = itemAt(line);
				return it && it->kind == rl::Menu::ItemKind::Binding;
			};
			for (int i = first; i < last; i++) {
				int ry = py + topH + (i - first) * rowH;
				bool isSel = (i == selLine && !menu.IsCapturing());
				if (isHeader(i)) {
					renderer.FillRect2D(px + 12, ry + 1, pw - 24, rowH - 2, 0.10f, 0.22f,
						0.38f, 1.0f);
					// Strip the "--- X ---" / "  label : value" framing.
					std::string t = lines[size_t(i)];
					if (t.rfind("--- ", 0) == 0) t = t.substr(4);
					while (!t.empty() && (t.back() == '-' || t.back() == ' ')) t.pop_back();
					renderer.DrawText2D(px + 24, ry + 4, t.c_str(), 2, 0.55f, 0.85f, 1.0f);
					continue;
				}
				if (isSel)
					renderer.FillRect2D(px + 12, ry, pw - 24, rowH, 0.12f, 0.55f, 0.85f,
						0.95f);
				// Split "prefix label : value" into label/value columns.
				std::string row = lines[size_t(i)];
				std::string body = row.size() > 2 ? row.substr(2) : row;
				std::string label = body, val;
				size_t c = body.find(" : ");
				if (c != std::string::npos) {
					label = body.substr(0, c);
					val = body.substr(c + 3);
				}
				float lr = isSel ? 1.0f : 0.88f, lg = isSel ? 1.0f : 0.92f,
					  lb = isSel ? 1.0f : 0.95f;
				renderer.DrawText2D(px + 30, ry + 4,
					(std::string(isSel ? "> " : "  ") + label).c_str(), 2, lr, lg, lb);
				if (!val.empty()) {
					bool conflict = false;
					if (isKeyBind(i)) {
						auto f = bindUse.find(val);
						conflict = (f != bindUse.end() && f->second > 1);
					}
					// Binding pill behind the value for keybind rows.
					if (isBind(i)) {
						int vw = (int)val.size() * 12 + 16;
						renderer.FillRect2D(px + pw - 24 - vw, ry + 2, vw, rowH - 4,
							isSel ? 0.02f : 0.10f, isSel ? 0.25f : 0.16f,
							isSel ? 0.42f : 0.26f, 1.0f);
					}
					int vx = px + pw - 28 - (int)val.size() * 12;
					float vr = 1.0f, vg = 1.0f, vb = 1.0f;
					if (conflict) {
						vr = 1.0f;
						vg = 0.6f;
						vb = 0.2f;
					} else if (!isSel) {
						vr = 0.55f;
						vg = 0.85f;
						vb = 1.0f;
					}
					renderer.DrawText2D(vx, ry + 4, val.c_str(), 2, vr, vg, vb);
				}
			}
			if ((int)lines.size() > kRows + 1) {
				float frac = float(first - 1) / float((int)lines.size() - 1);
				float h = float(last - first) / float((int)lines.size() - 1);
				int bx = px + pw - 8, by = py + topH;
				int bh = (int)((ph - topH - footH - 14) * h);
				int bo = (int)((ph - topH - footH - 14 - bh) * frac);
				renderer.FillRect2D(bx, by + bo, 4, std::max(12, bh), 0.15f, 0.6f, 0.85f,
					0.9f);
			}
			renderer.FillRect2D(px, py + ph - footH, pw, 2, 0.15f, 0.75f, 0.95f, 1.0f);
			renderer.DrawText2D(px + 24, py + ph - footH + 8,
				"Up/Down navigate   Enter rebind/adjust   Del unbind   Esc close", 1, 0.7f,
				0.8f, 0.9f);
		}
		{
			char hud[160];
			std::snprintf(hud, sizeof(hud), "%s SPD %.0f TICK %u%s",
				camera.BallCam() ? "BALLCAM" : "CARCAM", (double)speed,
				snap.tick, freeCam ? " FREECAM" : "");
			renderer.FillRect2D(20, 720 - 40, 460, 26, 0.02f, 0.03f, 0.05f, 0.6f);
			renderer.DrawText2D(28, 720 - 34, hud, 2, 0.9f, 0.95f, 1.0f);
		}
		// RL-style circular boost gauge (bottom-right, ~2/3 scale of 1080p).
		{
			float cx = 1157.0f, cy = 617.0f;
			float rIn = 59.0f, rOut = 70.0f; // orange band
			// Blue dial: layered discs, bright core fading to dark rim.
			renderer.DrawDisc2D(cx, cy, 56.0f, 48, 0.02f, 0.05f, 0.10f, 0.97f);
			renderer.DrawDisc2D(cx, cy, 50.0f, 48, 0.06f, 0.20f, 0.44f, 0.97f);
			renderer.DrawDisc2D(cx, cy, 40.0f, 48, 0.10f, 0.32f, 0.68f, 0.97f);
			renderer.DrawDisc2D(cx, cy, 28.0f, 48, 0.16f, 0.45f, 0.86f, 0.97f);
			// Filled portion: boost/100 of the 260-degree sweep (50° -> 310°).
			float frac = snap.boost / 100.0f;
			if (frac > 1) frac = 1;
			// Background track (dim) then orange segments over filled arc.
			renderer.DrawArc2D(cx, cy, rIn, rOut, 50.0f, 310.0f, 48,
				0.10f, 0.12f, 0.16f, 0.9f);
			const int SEG = 26;
			float sweep = 260.0f * frac;
			// Fixed 10-degree steps across the 260-degree sweep. The last step
			// is clipped, so arc length stays linear in boost.
			for (int s = 0; s < SEG; s++) {
				float t0 = float(s) / SEG, t1 = float(s + 1) / SEG;
				if (t0 >= frac) break;
				float a0 = 310.0f - 260.0f * t0; // sweep clockwise from 310°
				float a1 = 310.0f - 260.0f * t1;
				if (a1 < 310.0f - sweep) a1 = 310.0f - sweep;
				// Gradient: yellow near the 50° end, orange at 310° end.
				float g = 0.82f - 0.55f * t1;
				renderer.DrawArc2D(cx, cy, rIn, rOut, a1, a0, 3,
					1.0f, g < 0.45f ? 0.45f : g, 0.12f, 0.98f);
			}
			// White tick marks in the empty 100° zone (right side).
			for (int t = 0; t < 8; t++) {
				float ang = (50.0f - t * (100.0f / 7.0f)) * (float)M_PI / 180.0f;
				float c = cosf(ang), s2 = sinf(ang);
				renderer.DrawLine2D(cx + c * rIn, cy + s2 * rIn,
					cx + c * rOut, cy + s2 * rOut, 3.0f, 0.95f, 0.95f, 0.95f, 0.9f);
			}
			char num[8];
			std::snprintf(num, sizeof(num), "%.0f", (double)snap.boost);
			int w = (int)std::strlen(num) * 24;
			renderer.DrawText2D((int)(cx - w / 2), (int)(cy - 16), num, 4, 1, 1, 1);
			renderer.DrawText2D((int)(cx - 24), (int)(cy + 14), "BOOST", 1, 0.85f, 0.9f, 1.0f);
		}
		// Off during --shot-at-tick so VG1 captures stay byte-identical.
		rl::MatchHud::Draw(renderer, sim->tick(), kickoffTick, shotAtTick < 0);
		if (shotAtTick >= 0 && (long long)sim->tick() >= shotAtTick && !shotTaken) {
			shotTaken = 1;
			const char* p = shotPath.empty() ? "shot_tick.ppm" : shotPath.c_str();
			if (renderer.SaveScreenshot(p))
				std::fprintf(stderr, "[game] timed screenshot %s tick=%llu\n", p,
					(unsigned long long)sim->tick());
		}
		if (shotTaken == 2) {
			shotTaken = 0;
			char path[64];
			std::snprintf(path, sizeof(path), "shot_%llu.ppm",
				(unsigned long long)sim->tick());
			if (renderer.SaveScreenshot(path))
				std::fprintf(stderr, "[game] screenshot %s\n", path);
		}
		if (shotEvery > 0 && !shotDir.empty() &&
			(long long)sim->tick() >= shotAtTick + shotCount * shotEvery) {
			char path[256];
			std::snprintf(path, sizeof(path), "%s/frame_%06llu.ppm", shotDir.c_str(),
				(unsigned long long)sim->tick());
			renderer.SaveScreenshot(path);
			shotCount++;
		}
		renderer.EndFrame();
		frames++;
		if (frames <= 3) {
			// Window and GL setup stall the first frames. Drop that gap from the accumulator.
			lastMs = NowMs();
		}

		if (now - lastHudMs > 2000) {
			lastHudMs = now;
			rl::Renderer::Pacing pc = renderer.Stats();
			rl::Renderer::Counters rc = renderer.Stats3D();
			std::fprintf(stderr,
				"[hud] tick=%llu speed=%.0f boost=%.0f ball=(%.0f,%.0f,%.0f) drops=%llu fps~%.0f p50=%.2fms p99=%.2fms tris=%llu draws=%llu glerr=%llu in=(t%.1f,s%.1f,j%d,b%d,h%d)%s\n",
				(unsigned long long)sim->tick(), (double)speed, (double)snap.boost,
				(double)bl.x, (double)bl.y, (double)bl.z,
				(unsigned long long)sim->physicsDropCount(),
				pc.avg > 0 ? 1000.0 / pc.avg : 0.0, pc.p50, pc.p99,
				(unsigned long long)rc.tris, (unsigned long long)rc.drawCalls,
				(unsigned long long)rc.glErrors,
				(double)ctl.throttle, (double)ctl.steer, ctl.jump ? 1 : 0,
				ctl.boost ? 1 : 0, ctl.handbrake ? 1 : 0,
				freeCam ? " FREECAM" : (camera.BallCam() ? " BALLCAM" : " CARCAM"));
		}
	}

	rl::Renderer::Pacing pc = renderer.Stats();
	rl::Renderer::Counters rc = renderer.Stats3D();
	if (renderer.HasArenaPack()) {
		const rl::Renderer::ArenaPackStats& as = renderer.ArenaPackInfo();
		std::fprintf(stderr,
			"[arena] exit buckets=%u visible=%u culled=%u textured=%u fallback=%u "
			"tris=%llu\n",
			as.buckets, as.visible, as.culled, as.texturedBuckets,
			as.untexturedBuckets, (unsigned long long)as.tris);
	}
	std::fprintf(stderr,
		"[game] exit ticks=%llu drops=%llu frames=%llu wall=%.1fs p50=%.2fms p99=%.2fms avg=%.2fms frame_ms=%.2f unlimited=%d bot=%s audio_cb=%llu audio_frames=%llu tris_last=%llu draws_last=%llu tris_total=%llu draws_total=%llu glerr=%llu fennec_tris=%d fennec_verts=%d\n",
		(unsigned long long)sim->tick(), (unsigned long long)sim->physicsDropCount(),
		(unsigned long long)frames, double(NowMs() - t0) / 1000.0, pc.p50, pc.p99,
		pc.avg, (double)rc.frame_ms, unlimitedBoost ? 1 : 0, rlbot::BotStub::modeName(bot.mode()),
		(unsigned long long)audio.callbacks(), (unsigned long long)audio.framesMixed(),
		(unsigned long long)rc.tris, (unsigned long long)rc.drawCalls,
		(unsigned long long)rc.trisTotal, (unsigned long long)rc.drawsTotal,
		(unsigned long long)rc.glErrors, rl::FennecTriCount(), rl::FennecVertCount());

	// Exit does not write settings. The menu save action does.
	// Free the arena textures while the GL context is still current.
	rl::FreeArenaTextures();
	delete sim;
	return 0;
}
