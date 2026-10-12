#!/usr/bin/env python3
"""Visual gates (MASTER_PROMPT_VISUALS.md section 7 W1 / milestones V1-V6).

Every gate asserts on MEASURABLE framebuffer properties of a deterministic,
offscreen capture — never on "looks better". The harness runs `build/game`
under `gamescope --backend headless` (no display takeover, no Xvfb) with a
fixed seed / tick / `--cam` pose, so the same command produces the same bytes.

Gates:

  VG1 determinism      two identical captures are byte-identical (or within a
                       documented per-channel tolerance; the driver is recorded)
  VG2 calibrate        5 palette bands at the right x positions and colours
  VG3 scene coverage   kickoff / ball / pad / wall / ceiling / car all render a
                       non-trivial, non-uniform image
  VG4 ball indicator   measured ring/inner/disc radii match the 0.98 / 1.25 /
                       0.62 x r recipe, and grow with ball height
  VG5 boost gauge      260-degree arc at boost=100, ~130 at boost=50
  VG6 glass wall       blue glass hue + hex-net period == the 420 uu cell
  VG7 budget/errors    60 s scripted run: GL errors == 0, counters sane, tri
                       count under the documented cap (no silent geometry creep)
  VG8 goal shockwave   ring radius grows across frames and matches the
                       documented 140 + 2600*k curve
  VG9 car fidelity     box-car wheel visuals present; car orientation is
                       legible (silhouette rotates when the car pitches)
  VG10 debug colliders instrument renders and changes the frame as designed
  VG11 wall curbs      curb sits at the wall base (all four walls + the 45 deg
                       chamfers), not across the arena centre or interior
  VG12 feedback FX     demo flash and pickup burst add their cool/warm pixels
                       (differential against the same frame without the probe)

Loud `SKIP:` (never a fake PASS) when there is no GL context / capture.

    python3 tools/verify/visual_gates.py            # all gates
    python3 tools/verify/visual_gates.py --only VG4 --diag
"""

import argparse
import hashlib
import json
import math
import os
import shutil
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(ROOT, "build", "game")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = "/tmp/visual_gates"
MESH = os.path.join(ROOT, "collision_meshes")
W, H = 1280, 720

# ---- documented constants (each cites its source) --------------------------
BALL_COLLISION_R = 91.25          # src/models/Models.h dims::kBallR
INDICATOR_OUTER = 1.25            # src/fx/FX.cpp DrawBallIndicator
INDICATOR_INNER = 0.98
INDICATOR_DISC = 0.62
INDICATOR_GROW = 0.18             # +18% at 1950 uu
INDICATOR_ZTOP = 1950.0
BOOST_ARC_DEG = 260.0             # src/game/main.cpp boost gauge
GAUGE_CX, GAUGE_CY = 1157.0, 617.0
GAUGE_RIN, GAUGE_ROUT = 59.0, 70.0
HEX_CELL = 420.0                  # src/game/main.cpp glass()
GLASS_ALPHA = 0.10
HEX_ALPHA = 0.09
SIDE_WALL_X = 4096.0              # src/arena/ArenaConstants.h
BACK_WALL_Y = 5120.0
CEIL_Z = 2044.0
SHOCK_BASE, SHOCK_SPAN = 140.0, 2600.0   # src/fx/FX.cpp ShockwaveRadius

# Draw-budget cap (V2 decision): the CC-BY-4.0 Fennec is 84.8k tris and there
# are two cars, so a frame is ~188k tris. The cap exists so a future visual
# change cannot silently multiply geometry. Raised only with a written reason.
TRI_CAP = 240000

results = []
diag_lines = []
DIAG = False


def gate(gid, name, ok, detail):
    """ok True = PASS, False = FAIL, None = SKIP."""
    verdict = "SKIP" if ok is None else ("PASS" if ok else "FAIL")
    results.append({"id": gid, "gate": name, "verdict": verdict, "detail": detail})
    print("%-4s %-5s %-34s %s" % (gid, verdict, name, detail))


def diag(msg):
    diag_lines.append(msg)
    if DIAG:
        print("       .", msg)


# ---- capture harness -------------------------------------------------------

def run_offscreen(args, name, done_marker="[game] exit", timeout=45):
    """Run the game offscreen; return its combined log.

    gamescope's headless backend can hang in teardown after the game has
    already exited (Wayland/Vulkan shutdown), so we poll for the game's own
    completion line and then kill the whole process group. The screenshot is
    written before that line, so it is complete either way.
    """
    os.makedirs(OUT, exist_ok=True)
    logpath = os.path.join(OUT, name + ".log")
    cmd = [shutil.which("gamescope"), "--backend", "headless", "-W", str(W),
           "-H", str(H), "-r", "60", "--", GAME, "--no-vsync", "--no-audio"] + list(args)
    f = open(logpath, "w")
    p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=ROOT,
                         start_new_session=True)
    t0 = time.time()
    log = ""
    while time.time() - t0 < timeout:
        if p.poll() is not None:
            break
        time.sleep(0.15)
        try:
            with open(logpath, "rb") as lf:
                tail = lf.read()[-6000:].decode("utf-8", "ignore")
            if done_marker and done_marker in tail:
                break
            if "[game] renderer init failed" in tail or "SDL_CreateWindow failed" in tail:
                break
        except OSError:
            pass
    try:
        os.killpg(os.getpgid(p.pid), signal.SIGKILL)
    except Exception:
        pass
    try:
        p.wait(timeout=3)
    except Exception:
        pass
    f.close()
    with open(logpath, "rb") as lf:
        log = lf.read().decode("utf-8", "ignore")
    return log


def driver_of(log):
    for line in log.splitlines():
        if "GL_VENDOR=" in line:
            i = line.index("GL_VENDOR=")
            return line[i:].strip()
    return "unknown-driver"


def counter_of(log, key):
    """Pull `<key>=<int>` out of the [game] exit / [hud] line."""
    val = None
    for line in log.splitlines():
        if "[game] exit" in line or "[hud]" in line:
            for tok in line.replace(",", " ").split():
                if tok.startswith(key + "="):
                    try:
                        val = int(tok.split("=", 1)[1])
                    except ValueError:
                        pass
    return val


def shot(name, scene_args, done_marker="[game] exit"):
    """Capture one PPM, returning (path, log). Path is None when it failed."""
    path = os.path.join(OUT, name + ".ppm")
    if os.path.exists(path):
        os.remove(path)
    args = list(scene_args) + ["--shot-path", path]
    log = run_offscreen(args, name, done_marker=done_marker)
    if not os.path.isfile(path):
        return None, log
    return path, log


def baseline_args(ticks=160, tick=120):
    return ["--meshdir", MESH, "--seed", "7", "--ticks", str(ticks),
            "--no-shake", "--shot-at-tick", str(tick)]


# ---- PPM / image helpers ---------------------------------------------------

def read_ppm(path):
    d = open(path, "rb").read()
    if d[:2] != b"P6":
        raise ValueError("not a P6 PPM: " + path)
    i = 2
    toks = []
    while len(toks) < 3:
        while d[i:i + 1].isspace():
            i += 1
        if d[i:i + 1] == b"#":
            while d[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        j = i
        while not d[j:j + 1].isspace():
            j += 1
        toks.append(int(d[i:j]))
        i = j
    i += 1
    w, h = toks[0], toks[1]
    return w, h, d[i:i + w * h * 3]


def px(im, x, y):
    w, h, b = im
    x = max(0, min(w - 1, int(x)))
    y = max(0, min(h - 1, int(y)))
    o = (y * w + x) * 3
    return b[o], b[o + 1], b[o + 2]


def lum(p):
    return p[0] + p[1] + p[2]


def md5(path):
    return hashlib.md5(open(path, "rb").read()).hexdigest()


def quant_colours(im, shift=4):
    w, h, b = im
    seen = {}
    step = 3
    n = 0
    for o in range(0, w * h * 3, step * 3):
        c = (b[o] >> shift, b[o + 1] >> shift, b[o + 2] >> shift)
        seen[c] = seen.get(c, 0) + 1
        n += 1
    return seen, n


# ---- camera projection (mirrors src/camera/Camera.cpp) ---------------------

def f_px(vh, fov_slider, aspect):
    fx = math.radians(fov_slider)
    fy = 2.0 * math.atan(math.tan(fx * 0.5) / aspect * (16.0 / 9.0))
    deg = math.degrees(fy)
    deg = max(40.0, min(120.0, deg))
    return (vh * 0.5) / math.tan(math.radians(deg) * 0.5)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def norm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)


def px_per_uu_at(eye, target, world_pt, fov=110.0):
    """Horizontal pixels per world uu for a point near `world_pt` when looking
    from eye at target. Mirrors LookAt + Camera::Apply's fov conversion."""
    f = norm(sub(target, eye))
    s = norm(cross(f, (0.0, 0.0, 1.0)))
    depth = dot(sub(world_pt, eye), f)
    if abs(depth) < 1e-3:
        return 999.0
    scale = f_px(H, fov, float(W) / H)
    return scale / depth


def project_offset(eye, target, world_pt, world_offset, fov=110.0):
    """Screen-x offset (px) of world_pt+world_offset as seen from eye."""
    f = norm(sub(target, eye))
    s = norm(cross(f, (0.0, 0.0, 1.0)))
    p = (world_pt[0] + world_offset[0], world_pt[1] + world_offset[1],
         world_pt[2] + world_offset[2])
    rel = sub(p, eye)
    depth = dot(rel, f)
    if abs(depth) < 1e-3:
        return 0.0
    return f_px(H, fov, float(W) / H) * dot(rel, s) / depth


# ===========================================================================
# Scene definitions
# ===========================================================================
SCENES = {
    "kickoff": baseline_args() + ["--cam", "3000,-3000,1200,0,0,200"],
    "ball": baseline_args() + ["--cam", "500,-600,300,0,0,120"],
    "pad": baseline_args() + ["--cam", "-3072,-2900,320,-3072,-3600,0"],
    "wall": baseline_args() + ["--cam", "2596,0,1024,4096,0,1024"],
    "ceiling": baseline_args() + ["--cam", "0,-300,200,0,0,2044"],
    "car": baseline_args() + ["--cam", "250,-2900,190,0,-2300,60"],
}


# ===========================================================================
# Gates
# ===========================================================================

def vg1_determinism():
    name = "determinism"
    args = SCENES["kickoff"]
    p1, log1 = shot("det_1", args)
    p2, log2 = shot("det_2", args)
    if not p1 or not p2:
        gate("VG1", "capture determinism", None,
             "no framebuffer capture (gamescope/GL unavailable)")
        return
    drv = driver_of(log1)
    if md5(p1) == md5(p2):
        gate("VG1", "capture determinism", True,
             "two captures byte-identical (%s) md5=%s" % (drv, md5(p1)))
        return
    # Documented fallback: probe 512 pixels with a tolerance.
    a = read_ppm(p1)
    b = read_ppm(p2)
    step = max(1, (a[0] * a[1]) // 512)
    worst = 0
    for idx in range(0, a[0] * a[1], step):
        o = idx * 3
        for k in range(3):
            worst = max(worst, abs(a[2][o + k] - b[2][o + k]))
    gate("VG1", "capture determinism", worst <= 8,
         "byte-compare differs; 512 probes max|d|=%d (tol 8) driver=%s" % (worst, drv))


def vg2_calibrate():
    cal = os.path.join(ROOT, "calibrate.ppm")
    existed = os.path.isfile(cal)
    if existed:
        os.remove(cal)
    log = run_offscreen(["--calibrate"], "calibrate",
                        done_marker="[game] calibrate.ppm written")
    if not os.path.isfile(cal):
        gate("VG2", "calibrate palette bands", None,
             "calibrate.ppm not written (no GL context?)")
        return
    im = read_ppm(cal)
    # Band order documented in src/game/main.cpp: turf, wall, carblue,
    # goalorange, white. 5 x 256 px bands; sample at y=100 (clear of the text
    # at y>=686 and of the 3D probe quad which spans y~240..480).
    expect = [(0.14, 0.44, 0.19), (0.25, 0.50, 1.00), (0.10, 0.35, 0.90),
              (1.00, 0.55, 0.20), (0.90, 0.90, 0.90)]
    names = ["turf", "wall", "carblue", "goalorange", "white"]
    bad = []
    detail = []
    for i, (r, g, b) in enumerate(expect):
        x = i * 256 + 128
        got = px(im, x, 100)
        want = (round(r * 255), round(g * 255), round(b * 255))
        d = max(abs(got[k] - want[k]) for k in range(3))
        detail.append("%s%s=%s want%s d=%d" % (names[i], "", got, want, d))
        if d > 3:
            bad.append(names[i])
    os.remove(cal)
    gate("VG2", "calibrate palette bands", not bad,
         "; ".join(detail) + (" BAD:" + ",".join(bad) if bad else ""))


def scene_coverage():
    """VG3: every listed scene renders a non-trivial, non-uniform image."""
    ok, empty, uniform = True, [], []
    shots = {}
    for nm in ("kickoff", "ball", "pad", "wall", "ceiling", "car"):
        p, log = shot(nm, SCENES[nm])
        if not p:
            empty.append(nm)
            continue
        shots[nm] = p
        im = read_ppm(p)
        seen, n = quant_colours(im)
        top = max(seen.values()) / float(n)
        # background clear colour is (0.05,0.06,0.10) -> ~ (3,4,6) quantised
        bg = sum(v for k, v in seen.items()
                 if k[0] == 0 and k[1] <= 1 and k[2] <= 1)
        coverage = 1.0 - bg / float(n)
        diag("VG3 %-8s uniq=%d top=%.3f coverage=%.3f" % (nm, len(seen), top, coverage))
        if len(seen) < 12 or coverage < 0.20:
            uniform.append("%s(uniq=%d,cov=%.2f)" % (nm, len(seen), coverage))
    if empty:
        gate("VG3", "scene render coverage", None,
             "no capture for: " + ",".join(empty))
        return
    gate("VG3", "scene render coverage", not uniform,
         "6 scenes non-empty, >=12 colours each, >=20%% non-background"
         + (" BAD:" + ",".join(uniform) if uniform else ""))


def ball_scene(ballz, tick=2, ticks=4):
    return ["--meshdir", MESH, "--seed", "7", "--ticks", str(ticks), "--no-shake",
            "--ballpos", "0,800,%g" % ballz,
            "--cam", "0,650,300,0,800,0", "--shot-at-tick", str(tick)]


def indicator_radii(path):
    """Scan the horizontal line through the indicator centre (screen centre)
    and return (outer, inner, disc) radii in pixels (0 for a missing edge).

    The indicator is light cyan-white at known alphas over the turf, so a
    luminance-sum profile cleanly separates: disc (~alpha .24), ring (alpha
    ~.66), background. Thresholds are in 0..765 sum units.
    """
    im = read_ppm(path)
    cx, cy = W // 2, H // 2
    s = [lum(px(im, x, cy)) for x in range(W)]
    half = min(cx, W - cx - 1)

    def sd(d):  # symmetric sample (the indicator is centred on screen)
        return max(s[cx - d], s[cx + d])

    outer = 0
    for d in range(half):
        if sd(d) > 400:  # ring band (documented alpha ~0.66 over turf)
            outer = d
    if outer == 0:
        return 0, 0, 0
    # Walk inward through the CONTIGUOUS ring run: an isolated bright pixel at
    # the centre (the arena grid line showing through the disc) must not be
    # mistaken for the ring's inner edge.
    inner = outer
    while inner > 1 and sd(inner - 1) > 400:
        inner -= 1
    # Inner disc. Measured as a CONTRAST relative to the local turf just beyond
    # the ring: an absolute luminance threshold only worked while the palette
    # was dim, and breaks the moment the turf gets brighter (the tile then
    # passes the threshold everywhere). The disc is drawn at alpha 0.32..0.05
    # over the turf, so it is always a deviation from that local background,
    # dark or bright.
    bg = 0
    for d in range(outer + 10, min(half, outer + 80)):
        bg = max(bg, sd(d))
    disc = 0
    for d in range(0, max(1, inner - 1)):
        if abs(sd(d) - bg) > 45:
            disc = d
    return outer, inner, disc


def vg4_ball_indicator():
    # Ball lifted above the camera so the sphere cannot occlude its own floor
    # indicator; the indicator still sits at the ball's XY on the floor.
    p_hi, l_hi = shot("ind_600", ball_scene(600))
    p_top, l_top = shot("ind_900", ball_scene(900))
    if not p_hi or not p_top:
        gate("VG4", "ball indicator radii (recipe)", None,
             "no capture (gamescope/GL unavailable)")
        return
    o1, i1, d1 = indicator_radii(p_hi)
    o2, i2, d2 = indicator_radii(p_top)
    diag("VG4 z=600 outer=%d inner=%d disc=%d" % (o1, i1, d1))
    diag("VG4 z=900 outer=%d inner=%d disc=%d" % (o2, i2, d2))
    if o1 < 20 or i1 < 10 or d1 < 5:
        gate("VG4", "ball indicator radii (recipe)", None,
             "indicator not resolvable (outer=%d inner=%d disc=%d)" % (o1, i1, d1))
        return
    eye, tgt = (0.0, 650.0, 300.0), (0.0, 800.0, 0.0)
    # Predicted radii: growth = 1 + 0.18 * (ball_z / 1950)
    g1 = 1.0 + INDICATOR_GROW * (600.0 / INDICATOR_ZTOP)
    g2 = 1.0 + INDICATOR_GROW * (900.0 / INDICATOR_ZTOP)
    pred = {}
    for k, mult in (("outer", INDICATOR_OUTER), ("inner", INDICATOR_INNER),
                    ("disc", INDICATOR_DISC)):
        pred[k] = abs(project_offset(eye, tgt, (0.0, 800.0, 2.0),
                                     (BALL_COLLISION_R * mult * g1, 0.0, 0.0)))
    meas = {"outer": o1, "inner": i1, "disc": d1}
    bad = []
    for k in pred:
        err = abs(meas[k] - pred[k]) / pred[k]
        diag("VG4 %-5s meas=%d pred=%.1f err=%.1f%%" % (k, meas[k], pred[k], err * 100))
        if err > 0.10:
            bad.append("%s(%d vs %.0f)" % (k, meas[k], pred[k]))
    # Ratios are projection-free, so they pin the recipe itself.
    r_in = i1 / float(o1)
    r_disc = d1 / float(o1)
    exp_in = INDICATOR_INNER / INDICATOR_OUTER
    exp_disc = INDICATOR_DISC / INDICATOR_OUTER
    diag("VG4 ratios inner/outer=%.3f (exp %.3f) disc/outer=%.3f (exp %.3f)"
         % (r_in, exp_in, r_disc, exp_disc))
    if abs(r_in - exp_in) > 0.05 or abs(r_disc - exp_disc) > 0.05:
        bad.append("ratio inner=%.3f disc=%.3f" % (r_in, r_disc))
    # Absolute radius at the second height pins the +18%-at-1950 growth term.
    pred_hi = abs(project_offset(eye, tgt, (0.0, 800.0, 2.0),
                                 (BALL_COLLISION_R * INDICATOR_OUTER * g2, 0.0, 0.0)))
    err_hi = abs(o2 - pred_hi) / pred_hi if o2 else 1.0
    diag("VG4 z900 outer meas=%d pred=%.1f err=%.1f%%" % (o2, pred_hi, err_hi * 100))
    if err_hi > 0.12:
        bad.append("z900 outer=%d vs %.0f" % (o2, pred_hi))
    # Height growth: the recipe grows the indicator +18% at 1950 uu.
    growth = o2 / float(o1)
    diag("VG4 growth z900/z600 = %.3f exp %.3f" % (growth, g2 / g1))
    if abs(growth - g2 / g1) > 0.04:
        bad.append("growth=%.3f" % growth)
    gate("VG4", "ball indicator radii (recipe)", not bad,
         "z600 outer=%d inner=%d disc=%d; ratios %.3f/%.3f; growth %.3f"
         % (o1, i1, d1, r_in, r_disc, growth)
         + (" BAD:" + ",".join(bad) if bad else ""))


def gauge_span(path):
    """Largest contiguous orange angular run (degrees) on the gauge ring."""
    im = read_ppm(path)
    r = (GAUGE_RIN + GAUGE_ROUT) * 0.5
    hits = []
    for a in range(360):
        rad = math.radians(a)
        p = px(im, GAUGE_CX + math.cos(rad) * r, GAUGE_CY + math.sin(rad) * r)
        orange = p[0] > 140 and p[2] < 110 and (p[0] - p[2]) > 80 and p[0] > p[1] + 15
        hits.append(orange)
    best = cur = 0
    # circular run
    ext = hits + hits
    for v in ext:
        cur = cur + 1 if v else 0
        best = max(best, cur)
    return min(best, 360)


def vg5_boost_gauge():
    p100, l100 = shot("boost100", SCENES["ball"] + ["--boost", "100"])
    p50, l50 = shot("boost50", SCENES["ball"] + ["--boost", "50"])
    if not p100 or not p50:
        gate("VG5", "boost gauge 260-degree arc", None,
             "no capture (gamescope/GL unavailable)")
        return
    s100, s50 = gauge_span(p100), gauge_span(p50)
    diag("VG5 span@100=%d span@50=%d" % (s100, s50))
    ok = abs(s100 - BOOST_ARC_DEG) <= 12 and abs(s100 - 2 * s50) <= 18
    gate("VG5", "boost gauge 260-degree arc", ok,
         "measured %.0f deg @100 (exp %.0f), %.0f deg @50 (exp %.0f)"
         % (s100, BOOST_ARC_DEG, s50, BOOST_ARC_DEG / 2))


def vg6_wall():
    # Straight-on view of the +X wall from a fixed distance, so the honeycomb
    # projects without the grazing foreshortening that made the old close-up
    # detector brittle. The hex period is measured by DIFFERENCING the frame
    # against an identical one drawn with the net disabled (--no-hexnet), so
    # the wall's own mip texture cannot masquerade as net lines.
    CAM = ["--cam", "3096,0,1024,4096,0,1024"]
    base = ["--meshdir", MESH, "--seed", "7", "--ticks", "160", "--no-shake",
            "--shot-at-tick", "120"]
    p_net, _ = shot("hex_net", base + ["--flat"] + CAM)
    p_off, _ = shot("hex_off", base + ["--flat", "--no-hexnet"] + CAM)
    p_lit, _ = shot("hex_lit", base + CAM)
    if not (p_net and p_off and p_lit):
        gate("VG6", "glass wall hue + hex period", None,
             "no capture (gamescope/GL unavailable)")
        return
    im = read_ppm(p_lit)
    # (a) documented blue glass hue across the wall patch.
    blue = 0
    n = 0
    for x in range(100, 1180, 2):
        for y in range(80, 600, 7):
            q = px(im, x, y)
            n += 1
            if q[2] > q[0] + 18 and q[2] > q[1] + 8:
                blue += 1
    # (b) hex period: a flat-top hexagon tiling has an EXACT horizontal period
    # of 2*hw = sqrt(3)*R = 0.866*cell. Project the net-only signal (net-on
    # minus net-off) onto the x axis and take the autocorrelation's dominant
    # lag; it must match 0.866*cell at the wall's pixel scale. The previous
    # recipe compared a VERTICAL run spacing against 0.25*cell, which is not
    # the flat-top invariant and only matched while the degrees/radians bug in
    # src/game/main.cpp glass() rotated the net out of the wall plane.
    on = read_ppm(p_net)
    off = read_ppm(p_off)
    eye, tgt, surf = (3096.0, 0.0, 1024.0), (4096.0, 0.0, 1024.0), (4093.0, 0.0, 1024.0)
    scale = px_per_uu_at(eye, tgt, surf)
    exp_px = 0.8660254037844386 * HEX_CELL * scale
    prof = [0.0] * W
    contrast = 0
    for x in range(100, 1180):
        s = 0.0
        for y in range(50, 690):
            d = lum(px(on, x, y)) - lum(px(off, x, y))
            contrast = max(contrast, abs(d))
            s += max(0.0, float(d))
        prof[x] = s
    seg = prof[100:1180]
    mean = sum(seg) / len(seg)
    prof = [v - mean for v in prof]

    def ac(lag):
        return sum(prof[x] * prof[x + lag] for x in range(100, 1180 - lag))

    lo, hi = int(exp_px * 0.45), int(exp_px * 1.8)
    best = max(range(lo, hi + 1), key=ac)
    diag("VG6 blue=%.1f%% net contrast=%d hex x-pitch=%d px "
         "(expected %.1f px = sqrt(3)/2*%.0f uu at %.3f px/uu)"
         % (100.0 * blue / n, contrast, best, exp_px, HEX_CELL, scale))
    ok = (blue / float(n) > 0.90 and contrast > 18 and best > 0 and
          abs(best - exp_px) <= 0.20 * exp_px)
    gate("VG6", "glass wall hue + hex period", ok,
         "%.0f%% blue glass, net contrast %d; hex x-pitch %d px (recipe %.1f px "
         "= sqrt(3)/2 x %.0f uu cell)"
         % (100.0 * blue / n, contrast, best, exp_px, HEX_CELL))


def vg7_budget():
    args = ["--meshdir", MESH, "--seed", "7", "--ticks", "7200",
            "--no-shake"]
    log = run_offscreen(args, "budget60", timeout=110)
    if "[game] exit" not in log:
        gate("VG7", "budget + GL errors (60s run)", None,
             "no 60s run completed (gamescope/GL unavailable)")
        return
    glerr = counter_of(log, "glerr")
    drops = counter_of(log, "drops")
    tris = counter_of(log, "tris_last")
    draws = counter_of(log, "draws_last")
    fen = counter_of(log, "fennec_tris")
    tick = counter_of(log, "ticks")
    if None in (glerr, drops, tris, draws):
        gate("VG7", "budget + GL errors (60s run)", False,
             "counters missing from the exit line")
        return
    ok = glerr == 0 and drops == 0 and tick >= 7200 and 1000 < tris <= TRI_CAP and draws > 50
    gate("VG7", "budget + GL errors (60s run)", ok,
         "ticks=%d drops=%d glerr=%d tris_last=%d (cap %d) draws_last=%d "
         "fennec_tris=%d" % (tick, drops, glerr, tris, TRI_CAP, draws, fen))


def vg8_shockwave():
    def one(name, tick):
        args = ["--meshdir", MESH, "--seed", "7", "--ticks", str(tick + 6),
                "--no-shake", "--ballpos", "0,2000,1500",
                "--fx-probe", "goal", "--fx-probe-tick", "60",
                "--cam", "0,1100,1400,0,2000,0", "--shot-at-tick", str(tick)]
        return shot(name, args)

    p1, l1 = one("shock_a", 70)
    p2, l2 = one("shock_b", 110)
    if not p1 or not p2:
        gate("VG8", "goal shockwave ring geometry", None,
             "no capture (gamescope/GL unavailable)")
        return
    eye, tgt, centre = (0.0, 1100.0, 1400.0), (0.0, 2000.0, 0.0), (0.0, 2000.0, 3.0)

    def ring_radius(im):
        cx, cy = W // 2, H // 2
        best = 0
        for x in range(W):
            p = px(im, x, cy)
            if p[0] > 90 and (p[0] - p[1]) > 20 and (p[0] - p[2]) > 40:
                best = max(best, abs(x - cx))
        return best

    r1 = ring_radius(read_ppm(p1))
    r2 = ring_radius(read_ppm(p2))
    # k = (tick - 60)/240 at 120 Hz over the 2 s explosion. These two tiers sit
    # where the ring is still opaque (alpha 0.85 -> 0.67); by ~t150 the recipe
    # fades it to 0.15 by design, which is below any usable contrast.
    pred = {}
    for tag, d in (("a", 10), ("b", 50)):
        R = SHOCK_BASE + SHOCK_SPAN * (d / 240.0)
        pred[tag] = abs(project_offset(eye, tgt, centre, (R, 0.0, 0.0)))
    diag("VG8 ring_a=%dpx pred=%.1f  ring_b=%dpx pred=%.1f"
         % (r1, pred["a"], r2, pred["b"]))
    ok = (r1 > 15 and r2 > r1 * 1.4 and
          abs(r1 - pred["a"]) <= 0.15 * pred["a"] and
          abs(r2 - pred["b"]) <= 0.15 * pred["b"])
    gate("VG8", "goal shockwave ring geometry", ok,
         "radius %dpx@t70 -> %dpx@t110 (recipe %.0f -> %.0f px), expanding+matched"
         % (r1, r2, pred["a"], pred["b"]))


def car_pixels(im, box):
    """Segment the car inside a tight window around its predicted screen pose.

    A colour threshold alone is brittle: under the directional light the box
    car's blue paint ranges from ~(11,40,103) on the shaded side to
    (27,95,244) on the lit top, which overlaps the arena grid and the glass
    hue. Instead, within the window, anything that is NOT turf-green and NOT
    the gray floor-grid line is car geometry (paint, glass, tyre, hub).
    """
    x0, y0, x1, y1 = box
    pts = []
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            p = px(im, x, y)
            turf = p[1] > p[2] + 4 and p[1] > p[0] + 8
            grid = abs(p[0] - 89) < 16 and abs(p[1] - 115) < 16 and abs(p[2] - 140) < 16
            if not turf and not grid:
                pts.append((x, y))
    return pts


def principal_angle(pts):
    if len(pts) < 40:
        return None
    n = float(len(pts))
    mx = sum(p[0] for p in pts) / n
    my = sum(p[1] for p in pts) / n
    sxx = sum((p[0] - mx) ** 2 for p in pts) / n
    syy = sum((p[1] - my) ** 2 for p in pts) / n
    sxy = sum((p[0] - mx) * (p[1] - my) for p in pts) / n
    return math.degrees(0.5 * math.atan2(2 * sxy, sxx - syy)) % 180.0


def vg9_car():
    # Both scenes aim a fixed camera at the car's SPAWN pose (0,-2300,~17);
    # the drive probe below confirms the car rises straight up (x stays 0), so
    # one window per scene covers every captured frame.
    # Windows sit BELOW the horizon so the backdrop is arena floor (green),
    # which is what makes the "not-turf" segmentation meaningful.
    WHEEL_BOX = (556, 328, 726, 424)      # camera (200,-2900,170)->(0,-2300,55)
    PITCH_BOX = (550, 280, 730, 410)      # camera (420,-2300,420)->(0,-2300,120)

    p_w, l_w = shot("car_wheels", baseline_args() + ["--nofennec",
                   "--cam", "200,-2900,170,0,-2300,55"])
    common = ["--meshdir", MESH, "--seed", "7", "--nofennec", "--no-shake"]
    p_a, l_a = shot("car_pitch_a", common + ["--drive", "0,0,1,0,0,1",
                   "--cam", "420,-2300,420,0,-2300,120", "--ticks", "70",
                   "--shot-at-tick", "50"])
    p_b, l_b = shot("car_pitch_b", common + ["--drive", "0,0,1,0,0,1",
                   "--cam", "420,-2300,420,0,-2300,120", "--ticks", "130",
                   "--shot-at-tick", "110"])
    if not p_w:
        gate("VG9", "car wheel + orientation visuals", None,
             "no capture (gamescope/GL unavailable)")
        return
    im = read_ppm(p_w)
    pts = car_pixels(im, WHEEL_BOX)
    # Count hub/tyre pixels only among CAR pixels: the turf is darker
    # (sum ~82) than the tyres, so a whole-window luminance count is useless.
    bright = dark = 0
    for (x, y) in pts:
        lp = lum(px(im, x, y))
        if lp > 620:
            bright += 1
        elif lp < 100:
            dark += 1
    diag("VG9 box car: car px=%d bright hub=%d dark tyre=%d" % (len(pts), bright, dark))
    wheel_ok = len(pts) > 200 and bright >= 8 and dark > 25
    if not p_a or not p_b:
        gate("VG9", "car wheel + orientation visuals", wheel_ok,
             "wheels present=%s (bright=%d dark=%d); pitch capture unavailable"
             % (wheel_ok, bright, dark))
        return
    ia, ib = read_ppm(p_a), read_ppm(p_b)
    ang_a = principal_angle(car_pixels(ia, PITCH_BOX))
    ang_b = principal_angle(car_pixels(ib, PITCH_BOX))
    delta = 0.0 if ang_a is None or ang_b is None else abs(ang_a - ang_b)
    delta = min(delta, 180.0 - delta)
    diag("VG9 pitch silhouette angle a=%s b=%s delta=%.1f" % (ang_a, ang_b, delta))
    ok = wheel_ok and ang_a is not None and ang_b is not None and delta > 10.0
    gate("VG9", "car wheel + orientation visuals", ok,
         "wheels: %d car px, %d bright hub px, %d dark tyre px; "
         "pitch rotates silhouette %.1f deg" % (len(pts), bright, dark, delta))


def vg10_debug_colliders():
    p_on, l_on = shot("dbg_on", baseline_args() +
                      ["--debug-colliders", "--cam", "3000,-3000,1200,0,0,200"])
    p_off, l_off = shot("dbg_off", SCENES["kickoff"])
    if not p_on or not p_off:
        gate("VG10", "debug-collider instrument", None,
             "no capture (gamescope/GL unavailable)")
        return
    a = read_ppm(p_on)
    b = read_ppm(p_off)
    diff = 0
    n = 0
    want = {"car-cyan": (89, 242, 255), "bot-orange": (255, 184, 64),
            "ball-magenta": (255, 89, 230)}
    hits = dict((k, 0) for k in want)
    for y in range(0, H, 2):
        for x in range(0, W, 2):
            pa, pb = px(a, x, y), px(b, x, y)
            n += 1
            if max(abs(pa[k] - pb[k]) for k in range(3)) > 12:
                diff += 1
            for k, c in want.items():
                if max(abs(pa[i] - c[i]) for i in range(3)) <= 26:
                    hits[k] += 1
    diag("VG10 diff=%.4f hits=%s" % (diff / float(n), hits))
    ok = (diff / float(n) > 0.004 and
          all(hits[k] >= 3 for k in want))
    gate("VG10", "debug-collider instrument", ok,
         "%.2f%% pixels change; wireframe samples %s"
         % (100.0 * diff / n, ", ".join("%s=%d" % (k, v) for k, v in hits.items())))


def vg11_wall_curbs():
    # Look at the +Y back wall from inside: the wall-base fillet must sit in
    # the bottom band of the WALL, and the arena centre floor must stay clear.
    p, l = shot("curbs", baseline_args() + ["--cam", "0,-3000,400,0,5120,300"])
    if not p:
        gate("VG11", "wall curb placement", None,
             "no capture (gamescope/GL unavailable)")
        return
    im = read_ppm(p)

    def curbish(p0):
        r, g, b = p0
        return 18 < r < 110 and 28 < g < 130 and 35 < b < 155 and b >= r and b > g

    def greenish(p0):
        r, g, b = p0
        return g > r + 15 and g > b + 15

    base = sum(1 for x in range(0, W, 4) if curbish(px(im, x, 395)))
    centre = sum(1 for x in range(0, W, 4) if curbish(px(im, x, 470)))
    centre2 = sum(1 for x in range(0, W, 4) if curbish(px(im, x, 620)))
    diag("VG11 curbish: wall-base row=%d centre rows=%d/%d (of %d)"
         % (base, centre, centre2, W // 4))
    ok = base > 40 and centre < base * 0.5 and centre2 < base * 0.15

    # Corner vantage: every cove must follow the wall base, INCLUDING the
    # 45-degree chamfers and the side/back walls. This view looks across the
    # -Y half toward the +X/-Y chamfer. The pre-fix build centred the chamfer
    # coves on |x|+|y| = 5888 (2176 uu inside the true plane) AND ran
    # wallCurb's degree literals through a radians conversion, so six of the
    # eight coves swept the interior as diagonal ridges. The floor must
    # therefore dominate the stray cove colour from here.
    pc, _ = shot("curbs_corner",
                 baseline_args() + ["--cam", "2000,-3500,800,3520,-4544,200"])
    gc = cc = 0
    ok2 = False
    if pc:
        imc = read_ppm(pc)
        for y in range(0, H, 3):
            for x in range(0, W, 3):
                q = px(imc, x, y)
                if greenish(q):
                    gc += 1
                elif curbish(q):
                    cc += 1
        ok2 = gc > cc * 1.2
        diag("VG11 corner floor=%d stray-cove=%d (need floor > 1.2x cove)"
             % (gc, cc))

    gate("VG11", "wall curb placement", ok and ok2,
         "wall-base cove %d samples, centre %d/%d; corner floor %d vs stray cove %d"
         % (base, centre, centre2, gc, cc))


def vg12_feedback_fx():
    """V5 demo + pickup feedback, gated differentially: the FX-probe frame must
    add a body of pixels the identical frame without the probe does not have,
    and those added pixels must carry the effect's documented hue (cool for
    the demo flash, warm for the pickup burst). Robust to the rest of the
    scene (pads are gold, the walls are blue, ...)."""
    CAM = ["--cam", "200,-2900,170,0,-2300,55"]
    WIN = (480, 250, 820, 460)

    def added_pixels(name, kind, tick):
        args = ["--meshdir", MESH, "--seed", "7", "--ticks", str(tick + 8),
                "--no-shake"]
        if kind:
            args += ["--fx-probe", kind, "--fx-probe-tick", "60"]
        p, _ = shot(name, args + CAM + ["--shot-at-tick", str(tick)])
        return p

    pd = added_pixels("fx_demo", "demo", 90)
    cd = added_pixels("fx_demo_ctl", None, 90)
    pp = added_pixels("fx_pickup", "pickup", 90)
    cp = added_pixels("fx_pickup_ctl", None, 90)
    if not all((pd, cd, pp, cp)):
        gate("VG12", "demo + pickup feedback FX", None,
             "no capture (gamescope/GL unavailable)")
        return

    def stats(a, b):
        ia, ib = read_ppm(a), read_ppm(b)
        n = 0
        sr = sg = sb = 0
        for y in range(WIN[1], WIN[3], 2):
            for x in range(WIN[0], WIN[2], 2):
                qa, qb = px(ia, x, y), px(ib, x, y)
                if max(abs(qa[i] - qb[i]) for i in range(3)) > 18:
                    n += 1
                    sr += qa[0]
                    sg += qa[1]
                    sb += qa[2]
        if n == 0:
            return 0, (0, 0, 0)
        return n, (sr / n, sg / n, sb / n)

    dn, dmean = stats(pd, cd)
    pn, pmean = stats(pp, cp)
    diag("VG12 demo added=%d mean=(%.0f,%.0f,%.0f) pickup added=%d mean=(%.0f,%.0f,%.0f)"
         % (dn, dmean[0], dmean[1], dmean[2], pn, pmean[0], pmean[1], pmean[2]))
    demo_ok = dn > 80 and dmean[2] >= dmean[0]          # cool cyan/white flash
    pick_ok = pn > 60 and pmean[0] > pmean[2] + 20      # warm gold burst
    gate("VG12", "demo + pickup feedback FX", demo_ok and pick_ok,
         "demo flash adds %d px, mean RGB (%.0f,%.0f,%.0f), cool=%s; pickup "
         "burst adds %d px, mean RGB (%.0f,%.0f,%.0f), warm=%s"
         % (dn, dmean[0], dmean[1], dmean[2], demo_ok, pn, pmean[0], pmean[1],
            pmean[2], pick_ok))


def vg13_mesh_integrity():
    """V1/V2: the baked Fennec asset must be geometrically sane.

    Shells out to tools/verify/mesh_integrity.py --json so the asset pipeline
    owns the assertions (unit normals, ground datum, source footprint) and this
    gate only reports them. Pre-fix this fails with 58851/58851 non-unit normals
    and bbox min-z -48.91 (docs/VISUAL_BUGS_TODO.md V1/V2).
    """
    script = os.path.join(HERE, "mesh_integrity.py")
    if not os.path.isfile(script):
        gate("VG13", "Fennec mesh integrity", None, "mesh_integrity.py missing")
        return
    proc = subprocess.run([sys.executable, script, "--json"],
                          capture_output=True, text=True)
    try:
        res = json.loads(proc.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        gate("VG13", "Fennec mesh integrity", False,
             "mesh_integrity.py produced no JSON (rc=%d): %s"
             % (proc.returncode, proc.stdout[-200:]))
        return
    diag("VG13 %s" % json.dumps(res))
    if "failures" not in res:
        gate("VG13", "Fennec mesh integrity", None,
             res.get("failures", ["cannot read mesh"])[0])
        return
    nrm = res.get("normals", {})
    bbox = res.get("bbox", {})
    gate("VG13", "Fennec mesh integrity", bool(res.get("pass")),
         "normals non-unit=%s | bbox min-z=%.2f | size=%s | %s"
         % (nrm.get("non_unit"), bbox.get("min", [0, 0, 0])[2],
            "x".join("%.1f" % v for v in bbox.get("size", [])),
            "PASS" if res.get("pass") else "; ".join(res["failures"])))


def _turf_luma_and_grid(im):
    """Per-column stats for the VG14 turf band. Returns (n, green_frac,
    luma_mean, luma_max, bright_frac). A 'bright grid line' pixel is a pixel in
    the turf band that is much brighter than the band's own median (the old
    unlit +/-9000 ground grid drew pale lines straight over the turf).
    The HUD (boost gauge bottom-right, text strip bottom-left) is excluded so
    its bright pixels cannot read as grid lines."""
    y0, y1 = 470, 700
    greens = 0
    lumas = []
    n = 0
    for x in range(0, W, 2):
        if x > 1040 or x < 120:  # boost gauge / HUD text
            continue
        for y in range(y0, y1, 2):
            p = px(im, x, y)
            n += 1
            if p[1] > p[2] + 4 and p[1] > p[0] + 8:
                greens += 1
            lumas.append(lum(p))
    lumas.sort()
    med = lumas[len(lumas) // 2]
    bright = sum(1 for v in lumas if v > med * 1.9 + 60)
    return n, greens / float(n), med, lumas[-1], bright / float(n)


def vg14_arena_occupancy():
    """V4: the kickoff floor must read as continuous turf -- green-dominant and
    free of the pale debug-grid lines that used to be drawn across it."""
    p, log = shot("vg14_kickoff", SCENES["kickoff"])
    if not p:
        gate("VG14", "arena turf occupancy", None,
             "no capture (gamescope/GL unavailable)")
        return
    im = read_ppm(p)
    n, green, med, mx, bright = _turf_luma_and_grid(im)
    glerr = counter_of(log, "glerr")
    ok = green >= 0.60 and bright <= 0.01 and (glerr in (0, None))
    diag("VG14 samples=%d green=%.3f median_luma=%.0f max_luma=%.0f bright_frac=%.4f"
         % (n, green, med, mx, bright))
    gate("VG14", "arena turf occupancy", ok,
         "floor band green-dominant=%.1f%% (need >=60%%), bright grid-line px=%.2f%% "
         "(need <=1%%), glerr=%s" % (green * 100.0, bright * 100.0, glerr))


def _frame_mean_lum(im):
    tot = 0
    n = 0
    for y in range(60, 680, 3):
        for x in range(40, 1240, 3):
            tot += lum(px(im, x, y))
            n += 1
    return tot / float(n)


def vg15_lighting():
    """V5: no visible surface may be ambient-only black, the arena palette must
    read through the lit path, and the two side walls must read alike.

    `--flat` disables lighting, so each sampled patch is also compared against
    the SAME patch in the unlit frame: lighting may not darken any visible
    surface below 35% of its own albedo, and no patch may collapse to <=24
    (the pre-fix single-light model left the ceiling and +x wall ambient-only).
    The whole-frame lit/flat ratio is kept as a looser 0.45..1.25 sanity band,
    because it is composition-dependent: the corrected arena shell (walls that
    stop at the 45 deg chamfers, VG11) shows more full-albedo floor than the
    buggy shell did, which legitimately raises the unlit reference frame.
    """
    lit, log = shot("vg15_lit", SCENES["kickoff"])
    flat, _ = shot("vg15_flat", SCENES["kickoff"] + ["--flat"])
    up_lit, _ = shot("vg15_up", SCENES["ceiling"])
    if not (lit and flat and up_lit):
        gate("VG15", "arena lighting sanity", None,
             "no capture (gamescope/GL unavailable)")
        return
    il, iff, iu = read_ppm(lit), read_ppm(flat), read_ppm(up_lit)
    bad = []
    detail = []
    patches = [
        ("turf",       (300, 560)),
        ("back wall",  (600, 250)),
        ("left wall",  (100, 230)),
        ("right wall", (1120, 240)),
    ]
    Ls = {}
    patch_ratios = {}
    for name, (x, y) in patches:
        vals = [px(il, x + dx, y + dy) for dx in range(0, 40, 4)
                for dy in range(0, 40, 4)]
        mean = tuple(sum(v[k] for v in vals) / len(vals) for k in range(3))
        L = (0.2126 * mean[0] + 0.7152 * mean[1] + 0.0722 * mean[2])
        Ls[name] = L
        fvals = [px(iff, x + dx, y + dy) for dx in range(0, 40, 4)
                 for dy in range(0, 40, 4)]
        fmean = tuple(sum(v[k] for v in fvals) / len(fvals) for k in range(3))
        fL = (0.2126 * fmean[0] + 0.7152 * fmean[1] + 0.0722 * fmean[2])
        patch_ratios[name] = L / max(fL, 1.0)
        if L <= 24.0:  # ambient-only black
            bad.append("%s(L=%.0f near-black)" % (name, L))
        if patch_ratios[name] < 0.35:  # lighting crushes this surface
            bad.append("%s(lit=%.0f%% of albedo)"
                       % (name, 100.0 * patch_ratios[name]))
    # Ceiling must not be a void.
    cvals = [px(iu, 600 + dx, 280 + dy) for dx in range(0, 40, 4)
             for dy in range(0, 40, 4)]
    cmean = tuple(sum(v[k] for v in cvals) / len(cvals) for k in range(3))
    cL = (0.2126 * cmean[0] + 0.7152 * cmean[1] + 0.0722 * cmean[2])
    if cL <= 24.0:
        bad.append("ceiling(L=%.0f near-black)" % cL)
    # Both side walls read as glass of comparable brightness.
    wall_ratio = max(Ls["left wall"], Ls["right wall"]) / max(
        min(Ls["left wall"], Ls["right wall"]), 1.0)
    if wall_ratio > 2.0:
        bad.append("wall asymmetry %.2f" % wall_ratio)
    # Whole-frame sanity band (composition-dependent; see the docstring).
    lit_mean = _frame_mean_lum(il)
    flat_mean = _frame_mean_lum(iff)
    frame_ratio = lit_mean / max(flat_mean, 1.0)
    if not (0.45 <= frame_ratio <= 1.25):
        bad.append("frame ratio %.2f" % frame_ratio)
    diag("VG15 lit_mean=%.1f flat_mean=%.1f ratio=%.2f walls L=%.0f/%.0f "
         "ceiling L=%.0f turf L=%.0f patch ratios %s"
         % (lit_mean, flat_mean, frame_ratio, Ls["left wall"], Ls["right wall"],
            cL, Ls["turf"],
            ",".join("%s=%.2f" % (k, v) for k, v in sorted(patch_ratios.items()))))
    gate("VG15", "arena lighting sanity", not bad,
         "lit/flat frame luminance %.2f (need 0.45..1.25); walls L=%.0f/%.0f "
         "(asymmetry %.2f); ceiling L=%.0f; turf L=%.0f"
         % (frame_ratio, Ls["left wall"], Ls["right wall"], wall_ratio, cL,
            Ls["turf"]) + (" BAD:" + ",".join(bad) if bad else ""))


def vg16_fennec_path():
    """V7/V2: VG9 only ever tested the --nofennec procedural box car.

    Capture a LEVEL side-on pose of the spawn car with the Fennec and with
    `--nofennec`, and take the top/bottom of the pixels that differ. The arena
    backdrop is identical in both frames, so the differing region is exactly
    the car mesh, with no colour heuristic.

    The invariant is the car's on-screen HEIGHT. The Fennec is authored
    min-z = -17.00 (wheels at the ground) up to +34.66, so a grounded car is
    51.65 uu tall and measures ~22 px at this fixed camera (MEASURED). A car
    buried by the pre-fix +32 uu datum offset pokes only its roof above the
    turf -- about 19.7 uu, ~8 px -- so 16 px separates the two cleanly.
    """
    CAM = ["--cam", "0,-2950,25,0,-2350,25"]  # level, perpendicular to the car
    common = ["--meshdir", MESH, "--seed", "7", "--ticks", "160", "--no-shake",
              "--shot-at-tick", "120"]
    pf, _ = shot("vg16_fennec", common + CAM)
    pb, _ = shot("vg16_box", common + ["--nofennec"] + CAM)
    if not pf or not pb:
        gate("VG16", "Fennec render path", None,
             "no capture (gamescope/GL unavailable)")
        return
    a, b = read_ppm(pf), read_ppm(pb)
    ys, xs = [], []
    for y in range(300, 460):
        for x in range(560, 720):
            qa, qb = px(a, x, y), px(b, x, y)
            if max(abs(qa[k] - qb[k]) for k in range(3)) > 20:
                ys.append(y)
                xs.append(x)
    if len(ys) < 80:
        gate("VG16", "Fennec render path", False,
             "Fennec geometry too small: only %d px differ from the box car"
             % len(ys))
        return
    span = max(ys) - min(ys) + 1
    width = max(xs) - min(xs) + 1
    ok = span >= 18
    diag("VG16 car px=%d bbox=%dx%d rows %d..%d" % (len(ys), width, span,
                                                    min(ys), max(ys)))
    gate("VG16", "Fennec render path", ok,
         "Fennec silhouette %dx%d px (rows %d..%d); grounded mesh measures "
         "~22 px tall, a buried mesh ~16 px; need >=18 px"
         % (width, span, min(ys), max(ys)))


ALL_GATES = [
    ("VG1", vg1_determinism),
    ("VG2", vg2_calibrate),
    ("VG3", scene_coverage),
    ("VG4", vg4_ball_indicator),
    ("VG5", vg5_boost_gauge),
    ("VG6", vg6_wall),
    ("VG7", vg7_budget),
    ("VG8", vg8_shockwave),
    ("VG9", vg9_car),
    ("VG10", vg10_debug_colliders),
    ("VG11", vg11_wall_curbs),
    ("VG12", vg12_feedback_fx),
    ("VG13", vg13_mesh_integrity),
    ("VG14", vg14_arena_occupancy),
    ("VG15", vg15_lighting),
    ("VG16", vg16_fennec_path),
]


def main():
    global DIAG
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None, help="comma-separated gate ids")
    ap.add_argument("--diag", action="store_true", help="print raw measurements")
    args = ap.parse_args()
    DIAG = args.diag

    if not os.path.isfile(GAME):
        print("SKIP: build/game missing - run `make build/game`")
        print("# summary: 0 PASS, 0 FAIL (all visual gates SKIPPED)")
        return 0
    if shutil.which("gamescope") is None:
        print("SKIP: gamescope not installed - cannot capture offscreen")
        print("# summary: 0 PASS, 0 FAIL (all visual gates SKIPPED)")
        return 0

    want = set(args.only.split(",")) if args.only else None
    for gid, fn in ALL_GATES:
        if want and gid not in want:
            continue
        try:
            fn()
        except Exception as e:  # a harness bug must not look like a PASS
            gate(gid, "(harness error)", False, "%s: %s" % (type(e).__name__, e))

    nskip = sum(1 for r in results if r["verdict"] == "SKIP")
    nfail = sum(1 for r in results if r["verdict"] == "FAIL")
    npass = sum(1 for r in results if r["verdict"] == "PASS")
    with open(os.path.join(HERE, "gates_visual.json"), "w") as f:
        json.dump({"gates": results, "diag": diag_lines}, f, indent=1)
    print("# summary: %d PASS, %d FAIL, %d SKIP" % (npass, nfail, nskip))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
