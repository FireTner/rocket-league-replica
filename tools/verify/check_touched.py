#!/usr/bin/env python3
# Print which check tier applies to files changed in git. Never runs them.
import os
import subprocess


def changed_files():
    out = set()
    for args in (["git", "status", "--short"], ["git", "diff", "--name-only", "HEAD"]):
        try:
            r = subprocess.run(args, capture_output=True, text=True, timeout=15)
        except Exception:
            continue
        for line in r.stdout.splitlines():
            line = line.strip().strip('"')
            if not line:
                continue
            # `status --short` prefixes XY + space (" M foo", "?? foo");
            # `diff --name-only` is a bare path. Last token wins.
            parts = line.split()
            tok = parts[-1].strip('"') if len(parts) > 1 else line
            if tok and tok != "--short":
                out.add(tok)
    return sorted(out)


def classify(path):
    p = path.replace("\\", "/")
    sim = ("src/sim/", "src/arena/", "src/physics/", "tools/tas/tapes",
           "tools/headless_sim/", "third_party/RocketSim/")
    if p.startswith(sim) or p.endswith("Tuning.h"):
        return "hard"
    vis = ("src/render/", "src/camera/", "src/fx/", "src/models/",
           "src/audio/", "src/menu/", "src/input/", "src/settings/",
           "src/game/main.cpp", "config/", "assets/",
           # the arena-rip harness (tools/arena/**) is advisory: it is a
           # presentation asset pipeline, never a sim-path change.
           "tools/arena/")
    if p.startswith(vis):
        return "advisory"
    if p.startswith(("tools/verify/", "tools/tas/", "tools/soak/",
                     "tests/", "CMakeLists.txt", "tools/dev-local.sh")):
        return "harness"
    return "docs"


def main():
    files = changed_files()
    if not files:
        print("check-touched: tree clean — nothing to verify.")
        print("  full bar (end of work): cmake --build build --target check")
        return 0
    tiers = {}
    for f in files:
        tiers.setdefault(classify(f), []).append(f)
    print("check-touched: changed files -> tier (advisory; runs nothing)")
    for tier in ("hard", "advisory", "harness", "docs"):
        if tier in tiers:
            print(f"  [{tier}]")
            for f in tiers[tier]:
                print(f"    {f}")
    if "hard" in tiers:
        print("run: cmake --build build --target check")
    if "advisory" in tiers:
        print("run: cmake --build build --target game && "
              "python3 tools/verify/visual_gates.py --only VG<int> --diag "
              "(pick the touched VG*; full VG1-VG16 once at the end)")
    if any(f.replace("\\", "/").startswith("tools/arena/") for f in files):
        arena_gates = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "arena_gates.py")
        if os.path.isfile(arena_gates):
            print("run: python3 tools/verify/arena_gates.py "
                  "(AN1-AN7; SKIPs loudly when no pack is present)")
        else:
            print("DEFERRED: tools/verify/arena_gates.py (AN1-AN7) does not "
                  "exist yet — it ships with the deferred A7 phase; use the "
                  "pack verifier instead: "
                  "python3 tools/arena/rip_arena.py --verify <pack dir>")
    if "harness" in tiers and "hard" not in tiers and "advisory" not in tiers:
        print("run: the suite the harness file owns + `cmake -B build` "
              "(configure check, no build)")
    if set(tiers) == {"docs"}:
        print("run: nothing (docs-only).")
    print("end of work, any tier: cmake --build build --target check")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
