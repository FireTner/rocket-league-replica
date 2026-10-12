# tools/verify/sim_shim.py — MIT License
"""Shim: verification reads A1 SimAPI + A5 Controls.h via shims if missing.
Never blocks: try_measured() returns {} when the real sim is absent, and
check.py falls back to labelled STUB expectations.

When A1 lands, they should EITHER:
  (a) drop a real `sim_api.py` next to this file exposing read_*() helpers, or
  (b) ship `build/headless_sim` with the CLI in tools/tas/TAPE_FORMAT.md —
      this shim will auto-detect it and parse MEASURED values from dumps.
"""
import csv, math, os, shutil, subprocess

MODE = "STUB (no SimAPI/headless_sim found; analytic expectations only)"

def _find_sim():
    for c in ("build/headless_sim", "headless_sim"):
        p = shutil.which(c) or (c if os.path.isfile(c) and os.access(c, os.X_OK) else None)
        if p:
            return p
    # A1 python SimAPI, if they drop one here
    for c in ("tools/verify/sim_api.py", "src/sim/sim_api.py"):
        if os.path.isfile(c):
            return c
    return None

def try_measured():
    """Return {gate_id: measured_value} or {} if sim not ready."""
    global MODE
    sim = _find_sim()
    if sim is None:
        MODE = "STUB (no SimAPI/headless_sim found; analytic expectations only)"
        return {}
    if sim.endswith(".py"):
        MODE = f"MEASURED via python SimAPI ({sim}) — not yet implemented, STUB fallback"
        return {}
    # headless_sim binary path: full MEASURED flow lives here once A1 ships it.
    MODE = f"MEASURED via {sim} (headless dumps)"
    try:
        return _measure_via_binary(sim)
    except Exception as e:
        MODE = f"STUB (headless_sim probe failed: {e})"
        return {}

def _measure_via_binary(sim):
    # Probe: replay straight tape, read settled speed from dump.
    # Full gate extraction implemented when the binary exists; kept explicit
    # so a half-present binary can never silently fake a PASS.
    raise NotImplementedError("headless_sim CLI probe left for A1 landing")
