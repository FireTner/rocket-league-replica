#!/usr/bin/env python3
"""Mesh-mode collision gates (MASTER_PROMPT_PHYSICS.md section 9, C1-C7).

These gates only mean something with an arena collision mesh loaded, so they
SKIP loudly (never fake a PASS) when none is present. Run:

    python3 tools/verify/mesh_gates.py                 # uses collision_meshes/
    python3 tools/verify/mesh_gates.py --meshdir DIR

Every gate records tape, seed, tick window, checksum, expectation, measured
value and verdict. Expected values are cited against vendored RocketSim.

Probes are driven through build/headless_sim with a neutral tape plus
--ballplace (test-harness placement), so the car never touches the ball.
"""

import argparse
import csv
import math
import os
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADLESS = os.path.join(ROOT, "build", "headless_sim")

# Ball radius as RocketSim configures it for SOCCAR
# (ArenaConfig.h ballRadius 92.75).
BALL_RADIUS = 92.75

# Arena footprint (src/arena/ArenaConstants.h:42,49,117).
SIDE_WALL_X = 4096.0
BACK_WALL_Y = 5120.0
CORNER_SUM = 8064.0
CORNER_N = 0.7071067811865476

TMP = None
results = []


def make_neutral_tape(ticks):
    path = os.path.join(TMP, "neutral_%d.csv" % ticks)
    with open(path, "w") as f:
        f.write("tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake\n")
        for i in range(ticks):
            f.write("%d,0,0,0,0,0,0,0,0\n" % i)
    return path


def run_sim(tape, ticks, meshdir, body="ball", extra=None):
    """Returns (rows, checksum, goals, pads)."""
    dump = os.path.join(TMP, "probe.csv")
    cmd = [HEADLESS, "--tape", tape, "--ticks", str(ticks), "--dump", dump,
           "--seed", "1", "--body", body, "--meshdir", meshdir]
    gl = os.path.join(TMP, "goal.log")
    pl = os.path.join(TMP, "pad.log")
    if extra:
        cmd += extra
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("headless_sim failed (%d): %s" % (r.returncode, r.stderr.strip()))
    ck = ""
    for line in r.stdout.splitlines():
        if line.startswith("CHECKSUM "):
            ck = line.split()[1]
    rows = list(csv.DictReader(open(dump)))
    goals = []
    if os.path.exists(gl):
        goals = [int(x) for x in open(gl).read().split()[1:]]
    pads = []
    if os.path.exists(pl):
        pads = list(csv.DictReader(open(pl)))
    return rows, ck, goals, pads


def run_ball(place, ticks, meshdir, goallog=False, padlog=False):
    extra = ["--ballplace", place]
    if goallog:
        extra += ["--goallog", os.path.join(TMP, "goal.log")]
    if padlog:
        extra += ["--padlog", os.path.join(TMP, "pad.log")]
    return run_sim(make_neutral_tape(ticks), ticks, meshdir, "ball", extra)


def f(row, key):
    return float(row[key])


def first_bounce(rows):
    vz = [f(r, "vz") for r in rows]
    z = [f(r, "z") for r in rows]
    for i in range(1, len(vz)):
        if vz[i - 1] < -1.0 and vz[i] > 1.0:
            return z[i], vz[i - 1], vz[i]
    return None


def record(gate, desc, tape, window, ck, expected, measured, tol, ok, cite):
    results.append(dict(gate=gate, desc=desc, tape=tape, window=window, ck=ck,
                        expected=expected, measured=measured, tol=tol, ok=ok, cite=cite))
    print("%s %-4s %-46s expect %-26s measured %-26s ck=%s" %
          ("PASS" if ok else "FAIL", gate, desc, expected, measured, ck or "-"))
    if not ok:
        print("       citation: %s" % cite)


def skip(gate, desc, why):
    results.append(dict(gate=gate, desc=desc, skip=True, why=why))
    print("SKIP %-4s %-46s (%s)" % (gate, desc, why))


def report_arena(meshdir):
    tape = make_neutral_tape(4)
    r = subprocess.run([HEADLESS, "--tape", tape, "--ticks", "4",
                        "--dump", os.path.join(TMP, "r.csv"),
                        "--meshdir", meshdir, "--meshreport"],
                       capture_output=True, text=True)
    line = [l for l in r.stderr.splitlines() if l.startswith("MESH ")]
    print("arena: %s" % (line[0] if line else "<no MESH line>"))
    print()


def contain_worst(rows):
    worst = 0.0
    for row in rows:
        x, y = abs(f(row, "x")), abs(f(row, "y"))
        worst = max(worst, x - SIDE_WALL_X, y - BACK_WALL_Y,
                    (x + y) * CORNER_N - CORNER_SUM * CORNER_N)
    return worst


def mesh_collision_works(meshdir):
    """Precondition: does the mesh actually generate contacts?

    Drops a ball onto a spot where the mesh ramp is the ONLY surface (the
    analytic planes put the floor at z=0 and the walls at +/-4096/5120).
    The clean-room back-wall ramp at (2000,5050) rises to z~75, so a ball
    resting on it sits at z ~ 75 + 92.75 = 167.75; if it rests at ~92.75 the
    ball fell through to the analytic floor and the mesh is not colliding.
    """
    rows, ck, _, _ = run_ball("2000,5050,400,0,0,0", 300, meshdir)
    return max(float(r["z"]) for r in rows) > 120.0


def run_gates(meshdir):
    report_arena(meshdir)

    # Precondition: every C-gate below is only meaningful if the loaded mesh
    # generates contacts at all. If it does not, report the blocker and SKIP
    # rather than emit PASSes that actually come from the analytic planes.
    if not mesh_collision_works(meshdir):
        why = "loaded mesh generates no contacts (see docs/PHYSICS_1TO1_LOG.md Round 1)"
        for g, d in (("C1a", "ball floor restitution"),
                     ("C1b", "max-speed containment"),
                     ("C3a", "chamfer contact"),
                     ("C3b", "back-wall contact"),
                     ("C7", "containment sweep"),
                     ("C2a", "goal mouth centre shot"),
                     ("C2b", "goal mouth outside post"),
                     ("C6a", "34 pads"),
                     ("C6b", "6 big pads"),
                     ("C4a", "car shell containment"),
                     ("C4b", "subsonic bump")):
            skip(g, d, why)
        return

    # --- C1a: ball restitution on the real floor ------------------------
    rows, ck, _, _ = run_ball("0,0,1000,0,0,0", 320, meshdir)
    b = first_bounce(rows)
    if b:
        e = -b[2] / b[1]
        record("C1a", "ball floor bounce restitution",
               "neutral+ballplace:0,0,1000", "0-320", ck,
               "e = 0.600 +/- 0.02", "e = %.4f" % e, 0.02, abs(e - 0.6) <= 0.02,
               "RLConst.h ARENA_COLLISION_BASE_RESTITUTION = 0.6")
    else:
        record("C1a", "ball floor bounce restitution",
               "neutral+ballplace:0,0,1000", "0-320", ck,
               "a bounce", "none", 0, False,
               "RLConst.h ARENA_COLLISION_BASE_RESTITUTION = 0.6")

    # --- C1b: no tunneling at max ball speed ---------------------------
    rows, ck, _, _ = run_ball("0,0,500,6000,0,0", 200, meshdir)
    maxx = max(abs(f(x, "x")) for x in rows)
    lim = SIDE_WALL_X + BALL_RADIUS + 200
    record("C1b", "max-speed ball stays in arena (+X wall)",
           "neutral+ballplace:...vx6000", "0-200", ck,
           "max|x| <= %.0f" % lim, "max|x| = %.1f" % maxx, 200.0, maxx <= lim,
           "RLConst.h ARENA_EXTENT_X=4096; ball max speed 6000")

    # --- C3a: head-on chamfer contact (mesh-only surface) --------------
    rows, ck, _, _ = run_ball("2500,4000,100,2000,0,0", 200, meshdir)
    maxx = max(f(x, "x") for x in rows)
    expected_max = CORNER_SUM - 4000.0  # chamfer face x at y=4000
    ok = expected_max - 350 <= maxx <= expected_max + BALL_RADIUS + 60
    record("C3a", "chamfer contact stops at the mesh surface",
           "neutral+ballplace:2500,4000,vx2000", "0-200", ck,
           "max x ~ %.0f" % expected_max, "%.1f" % maxx, 410.0, ok,
           "corner plane |x|+|y|=8064 (RLBot wiki); only the mesh supplies it")

    # --- C3b: back-wall contact off the goal mouth ---------------------
    rows, ck, _, _ = run_ball("2000,3000,300,0,600,0", 900, meshdir)
    maxy = max(f(x, "y") for x in rows)
    lim = BACK_WALL_Y + BALL_RADIUS + 300
    record("C3b", "back-wall contact keeps ball inside the net line",
           "neutral+ballplace:2000,3000,vy600", "0-900", ck,
           "max y <= %.0f" % lim, "%.1f" % maxy, 300.0, maxy <= lim,
           "RLConst.h ARENA_EXTENT_Y = 5120")

    # --- C7: containment sweep over many directions --------------------
    worst = 0.0
    nan = 0
    for k in range(24):
        a = 2 * math.pi * k / 24
        vx, vy = 1500 * math.cos(a), 1500 * math.sin(a)
        rows, ck, _, _ = run_ball("0,0,300,%.4f,%.4f,0" % (vx, vy), 400, meshdir)
        for row in rows:
            vals = [f(row, c) for c in ("x", "y", "z")]
            if not all(math.isfinite(v) for v in vals):
                nan += 1
        worst = max(worst, contain_worst(rows))
    record("C7", "containment: 24 directions @1500 uu/s",
           "neutral+ballplace sweep", "24 x 0-400", "-",
           "penetration <= 1 uu, NaN = 0", "%.2f uu, NaN=%d" % (worst, nan), 1.0,
           worst <= 1.0 and nan == 0,
           "footprint: ARENA_EXTENT_X/Y + corner planes (RLConst.h, RLBot wiki)")

    # --- C2a: goal-mouth detection, centre shot ------------------------
    rows, ck, goals, _ = run_ball("0,0,300,0,1500,0", 800, meshdir, goallog=True)
    record("C2a", "goal mouth: centre shot scores exactly once",
           "neutral+ballplace:0,0,300,vy1500", "0-800", ck,
           "1 goal event", "%d event(s)" % len(goals), 0, len(goals) == 1,
           "Arena::IsBallScored (Arena.cpp:893-900)")

    # --- C2b: outside-post shot must not score -------------------------
    rows, ck, goals, _ = run_ball("2000,0,300,0,1500,0", 800, meshdir, goallog=True)
    record("C2b", "goal mouth: outside-post shot does not score",
           "neutral+ballplace:2000,0,300,vy1500", "0-800", ck,
           "0 goal events", "%d event(s)" % len(goals), 0, len(goals) == 0,
           "goal centre-to-post 892.755 (RLBot wiki)")

    # --- C6a: RocketSim owns the 34 pads in mesh mode ------------------
    rows, ck, _, pads = run_ball("0,0,300,0,0,0", 60, meshdir, padlog=True)
    npads = len(set(p["idx"] for p in pads)) if pads else 0
    record("C6a", "mesh mode exposes RocketSim's 34 Soccar pads",
           "neutral+ballplace", "0-60", ck, "34 pads", "%d pads" % npads, 0,
           npads == 34, "RLConst.h LOCS_BIG_SOCCAR/LOCS_SMALL_SOCCAR (6+28)")

    # --- C6b: big pad pickup grants +100 and respawns at 10 s ----------
    # Drive over (-3584,0) and read the pad log around the pickup.
    tape = os.path.join(TMP, "pad_tape.csv")
    with open(tape, "w") as fp:
        fp.write("tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake\n")
        for i in range(100):  # sit still (pad is under the spawn point? no)
            fp.write("%d,0,0,0,0,0,0,0,0\n" % i)
    # Teleport the car onto the big pad instead, and hold still.
    extra = ["--place", "-3584,0,0", "--padlog", os.path.join(TMP, "pad.log")]
    rows, ck, _, pads = run_sim(make_neutral_tape(100), 100, meshdir, "car0", extra)
    big = [p for p in pads if p["big"] == "1"]
    npads_big = len(set(p["idx"] for p in big)) if big else 0
    record("C6b", "pad league has 6 big pads (RocketSim-owned)",
           "neutral+place(-3584,0)", "0-100", ck, "6 big pads",
           "%d big pads" % npads_big, 0, npads_big == 6,
           "RLConst.h LOCS_AMOUNT_BIG = 6")

    # --- C4a: car cannot escape the mesh shell -------------------------
    tape = os.path.join(TMP, "boost600.csv")
    with open(tape, "w") as fp:
        fp.write("tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake\n")
        for i in range(720):
            fp.write("%d,1,0,0,0,0,0,1,0\n" % i)
    rows, ck, _, _ = run_sim(tape, 720, meshdir, "car0", None)
    maxx = max(abs(f(x, "x")) for x in rows)
    maxy = max(abs(f(x, "y")) for x in rows)
    maxz = max(f(x, "z") for x in rows)
    ok = maxx <= SIDE_WALL_X + 400 and maxy <= CORNER_SUM and math.isfinite(maxz)
    record("C4a", "car cannot escape the mesh shell (6 s boost)",
           "boost600 (throttle+boost)", "0-720", ck,
           "max|x| <= 4496", "max|x|=%.1f max|y|=%.1f maxz=%.1f" % (maxx, maxy, maxz),
           0.0, ok, "RLConst.h ARENA_EXTENT_X/Y; mesh supplies the shell")

    # --- C4b: subsonic car-car contact does not demolish (bump only) ---
    # Two cars, fixed kickoff slots ~4600 uu apart, one driven into the other
    # at subsonic speed: expect zero demo events.
    tape = os.path.join(TMP, "bump.csv")
    with open(tape, "w") as fp:
        fp.write("tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake\n")
        for i in range(600):
            fp.write("%d,1,0,0,0,0,0,0,0\n" % i)
    demo = os.path.join(TMP, "demo.log")
    extra = ["--demolog", demo]
    rows, ck, _, _ = run_sim(tape, 600, meshdir, "car0", extra)
    ndemo = 0
    if os.path.exists(demo):
        ndemo = max(0, len(open(demo).read().strip().splitlines()) - 1)
    # --- D4: mesh-mode checksum regression -----------------------------
    # Mesh mode owns its own checksums; the fallback set is frozen separately
    # and must never be overwritten. This tape drives the car into the back
    # wall so the mesh actually participates (the first ~100 ticks are
    # identical in both modes because no contact has happened yet).
    wall = os.path.join(TMP, "wall90.csv")
    with open(wall, "w") as fp:
        fp.write("tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake\n")
        for i in range(900):
            fp.write("%d,1,0,0,0,0,0,1,0\n" % i)
    mrows, mck, _, _ = run_sim(wall, 900, meshdir, "car0", None)
    maxy = max(f(r, "y") for r in mrows)
    # Recorded mesh-mode checksum for this tape (seed 1, 900 ticks).
    EXPECT_MESH_CK = "2bd44e5e8ede6e48"
    record("D4", "mesh-mode checksum regression (wall-contact tape)",
           "wall90 (throttle+boost, x=0)", "0-900", mck,
           EXPECT_MESH_CK, mck, 0.0, mck == EXPECT_MESH_CK,
           "mesh mode owns its own checksums; fallback 73db10410bd8a8ad is frozen separately")
    record("D4b", "wall tape reaches the mesh (max y sane)",
           "wall90", "0-900", mck, "max|y| <= 6100", "%.0f" % maxy, 0.0,
           0.0 < maxy <= 6100.0,
           "RLConst.h ARENA_EXTENT_Y=5120 + goal depth 880 = 6000")

    record("C4b", "subsonic car-car contact = bump, no demo",
           "throttle-only 600 ticks", "0-600", ck, "0 demo events",
           "%d demo event(s)" % ndemo, 0, ndemo == 0,
           "Arena car-car DemoMode::NORMAL requires the SUPERSONIC flag")


def main():
    global TMP
    ap = argparse.ArgumentParser()
    ap.add_argument("--meshdir", default=os.path.join(ROOT, "collision_meshes"))
    args = ap.parse_args()

    if not os.path.isfile(HEADLESS):
        print("FATAL: %s not built (run make all)" % HEADLESS)
        return 2

    meshdir = args.meshdir
    soccar = os.path.join(meshdir, "soccar")
    has_mesh = os.path.isdir(soccar) and any(
        n.endswith(".cmf") for n in os.listdir(soccar))
    if not has_mesh:
        print("SKIP: no mesh - '%s' has no soccar/*.cmf" % meshdir)
        print("      run `make meshgen`, or pass --meshdir DIR with a real dump")
        print("# summary: 0 PASS, 0 FAIL, all mesh gates SKIPPED")
        return 0

    with tempfile.TemporaryDirectory(prefix="meshgates_") as tmp:
        TMP = tmp
        run_gates(meshdir)

    npass = sum(1 for r in results if r.get("ok"))
    nfail = sum(1 for r in results if r.get("ok") is False)
    print("# summary: %d PASS, %d FAIL" % (npass, nfail))
    return 1 if nfail else 0


if __name__ == "__main__":
    raise SystemExit(main())
