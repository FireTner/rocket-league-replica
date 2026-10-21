// A6b contract test: the `[arena] arena_dir` INI reader + the shipped
// config/settings.ini keys the --arenadir feature depends on.
//
// Build: g++ -std=c++20 -I src tests/test_arena_dir.cpp -o /tmp/t_adir
// Headless: no GL context is created (the reader is plain C++).  Exit 0 = all
// checks pass.
//
// Why a separate file instead of the settings roundtrip: the reader lives in
// src/render/Renderer.h (A6b's owned file) because src/settings/** is out of
// A6b's scope — the A5 `Settings` struct does not carry `arena_dir`, so the
// game reads that one key itself.  The roundtrip test still covers the
// shipped file's [arena] section (see tests/test_settings_roundtrip.cpp).
#define SDL_MAIN_HANDLED 1  // keep SDL from renaming main()
#include <cstdio>
#include <fstream>
#include <string>

#include "render/Renderer.h"

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

// The [arena] section of the shipped INI, read verbatim (the A5 Settings
// loader ignores keys it does not know, so the raw file is the contract).
static std::string ArenaSection(const std::string& path) {
    std::ifstream f(path);
    std::string line, out;
    bool in = false;
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] == '[') {
            in = (line.find("[arena]") == 0);
            continue;
        }
        if (in) out += line + "\n";
    }
    return out;
}

int main() {
    std::printf("[1] the shipped [arena] section carries both keys\n");
    const std::string sec = ArenaSection("config/settings.ini");
    CHECK(sec.find("arena_dir") != std::string::npos,
          "[arena] has an arena_dir key");
    CHECK(sec.find("mesh_dir") != std::string::npos,
          "[arena] has a mesh_dir key");

    std::printf("[2] the reader finds them in config/settings.ini\n");
    std::string v;
    CHECK(rl::ReadIniKey("config/settings.ini", "arena", "arena_dir", &v),
          "arena_dir is present");
    CHECK(v.empty(), "arena_dir defaults to empty (the analytic shell)");
    CHECK(rl::ReadIniKey("config/settings.ini", "arena", "mesh_dir", &v),
          "mesh_dir is present");
    CHECK(v.empty(), "mesh_dir defaults to empty");

    std::printf("[3] case-insensitive section + key\n");
    CHECK(rl::ReadIniKey("config/settings.ini", "ARENA", "Arena_Dir", &v),
          "ARENA/Arena_Dir resolves");
    CHECK(rl::ReadIniKey("config/settings.ini", "Arena", "arena_dir", &v),
          "mixed-case Arena resolves");

    std::printf("[4] missing file / section / key all fail loudly\n");
    CHECK(!rl::ReadIniKey("/tmp/does_not_exist_xyz.ini", "arena", "arena_dir",
                           &v),
          "missing file -> false");
    CHECK(!rl::ReadIniKey("config/settings.ini", "nosuchsection", "arena_dir",
                           &v),
          "missing section -> false");
    CHECK(!rl::ReadIniKey("config/settings.ini", "arena", "nosuchkey", &v),
          "missing key -> false");

    std::printf("[5] a value file: comments, trim, first match wins\n");
    {
        const char* p = "/tmp/t_adir_sample.ini";
        FILE* f = std::fopen(p, "w");
        std::fprintf(f, "; a comment\n");
        std::fprintf(f, "[other]\n");
        std::fprintf(f, "arena_dir = /wrong/section\n");
        std::fprintf(f, "[arena]\n");
        std::fprintf(f, "soccar = true\n");
        std::fprintf(f, "arena_dir =   /tmp/pack   ; trailing comment\n");
        std::fprintf(f, "arena_dir = /second/one\n");
        std::fclose(f);
        CHECK(rl::ReadIniKey(p, "arena", "arena_dir", &v),
              "value file: arena_dir found");
        CHECK(v == "/tmp/pack",
              "value file: trimmed, comment stripped, first match wins");
        CHECK(rl::ReadIniKey(p, "other", "arena_dir", &v) &&
                  v == "/wrong/section",
              "value file: the other section is read separately");
    }

    std::printf(failures == 0 ? "ARENA_DIR PASS\n" : "ARENA_DIR FAIL\n");
    return failures == 0 ? 0 : 1;
}
