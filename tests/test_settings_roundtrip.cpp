// A5 contract test: Settings parse -> modify -> save -> reparse round-trip.
// Build: g++ -std=c++20 -I src tests/test_settings_roundtrip.cpp
//        src/settings/Settings.cpp -o /tmp/t_rt && /tmp/t_rt
// No SDL needed. Exit 0 = all checks pass.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "settings/Settings.h"

static int failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (cond) {                                                   \
            std::printf("  ok   %s\n", msg);                          \
        } else {                                                      \
            std::printf("  FAIL %s\n", msg);                          \
            ++failures;                                               \
        }                                                             \
    } while (0)

int main() {
    std::printf("[1] load shipped config/settings.ini\n");
    rl::Settings s;
    std::string err;
    bool loaded = rl::Settings::Load("config/settings.ini", s, &err);
    CHECK(loaded, ("load: " + (loaded ? std::string("ok") : err)).c_str());
    CHECK(s.camera.fov == 110.0f, "camera.fov == 110");
    CHECK(s.camera.ballcam == true, "camera.ballcam == true");
    CHECK(s.camera.transitionSpeed == 1.2f, "camera.transition_speed == 1.2");
    CHECK(s.camera.swivelSpeed == 5.0f, "camera.swivel_speed == 5.0");
    CHECK(s.camera.shake == true, "camera.shake == true");
    CHECK(s.camera.invertSwivel == false, "camera.invert_swivel == false");
    CHECK(s.camera.snap == true, "camera.snap == true");
    CHECK(s.input.throttle.slotA.size() == 2 && s.input.throttle.slotA[0] == "W" &&
              s.input.throttle.slotA[1] == "Up",
          "throttle slotA == {W,Up}");
    CHECK(s.input.throttle.slotB.size() == 2 && s.input.throttle.slotB[0] == "S",
          "throttle slotB starts with S");
    CHECK(s.input.boost.size() == 1 && s.input.boost[0] == "K",
          "boost == {K}");
    CHECK(s.input.handbrake.size() == 2, "handbrake has 2 keys");
    CHECK(s.input.kickoff[4].size() == 1 && s.input.kickoff[4][0] == "5",
          "kickoff_4 == {5}");
    CHECK(s.physics.tick == 120, "physics.tick == 120");
    CHECK(s.boost.unlimited == false, "boost.unlimited == false");
    CHECK(s.arena.soccar == true, "arena.soccar == true");
    CHECK(s.game.kickoffIndex == 0, "game.kickoff_index == 0");

    std::printf("[2] modify every section, save to /tmp/rt_out.ini\n");
    s.camera.fov = 100.0f;
    s.camera.ballcam = false;
    s.camera.transitionSpeed = 1.5f;
    s.camera.swivelSpeed = 7.0f;
    s.camera.shake = false;
    s.camera.invertSwivel = true;
    s.camera.snap = false;
    s.input.throttle.slotA = {"T"};
    s.input.jump = {"J", "Space"};
    s.physics.tick = 60;
    s.boost.unlimited = true;
    s.boost.drain = 50.0f;
    s.arena.soccar = false;
    s.game.kickoffIndex = 3;
    bool saved = rl::Settings::Save("/tmp/rt_out.ini", s, &err);
    CHECK(saved, ("save: " + (saved ? std::string("ok") : err)).c_str());

    std::printf("[3] reparse /tmp/rt_out.ini, verify mutations survived\n");
    rl::Settings r;
    bool reloaded = rl::Settings::Load("/tmp/rt_out.ini", r, &err);
    CHECK(reloaded, ("reparse: " + (reloaded ? std::string("ok") : err)).c_str());
    CHECK(r.camera.fov == 100.0f, "fov mutation survived (100)");
    CHECK(r.camera.ballcam == false, "ballcam mutation survived (false)");
    CHECK(r.camera.transitionSpeed == 1.5f, "transition_speed mutation survived (1.5)");
    CHECK(r.camera.swivelSpeed == 7.0f, "swivel_speed mutation survived (7)");
    CHECK(r.camera.shake == false, "shake mutation survived (false)");
    CHECK(r.camera.invertSwivel == true, "invert_swivel mutation survived (true)");
    CHECK(r.camera.snap == false, "snap mutation survived (false)");
    CHECK(r.camera.distance == 270.0f, "untouched camera.distance intact (270)");
    CHECK(r.input.throttle.slotA.size() == 1 && r.input.throttle.slotA[0] == "T",
          "throttle.slotA mutation survived ({T})");
    CHECK(r.input.throttle.slotB.size() == 2, "throttle.slotB untouched");
    CHECK(r.input.jump.size() == 2 && r.input.jump[0] == "J", "jump alternates survived");
    CHECK(r.physics.tick == 60, "tick mutation survived (60)");
    CHECK(r.boost.unlimited == true, "unlimited mutation survived (true)");
    CHECK(r.boost.drain == 50.0f, "drain mutation survived (50)");
    CHECK(r.boost.recharge == 10.0f, "untouched recharge intact (10)");
    CHECK(r.arena.soccar == false, "soccar mutation survived (false)");
    CHECK(r.game.kickoffIndex == 3, "kickoff_index mutation survived (3)");

    std::printf("[4] tolerance checks\n");
    rl::Settings d = rl::Settings::Defaults();
    CHECK(d.camera.fov == 110.0f && d.physics.tick == 120, "Defaults() matches shipped ini");
    rl::Settings missing;
    bool missOk = rl::Settings::Load("/tmp/does_not_exist_xyz.ini", missing, &err);
    CHECK(!missOk && missing.camera.fov == 110.0f, "missing file -> defaults + false");

    // A6b: the [arena] section of the SHIPPED file. `mesh_dir` and the new
    // `arena_dir` are read by the game (the A5 Settings struct does not carry
    // `arena_dir` yet — see docs/INI_SCHEMA.md), so the roundtrip contract
    // for them is the file itself: both keys must be present, in [arena],
    // with the shipped default of "off" (empty).
    std::printf("[5] the shipped [arena] section (mesh_dir + arena_dir)\n");
    {
        std::ifstream f("config/settings.ini");
        std::string line, arena;
        bool in = false;
        while (std::getline(f, line)) {
            if (!line.empty() && line[0] == '[') {
                in = (line.compare(0, 7, "[arena]") == 0);
                continue;
            }
            if (in) arena += line + "\n";
        }
        CHECK(arena.find("mesh_dir") != std::string::npos,
              "[arena] carries mesh_dir");
        CHECK(arena.find("arena_dir") != std::string::npos,
              "[arena] carries arena_dir");
        // Both must be OFF by default: an empty value = the analytic shell /
        // RocketSim's default collision-mesh path.
        bool meshEmpty = false, arenaEmpty = false;
        std::istringstream is(arena);
        while (std::getline(is, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), val = line.substr(eq + 1);
            while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
                val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' ||
                                    val.back() == '\r'))
                val.pop_back();
            if (k == "mesh_dir") meshEmpty = val.empty();
            if (k == "arena_dir") arenaEmpty = val.empty();
        }
        CHECK(meshEmpty, "mesh_dir ships empty (off)");
        CHECK(arenaEmpty, "arena_dir ships empty (off)");
        // The A5 loader stays tolerant of the key it does not know: the
        // shipped file loads (step 1) and the SAVED file still loads. The A5
        // writer DROPS the unknown key — that is the documented A6b debt
        // (fold `arena_dir` into ArenaSettings; docs/INI_SCHEMA.md).
        std::ifstream g("/tmp/rt_out.ini");
        std::string gl, gout;
        bool gin = false;
        while (std::getline(g, gl)) {
            if (!gl.empty() && gl[0] == '[') {
                gin = (gl.compare(0, 7, "[arena]") == 0);
                continue;
            }
            if (gin) gout += gl + "\n";
        }
        CHECK(gout.find("arena_dir") == std::string::npos,
              "Settings::Save drops the unknown arena_dir key (A6b debt)");
        rl::Settings rr;
        CHECK(rl::Settings::Load("/tmp/rt_out.ini", rr, &err),
              "the saved file still loads (unknown key ignored)");
    }

    std::printf(failures == 0 ? "ROUNDTRIP PASS\n" : "ROUNDTRIP FAIL\n");
    return failures == 0 ? 0 : 1;
}
