"""Locate headless_sim or a python sim API for the number checklist.
try_measured() returns {} when neither is usable, and the checklist uses its stub.
"""

import csv, math, os, shutil, subprocess

MODE = "STUB (no SimAPI/headless_sim found; analytic expectations only)"

def _find_sim():
    for c in ("build/headless_sim", "headless_sim"):
        p = shutil.which(c) or (c if os.path.isfile(c) and os.access(c, os.X_OK) else None)
        if p:
            return p
    # python SimAPI, if one is dropped next to this file
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
    # headless_sim binary path.
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
