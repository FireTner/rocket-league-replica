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
    CHECK(s.arena.arenaDir.empty(), "shipped arena_dir is empty");
    CHECK(s.game.kickoffIndex == 0, "game.kickoff_index == 0");
    CHECK(s.render.wallOpacity == 0.40f, "render.wall_opacity == 0.40");

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
    s.arena.arenaDir = "/tmp/rl_arena_roundtrip";
    s.game.kickoffIndex = 3;
    s.render.wallOpacity = 0.65f;
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
    CHECK(r.arena.arenaDir == "/tmp/rl_arena_roundtrip",
          "arena_dir mutation survived");
    CHECK(r.game.kickoffIndex == 3, "kickoff_index mutation survived (3)");
    CHECK(r.render.wallOpacity == 0.65f, "wall_opacity mutation survived (0.65)");

    std::printf("[4] tolerance checks\n");
    rl::Settings d = rl::Settings::Defaults();
    CHECK(d.camera.fov == 110.0f && d.physics.tick == 120, "Defaults() matches shipped ini");
    CHECK(d.render.wallOpacity == 0.40f, "Defaults() wall_opacity is 0.40");
    CHECK(d.arena.arenaDir.empty(), "Defaults() arena_dir is empty");
    {
        std::ofstream bare("/tmp/rt_no_wall.ini");
        bare << "[version]\nschema = 1\n[camera]\nfov = 110\n";
        bare.close();
        rl::Settings bareS;
        CHECK(rl::Settings::Load("/tmp/rt_no_wall.ini", bareS, &err),
              "ini without wall_opacity loads");
        CHECK(bareS.render.wallOpacity == 0.40f,
              "missing wall_opacity keeps the 0.40 glass default");
    }
    rl::Settings missing;
    bool missOk = rl::Settings::Load("/tmp/does_not_exist_xyz.ini", missing, &err);
    CHECK(!missOk && missing.camera.fov == 110.0f, "missing file -> defaults + false");

    // Shipped [arena] stays off (empty). A non-empty arena_dir must survive
    // Save, and a default save must still write the key with an empty value.
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
        CHECK(gout.find("arena_dir = /tmp/rl_arena_roundtrip") != std::string::npos,
              "Settings::Save writes the non-empty arena_dir");
        rl::Settings rr;
        CHECK(rl::Settings::Load("/tmp/rt_out.ini", rr, &err),
              "the saved file still loads");
        CHECK(rr.arena.arenaDir == "/tmp/rl_arena_roundtrip",
              "reloaded save keeps the non-empty arena_dir");
        bool emptySaved = rl::Settings::Save("/tmp/rt_empty_arena.ini", d, &err);
        CHECK(emptySaved, "save of Defaults() writes arena_dir");
        rl::Settings emptyLoaded;
        CHECK(rl::Settings::Load("/tmp/rt_empty_arena.ini", emptyLoaded, &err),
              "empty arena_dir save reloads");
        CHECK(emptyLoaded.arena.arenaDir.empty(),
              "saved default arena_dir stays empty");
        std::ifstream eg("/tmp/rt_empty_arena.ini");
        std::string el, eout;
        bool ein = false;
        while (std::getline(eg, el)) {
            if (!el.empty() && el[0] == '[') {
                ein = (el.compare(0, 7, "[arena]") == 0);
                continue;
            }
            if (ein) eout += el + "\n";
        }
        CHECK(eout.find("arena_dir =") != std::string::npos,
              "default save still emits the arena_dir key");
    }

    std::printf(failures == 0 ? "ROUNDTRIP PASS\n" : "ROUNDTRIP FAIL\n");
    return failures == 0 ? 0 : 1;
}
