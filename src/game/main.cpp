// Free-play game: SDL2+GL2.1 720p60 renderer + RocketSim 120Hz sim core.
// MIT License.
//
// Loop (physics-first): wall-clock dt -> Sim::StepAccumulator (120Hz fixed,
// max 5/frame, drops counted) -> render latest snapshot (vsync; on overrun we
// skip submission, physics never stalls).
//
// Usage: game [--bot stationary|mirror|slow-chase] [--soak SECS] [--ticks N]
//   [--seed S] [--no-vsync]
//   --soak/--ticks auto-exit with pacing+drop report (soak harness).
#include <SDL2/SDL.h>
#include <GL/gl.h> // --calibrate 3D-path probe draws raw fixed-function quads

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "bot/BotStub.h"
#include "audio/Audio.h"
#include "camera/Camera.h"
#include "fx/FX.h"
#include "input/Keyboard.h"
#include "input/Gamepad.h"
#include "menu/Menu.h"
#include "models/Fennec.h"
#include "models/Models.h"
#include "models/ArenaRip.h"
#include "models/ArenaTextures.h"
#include "render/Renderer.h"
#include "settings/Settings.h"
#include "sim/Sim.h"

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

} // namespace

int main(int argc, char** argv) {
	const char* botName = "stationary";
	double soakSecs = 0;
	long long tickBudget = -1;
	uint64_t seed = 0xA15EEDULL;
	bool seedGiven = false, noVsync = false, menuAtStart = false, noAudio = false, flatShading = false;
	bool calibrate = false;
	std::string meshDir; // --meshdir override; empty = settings [arena] mesh_dir
	// A6b: --arenadir override; empty = [arena] arena_dir in config/settings.ini.
	// The pack is a Psyonix-owned RUNTIME input (gitignored arena_assets/): it
	// is read once at startup, never written, never committed.
	std::string arenaDir;
	bool arenaDirFlag = false;
	float camEye[3] = {0, 0, 0}, camTarget[3] = {0, 0, 0};
	bool camSet = false; // --cam hook: visual-verification freecam pose
	long long shotAtTick = -1, shotEvery = 0, shotCount = 0;
	std::string shotPath, shotDir;
	// ---- visual-harness hooks (MASTER_PROMPT_VISUALS.md W1) ----------------
	// All coordinator-owned, deterministic, debug-only. They never alter the
	// sim's physics path: --ballpos uses the existing Sim::SetBall test
	// teleport, --boost uses Sim::SetBoost, --nofennec only changes what is
	// drawn, --fx-probe arms a captured FX event on a fixed tick.
	bool ballPosSet = false;
	float ballPos[3] = {0, 0, 0};
	float forcedBoost = -1.0f; // <0 = off
	bool noFennec = false;
	bool debugColliders = false;
	bool debugGrid = false;
	bool noHexNet = false;
	bool noShake = false;
	bool driveOverride = false; // --drive harness input injection
	rl::Controls driveCtl;      // filled from --drive
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
			flatShading = true; // unlit: pure glColor (lighting debug + weak GPUs)
		} else if (a == "--debug-grid") {
			// Instrument only: the grid used to be drawn unconditionally in the
			// play path, covering +/-9000 uu (2x past the walls) and reading as
			// an endless "playground" floor (docs/VISUAL_BUGS_TODO.md V4).
			debugGrid = true;
		} else if (a == "--no-hexnet") {
			// Instrument for VG6: draw the glass shell WITHOUT the honeycomb
			// net, so the gate can difference the two frames and isolate the
			// net's line pattern from the wall's own texture.
			noHexNet = true;
		} else if (a == "--no-audio") {
			noAudio = true;
		} else if (a == "--menu") {
			menuAtStart = true; // verification hook: open menu on launch
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
			// Visual-verification hook: freecam at eye,tx,ty,tz (uu).
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
			noFennec = true; // force the procedural box car (wheel/FX gates)
		} else if (a == "--debug-colliders") {
			debugColliders = true; // W6 instrument: draw collision proxies
		} else if (a == "--no-shake") {
			noShake = true;
		} else if (a == "--drive") {
			// Harness hook: inject constant controls every frame (visual
			// gates need a deterministic, scripted car pose without input).
			// Format: throttle,steer,pitch,yaw,roll,jump,boost,handbrake.
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
			calibrate = true; // render palette quads only + screenshot, then exit
		} else if (a == "--help" || a == "-h") {
			std::fprintf(stderr,
				"usage: %s [--bot stationary|mirror|slow-chase] [--soak SECS] [--ticks N] [--seed S]\n"
				"       [--menu] [--shot-at-tick N --shot-path P] [--shot-every N --shot-dir D]\n"
				"       [--no-vsync] [--no-audio] [--meshdir DIR] [--arenadir DIR]\n"
				"visual-harness (debug): [--ballpos x,y,z] [--boost N] [--drive t,s,p,y,r,j,b,h]\n"
				"       [--nofennec] [--debug-colliders] [--debug-grid] [--no-hexnet] [--no-shake]\n"
				"       [--fx-probe goal|demo|pickup --fx-probe-tick N]\n"
				"keys: WASD drive, Space jump, Shift boost, S/Ctrl slide, Q/E roll, C/B cam,\n"
				"      F freecam, Esc menu, R/T resets, F1-F5 kickoffs, M mute, F12 screenshot\n",
				argv[0]);
			return 0;
		} else {
			std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
			return 2;
		}
	}

	// Settings (versioned .ini, contract source).
	rl::Settings settings = rl::Settings::Defaults();
	{
		std::string err;
		if (!rl::Settings::Load("config/settings.ini", settings, &err))
			std::fprintf(stderr, "[game] settings load: %s (defaults)\n", err.c_str());
	}

	// ---- A6b: the ripped arena pack ---------------------------------------
	// Precedence: the --arenadir FLAG wins over [arena] arena_dir in
	// config/settings.ini.  A directory that yields no loadable pack is a
	// LOUD FATAL (stderr, non-zero exit): there is never a silent fallback
	// to the analytic shell.  With no flag and no INI key NOTHING happens:
	// no pack is read, no GL object is created, the default path is
	// byte-identical.
	rl::ArenaPack arenaPack;
	bool useArenaPack = false;
	{
		std::string iniArena;
		if (!arenaDirFlag &&
			rl::ReadIniKey("config/settings.ini", "arena", "arena_dir", &iniArena))
			arenaDir = iniArena;
		if (!arenaDir.empty()) {
			if (!rl::LoadArenaPack(arenaDir, arenaPack)) {
				std::fprintf(stderr,
					"[game] FATAL: arena dir '%s' holds no loadable arena pack "
					"(expected the manifest arena.json): %s\n",
					arenaDir.c_str(), rl::ArenaPackError(arenaPack));
				return 1;
			}
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

	// Collision mesh: --meshdir wins, else [arena] mesh_dir, else RocketSim's
	// built-in default path (./collision_meshes/). A directory that loads no
	// Soccar mesh is a hard error, never a silent fallback to analytic shapes.
	if (meshDir.empty())
		meshDir = settings.arena.meshDir;
	Sim* sim = Sim::CreateSoccar(meshDir.empty() ? nullptr : meshDir.c_str());
	if (!sim) {
		std::fprintf(stderr, "[game] failed to create sim\n");
		return 1;
	}
	if (!meshDir.empty() && !sim->usingRealSoccar()) {
		std::fprintf(stderr,
			"[game] FATAL: mesh dir '%s' loaded no Soccar collision mesh "
			"(expected %s/soccar/*.cmf; see docs/MESH_SETUP.md)\n",
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
		// Harness hook: deterministic ball placement for FX/indicator capture
		// (uses Sim's existing test teleport; not a gameplay path).
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
	// GRAPHICS45 G2: request GL 4.5 compatibility profile via SDL_GL attrs.
	// (Renderer::Init re-asserts the same values before CreateWindow; both
	// agree. No fallback — Init aborts loudly when 4.5-compat is unavailable.)
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
	// A6b: upload the ripped arena ONCE (static buffers, no per-frame work).
	// The GL context is live now, so the upload can happen here — never in
	// the frame loop.
	if (useArenaPack) {
		if (!renderer.BuildArenaPack(arenaPack)) {
			std::fprintf(stderr,
				"[game] FATAL: the arena pack in '%s' holds no drawable geometry\n",
				arenaDir.c_str());
			delete sim;
			return 1;
		}
		const rl::Renderer::ArenaPackStats& as = renderer.ArenaPackInfo();
		std::fprintf(stderr,
			"[arena] draw path: buckets=%u visible(last)=%u culled(last)=%u "
			"textured=%u fallback=%u tris=%llu fallback_material=neutral-grey(0.55,0.55,0.55)\n",
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
	rlbot::BotStub bot(ParseBot(botName));
	bool unlimitedBoost = false;
	if (calibrate) {
		// Palette ground truth through the game's own context: 5 fullscreen
		// bands in exact game colors, straight to the screenshot.
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
	bool audioOn = !noAudio && audio.Init();
	float prevBoostHud = 0;
	float prevBoostFx = 0; // pickup-burst edge, independent of audio
	bool prevJumpHeld = false;

	// Menu wired to free-play actions.
	rl::Menu menu(settings, rl::FreePlayActions{
		[&]() { sim->ResetCarToSpawn(player); sim->ResetCarToSpawn(botIdx); },
		[&]() { sim->ResetBall(); },
		[&](int) { sim->ResetCarToSpawn(player); sim->ResetCarToSpawn(botIdx); sim->ResetBall(); },
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
		// Discrete key events.
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
					// Unattended harness mode: the desktop may close/reparent
					// windows; log and keep measuring (timer ends the run).
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
				} else if (k >= SDLK_1 && k <= SDLK_5) {
					Sim::BodySample cc;
					sim->GetCar(player, &cc);
					float fx0 = 0, fy0 = 1;
					float fz0 = 0;
					sim->GetCarForward(player, &fx0, &fy0, &fz0);
					float fl = sqrtf(fx0*fx0 + fy0*fy0);
					if (fl > 1e-6f) { fx0 /= fl; fy0 /= fl; }
					int drill = (int)(k - SDLK_1);
					if (drill == 0) sim->SetBall(cc.x + fx0*400, cc.y + fy0*400, 110, 0, 0, 0);
					else if (drill == 1) sim->SetBall(cc.x, cc.y, cc.z + 250, cc.vx, cc.vy, cc.vz);
					else if (drill == 2) sim->SetBall(cc.x + fx0*1800, cc.y + fy0*1800, 300, -fx0*1200, -fy0*1200, 200);
					else if (drill == 3) sim->SetBall(cc.x, cc.y, 110, 0, 0, 1400);
					else sim->SetBall(cc.x * 0.3f, (cc.y > 0 ? 3500 : -3500), 500, 0, (cc.y > 0 ? 1200 : -1200), 300);
					std::fprintf(stderr, "[game] drill %d ball spawn\n", drill + 1);
				}
			}
		}

		keyboard.Poll(ctl, ev);
		// Gamepad merge (additive): the pad's analog stick owns throttle/steer
		// when connected; digital buttons and edge events are OR-ed so either
		// device can drive the car and neither cancels the other. A
		// disconnected pad contributes nothing (keyboard-only behaviour is
		// bit-identical to before).
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
			if (ev.resetCar)
				sim->ResetCarToSpawn(player);
			if (ev.resetBall)
				sim->ResetBall();
			if (ev.toggleUnlimitedBoost)
				unlimitedBoost = !unlimitedBoost;
			if (ev.kickoffIndex >= 0) {
				// Ball drills 1..5 (rebindable via kickoff_0..4): ball-only.
				Sim::BodySample cc;
				sim->GetCar(player, &cc);
				float fx0 = 0, fy0 = 1;
				float fz0 = 0;
				sim->GetCarForward(player, &fx0, &fy0, &fz0);
				float fl = sqrtf(fx0*fx0 + fy0*fy0);
				if (fl > 1e-6f) { fx0 /= fl; fy0 /= fl; }
				int drill = ev.kickoffIndex;
				if (drill == 0) sim->SetBall(cc.x + fx0*400, cc.y + fy0*400, 110, 0, 0, 0);
				else if (drill == 1) sim->SetBall(cc.x, cc.y, cc.z + 250, cc.vx, cc.vy, cc.vz);
				else if (drill == 2) sim->SetBall(cc.x + fx0*1800, cc.y + fy0*1800, 300, -fx0*1200, -fy0*1200, 200);
				else if (drill == 3) sim->SetBall(cc.x, cc.y, 110, 0, 0, 1400);
				else sim->SetBall(cc.x * 0.3f, (cc.y > 0 ? 3500 : -3500), 500, 0, (cc.y > 0 ? 1200 : -1200), 300);
				std::fprintf(stderr, "[game] drill %d ball spawn\n", drill + 1);
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
			drive = driveCtl; // harness input wins (visual gates)
		sim->SetControls(player, drive.throttle, drive.steer, drive.pitch, drive.yaw,
			drive.roll, drive.jump, drive.boost, drive.handbrake);
		// Bot -> sim.
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
			// Harness hook: hold a known boost value for gauge captures.
			sim->SetBoost(player, forcedBoost);
		}
		// Audio: continuous engine + event blips.
		if (audioOn) {
			float s01 = lastSpeed / 2300.0f;
			if (s01 > 1)
				s01 = 1;
			audio.Engine(s01, sim->IsBoosting(player));
			if (drive.jump && !prevJumpHeld)
				audio.PlayJump();
			float pb = sim->GetBoost(player);
			if (!unlimitedBoost && pb - prevBoostHud >= 5.0f)
				audio.PlayPickup(pb >= 99.0f);
			prevBoostHud = pb;
			prevJumpHeld = drive.jump;
		}
		// Pickup burst FX on the same edge, audio-independent (W5 feedback).
		{
			float pb = sim->GetBoost(player);
			if (!unlimitedBoost && forcedBoost < 0 && pb - prevBoostFx >= 5.0f)
				fx.OnPickup(pb >= 99.0f);
			prevBoostFx = pb;
		}

		// Physics-first stepping.
		uint64_t now = NowMs();
		double dt = double(now - lastMs) / 1000.0;
		lastMs = now;
		if (dt < 0)
			dt = 0;
		sim->StepAccumulator(dt);
		// No warmup exclusion: the frame clock resyncs after the first 3
		// presented frames (see below), so GL-context creation stall never
		// enters the accumulator. Every counted drop is real.
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

		// Snapshot -> camera/render.
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
		snap.boost = sim->GetBoost(player);
		snap.boosting = sim->IsBoosting(player);
		snap.handbrake = ctl.handbrake;
		// Render-only airborne heuristic: clearly off the ground or moving
		// vertically fast. No sim coupling (no new Sim API).
		snap.airborne = (cs.z > 45.0f) || (cs.vz > 250.0f) || (cs.vz < -250.0f);
		{
			// Frame angular speed from fwd/up change (flip / stall gate).
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
			// Supersonic threshold 2200 (G8 MEASURED) selects the extra
			// dynamic distance/FOV kick (Rocket Science #2). #2 air heading:
			// nose vector + velocity + airborne heuristic select the air
			// branch (presentation-only; filled render-only above, no sim).
			camera.Update(dtf, snap.carPos, snap.carYaw, snap.ballPos,
				camera.BallCam(), snap.carSpeed, snap.carSpeed >= 2200.0f,
				swivelX, swivelY, snap.carForward, snap.carVel,
				snap.airborne);
		}
		// Camera vs arena (1:1 feel, corrected 2026-10-21): RL does NOT
		// reposition the chase eye against side/back walls — `Camera_TA`'s
		// `ClipToField`/`ClipRate` is the replay/spectator clip, and the playing
		// camera is free to enter wall geometry (hug a wall in RL and the wall
		// fills the screen; ballcam near a goal shows through the back net).
		// The old hard AABB clamp here (eye forced 120 uu inside
		// +/-4096/+-5120/2048) was ours, not RL's, and it teleported the eye
		// sideways instead of letting the rig sit where RL puts it. Removed.
		// Kept: the vertical guards only — the floor (never see the underside
		// of the pitch) and the ceiling inset, which `ClipToField` does cover.
		{
			float eye[3], ctr[3];
			camera.Eye(eye);
			camera.Center(ctr);
			float cx = eye[0], cy = eye[1], cz = eye[2];
			if (cz > 2048 - 120) cz = 2048 - 120;
			if (cz < 40) cz = 40;
			if (cx != eye[0] || cy != eye[1] || cz != eye[2]) {
				float ne[3] = {cx, cy, cz};
				camera.SetFreeView(ne, ctr);
			}
		}
		// Camera shake (W5): a presentation-only overlay on the eye. It never
		// touches CameraParams (the 1:1 defaults stay exact) and is disabled
		// for freecam/capture, with --no-shake, and by the [camera] shake
		// setting (RL default ON), so gate captures stay pixel-stable.
		if (!freeCam && !noShake && settings.camera.shake) {
			float sh[2];
			fx.ShakeOffset(sh);
			if (sh[0] != 0.0f || sh[1] != 0.0f) {
				float eye[3], ctr[3];
				camera.Eye(eye);
				camera.Center(ctr);
				eye[0] += sh[0]; eye[1] += sh[1];
				ctr[0] += sh[0] * 0.5f; ctr[1] += sh[1] * 0.5f;
				camera.SetFreeView(eye, ctr);
			}
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
		}
		// Harness FX probe (W5 gates): arm one effect on a fixed, scripted
		// tick so its geometry can be captured deterministically.
		if (!fxProbe.empty() && (long long)sim->tick() == fxProbeTick) {
			if (fxProbe == "goal")
				fx.OnGoal();
			else if (fxProbe == "demo")
				fx.OnDemo();
			else if (fxProbe == "pickup")
				fx.OnPickup(true);
		}

		renderer.BeginFrame();
		camera.Apply(1280, 720);
		// ---- Turf: mowed stripes + markings + wall-base glow lines --------
		// The debug ground grid is an instrument only (--debug-grid) and is drawn
		// BEFORE the turf so the turf occludes it, clipped to the arena extents
		// (x +/-4096, y +/-5120) rather than the old +/-9000.
		if (debugGrid)
			renderer.DrawGroundGrid(5120.0f, 512.0f);
		// G3c: arena draw buckets (GRAPHICS45 §5/G3 step 3). Replaces the
		// per-frame immediate arena calls below (turf B1 + markings B2 overlay +
		// glass/crown/round walls C1-C4 + trim/beams/cove/skirt/goal-trim A/D +
		// ceiling B4 + bases B3/B7 + skirts B5/B6 + opaque goal boxes A5/A6).
		// Order inside buckets (opaque -> B2 overlay -> glass) matches the old
		// state sandwiches (V5/V6); B2 keeps depth-test OFF and C5/C6 keep
		// depth-write OFF inside their own draws. Grid instrument above stays.
		//
		// A6b: with a ripped arena pack loaded, the PACK replaces the analytic
		// shell (its own floor/walls/ceiling) — drawing both would double the
		// geometry and z-fight the two floors. Without a pack this is the
		// untouched shipped path.
		if (renderer.HasArenaPack())
			renderer.DrawArenaPack();
		else
			renderer.DrawArenaBuckets(sim->usingRealSoccar(), noHexNet);
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
			if (!pads.empty())
				fx.DrawPads(renderer, pads.data(), int(pads.size()));
		}
		// Cars: Fennec GLB body when available (CC-BY-4.0), box fallback else.
		// Orientation now carries pitch/roll (carForward/carUp), so rolls and
		// flips are visible instead of being flattened to a 2D yaw.
		if (rl::FennecAvailable() && !noFennec) {
			rl::FennecDraw(renderer, snap.carPos, snap.carForward, snap.carUp, false);
			rl::FennecDraw(renderer, osnap.carPos, osnap.carForward, osnap.carUp, true);
		} else {
			rl::DrawCar(renderer, snap, true);
			rl::DrawCar(renderer, osnap, false);
		}
		rl::DrawBall(renderer, snap);
		// G3c: goal interiors via buckets (C5 shells + C6 weave, depth-write
		// OFF inside). Stays AFTER cars/ball: C5/C6 blend OVER cars today.
		// A6b: the ripped arena carries its own goal, so the analytic goal
		// interiors are skipped when a pack is loaded.
		if (!renderer.HasArenaPack())
			renderer.DrawArenaGoalInteriors(sim->usingRealSoccar());
		if (debugColliders) {
			// W6 instrument: x-ray collision proxies over the shaded scene.
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
			// Analytic arena shell bounds + goal mouths (1:1 numbers).
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
		fx.DrawBoostFlame(renderer, snap);
		fx.DrawDrift(renderer, snap);
		fx.DrawAir(renderer, snap);
		fx.DrawTrail(renderer, snap);
		if (menu.IsOpen()) {
			// RL-style SETTINGS panel: full-screen dim, centered card with a
			// title bar, section sub-bars, a cyan selection bar, right-
			// aligned values (binding pills), scroll-follow and a footer
			// with hints. Data comes straight from Menu::RenderText() so
			// the view can never diverge from settings.ini (schema v1).
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
			// Title bar (RL blue) + accent line.
			renderer.FillRect2D(px, py, pw, topH, 0.07f, 0.28f, 0.55f, 1.0f);
			renderer.FillRect2D(px, py + topH - 3, pw, 3, 0.15f, 0.75f, 0.95f, 1.0f);
			renderer.DrawText2D(px + 24, py + 10, "SETTINGS", 3, 1, 1, 1);
			renderer.DrawText2D(px + pw - 320, py + 14, "ROCKET LEAGUE-STYLE  |  schema v1", 1,
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
			// Scrollbar.
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
		// HUD state line (always on).
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
			// Fixed 10-degree segments across the FULL 260-degree sweep, with
			// the last one clipped to the filled fraction: the arc length is
			// then exactly linear in boost (the old code shrank both the
			// segment count and each segment's width by frac, making the arc
			// grow as frac^2 — VG5 measured 65 deg at boost=50, not 130).
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
			// Number + label, centered on the dial.
			char num[8];
			std::snprintf(num, sizeof(num), "%.0f", (double)snap.boost);
			int w = (int)std::strlen(num) * 24;
			renderer.DrawText2D((int)(cx - w / 2), (int)(cy - 16), num, 4, 1, 1, 1);
			renderer.DrawText2D((int)(cx - 24), (int)(cy + 14), "BOOST", 1, 0.85f, 0.9f, 1.0f);
		}
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
			// Clock resync: window/GL creation blocks the loop during
			// startup; exclude it from the physics accumulator so the
			// soak gate measures the running loop only (SOAK_PLAN.md).
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
	// A6b: the ripped arena's own per-frame budget (the [game] exit line above
	// stays byte-identical for the default path).
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

	// No exit-time settings save: the shipped config/settings.ini is the
	// DEFAULTS contract (ROUNDTRIP asserts it), so silent persistence of
	// live session state (a Space ballcam toggle, an unsaved menu tweak)
	// would corrupt it on every run. Persist deliberately via the menu's
	// "Save & close" action instead.
	// Release the arena textures while the GL context is still live. This is
	// the counterpart to the lazy Build*() uploads in ArenaTextures.cpp; it is
	// a real teardown step, not decoration (the Fennec VBO path does the same
	// in FennecShutdown()).
	rl::FreeArenaTextures();
	delete sim;
	return 0;
}
