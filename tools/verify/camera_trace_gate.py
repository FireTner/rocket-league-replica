#!/usr/bin/env python3
"""CT1-CT3 — camera trace gate (LOCAL ONLY, SKIPs loudly, exit 0 without traces).

`MASTER_PROMPT_CAMERA_EXACT.md` §6 P3.4 asks for the exact-feel bar as a replayable
trace instead of a vibe. This gate is that bar. It grades three different things
and never conflates them:

  CT1  self-replay exact   Load a trace, write it back out, reload it, and every
                           float32 field plus the POV convention round trip must
                           be BIT-EXACT (0.0, not "small"). This is the format +
                           loader contract; it holds for synthetic fixtures too,
                           because it is not a claim about our camera.
  CT2  exact feel vs RL    Per scenario and per probe channel, our replay's step
                           response (settle time + overshoot) must land within
                           TOL of the RECORDED trace's own response, measured with
                           the same formula on both sides.
  CT3  no regression       The same metrics plus the per-field error table must
                           not be WORSE than the stored baseline
                           `decomp/traces/camera_trace_baseline.json`.

TRACE HOME (binding, Rounds 8/14): the canonical in-repo gitignored directory
`decomp/traces/` (`cam_trace.DEFAULT_TRACE_DIR`), never a filesystem-root
`/decomp` — that cannot exist on this machine (`/` is read-only) and was a real
blocker in Round 8. A trace is Psyonix-derived measurement data, so it never
enters the tree; the tooling does.

SKIP CONTRACT (the reason a public clone stays green): with no trace whose header
declares `source: "live"` — i.e. only the synthetic fixtures under
`decomp/traces/synthetic/` — CT2 and CT3 have nothing to measure and SKIP, and
the whole gate exits 0. CT1 still runs on the fixtures, labelled as format
plumbing. Missing directory, broken trace, unusable replay toolchain: SKIP or
FAIL with a stated reason, never a stack trace and never a silent pass.

WHAT IS SCORED, AND WHAT IS NOT (honest limits, the same posture as
`tools/decomp/camera_trace_diff.py`):

  * scored channels: `rot_yaw` / `rot_pitch` (UE rotator units, wrapped) and
    `fov_deg`. Rotation is the POV comparison with the ray-length term removed.
  * `distance_uu` is scored only when the recorded Distance is demonstrably the
    same quantity as ours (`|Eye - Center|`, the diff tool's empirical
    `[0.5, 2.0]` median-ratio probe). A live RL trace normally records the RIG
    distance (~270 uu) while `|Eye - Center|` is the distance to the look target
    (~1600 uu in ballcam) — Round 14's finding, which is why `Camera::Pov()`
    exists. `Pov()` is invisible on this path because the replay driver
    (`tools/decomp/camera_trace_diff.py`, P2-owned) prints the GL pair; extending
    that driver is P2-scope, so this gate reports Distance `UNVERIFIED` with the
    reason instead of scoring a semantics difference as camera error.
  * Focus vs Rotation are ALWAYS compared as separate fields. During a transition
    the blended rotation and the blended focus point are interpolated
    independently, so they legitimately disagree (lead-measured up to 103.9 deg);
    any metric here that assumed they agree would be false exactly where the
    `ballcam-toggle` scenario spends its frames.

PROBE CHANNELS / METRICS (documented so every number is reproducible):

    step_frame     the largest single-frame move after frame 0 (response onset)
    span           |final - value_before_step|, final = mean of the last 10%
    settle_frames  frames from step_frame until the series stays inside a
                   SETTLE_BAND (5%) band around `final`; "never settled" = the
                   rest of the trace
    settle_s       the same thing in seconds (sum of the recorded dt values)
    overshoot_pct  the largest excursion PAST `final` in the step's direction, as
                   a percent of `span` (0 when it only approaches)
    flat           span below the channel's noise floor -> no step, not scored

Usage:
    python3 tools/verify/camera_trace_gate.py                  # default dir
    python3 tools/verify/camera_trace_gate.py --json           # machine-readable
    python3 tools/verify/camera_trace_gate.py --tracedir DIR --baseline FILE
    python3 tools/verify/camera_trace_gate.py --write-baseline # operator action

Exit codes: 0 PASS or SKIP, 1 FAIL, 2 tool/config error (never a silent pass).
"""
import argparse
import json
import os
import struct
import sys
import tempfile
import time
import zlib

sys.dont_write_bytecode = True  # never leave tools/decomp/__pycache__ dirt

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DECOMP_TOOLS = os.path.join(REPO, 'tools', 'decomp')
if DECOMP_TOOLS not in sys.path:
    sys.path.insert(0, DECOMP_TOOLS)

try:
    import cam_trace as ct  # the ONE format implementation (loader + convention)
except Exception as _ct_err:  # pragma: no cover - a partial checkout must not crash
    print('camera_trace_gate: SKIP — tools/decomp/cam_trace.py is unusable (%s)'
          % _ct_err)
    print('  The trace format lives in tools/decomp/cam_trace.py (P2-owned); this')
    print('  gate never re-implements it. Restore that file to re-enable CT1-CT3.')
    sys.exit(0)

try:
    import camera_trace_diff as diff  # P2's replay backends (real camera + loader)
    _diff_err = None
except Exception as _e:  # pragma: no cover
    diff = None
    _diff_err = _e

# ── the gate's knobs (values, not preferences) ───────────────────────────────
#: The "-5%" band from §6 P3.4, in ONE place: every CT2 match and every CT3
#: regression bound is a 5% allowance. Never relax it to make a run green.
TOL = 0.05
#: Standard step-response settle band: 5% of the response span.
SETTLE_BAND = 0.05
#: A non-wrapped channel must also move this fraction of its own magnitude to
#: count as a response (a percent-overshoot on a near-flat series is noise).
REL_FLOOR = 0.01
#: Overshoot is in percentage points, so 5% OF it is meaningless near zero;
#: 1.0 pp is the effective floor of that comparison.
OVER_FLOOR_PCT = 1.0
#: Per-field noise floors for CT3, in each field's own unit. These are the same
#: numbers `camera_trace_diff.RANK_TOLERANCES` uses to rank its worst frames,
#: reused so the gate and the diff table agree on what "noise" means.
FIELD_FLOORS = {
    'focus_uu': 1.0,
    'distance_uu': 1.0,
    'rotation_mag': 100.0,
    'fov_deg': 1.0,
}
#: channel -> (unit, noise/step floor in channel units, wrap?)
PROBE_CHANNELS = (
    ('rot_yaw', 'rotator units', 100.0, True),
    ('rot_pitch', 'rotator units', 100.0, False),
    ('fov_deg', 'degrees', 0.25, False),
    ('distance_uu', 'uu', 1.0, False),
)
BASELINE_NAME = 'camera_trace_baseline.json'
BASELINE_FORMAT = 'camera_trace_gate_baseline'
BASELINE_VERSION = 1


class GateError(Exception):
    """A tool/configuration problem: exit 2, never a silent pass."""


# ── trace discovery ─────────────────────────────────────────────────────────
def scan(root):
    """Every `.rlt` under `root`, loaded through cam_trace's own loader."""
    found = []
    if not os.path.isdir(root):
        return found
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith('.'))
        for name in sorted(filenames):
            if not name.endswith('.rlt'):
                continue
            path = os.path.join(dirpath, name)
            entry = {'path': path, 'rel': os.path.relpath(path, root),
                     'scenario': None, 'live': False, 'records': 0,
                     'source': '?', 'error': None, 'trace': None}
            try:
                tr = ct.load(path)
            except ct.TraceError as e:
                entry['error'] = str(e)
                found.append(entry)
                continue
            entry['trace'] = tr
            entry['source'] = tr.source
            entry['live'] = (tr.source == 'live')
            entry['scenario'] = tr.header.get('scenario') or 'unnamed'
            entry['records'] = tr.record_count
            found.append(entry)
    return found


def trace_crc(tr):
    """The record block's crc32 — the trace's identity, read from the file."""
    with open(tr.path, 'rb') as f:
        raw = f.read()
    _magic, _ver, header_len = struct.unpack_from('<4sII', raw, 0)
    body = raw[ct.PAYLOAD_OFFSET + header_len:]
    return '%08x' % (zlib.crc32(body) & 0xFFFFFFFF)


# ── CT1: self-replay must be bit-exact ─────────────────────────────────────
def ct1_self_replay(entry, tmpdir):
    """write -> load -> compare every field exactly, then the POV round trip."""
    tr = entry['trace']
    out = {'trace': entry['rel'], 'source': entry['source'],
           'records': tr.record_count, 'field_max': 0.0, 'field_at': None,
           'pov_max': 0.0, 'pov_at': None, 'header_exact': True,
           'ok': False, 'reason': None}
    path = os.path.join(tmpdir, 'ct1_' + os.path.basename(entry['path']))
    try:
        ct.write_trace(path, tr.header, [tr[i] for i in range(tr.record_count)])
        rt = ct.load(path)
    except ct.TraceError as e:
        out['reason'] = 're-write/reload failed: %s' % e
        return out
    out['header_exact'] = (rt.header == tr.header)
    if rt.record_count != tr.record_count:
        out['reason'] = ('reload produced %d records, expected %d'
                         % (rt.record_count, tr.record_count))
        return out
    worst, where = 0.0, None
    for i in range(tr.record_count):
        for name in ct.FIELD_NAMES:
            d = abs(rt.value(i, name) - tr.value(i, name))
            if d > worst:
                worst, where = d, (i, name)
    out['field_max'], out['field_at'] = worst, where
    worst_p, where_p = 0.0, None
    for i in range(tr.record_count):
        rec = {'focus': tr.focus(i), 'rotation': tr.rotation(i),
               'distance': tr.value(i, 'distance'),
               'fov': tr.value(i, 'fov'),
               'time': tr.value(i, 'out_time')}
        eye = ct.eye_from_pov(rec['focus'], rec['rotation'], rec['distance'])
        pov = ct.pov_from_eye_center(eye, rec['focus'], rec['fov'], rec['time'])
        if pov is None:
            out['reason'] = ('frame %d has no view axis (eye == focus): the '
                             'POV convention cannot be replayed' % i)
            return out
        err = ct.pov_field_errors(rec, pov, quantize=True)
        for key in ('focus_axis_uu', 'distance_uu', 'rotation_mag', 'fov_deg'):
            if err[key] > worst_p:
                worst_p, where_p = err[key], (i, key)
    out['pov_max'], out['pov_at'] = worst_p, where_p
    out['ok'] = (worst == 0.0 and worst_p == 0.0 and out['header_exact'])
    if not out['ok'] and out['reason'] is None:
        bad = []
        if not out['header_exact']:
            bad.append('header is not identical after a round trip')
        if worst != 0.0:
            bad.append('field %s at frame %d differs by %r'
                       % (where[1], where[0], worst))
        if worst_p != 0.0:
            bad.append('POV %s at frame %d differs by %r'
                       % (where_p[1], where_p[0], worst_p))
        out['reason'] = '; '.join(bad)
    return out


# ── step-response metrics (the same formula on both sides) ─────────────────
def _unwrap_wrapped(vals):
    """Rotator series -> continuous series, following the short arc frame to frame."""
    out = [float(vals[0])]
    for v in vals[1:]:
        out.append(out[-1] + ct.wrap_rotator(v - out[-1]))
    return out


def response_metrics(vals, dts, flat_eps, wrap):
    """Step response of one channel — see the module docstring for every field."""
    n = len(vals)
    if n < 8:
        return {'flat': True, 'reason': 'only %d frames (need >= 8)' % n}
    u = _unwrap_wrapped(vals) if wrap else [float(v) for v in vals]
    tail = max(1, n // 10)
    final = sum(u[-tail:]) / tail
    step_i, step_d = 1, 0.0
    for i in range(1, n):
        d = abs(u[i] - u[i - 1])
        if d > step_d:
            step_d, step_i = d, i
    span = abs(final - u[step_i - 1])
    if not wrap:
        # A percent-overshoot is meaningless on a near-flat series, so a
        # non-wrapped channel also has to move at least 1% of its own magnitude
        # (e.g. 2.7 uu of a 270 uu distance) before it counts as a response.
        mags = sorted(abs(v) for v in u)
        flat_eps = max(flat_eps, REL_FLOOR * mags[len(mags) // 2])
    if span < flat_eps:
        return {'flat': True, 'span': span, 'final': final,
                'reason': 'span %.4g < noise floor %.4g' % (span, flat_eps)}
    band = SETTLE_BAND * span
    # suffix max of |u[k] - final| makes "stays in band from here on" O(1)
    suffix = [0.0] * (n + 1)
    for k in range(n - 1, -1, -1):
        suffix[k] = max(suffix[k + 1], abs(u[k] - final))
    settle_end = n
    for j in range(step_i, n):
        if suffix[j] <= band:
            settle_end = j
            break
    settle_frames = settle_end - step_i
    settle_s = float(sum(dts[step_i:settle_end])) if settle_end > step_i else 0.0
    sign = 1.0 if final > u[step_i - 1] else -1.0
    excursion = max(sign * (u[k] - final) for k in range(step_i, n))
    return {
        'flat': False,
        'final': final,
        'step_frame': step_i,
        'step_mag': step_d,
        'span': span,
        'settle_frames': settle_frames,
        'settle_s': settle_s,
        'settled': settle_end < n,
        'overshoot_pct': 100.0 * max(0.0, excursion) / span,
    }


def channel_series(povs, name):
    if name == 'rot_yaw':
        return [p['rotation'][1] for p in povs]
    if name == 'rot_pitch':
        return [p['rotation'][0] for p in povs]
    if name == 'fov_deg':
        return [p['fov'] for p in povs]
    if name == 'distance_uu':
        return [p['distance'] for p in povs]
    raise GateError('unknown probe channel %r' % name)


def recorded_povs(tr):
    """The trace's OWN POV, re-derived through the loader's convention."""
    out = []
    for i in range(tr.record_count):
        focus, rot = tr.focus(i), tr.rotation(i)
        eye = ct.eye_from_pov(focus, rot, tr.value(i, 'distance'))
        pov = ct.pov_from_eye_center(eye, focus, tr.value(i, 'fov'),
                                     tr.value(i, 'out_time'))
        if pov is None:
            return None
        out.append(pov)
    return out


def ours_povs(tr, frames):
    """Our replay's (Eye, Center, EffFov) expressed in the same POV shape."""
    out = []
    for i, fr in enumerate(frames):
        pov = ct.pov_from_eye_center(fr['eye'], fr['center'], fr['fov'],
                                     tr.value(i, 'out_time'))
        if pov is None:
            return None
        out.append(pov)
    return out


def channel_metrics(tr, povs):
    dts = list(tr.column('dt'))
    out = {}
    for name, unit, flat_eps, wrap in PROBE_CHANNELS:
        m = response_metrics(channel_series(povs, name), dts, flat_eps, wrap)
        m['unit'] = unit
        out[name] = m
    return out


def distance_probe(rows):
    """The diff tool's empirical Distance-semantics probe, reused verbatim."""
    try:
        probe = diff.distance_semantics(rows)
    except Exception:  # pragma: no cover - the call is pure python
        return False, 'probe failed'
    if probe['ok']:
        return True, 'median ratio %.3f in [0.5, 2.0]' % probe['ratio']
    return False, ('median |Eye-Center| %.1f uu vs recorded Distance %.1f uu '
                   '(ratio %.2f): not the same quantity. RL Distance is the RIG '
                   'distance, |Eye-Center| is the look-target distance '
                   '(Round 14). Camera::Pov() exposes the former, but the P2 '
                   'driver prints the GL pair.'
                   % (probe['med_ours'], probe['med_rec'], probe['ratio']))


# ── per-trace measurement (shared by CT2 and CT3) ──────────────────────────
def measure(entry, verbose=False):
    """Replay one live trace through our camera and reduce it to numbers."""
    tr = entry['trace']
    if diff is None:
        raise GateError('tools/decomp/camera_trace_diff.py is unusable (%s), so '
                        'our camera cannot be replayed' % _diff_err)
    ref_povs = recorded_povs(tr)
    if ref_povs is None:
        raise GateError('%s: the recorded POV has a frame with no view axis'
                        % entry['rel'])
    frames = diff.run_ours(tr, verbose=verbose)
    our_povs = ours_povs(tr, frames)
    if our_povs is None:
        raise GateError('%s: our replay produced a frame with no view axis'
                        % entry['rel'])
    rows, skipped = diff.compare(tr, frames)
    comparable, note = distance_probe(rows)
    fields = {}
    for key in ('focus_uu', 'distance_uu', 'rotation_mag', 'fov_deg'):
        vals = sorted(abs(r[key]) for r in rows)
        if vals:
            fields[key] = {'median': ct._median(vals), 'max': vals[-1]}
    return {
        'trace': entry['rel'],
        'scenario': entry['scenario'],
        'records': tr.record_count,
        'crc': trace_crc(tr),
        'dt_floor': ct._median(list(tr.column('dt'))),
        'reference': channel_metrics(tr, ref_povs),
        'ours': channel_metrics(tr, our_povs),
        'field_errors': fields,
        'distance_comparable': comparable,
        'distance_note': note,
        'skipped_frames': skipped,
    }


def _pct(a, b):
    return 100.0 * (a - b) / b if b else float('nan')


def ct2_findings(m):
    """Our step response vs the RECORDED one, per channel (two-sided 5%)."""
    findings = []
    dt_floor = m['dt_floor']
    for name, _unit, _flat, _wrap in PROBE_CHANNELS:
        o, r = m['ours'].get(name), m['reference'].get(name)
        if o is None or r is None:
            findings.append((None, 'skip', '%-12s no metrics' % name))
            continue
        if name == 'distance_uu' and not m['distance_comparable']:
            findings.append((None, 'UNVERIFIED', '%-12s %s'
                             % (name, m['distance_note'])))
            continue
        if o.get('flat') or r.get('flat'):
            findings.append((None, 'flat', '%-12s no step (%s)'
                             % (name, (o.get('reason') or r.get('reason')
                                       or 'flat'))))
            continue
        ok_s = (abs(o['settle_s'] - r['settle_s'])
                <= TOL * max(abs(r['settle_s']), dt_floor))
        ok_o = (abs(o['overshoot_pct'] - r['overshoot_pct'])
                <= TOL * max(abs(r['overshoot_pct']), OVER_FLOOR_PCT))
        rel = ('n/a (ref 0)' if r['settle_s'] <= 0.0
               else '%+6.1f%%' % _pct(o['settle_s'], r['settle_s']))
        findings.append((bool(ok_s and ok_o), 'ok' if (ok_s and ok_o) else 'FAIL',
                         '%-12s settle %6.3f s vs %6.3f s (%-10s)  '
                         'overshoot %6.2f%% vs %6.2f%% (%+6.2f pp)'
                         % (name, o['settle_s'], r['settle_s'], rel,
                            o['overshoot_pct'], r['overshoot_pct'],
                            o['overshoot_pct'] - r['overshoot_pct'])))
    return findings


def ct3_findings(m, base):
    """One-sided bounds against the STORED baseline: worse is a regression."""
    findings = []
    dt_floor = m['dt_floor']
    for name, _unit, _flat, _wrap in PROBE_CHANNELS:
        o = (m.get('ours') or {}).get(name)
        b = (base.get('ours') or {}).get(name)
        if o is None or b is None or o.get('flat') or b.get('flat'):
            findings.append((None, 'flat',
                             '%-12s no step (flat/missing)' % name))
            continue
        if name == 'distance_uu' and not m['distance_comparable']:
            findings.append((None, 'UNVERIFIED', '%-12s %s'
                             % (name, m['distance_note'])))
            continue
        cap_s = abs(b['settle_s']) * (1.0 + TOL) + dt_floor
        cap_o = (abs(b['overshoot_pct'])
                 + TOL * max(abs(b['overshoot_pct']), OVER_FLOOR_PCT))
        ok = o['settle_s'] <= cap_s and o['overshoot_pct'] <= cap_o
        findings.append((bool(ok), 'ok' if ok else 'FAIL',
                         '%-12s settle %6.3f s (cap %6.3f)   overshoot %6.2f%% '
                         '(cap %6.2f%%)'
                         % (name, o['settle_s'], cap_s, o['overshoot_pct'],
                            cap_o)))
    for key, floor in sorted(FIELD_FLOORS.items()):
        o = (m.get('field_errors') or {}).get(key)
        b = (base.get('field_errors') or {}).get(key)
        if o is None or b is None:
            findings.append((None, 'skip', '%-12s error field missing' % key))
            continue
        if key == 'distance_uu' and not m['distance_comparable']:
            findings.append((None, 'UNVERIFIED', '%-12s %s'
                             % (key, m['distance_note'])))
            continue
        cap_m = abs(b['median']) * (1.0 + TOL) + floor
        cap_x = abs(b['max']) * (1.0 + TOL) + floor
        ok = o['median'] <= cap_m and o['max'] <= cap_x
        findings.append((bool(ok), 'ok' if ok else 'FAIL',
                         '%-12s median %.4f (cap %.4f)   max %.4f (cap %.4f)'
                         % (key, o['median'], cap_m, o['max'], cap_x)))
    return findings


def emit(findings, cid, scenario, say):
    """Print findings, return the status they imply (FAIL outranks PASS)."""
    status = 'PASS'
    for ok, tag, text in findings:
        if ok is False:
            status = 'FAIL'
        say('    %-10s %s %-24s %s' % (tag, cid, scenario, text))
    return status


# ── baseline io ────────────────────────────────────────────────────────────
def load_baseline(path):
    if not os.path.exists(path):
        return None
    try:
        with open(path) as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        raise GateError('baseline %s is unreadable (%s)' % (path, e))
    if doc.get('format') != BASELINE_FORMAT:
        raise GateError('baseline %s is %r, expected %r'
                        % (path, doc.get('format'), BASELINE_FORMAT))
    if doc.get('version') != BASELINE_VERSION:
        raise GateError('baseline %s is version %r, this gate writes %d'
                        % (path, doc.get('version'), BASELINE_VERSION))
    if not isinstance(doc.get('traces'), dict):
        raise GateError('baseline %s has no "traces" object' % path)
    return doc


def write_baseline(path, measured, tol):
    doc = {'format': BASELINE_FORMAT,
           'version': BASELINE_VERSION,
           'created_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
           'tolerance': tol,
           'note': ('CT3 reference for tools/verify/camera_trace_gate.py. '
                    'Derived from live traces under decomp/traces/ (gitignored, '
                    'Rule 1). Re-run --write-baseline only when the recorded '
                    'input changed on purpose.'),
           'traces': {m['scenario']: m for m in measured}}
    d = os.path.dirname(os.path.abspath(path))
    if d and not os.path.isdir(d):
        os.makedirs(d, exist_ok=True)
    with open(path, 'w') as f:
        json.dump(doc, f, indent=2, sort_keys=True)
        f.write('\n')
    return doc


# ── main ───────────────────────────────────────────────────────────────────
def main(argv=None):
    ap = argparse.ArgumentParser(
        description='CT1-CT3 camera trace gate (local tier; SKIPs loudly)')
    ap.add_argument('--tracedir', default=ct.DEFAULT_TRACE_DIR,
                    help='trace home (default: the gitignored decomp/traces/)')
    ap.add_argument('--baseline', default=None,
                    help='baseline json (default: <tracedir>/%s)' % BASELINE_NAME)
    ap.add_argument('--write-baseline', action='store_true',
                    help='(operator action) record the CT3 baseline from the live '
                         'traces; refuses without a recorded trace')
    ap.add_argument('--json', action='store_true',
                    help='emit one JSON document on stdout (human lines on stderr)')
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args(argv)

    tracedir = os.path.abspath(a.tracedir)
    baseline_path = (os.path.abspath(a.baseline) if a.baseline
                     else os.path.join(tracedir, BASELINE_NAME))
    out = sys.stderr if a.json else sys.stdout

    def say(msg=''):
        print(msg, file=out)

    doc = {'tool': 'camera_trace_gate', 'tier': 'local',
           'trace_dir': tracedir, 'baseline': baseline_path,
           'tolerance': TOL, 'traces': {}, 'checks': [],
           'verdict': 'SKIP'}
    checks = []

    def record(cid, status, detail, **extra):
        checks.append(dict(id=cid, status=status, detail=detail, **extra))

    entries = scan(tracedir)
    live = [e for e in entries if e['live'] and e['trace'] is not None]
    synthetic = [e for e in entries if e['trace'] is not None and not e['live']]
    broken = [e for e in entries if e['error']]
    doc['traces'] = {'total': len(entries), 'live': len(live),
                     'synthetic': len(synthetic), 'broken': len(broken)}
    shown_dir = (os.path.relpath(tracedir, REPO)
                 if tracedir == REPO or tracedir.startswith(REPO + os.sep)
                 else tracedir)
    say('camera_trace_gate: traces=%s (%d live, %d synthetic, %d broken)'
        % (shown_dir, len(live), len(synthetic), len(broken)))
    say('  canonical trace home: decomp/traces/ (gitignored; never a root /decomp)')
    for e in broken:
        say('  BROKEN %-46s %s' % (e['rel'], e['error']))

    # ── CT1 — self-replay exact ────────────────────────────────────────────
    if not entries:
        say('  SKIP CT1 self-replay exact — no trace under %s' % shown_dir)
        record('CT1', 'SKIP', 'no trace under %s' % shown_dir)
    else:
        results = []
        tmpdir = tempfile.mkdtemp(prefix='cam-trace-ct1-')
        try:
            for e in entries:
                if e['trace'] is None:
                    continue
                r = ct1_self_replay(e, tmpdir)
                results.append(r)
                say('  %s CT1 self-replay exact %-38s %4d rec  field max %.6f  '
                    'POV max %.6f'
                    % ('PASS' if r['ok'] else 'FAIL', e['rel'], r['records'],
                       r['field_max'], r['pov_max']))
                if not r['ok']:
                    say('       reason: %s' % r['reason'])
        finally:
            try:
                for name in os.listdir(tmpdir):
                    os.unlink(os.path.join(tmpdir, name))
                os.rmdir(tmpdir)
            except OSError:
                pass
        if not results:
            # Nothing loadable: the BROKEN lines above are the finding, and CT1
            # has no subject. A stray partial download must not red a clone.
            say('  SKIP CT1 self-replay exact — %d file(s), none loadable'
                % len(broken))
            record('CT1', 'SKIP', 'no loadable trace (%d broken)' % len(broken))
        else:
            ok = all(r['ok'] for r in results)
            status = 'PASS' if ok else 'FAIL'
            scope = ('synthetic fixtures only — format plumbing, NOT a claim '
                     'about our camera' if not live
                     else '%d trace(s)' % len(results))
            record('CT1', status, 'self-replay bit-exact on %d trace(s) (%s)'
                   % (len(results), scope), traces=results)
            say('  %s CT1 %d trace(s): self-replay bit-exact (%s)'
                % (status, len(results), scope))

    # ── CT2 / CT3 — need a RECORDED trace ──────────────────────────────────
    if not live:
        why = ('no recorded trace — %d trace(s) under %s all declare '
               'source="synthetic"' % (len(synthetic), shown_dir) if entries
               else 'no trace under %s' % shown_dir)
        say('  SKIP CT2 exact-feel settle/overshoot — %s' % why)
        say('  SKIP CT3 no-regression vs %s — %s' % (BASELINE_NAME, why))
        record('CT2', 'SKIP', why + '; exact feel needs the recorded response')
        record('CT3', 'SKIP', why + '; a baseline needs a recorded trace')
    elif diff is None:
        why = 'replay tooling unusable (%s)' % _diff_err
        say('  SKIP CT2/CT3 — %s' % why)
        record('CT2', 'SKIP', why)
        record('CT3', 'SKIP', why)
    else:
        measured = []
        for e in live:
            try:
                measured.append(measure(e, verbose=a.verbose))
            except (GateError, ct.TraceError) as ex:
                say('  FAIL CT2/CT3 measure %s: %s' % (e['rel'], ex))
                record('CT2', 'FAIL', '%s: %s' % (e['rel'], ex))
                record('CT3', 'FAIL', '%s: %s' % (e['rel'], ex))
        if measured:
            if a.write_baseline:
                doc_b = write_baseline(baseline_path, measured, TOL)
                say('  WROTE baseline %s (%d scenario(s))'
                    % (baseline_path, len(doc_b['traces'])))
            base = load_baseline(baseline_path)

            status2 = 'PASS'
            for m in measured:
                st = emit(ct2_findings(m), 'CT2', m['scenario'], say)
                if st == 'FAIL':
                    status2 = 'FAIL'
            record('CT2', status2,
                   'per-channel settle/overshoot within %.0f%% of the recorded '
                   'response, %d scenario(s)' % (100 * TOL, len(measured)),
                   measured=measured)

            findings3 = []
            baselined = []
            for m in measured:
                b = (base or {}).get('traces', {}).get(m['scenario'])
                if b is None:
                    say('    %-10s %s %-24s SKIP — not baselined (run '
                        '--write-baseline)' % ('SKIP', 'CT3', m['scenario']))
                    continue
                if b.get('crc') != m['crc']:
                    say('    %-10s %s %-24s SKIP — recorded input changed '
                        '(crc %s -> %s): re-run --write-baseline'
                        % ('SKIP', 'CT3', m['scenario'], b.get('crc'), m['crc']))
                    continue
                baselined.append((m, b))
            findings3 = []
            for m, b in baselined:
                findings3.append((m['scenario'], ct3_findings(m, b)))
            if not baselined:
                why = ('no live trace matches the stored baseline %s'
                       % baseline_path if base
                       else 'no baseline file at %s' % baseline_path)
                say('  SKIP CT3 no-regression — %s' % why)
                record('CT3', 'SKIP', why)
            else:
                status3 = 'PASS'
                for scenario, fs in findings3:
                    st = emit(fs, 'CT3', scenario, say)
                    if st == 'FAIL':
                        status3 = 'FAIL'
                record('CT3', status3,
                       'no regression vs %s over %d scenario(s)'
                       % (os.path.basename(baseline_path), len(baselined)),
                       measured=measured)

    # ── summary ────────────────────────────────────────────────────────────
    doc['checks'] = checks
    counts = {'PASS': 0, 'FAIL': 0, 'SKIP': 0}
    for c in checks:
        counts[c['status']] = counts.get(c['status'], 0) + 1
    doc['summary'] = counts
    doc['verdict'] = ('FAIL' if counts['FAIL']
                      else 'SKIP' if counts['SKIP'] else 'PASS')
    say('')
    say('camera_trace_gate: %d PASS / %d FAIL / %d SKIP — %s'
        % (counts['PASS'], counts['FAIL'], counts['SKIP'], doc['verdict']))
    if doc['verdict'] == 'SKIP':
        say('  This is the LOCAL tier by design: CT2/CT3 grade exact feel against')
        say('  a RECORDED Rocket League trace, and only synthetic fixtures exist')
        say('  here — so a public clone stays green. To make it real, record the')
        say('  §6.2 deck offline (P2: an instance the recorder launches itself)')
        say('  into decomp/traces/, then run --write-baseline once.')
    if a.json:
        print(json.dumps(doc, indent=2, sort_keys=True))
    return 1 if doc['verdict'] == 'FAIL' else 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except GateError as e:
        print('camera_trace_gate: ERROR — %s' % e)
        sys.exit(2)
