#!/usr/bin/env python3
# tools/verify/check.py — MIT License
"""Number-checklist runner (A6).
Each gate tries, in order:
  1. MEASURED — import the real SimAPI/Controls shims or headless_sim if present.
  2. STUB     — analytic expectation model (clearly labelled).
Never blocks on missing A1/A5 code: stubs + notes instead.
Exit 0 always unless --strict and a MEASURED gate fails (stub UNKNOWN never fails).
"""
import argparse, csv, json, math, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import sim_shim as SHIM
    SHIM_MODE = SHIM.MODE
except Exception as e:
    SHIM = None
    SHIM_MODE = f"shim import failed: {e}"

# ---- Gate table: (id, name, expected, tolerance, unit, how) ----
GATES = [
    ("G1",  "throttle top speed (no boost)",   1410, 1,   "uu/s", "straight_boost tape w/o boost, settle speed"),
    ("G2",  "boost top speed (cap)",            2300, 1,  "uu/s", "full throttle+boost, settle speed"),
    ("G3",  "held-jump apex height",            2.55, 0.05,"m",   "jump held: apex of z(t)"),
    ("G4",  "double-jump apex height",          4.82, 0.05,"m",   "jump, release, 2nd jump pre-apex"),
    ("G5",  "ground turn rate (no slide)",      1.60, 0.10,"rad/s","full steer @ speed, yaw rate"),
    ("G6",  "powerslide turn rate",             2.40, 0.15,"rad/s","full steer + handbrake, yaw rate"),
    ("G7",  "front-flip rotation",              350, 15,  "deg",  "dodge forward: total pitch over flip"),
    ("G8",  "supersonic threshold",             2200, 20, "uu/s", "boost: speed at trail/flag onset"),
    ("G9a", "big pad refill",                   100, 0,   "boost", "drive over big pad"),
    ("G9b", "small pad refill",                 12,  0,   "boost", "drive over small pad"),
    ("G9c", "big pad respawn",                  10,  0.5, "s",    "time until big pad live again"),
    ("G9d", "small pad respawn",                4,  0.5,  "s",    "time until small pad live again"),
]

# ---- Analytic STUB model (mirrors replay.py self-model + jump ballistics) ----
G_ACCEL, B_ACCEL = 1410.0, 890.0
VMAX, VMAX_B = 1410.0, 2300.0
JUMP_V, JUMP_HOLD_A, JUMP_A, DBL_V, DBL_T = 7.0, -4.0, -20.0, 10.9, 0.30
# (v0=7 m/s, net -4 m/s^2 while held first 0.25 s, -20 after,
#  double-jump re-impulse to 10.9 m/s at t=0.30 s)

def stub_jump_apex(held=True, double=False):
    dt, z, v, t, apex = 1/120, 0.0, JUMP_V, 0.0, 0.0
    second = False
    while t < 4.0:
        # hold reduces net gravity for first 0.25 s; tap skips the benefit
        a = JUMP_HOLD_A if (held and t < 0.25) else JUMP_A
        if double and not second and t >= DBL_T:
            v = DBL_V; second = True
        v += a*dt; z += v*dt; apex = max(apex, z)
        if v < 0 and z <= 0: break
        t += dt
    return apex

def stub_values():
    return {
        "G1": VMAX, "G2": VMAX_B,
        "G3": stub_jump_apex(True, False), "G4": stub_jump_apex(True, True),
        "G5": 1.60, "G6": 2.40, "G7": 350.0, "G8": 2200.0,
        "G9a": 100, "G9b": 12, "G9c": 10.0, "G9d": 4.0,
    }

def fmt(v, u):
    return f"{v:.2f}{u}" if isinstance(v, float) else f"{v}{u}"

def run(strict=False):
    measured = (SHIM is not None and SHIM.try_measured()) or {}
    stubs = stub_values()
    rows = []
    print(f"# verify/check.py — shim: {SHIM_MODE}")
    print(f"# {'ID':4} {'gate':32} {'expected':>12} {'got':>12} {'status':>10}  src")
    fails = 0
    for gid, name, exp, tol, unit, how in GATES:
        if gid in measured:
            got, src = measured[gid], "MEASURED"
            ok = abs(got - exp) <= tol
            status = "PASS" if ok else "FAIL"
            if not ok and strict: fails += 1
        else:
            got, src = stubs[gid], "STUB"
            ok = abs(got - exp) <= tol
            status = "EXPECT-OK" if ok else "EXPECT-DRIFT"
        print(f"  {gid:4} {name:32} {fmt(exp,unit):>12} {fmt(got,unit):>12} {status:>10}  {src}")
        rows.append({"id": gid, "name": name, "expected": exp, "tol": tol,
                     "unit": unit, "got": round(got, 4) if isinstance(got, float) else got,
                     "status": status, "src": src, "how": how})
    n_meas = sum(1 for r in rows if r["src"] == "MEASURED")
    n_stub = len(rows) - n_meas
    print(f"# summary: {n_meas} MEASURED, {n_stub} STUB, fails={fails}")
    print("# NOTE: STUB rows are analytic expectations, NOT measurements. "
          "They become MEASURED once A1 SimAPI lands.")
    return rows, fails

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default=None, help="write gate table JSON here")
    ap.add_argument("--strict", action="store_true",
                    help="nonzero exit if a MEASURED gate fails")
    a = ap.parse_args()
    rows, fails = run(a.strict)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"shim": SHIM_MODE, "gates": rows}, f, indent=2)
        print(f"# wrote {a.json}")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
