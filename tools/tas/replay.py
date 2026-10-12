#!/usr/bin/env python3
# tools/tas/replay.py — MIT License
"""TAS tape record/replay harness (A6).
Modes:
  validate  — check a tape against TAPE_FORMAT.md, report clamp/parse events.
  self      — run the analytic reference model over a tape, dump states+checksum.
  sim       — drive headless_sim CLI if present, else fall back to self-check.
  compare   — diff two state dumps tick-by-tick, report drift curve stats.
  drift     — self-model 10k-tick drift-vs-baseline report (analytic; STUB-labelled
              until headless_sim exists). Writes docs/drift_curve.md data table.
Exit codes: 0 ok, 1 usage/parse error, 2 divergence detected (compare), 3 sim missing
(only when --strict and no headless_sim found).
"""
import argparse, csv, hashlib, math, os, shutil, subprocess, sys

HEADER = ["tick","throttle","steer","pitch","yaw","roll","jump","boost","handbrake"]
RATE = 120.0
DT = 1.0 / RATE

# Analytic expectations (mirror docs/number_gates.md). Kinematic only — STUB model.
ACCEL_FULL = 1410.0      # uu/s^2 at full throttle, low speed
BOOST_EXTRA = 890.0      # extra accel while boosting (1410 -> 2300 total)
V_MAX = 2300.0           # boost top speed
V_MAX_NOBOOST = 1410.0
DRAG = 0.12              # light linear drag for reference curve

def load_tape(path):
    rows, clamps, meta = [], 0, {}
    with open(path, newline='', encoding='utf-8') as f:
        lines = f.readlines()
    # strip #-comments before header
    idx = 0
    while idx < len(lines) and lines[idx].lstrip().startswith('#'):
        kv = lines[idx].lstrip('#').strip()
        if '=' in kv:
            k, v = kv.split('=', 1)
            meta[k.strip()] = v.strip()
        idx += 1
    rest = lines[idx:]
    if not rest:
        raise ValueError("empty tape (no header)")
    if rest[0].strip() != ','.join(HEADER):
        raise ValueError(f"bad header: {rest[0].strip()!r} expected {','.join(HEADER)!r}")
    reader = csv.DictReader(rest)
    expect = 0
    for r in reader:
        try:
            tick = int(r['tick'])
        except Exception:
            raise ValueError(f"bad tick value {r.get('tick')!r}")
        if tick != expect:
            raise ValueError(f"tick gap: expected {expect}, got {tick}")
        expect += 1
        row = {'tick': tick}
        for col in ("throttle","steer","pitch","yaw","roll"):
            try:
                v = float(r[col])
            except Exception:
                raise ValueError(f"tick {tick}: bad float {col}={r[col]!r}")
            if not (-1.0 <= v <= 1.0):
                clamps += 1
                v = max(-1.0, min(1.0, v))
            row[col] = v
        for col in ("jump","boost","handbrake"):
            s = (r[col] or '').strip()
            if s not in ("0","1"):
                raise ValueError(f"tick {tick}: bad int {col}={r[col]!r}")
            row[col] = int(s)
        rows.append(row)
    return rows, clamps, meta

def validate(path):
    try:
        rows, clamps, meta = load_tape(path)
    except ValueError as e:
        print(f"INVALID {path}: {e}")
        return 1
    print(f"VALID {path}: {len(rows)} ticks ({len(rows)/RATE:.2f}s), clamps={clamps}, meta={meta or '{}'}")
    return 0

def analytic_run(rows):
    """1-D + heading kinematic reference. Returns list of (x, y, v, heading)."""
    x = y = v = h = 0.0
    out = []
    for r in rows:
        a = ACCEL_FULL * max(0.0, r['throttle'])
        if r['boost'] and r['throttle'] > 0:
            a += BOOST_EXTRA
        if r['throttle'] < 0:
            a = ACCEL_FULL * r['throttle']  # reverse
        cap = V_MAX if r['boost'] else V_MAX_NOBOOST
        v += (a - DRAG * v) * DT
        v = max(-800.0, min(cap, v))
        h += r['steer'] * (2.4 if r['handbrake'] else 1.6) * DT
        x += math.cos(h) * v * DT
        y += math.sin(h) * v * DT
        out.append((x, y, v, h))
    return out

def fnv1a64(states):
    h = 0xcbf29ce484222325
    for (x, y, v, hd) in states:
        for f in (x, y, v, hd):
            for b in str(f).encode():
                h ^= b
                h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"

def dump_states(states, path):
    with open(path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(["tick","x","y","speed","heading"])
        for i, (x, y, v, h) in enumerate(states):
            w.writerow([i, f"{x:.4f}", f"{y:.4f}", f"{v:.4f}", f"{h:.6f}"])

def find_sim():
    for c in (os.path.join("build","headless_sim"), "headless_sim"):
        p = shutil.which(c) or (c if os.path.isfile(c) and os.access(c, os.X_OK) else None)
        if p:
            return p
    return None

def cmd_self(a):
    rows, clamps, _ = load_tape(a.tape)
    states = analytic_run(rows)
    if a.dump:
        dump_states(states, a.dump)
    print(f"SELF {a.tape}: ticks={len(rows)} clamps={clamps}")
    print(f"SELF final: x={states[-1][0]:.2f} y={states[-1][1]:.2f} v={states[-1][2]:.2f}")
    print(f"CHECKSUM {fnv1a64(states)}")
    print("MODE=STUB (analytic reference model, not physics oracle)")
    return 0

def cmd_sim(a):
    sim = find_sim()
    if sim is None:
        print("headless_sim NOT FOUND on PATH or build/headless_sim.", file=sys.stderr)
        if a.strict:
            return 3
        print("Falling back to self-check mode (STUB).")
        return cmd_self(a)
    ticks = a.ticks or 0
    cmd = [sim, "--tape", a.tape]
    if ticks: cmd += ["--ticks", str(ticks)]
    if a.dump: cmd += ["--dump", a.dump]
    if a.seed is not None: cmd += ["--seed", str(a.seed)]
    print(f"RUN {' '.join(cmd)}")
    r = subprocess.run(cmd)
    print(f"headless_sim exit={r.returncode} (MODE=MEASURED)")
    return r.returncode

def load_dump(path):
    rows = []
    with open(path, newline='', encoding='utf-8') as f:
        r = csv.DictReader(f)
        for row in r:
            rows.append((float(row['x']), float(row['y']), float(row['speed']), float(row['heading'])))
    return rows

def cmd_compare(a):
    A, B = load_dump(a.dump_a), load_dump(a.dump_b)
    n = min(len(A), len(B))
    if len(A) != len(B):
        print(f"LENGTH MISMATCH: {len(A)} vs {len(B)} (comparing first {n})")
    maxd = meand = maxhd = 0.0
    for i in range(n):
        d = math.hypot(A[i][0]-B[i][0], A[i][1]-B[i][1])
        meand += d; maxd = max(maxd, d)
        maxhd = max(maxhd, abs(A[i][3]-B[i][3]))
    meand /= max(n, 1)
    print(f"COMPARE {a.dump_a} vs {a.dump_b}: ticks={n} max_pos_drift={maxd:.6f}uu mean={meand:.6f}uu max_heading={maxhd:.6f}rad")
    tol = a.tol
    if maxd <= tol and len(A) == len(B):
        print(f"ZERO-DIVERGENCE PASS (tol={tol})")
        return 0
    print(f"DIVERGENCE {'(length mismatch)' if len(A)!=len(B) else f'(max {maxd:.6f} > tol {tol})'}")
    return 2

def cmd_drift(a):
    # 10k-tick straight-boost analytic baseline; perturb copies to show method.
    n = 10000
    rows = [{'tick':i,'throttle':1.0,'steer':0.0,'pitch':0.0,'yaw':0.0,'roll':0.0,
             'jump':0,'boost':1,'handbrake':0} for i in range(n)]
    base = analytic_run(rows)
    # perturbed: 1e-6 extra drag — stands in for a second binary until sim exists
    global DRAG
    DRAG_SAVE = DRAG; DRAG = DRAG_SAVE + 1e-6
    pert = analytic_run(rows); DRAG = DRAG_SAVE
    per = [1000, 2000, 5000, 10000]
    print("DRIFT REPORT (MODE=STUB analytic-vs-analytic; method demo for real sim later)");
    print("tick,max_pos_drift_uu,mean_pos_drift_uu,baseline_speed_uu_s")
    prev = {}
    for k in per:
        ds = [math.hypot(base[i][0]-pert[i][0], base[i][1]-pert[i][1]) for i in range(k)]
        print(f"{k},{max(ds):.6f},{sum(ds)/len(ds):.6f},{base[k-1][2]:.3f}")
        prev[k] = (max(ds), sum(ds)/len(ds), base[k-1][2])
    print(f"BASELINE_CHECKSUM {fnv1a64(base)}")
    print("NOTE: STUB curve — same-binary sim replay not yet available; method is final.")
    if a.out:
        with open(a.out, 'w', encoding='utf-8') as f:
            f.write("tick,max_pos_drift_uu,mean_pos_drift_uu,baseline_speed_uu_s\n")
            for k in per:
                mx, mn, sp = prev[k]
                f.write(f"{k},{mx:.6f},{mn:.6f},{sp:.3f}\n")
        print(f"wrote {a.out}")
    return 0

def main():
    ap = argparse.ArgumentParser(description="TAS tape harness")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("validate"); p.add_argument("tape"); p.set_defaults(fn=lambda a: validate(a.tape))
    p = sub.add_parser("self"); p.add_argument("tape"); p.add_argument("--dump", default=None); p.set_defaults(fn=cmd_self)
    p = sub.add_parser("sim"); p.add_argument("tape"); p.add_argument("--dump", default=None)
    p.add_argument("--ticks", type=int, default=0); p.add_argument("--seed", type=int, default=None)
    p.add_argument("--strict", action="store_true"); p.set_defaults(fn=cmd_sim)
    p = sub.add_parser("compare"); p.add_argument("dump_a"); p.add_argument("dump_b")
    p.add_argument("--tol", type=float, default=0.0); p.set_defaults(fn=cmd_compare)
    p = sub.add_parser("drift"); p.add_argument("--out", default=None); p.set_defaults(fn=cmd_drift)
    a = ap.parse_args()
    try:
        sys.exit(a.fn(a))
    except FileNotFoundError as e:
        print(f"FILE NOT FOUND: {e}", file=sys.stderr); sys.exit(1)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr); sys.exit(1)

if __name__ == "__main__":
    main()
