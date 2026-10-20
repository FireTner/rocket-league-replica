// A5-owned: Settings load/save/migrate implementation.
// Minimal self-contained INI reader/writer (no third-party code).
// MIT License.
#include "Settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace rl {
namespace {

// ---- string utils ----
std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::vector<std::string> Split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            out.push_back(Trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(Trim(cur));
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const std::string& x) { return x.empty(); }),
              out.end());
    return out;
}

// Canonical key-name aliasing: Shift->LeftShift etc. (documented in schema).
std::string CanonicalKeyName(std::string n) {
    n = Trim(n);
    std::string l = Lower(n);
    if (l == "shift") return "LeftShift";
    if (l == "ctrl" || l == "control") return "LeftCtrl";
    if (l == "esc") return "Escape";
    if (l == "del") return "Delete";
    if (l == "up" || l == "down" || l == "left" || l == "right") {
        n = (char)std::toupper((unsigned char)l[0]) + l.substr(1);
        return n;
    }
    if (l.size() == 1 && std::isalpha((unsigned char)l[0]))
        return std::string(1, (char)std::toupper((unsigned char)l[0]));
    return n;
}

// ---- value parsing ----
bool ParseBool(const std::string& raw, bool& out) {
    std::string l = Lower(Trim(raw));
    if (l == "true" || l == "1" || l == "yes" || l == "on") {
        out = true;
        return true;
    }
    if (l == "false" || l == "0" || l == "no" || l == "off") {
        out = false;
        return true;
    }
    return false;
}

bool ParseFloat(const std::string& raw, float& out) {
    try {
        size_t pos = 0;
        double v = std::stod(Trim(raw), &pos);
        if (pos != Trim(raw).size() || !std::isfinite(v)) return false;
        out = (float)v;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseInt(const std::string& raw, int& out) {
    try {
        size_t pos = 0;
        long v = std::stol(Trim(raw), &pos);
        if (pos != Trim(raw).size()) return false;
        out = (int)v;
        return true;
    } catch (...) {
        return false;
    }
}

// "A,B/C,D" -> AxisBinding{{A,B},{C,D}} ; "X" -> {{X},{}}.
AxisBinding ParseAxis(const std::string& raw) {
    AxisBinding b;
    auto halves = Split(raw, '/');
    auto conv = [](const std::string& h) {
        std::vector<std::string> v;
        for (auto& k : Split(h, ',')) v.push_back(CanonicalKeyName(k));
        return v;
    };
    if (!halves.empty()) b.slotA = conv(halves[0]);
    if (halves.size() > 1) b.slotB = conv(halves[1]);
    return b;
}

std::vector<std::string> ParseKeys(const std::string& raw) {
    std::vector<std::string> v;
    for (auto& k : Split(raw, ',')) v.push_back(CanonicalKeyName(k));
    return v;
}

std::string JoinKeys(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += v[i];
    }
    return s;
}

std::string AxisToString(const AxisBinding& b) {
    if (b.slotB.empty()) return JoinKeys(b.slotA);
    return JoinKeys(b.slotA) + "/" + JoinKeys(b.slotB);
}

// ---- INI model: ordered sections, ordered keys ----
struct IniFile {
    std::vector<std::string> sectionOrder;
    std::map<std::string, std::vector<std::string>> keyOrder;
    std::map<std::pair<std::string, std::string>, std::string> values;

    const std::string* get(const std::string& sec, const std::string& key) const {
        auto it = values.find({Lower(sec), Lower(key)});
        return it == values.end() ? nullptr : &it->second;
    }
    void set(const std::string& sec, const std::string& key, const std::string& val) {
        std::string ls = Lower(sec), lk = Lower(key);
        if (values.find({ls, lk}) == values.end()) {
            if (keyOrder.find(ls) == keyOrder.end()) sectionOrder.push_back(sec);
            keyOrder[ls].push_back(key);
        }
        values[{ls, lk}] = val;
    }
};

bool ParseIni(std::istream& in, IniFile& ini, std::string* err) {
    std::string line, section;
    int lineno = 0;
    for (; std::getline(in, line);) {
        ++lineno;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t[0] == '[') {
            if (t.back() != ']') {
                if (err) *err = "line " + std::to_string(lineno) + ": unterminated [section]";
                return false;
            }
            section = Trim(t.substr(1, t.size() - 2));
            if (section.empty()) {
                if (err) *err = "line " + std::to_string(lineno) + ": empty [section]";
                return false;
            }
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos || section.empty()) {
            if (err) *err = "line " + std::to_string(lineno) + ": expected key=value";
            return false;
        }
        std::string key = Trim(t.substr(0, eq));
        std::string val = Trim(t.substr(eq + 1));
        // Strip inline comments (" ;" or " #").
        {
            size_t c = val.find(" ;");
            size_t h = val.find(" #");
            size_t cut = std::string::npos;
            if (c != std::string::npos) cut = c;
            if (h != std::string::npos) cut = std::min(cut, h);
            if (cut != std::string::npos) val = Trim(val.substr(0, cut));
        }
        if (key.empty()) {
            if (err) *err = "line " + std::to_string(lineno) + ": empty key";
            return false;
        }
        ini.set(section, key, val);
    }
    return true;
}

float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

Settings Settings::Defaults() {
    Settings s;
    auto ax = [](std::initializer_list<const char*> a, std::initializer_list<const char*> b) {
        AxisBinding x;
        for (auto k : a) x.slotA.emplace_back(k);
        for (auto k : b) x.slotB.emplace_back(k);
        return x;
    };
    s.input.throttle = ax({"W", "Up"}, {"S", "Down"});
    s.input.steer = ax({"A", "Left"}, {"D", "Right"});
    s.input.pitch = ax({"W", "Up"}, {"S", "Down"});
    s.input.yaw = ax({"A", "Left"}, {"D", "Right"});
    s.input.roll = ax({"Q"}, {"E"});
    s.input.jump = {"L"};
    s.input.boost = {"K"};
    s.input.handbrake = {"LeftShift", "RightShift"};
    s.input.ballcamToggle = {"Space"};
    s.input.resetCar = {"R"};
    s.input.resetBall = {"C"};
    s.input.unlimited = {"U"};
    s.input.menu = {"Escape"};
    s.input.kickoff[0] = {"1"};
    s.input.kickoff[1] = {"2"};
    s.input.kickoff[2] = {"3"};
    s.input.kickoff[3] = {"4"};
    s.input.kickoff[4] = {"5"};
    const char* pdef[5] = {"DPadUp", "DPadDown", "DPadLeft", "DPadRight", ""};
    for (int i = 0; i < 5; ++i) s.gamepad.kickoff[i] = pdef[i];
    return s;
}

void Settings::Migrate(Settings& s, int fileSchema) {
    (void)fileSchema;  // v1 is the first version: clamp ranges, fill empties.
    const Settings d = Defaults();
    auto fixAxis = [&](AxisBinding& b, const AxisBinding& def) {
        if (b.slotA.empty() && b.slotB.empty()) b = def;
    };
    fixAxis(s.input.throttle, d.input.throttle);
    fixAxis(s.input.steer, d.input.steer);
    fixAxis(s.input.pitch, d.input.pitch);
    fixAxis(s.input.yaw, d.input.yaw);
    fixAxis(s.input.roll, d.input.roll);
    auto fixKeys = [&](std::vector<std::string>& v, const std::vector<std::string>& def) {
        if (v.empty()) v = def;
    };
    fixKeys(s.input.jump, d.input.jump);
    fixKeys(s.input.boost, d.input.boost);
    fixKeys(s.input.handbrake, d.input.handbrake);
    fixKeys(s.input.ballcamToggle, d.input.ballcamToggle);
    fixKeys(s.input.resetCar, d.input.resetCar);
    fixKeys(s.input.resetBall, d.input.resetBall);
    fixKeys(s.input.unlimited, d.input.unlimited);
    fixKeys(s.input.menu, d.input.menu);
    for (int i = 0; i < 5; ++i) fixKeys(s.input.kickoff[i], d.input.kickoff[i]);
    // Range clamps (see INI_SCHEMA.md for authoritative ranges).
    // FOV cap is 110 (RL slider max; Rocket Science #2 + Liquipedia 60..110),
    // distance/height/angle match the RL menu ranges (Camera.cpp enforces the
    // same, so the two owners never disagree).
    s.camera.fov = ClampF(s.camera.fov, 60.0f, 110.0f);
    s.camera.distance = ClampF(s.camera.distance, 100.0f, 400.0f);
    s.camera.height = ClampF(s.camera.height, 40.0f, 200.0f);
    s.camera.angle = ClampF(s.camera.angle, -30.0f, 15.0f);
    s.camera.stiffness = ClampF(s.camera.stiffness, 0.0f, 1.0f);
    s.camera.transitionSpeed = ClampF(s.camera.transitionSpeed, 1.0f, 2.0f);
    s.camera.swivelSpeed = ClampF(s.camera.swivelSpeed, 1.0f, 10.0f);
    s.physics.tick = ClampI(s.physics.tick, 30, 240);
    s.boost.drain = ClampF(s.boost.drain, 0.0f, 100.0f);
    s.boost.recharge = ClampF(s.boost.recharge, 0.0f, 100.0f);
    s.game.kickoffIndex = ClampI(s.game.kickoffIndex, 0, 4);
    // GRAPHICS45 G2/G5 [render]: msaa 0=off (shipped path), clamp 0..16;
    // max_fps 0=off (shipped pacing), else clamp 15..1000 (limiter only
    // engages with --no-vsync; vsync-on behavior unchanged).
    s.render.msaa = ClampI(s.render.msaa, 0, 16);
    s.render.maxFps = ClampI(s.render.maxFps, 0, 1000);
    if (s.render.maxFps > 0 && s.render.maxFps < 15) s.render.maxFps = 15;
    // Gamepad: deadzone range documented in INI_SCHEMA.md; empty button names
    // fall back to the default binding (an empty name is a misconfiguration,
    // not a valid unbind).
    s.gamepad.deadzone = ClampF(s.gamepad.deadzone, 0.0f, 0.5f);
    auto fixG = [&](std::string& v, const std::string& def) {
        if (v.empty()) v = def;
    };
    fixG(s.gamepad.jump, d.gamepad.jump);
    fixG(s.gamepad.boost, d.gamepad.boost);
    fixG(s.gamepad.handbrake, d.gamepad.handbrake);
    fixG(s.gamepad.ballcamToggle, d.gamepad.ballcamToggle);
    fixG(s.gamepad.resetCar, d.gamepad.resetCar);
    fixG(s.gamepad.resetBall, d.gamepad.resetBall);
    fixG(s.gamepad.unlimited, d.gamepad.unlimited);
    fixG(s.gamepad.menu, d.gamepad.menu);
    for (int i = 0; i < 5; ++i) fixG(s.gamepad.kickoff[i], d.gamepad.kickoff[i]);
}

bool Settings::Load(const std::string& path, Settings& out, std::string* err) {
    out = Defaults();
    std::ifstream in(path);
    if (!in) {
        if (err) *err = "cannot open " + path + "; using defaults";
        return false;
    }
    IniFile ini;
    if (!ParseIni(in, ini, err)) return false;

    int fileSchema = 1;
    if (const std::string* v = ini.get("version", "schema")) {
        if (!ParseInt(*v, fileSchema)) {
            if (err) *err = "[version] schema is not an integer: " + *v;
            return false;
        }
    }
    // Unknown future schema: keep defaults, report.
    if (fileSchema > kSchemaVersion) {
        if (err) *err = "schema " + std::to_string(fileSchema) + " newer than supported " +
                        std::to_string(kSchemaVersion) + "; using defaults";
        return false;
    }

    bool ok = true;
    std::string errors;
    auto need = [&](const std::string* v, const char* what) -> bool {
        if (!v) return false;  // missing -> keep default (not an error)
        (void)what;
        return true;
    };
    auto getF = [&](const char* sec, const char* key, float& dst) {
        const std::string* v = ini.get(sec, key);
        if (!need(v, key)) return;
        float f;
        if (!ParseFloat(*v, f)) {
            ok = false;
            errors += std::string("[") + sec + "] " + key + " invalid float: " + *v + "\n";
        } else {
            dst = f;
        }
    };
    auto getI = [&](const char* sec, const char* key, int& dst) {
        const std::string* v = ini.get(sec, key);
        if (!need(v, key)) return;
        int i;
        if (!ParseInt(*v, i)) {
            ok = false;
            errors += std::string("[") + sec + "] " + key + " invalid int: " + *v + "\n";
        } else {
            dst = i;
        }
    };
    auto getB = [&](const char* sec, const char* key, bool& dst) {
        const std::string* v = ini.get(sec, key);
        if (!need(v, key)) return;
        bool b;
        if (!ParseBool(*v, b)) {
            ok = false;
            errors += std::string("[") + sec + "] " + key + " invalid bool: " + *v + "\n";
        } else {
            dst = b;
        }
    };

    getF("camera", "fov", out.camera.fov);
    getF("camera", "distance", out.camera.distance);
    getF("camera", "height", out.camera.height);
    getF("camera", "angle", out.camera.angle);
    getF("camera", "stiffness", out.camera.stiffness);
    getB("camera", "ballcam", out.camera.ballcam);
    getF("camera", "transition_speed", out.camera.transitionSpeed);
    getF("camera", "swivel_speed", out.camera.swivelSpeed);
    getB("camera", "shake", out.camera.shake);
    getB("camera", "invert_swivel", out.camera.invertSwivel);
    getB("camera", "snap", out.camera.snap);

    auto getA = [&](const char* key, AxisBinding& dst) {
        const std::string* v = ini.get("input", key);
        if (v) dst = ParseAxis(*v);
    };
    auto getK = [&](const char* key, std::vector<std::string>& dst) {
        const std::string* v = ini.get("input", key);
        if (v) dst = ParseKeys(*v);
    };
    getA("throttle", out.input.throttle);
    getA("steer", out.input.steer);
    getA("pitch", out.input.pitch);
    getA("yaw", out.input.yaw);
    getA("roll", out.input.roll);
    getK("jump", out.input.jump);
    getK("boost", out.input.boost);
    getK("handbrake", out.input.handbrake);
    getK("ballcam_toggle", out.input.ballcamToggle);
    getK("reset_car", out.input.resetCar);
    getK("reset_ball", out.input.resetBall);
    getK("unlimited", out.input.unlimited);
    getK("menu", out.input.menu);
    const char* knames[5] = {"kickoff_0", "kickoff_1", "kickoff_2", "kickoff_3", "kickoff_4"};
    for (int i = 0; i < 5; ++i) getK(knames[i], out.input.kickoff[i]);

    // Gamepad bindings + deadzone. Values are SDL_GameControllerButton names,
    // canonicalised by src/input/Gamepad.cpp::ButtonForName.
    auto getG = [&](const char* key, std::string& dst) {
        const std::string* v = ini.get("input", key);
        if (v) dst = *v;
    };
    getF("input", "deadzone", out.gamepad.deadzone);
    getG("pad_jump", out.gamepad.jump);
    getG("pad_boost", out.gamepad.boost);
    getG("pad_handbrake", out.gamepad.handbrake);
    getG("pad_ballcam_toggle", out.gamepad.ballcamToggle);
    getG("pad_reset_car", out.gamepad.resetCar);
    getG("pad_reset_ball", out.gamepad.resetBall);
    getG("pad_unlimited", out.gamepad.unlimited);
    getG("pad_menu", out.gamepad.menu);
    const char* pnames[5] = {"pad_kickoff_0", "pad_kickoff_1", "pad_kickoff_2",
                             "pad_kickoff_3", "pad_kickoff_4"};
    for (int i = 0; i < 5; ++i) getG(pnames[i], out.gamepad.kickoff[i]);

    getI("physics", "tick", out.physics.tick);
    getB("boost", "unlimited", out.boost.unlimited);
    getF("boost", "drain", out.boost.drain);
    getF("boost", "recharge", out.boost.recharge);
    getB("arena", "soccar", out.arena.soccar);
    {
        const std::string* v = ini.get("arena", "mesh_dir");
        if (v) out.arena.meshDir = *v;  // free text; empty = built-in default
    }
    getI("game", "kickoff_index", out.game.kickoffIndex);
    getI("render", "msaa", out.render.msaa);
    getI("render", "max_fps", out.render.maxFps);
    getB("render", "pacing_log", out.render.pacingLog);
    getB("render", "pbr", out.render.pbr);  // G6a PBR-lite, default OFF

    if (!ok) {
        if (err) *err = errors;
        return false;
    }
    Migrate(out, fileSchema);
    return true;
}

bool Settings::Save(const std::string& path, const Settings& s, std::string* err) {
    std::ofstream out(path);
    if (!out) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    char buf[64];
    auto f = [&](float v) {
        std::snprintf(buf, sizeof(buf), "%.4g", (double)v);
        return std::string(buf);
    };
    out << "; RocketLeague free-play clone — user settings (A5-owned contract, schema v1).\n"
        << "; See docs/INI_SCHEMA.md. Written by Settings::Save.\n"
        << "\n[version]\nschema = " << kSchemaVersion << "\n"
        << "\n[camera]\n"
        << "fov = " << f(s.camera.fov) << "\n"
        << "distance = " << f(s.camera.distance) << "\n"
        << "height = " << f(s.camera.height) << "\n"
        << "angle = " << f(s.camera.angle) << "\n"
        << "stiffness = " << f(s.camera.stiffness) << "\n"
        << "ballcam = " << (s.camera.ballcam ? "true" : "false") << "\n"
        << "transition_speed = " << f(s.camera.transitionSpeed) << "\n"
        << "swivel_speed = " << f(s.camera.swivelSpeed) << "\n"
        << "shake = " << (s.camera.shake ? "true" : "false") << "\n"
        << "invert_swivel = " << (s.camera.invertSwivel ? "true" : "false") << "\n"
        << "snap = " << (s.camera.snap ? "true" : "false") << "\n"
        << "\n[input]\n"
        << "throttle = " << AxisToString(s.input.throttle) << "\n"
        << "steer = " << AxisToString(s.input.steer) << "\n"
        << "pitch = " << AxisToString(s.input.pitch) << "\n"
        << "yaw = " << AxisToString(s.input.yaw) << "\n"
        << "roll = " << AxisToString(s.input.roll) << "\n"
        << "jump = " << JoinKeys(s.input.jump) << "\n"
        << "boost = " << JoinKeys(s.input.boost) << "\n"
        << "handbrake = " << JoinKeys(s.input.handbrake) << "\n"
        << "ballcam_toggle = " << JoinKeys(s.input.ballcamToggle) << "\n"
        << "reset_car = " << JoinKeys(s.input.resetCar) << "\n"
        << "reset_ball = " << JoinKeys(s.input.resetBall) << "\n"
        << "unlimited = " << JoinKeys(s.input.unlimited) << "\n"
        << "menu = " << JoinKeys(s.input.menu) << "\n";
    const char* knames[5] = {"kickoff_0", "kickoff_1", "kickoff_2", "kickoff_3", "kickoff_4"};
    for (int i = 0; i < 5; ++i)
        out << knames[i] << " = " << JoinKeys(s.input.kickoff[i]) << "\n";
    // Gamepad (separate table; left stick is fixed and not rebindable here).
    out << "deadzone = " << f(s.gamepad.deadzone) << "\n"
        << "pad_jump = " << s.gamepad.jump << "\n"
        << "pad_boost = " << s.gamepad.boost << "\n"
        << "pad_handbrake = " << s.gamepad.handbrake << "\n"
        << "pad_ballcam_toggle = " << s.gamepad.ballcamToggle << "\n"
        << "pad_reset_car = " << s.gamepad.resetCar << "\n"
        << "pad_reset_ball = " << s.gamepad.resetBall << "\n"
        << "pad_unlimited = " << s.gamepad.unlimited << "\n"
        << "pad_menu = " << s.gamepad.menu << "\n";
    const char* pnames[5] = {"pad_kickoff_0", "pad_kickoff_1", "pad_kickoff_2",
                             "pad_kickoff_3", "pad_kickoff_4"};
    for (int i = 0; i < 5; ++i)
        out << pnames[i] << " = " << s.gamepad.kickoff[i] << "\n";
    out << "\n[physics]\ntick = " << s.physics.tick << "\n"
        << "\n[boost]\n"
        << "unlimited = " << (s.boost.unlimited ? "true" : "false") << "\n"
        << "drain = " << f(s.boost.drain) << "\n"
        << "recharge = " << f(s.boost.recharge) << "\n"
        << "\n[arena]\nsoccar = " << (s.arena.soccar ? "true" : "false") << "\n"
        << "mesh_dir = " << s.arena.meshDir << "\n"
        << "\n[game]\nkickoff_index = " << s.game.kickoffIndex << "\n"
        << "\n[render]\nmsaa = " << s.render.msaa << "\n"
        << "max_fps = " << s.render.maxFps << "\n"
        << "pacing_log = " << (s.render.pacingLog ? "true" : "false") << "\n"
        << "pbr = " << (s.render.pbr ? "true" : "false") << "\n";
    if (!out) {
        if (err) *err = "write failed for " + path;
        return false;
    }
    return true;
}

}  // namespace rl
