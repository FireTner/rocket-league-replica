#!/usr/bin/env python3
# tools/verify/measured.py — MEASURED gate verifier (coordinator-owned).
# Runs build/headless_sim over the frozen + gate tapes and checks Section 7
# values. Exit 0 = all measured gates green, 1 = any FAIL, 2 = sim missing.
# Writes gates_measured.json beside itself. MIT License.
import csv, json, math, os, subprocess, sys

SIM = "build/headless_sim"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TAPES = os.path.join(ROOT, "tools", "tas", "tapes")
TMP = "/tmp/measured_gates"
os.makedirs(TMP, exist_ok=True)

GATES = []


def run(tape, ticks, seed=1, extra=()):
    tp = os.path.join(TAPES, tape) if not os.path.isabs(tape) else tape
    dump = os.path.join(TMP, os.path.basename(tape) + ".out")
    cmd = [os.path.join(ROOT, SIM), "--tape", tp, "--ticks", str(ticks),
           "--dump", dump, "--seed", str(seed)] + list(extra)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("SIM RUN FAILED:", " ".join(cmd), r.stderr.strip()[-300:])
        sys.exit(2)
    cks = [l for l in r.stdout.splitlines() if l.startswith("CHECKSUM")]
    rows = list(csv.DictReader(open(dump)))
    return rows, (cks[0].split()[1] if cks else "?")


def load(p):
    return list(csv.DictReader(open(p)))


def gate(gid, name, ok, detail):
    GATES.append({"id": gid, "gate": name, "pass": bool(ok), "detail": detail})
    print(("PASS " if ok else "FAIL ") + f"{gid} {name}: {detail}")


def main():
    if not (os.path.isfile(os.path.join(ROOT, SIM)) and os.access(os.path.join(ROOT, SIM), os.X_OK)):
        print("headless_sim missing: build it with `make build/headless_sim`")
        return 2

    rows, cks = run("straight_throttle_600.csv", 600)
    sp = [float(r["speed"]) for r in rows]
    v = sum(sp[-60:]) / 60
    gate("G1", "throttle top speed 1410±1", abs(v - 1410) <= 1, f"{v:.2f} uu/s ck={cks}")

    rows, cks = run("boost_top_1200.csv", 1200, extra=["--boost", "100"])
    sp = [float(r["speed"]) for r in rows]
    gate("G2", "boost cap 2300±1", abs(max(sp) - 2300) <= 1, f"max {max(sp):.1f} ck={cks}")
    sup = next((i for i, s in enumerate(sp) if s >= 2200), None)
    gate("G8", "supersonic >=2200", sup is not None, f"first tick {sup}")

    rows, cks = run("jump_hold_240.csv", 240)
    rise = max(float(r["z"]) for r in rows) - 17.0
    gate("G3", "held single jump (truth 214.6±5)", abs(rise - 214.6) <= 5, f"rise {rise:.1f} ck={cks}")

    rows, cks = run("double_jump_max_400.csv", 400)
    rise = max(float(r["z"]) for r in rows) - 17.0
    gate("G4", "double jump (truth 449.3±8)", abs(rise - 449.3) <= 8, f"rise {rise:.1f} ck={cks}")

    rows, cks = run("pad_run_800.csv", 800)
    prev = float(rows[0]["boost"])
    small_ok = False
    for r in rows[1:]:
        b = float(r["boost"])
        if b - prev > 5 and abs((b - prev) - 12.0) < 0.05:
            small_ok = True
            break
        prev = b
    gate("G9b", "small pad +12", small_ok, f"ck={cks}")

    rows, cks = run("pad_big_1500.csv", 1500, extra=["--boost", "0", "--place", "-3072,-3600,-90"])
    prev = 0.0
    big_ok = False
    for r in rows:
        b = float(r["boost"])
        if b - prev > 50 and abs(b - 100.0) < 0.01:
            big_ok = True
        prev = b
    gate("G9a", "big pad refill 100", big_ok, f"ck={cks}")

    # cooldowns via padlog (big tape for big pad; small tape, extended past
    # the 480-tick small respawn, for small pads)
    import collections
    plog = os.path.join(TMP, "cooldown.csv")
    subprocess.run([os.path.join(ROOT, SIM), "--tape", os.path.join(TAPES, "pad_big_1500.csv"),
                    "--ticks", "1500", "--dump", os.path.join(TMP, "x.csv"), "--seed", "1",
                    "--boost", "0", "--place", "-3072,-3600,-90", "--padlog", plog],
                   capture_output=True)
    byidx = collections.defaultdict(list)
    for p in csv.DictReader(open(plog)):
        byidx[int(p["idx"])].append((int(p["tick"]), int(p["active"])))
    spans = {}
    for i, seq in byidx.items():
        off = [t for t, a in seq if a == 0]
        if off:
            on = [t for t, a in seq if a == 1 and t > off[0]]
            if on:
                spans[i] = on[0] - off[0]
    big_span = spans.get(4)
    gate("G9c", "big respawn 1200±5 ticks", big_span is not None and abs(big_span - 1200) <= 5,
         f"span {big_span}")
    plog2 = os.path.join(TMP, "cooldown_small.csv")
    subprocess.run([os.path.join(ROOT, SIM), "--tape", os.path.join(TAPES, "pad_run_800.csv"),
                    "--ticks", "1100", "--dump", os.path.join(TMP, "y.csv"), "--seed", "1",
                    "--padlog", plog2],
                   capture_output=True)
    byidx2 = collections.defaultdict(list)
    for p in csv.DictReader(open(plog2)):
        byidx2[int(p["idx"])].append((int(p["tick"]), int(p["active"])))
    small_spans = []
    for i, seq in byidx2.items():
        off = [t for t, a in seq if a == 0]
        if off:
            on = [t for t, a in seq if a == 1 and t > off[0]]
            if on:
                small_spans.append(on[0] - off[0])
    small_ok_cd = any(abs(s - 480) <= 5 for s in small_spans)
    gate("G9d", "small respawn 480±5 ticks", small_ok_cd, f"small spans {small_spans[:4]}")

    # TAS zero-divergence: same tape twice
    r1, c1 = run("powerslide_circle_720.csv", 720)
    r2, c2 = run("powerslide_circle_720.csv", 720)
    gate("TAS", "zero divergence", c1 == c2, f"{c1} vs {c2}")

    # turn/flip spot checks (truth values from Round 5)
    rows, cks = run("steer_left_360.csv", 360)

    def yaw(i):
        return math.atan2(float(rows[i]["fy"]), float(rows[i]["fx"]))
    prev = yaw(100)
    acc = 0.0
    for t in range(101, 220):
        y = yaw(t)
        d = y - prev
        while d > math.pi:
            d -= 2 * math.pi
        while d < -math.pi:
            d += 2 * math.pi
        prev = y
        acc += d
    w = acc / 119 * 120
    gate("G5/6", "turn rate in family (2.0-2.6 rad/s @~10m/s)", 2.0 <= w <= 2.6, f"{w:.2f} rad/s")

    def yawrate_of(rows, t0, t1):
        def yw(i):
            return math.atan2(float(rows[i]["fy"]), float(rows[i]["fx"]))
        prev = yw(t0)
        acc2 = 0.0
        for t in range(t0 + 1, t1):
            y = yw(t)
            d = y - prev
            while d > math.pi:
                d -= 2 * math.pi
            while d < -math.pi:
                d += 2 * math.pi
            prev = y
            acc2 += d
        n = t1 - t0
        sp = sum(float(rows[t]["speed"]) for t in range(t0, t1)) / n
        return acc2 / n * 120, sp

    rows, cks = run("turn_14ms_900.csv", 900)
    w14, s14 = yawrate_of(rows, 700, 800)
    gate("G5a", "turn rate ~2.28 @~1292 (tol ±0.15)", abs(w14 - 2.28) <= 0.15,
         f"{w14:.2f} rad/s @ {s14:.0f} uu/s ck={cks}")
    rows, cks = run("turn_23ms_660.csv", 660, extra=["--boost", "100"])
    w23, s23 = yawrate_of(rows, 420, 520)
    gate("G5b", "turn rate ~1.98 @~1980 (tol ±0.15)", abs(w23 - 1.98) <= 0.15,
         f"{w23:.2f} rad/s @ {s23:.0f} uu/s ck={cks}")

    rows, cks = run("flip_forward_240.csv", 240)
    angs = [math.atan2(float(rows[t]["fz"]), float(rows[t]["fy"])) for t in range(25, 141)]
    tot = 0.0
    for a, b in zip(angs, angs[1:]):
        d = b - a
        while d > math.pi:
            d -= 2 * math.pi
        while d < -math.pi:
            d += 2 * math.pi
        tot += d
    deg = math.degrees(tot)
    gate("G7", "front flip (truth 324±10 deg)", abs(deg - 324) <= 10, f"{deg:.1f} deg ck={cks}")

    # G10 demolition (RocketSim DemoMode::NORMAL): supersonic contact demos the
    # victim; it respawns after exactly 360 ticks (3.0 s,
    # RLConst::DEMO_RESPAWN_TIME, cross-checked vs car-soccer.com via rl-like).
    # Subsonic contact (spawn 33.3 boost, ~2150 at contact) must NOT demo.
    dlog = os.path.join(TMP, "demo_supersonic_600.csv.demolog")
    rows, cks = run("demo_supersonic_600.csv", 800, extra=[
        "--body", "car1", "--boost", "100", "--demolog", dlog])
    evs = load(dlog)
    ok = (len(evs) == 1 and evs[0]["bumper"] == "0" and evs[0]["victim"] == "1")
    dt = "demo " + (f"tick {evs[0]['tick']} bumper 0 victim 1" if evs else "NONE")
    if ok:
        T = int(evs[0]["tick"])
        y = [float(r["y"]) for r in rows]
        z = [float(r["z"]) for r in rows]
        # Victim frozen (demoed) from the hit until respawn 360 ticks later.
        frozen = all(abs(y[t] - y[T]) < 1 and abs(z[t] - z[T]) < 1
                     for t in range(T, min(T + 360, len(rows))))
        ok = ok and frozen
        dt += " frozen OK" if frozen else " frozen BAD"
        if T + 360 < len(rows):
            # Respawn: own-side spawn (center or diagonal, y ≈ +2300..+4608).
            resp = abs(y[T + 360] - 2300.0) < 10 or abs(y[T + 360] - 4608.0) < 10
            ok = ok and resp
            dt += f" respawn@{T + 360} y={y[T + 360]:.0f} {'OK' if resp else 'BAD'}"
    gate("G10", "supersonic demo + 360-tick respawn", ok, dt + f" ck={cks}")
    run("demo_supersonic_600.csv", 600, extra=["--body", "car1", "--demolog", dlog])
    evs2 = load(dlog)
    gate("G10b", "subsonic contact = bump, no demo", len(evs2) == 0,
         f"{len(evs2)} events (expect 0)")

    # ------------------------------------------------------------------
    # I* input-semantics gates (MASTER_PROMPT_PHYSICS.md §8 B3/B4).
    # These isolate the INPUT side: the dodge threshold is the preset
    # dodgeDeadzone (CarConfig.h:37 = 0.5f) tested against
    # |yaw|+|pitch|+|roll| (Car.cpp:705-706).
    # ------------------------------------------------------------------
    def dodge_tape(path, pitch, ticks=200):
        with open(path, "w") as f:
            f.write("tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake\n")
            for i in range(ticks):
                # hold jump 0..29, release 30..34, press again 35..39
                j = 1 if (i < 30 or (35 <= i < 40)) else 0
                f.write("%d,0,0,%s,0,0,%d,0,0\n" % (i, pitch, j))

    def peak_rot_rate(rows):
        """Peak up-vector rotation rate (deg/s) after the 2nd jump press."""
        best = 0.0
        prev = (float(rows[34]["ux"]), float(rows[34]["uy"]), float(rows[34]["uz"]))
        for r in rows[35:]:
            cur = (float(r["ux"]), float(r["uy"]), float(r["uz"]))
            d = sum(a * b for a, b in zip(prev, cur))
            d = max(-1.0, min(1.0, d))
            best = max(best, math.degrees(math.acos(d)) * 120.0)
            prev = cur
        return best

    rates = {}
    for name, pitch in (("below", "0.25"), ("just_below", "0.49"),
                        ("at", "0.5"), ("above", "1.0")):
        tp = os.path.join(TMP, "dodge_%s.csv" % name)
        dodge_tape(tp, pitch)
        rows, cks = run(tp, 200)
        rates[name] = peak_rot_rate(rows)

    # Below the preset threshold the second press is a dodge-less double jump:
    # slow sustained rotation. At/above it is a flip: a sharp rotation-rate
    # discontinuity. CarConfig.h default dodgeDeadzone = 0.5, so the boundary
    # sits between 0.49 and 0.5.
    below = rates["just_below"]
    at = rates["at"]
    gate("I1", "dodge threshold discontinuity at dodgeDeadzone 0.5",
         below < 300.0 <= at and rates["below"] < below,
         "peak rot rate: 0.25=%.0f, 0.49=%.0f (dodge-less), 0.5=%.0f, 1.0=%.0f deg/s"
         % (rates["below"], below, at, rates["above"]))

    # Monotonic input response: more stick travel must not reduce rotation.
    gate("I2", "analog input response is monotonic",
         rates["below"] <= below <= at,
         "0.25 <= 0.49 <= 0.5 rotation rates hold")

    # The threshold is a SUM over the three rotational channels, so yaw or
    # roll alone at 0.5 must also reach the flip regime. Pitch 0.0 + roll 0.5
    # is exercised via the roll channel: |0|+|0|+|0.5| >= 0.5.
    tp = os.path.join(TMP, "dodge_roll.csv")
    with open(tp, "w") as f:
        f.write("tick,throttle,steer,pitch,yaw,roll,jump,boost,handbrake\n")
        for i in range(200):
            j = 1 if (i < 30 or (35 <= i < 40)) else 0
            f.write("%d,0,0,0,0,0.5,%d,0,0\n" % (i, j))
    rows, cks = run(tp, 200)
    roll_rate = peak_rot_rate(rows)
    gate("I3", "dodge threshold sums across pitch/yaw/roll channels",
         roll_rate >= 300.0,
         "roll=0.5 alone -> peak rot rate %.0f deg/s (flip regime)" % roll_rate)

    fails = [g for g in GATES if not g["pass"]]
    with open(os.path.join(HERE, "gates_measured.json"), "w") as f:
        json.dump(GATES, f, indent=1)
    print(f"# summary: {len(GATES)-len(fails)} PASS, {len(fails)} FAIL")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
