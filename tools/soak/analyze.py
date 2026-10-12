#!/usr/bin/env python3
# tools/soak/analyze.py — MIT License
"""Analyze a real soak run (for use once the binary exists).
Usage: analyze.py perf.csv --p99-budget 17.5 --drops phys_trace.csv
perf.csv columns: frame,dt_ms      phys_trace.csv columns: frame,owed,done,drops
Prints PASS/FAIL per gate counter. Exit 0 iff phys_drops==0 (p99 reported, waived on llvmpipe via --llvmpipe).
"""
import argparse, csv, statistics, sys

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("perf"); ap.add_argument("--p99-budget", type=float, default=17.5)
    ap.add_argument("--drops", default=None); ap.add_argument("--llvmpipe", action="store_true")
    a = ap.parse_args()
    with open(a.perf, newline='') as f:
        dts = [float(r["dt_ms"]) for r in csv.DictReader(f)]
    drops = 0
    if a.drops:
        with open(a.drops, newline='') as f:
            for r in csv.DictReader(f):
                drops += int(r.get("drops", 0))
    p99 = statistics.quantiles(dts, n=100)[98] if len(dts) >= 100 else max(dts)
    p50 = statistics.median(dts)
    miss = sum(1 for d in dts if d > a.p99_budget)
    print(f"frames={len(dts)} p50={p50:.2f}ms p99={p99:.2f}ms max={max(dts):.2f}ms miss={miss}")
    print(f"phys_drops={drops}")
    ok = drops == 0 and (a.llvmpipe or p99 <= a.p99_budget)
    print("SOAK " + ("PASS" if ok else "FAIL")
          + (f" (p99 waived: llvmpipe profile)" if a.llvmpipe and p99 > a.p99_budget else ""))
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
