#!/usr/bin/env python3
"""cam_trace.py — Rocket League camera trace format **v1** (writer + loader + fixture).

`MASTER_PROMPT_CAMERA_EXACT.md` §6 P2.3 asks for a trace format whose *header*
carries package identity, `ProfileCameraSettings` and the tick rate, followed by
**fixed-stride float32 records**. This module is that format, plus the loader /
validator the diff tool (and P3's `camera_trace_gate.py`) read it through, plus a
deterministic **synthetic** generator so the tooling is testable with no game.

Nothing here touches the game. A trace is Psyonix-derived measurement data, so it
lives under `decomp/traces/` (gitignored, repo rule 1) and never in the tree; the
*format* and this code are ours (MIT).

-------------------------------------------------------------------------------
FILE LAYOUT (v1)
-------------------------------------------------------------------------------
    offset  size  content
    0       4     magic  b"RLCT"
    4       4     uint32 version            (1)
    8       4     uint32 header_len         (bytes of UTF-8 JSON header)
    12      4     uint32 stride             (bytes per record = 4 * field_count)
    16      4     uint32 field_count
    20      4     uint32 record_count
    24      4     uint32 crc32(record block)
    28      H     UTF-8 JSON header (package identity, settings, tick rate, ...)
    28+H    N*S   record block, N = record_count, S = stride, little-endian float32

Records are **fixed-stride float32** in the field order pinned by
`FIELD_SPECS`; the header repeats the field table, and the loader fails loudly if
the two disagree, so a silent reorder (or a v2 file read by a v1 reader) is
detectable instead of silently misread. Bump `VERSION` for any change to the
field list, the field order, or the header's required keys.

-------------------------------------------------------------------------------
"POV -> WORLD POSITION" CONVENTION (read this before comparing anything)
-------------------------------------------------------------------------------
RL's camera decomposes into a POV: a **Focus** world point it looks at, a
**Rotation** (UE rotator, 65536 units = 360 deg), a **Distance** and a **FOV**.
`Camera_X.ClampPOV` writes that POV; the rig turns it into an eye position.

Our camera (`src/camera/Camera.{h,cpp}`) publishes a different pair:

    Center()  == the smoothed look-at point   -> the FOCUS
    Eye()     == the smoothed eye position    -> derived from Focus/Rotation/Distance
    EffFov()  == slider FOV + dynamic kick    -> the FOV

so the one conversion both tools use (and the ONLY one they are allowed to
assume) is `POV_CONVENTION = "ue_rotator_focus_minus_fwd_distance_v1"`:

    forward = ( cos(pitch)*cos(yaw), cos(pitch)*sin(yaw), sin(pitch) )   # roll ignored
    eye     = focus - forward * distance                                # RL: "uu behind focus"

and back again, from a camera's (eye, center) pair:

    focus    = center
    distance = |eye - center|
    rotation = rotator_from_dir(center - eye)      # roll reported as 0.0

Space is UE left-handed (X forward, Y right, Z up); a rotator is stored as
`degrees * 65536/360`. Two honest limits, both surfaced by the diff tool as
`UNVERIFIED` instead of guessed:

  * **Roll.** Our rig never rolls (`Camera.cpp` `rotationFromDir` writes 0, and
    `Apply()` builds an up=+Z basis), so a recorded non-zero roll cannot be
    compared to ours — the tool reports roll as UNVERIFIED rather than scoring a
    mismatch it cannot explain.
  * **Swivel units.** RL's `Camera_TA.CurrentSwivel` unit is not established by
    the decompile (it is a struct member, not a formula). The header declares
    `swivel_convention`; unless it says `radians`, the diff tool refuses to score
    swivel and says so.

Header fields a reader must trust, not re-derive: `source` (`synthetic` | `live`),
`pov_convention`, `swivel_convention`, `tick_rate`, `settings`, `package`.

-------------------------------------------------------------------------------
CLI
-------------------------------------------------------------------------------
    # deterministic fixture, clearly labelled synthetic
    python3 tools/decomp/cam_trace.py gen --scenario swivel-sweep-release \\
        --frames 480 --seed 7 --out decomp/traces/synthetic_swivel.rlt
    python3 tools/decomp/cam_trace.py gen-all --outdir decomp/traces/synthetic
    python3 tools/decomp/cam_trace.py info      <trace.rlt> [--json]
    python3 tools/decomp/cam_trace.py validate  <trace.rlt>
    python3 tools/decomp/cam_trace.py selfcheck [--out decomp/traces/selfcheck.rlt]
    python3 tools/decomp/cam_trace.py deck            # §6.2 scenario deck

`selfcheck` is the format's own self-consistency gate: write -> load -> compare
every field bit-for-bit, and write -> load -> POV-convention round trip. Both
print `0.000000` max error when the format and the convention are consistent.
"""
import argparse
import json
import math
import os
import random
import struct
import sys
import time
import zlib
from array import array

# ── format identity ───────────────────────────────────────────────────────────
MAGIC = b"RLCT"
VERSION = 1

#: magic, version, header_len, stride, field_count, record_count, crc32
FILE_PREFIX = struct.Struct("<4sIIIIII")
PAYLOAD_OFFSET = FILE_PREFIX.size  # 28

# ── record field table (order IS the format; bump VERSION to change it) ───────
# (name, unit, group, doc)
FIELD_SPECS = (
    # --- OutPOV (what the hooked ClampPOV wrote) -------------------------------
    ("out_time", "s", "pov", "OutPOV.Time (state-local camera clock)"),
    ("focus_x", "uu", "pov", "OutPOV.Focus.X (world look-at point)"),
    ("focus_y", "uu", "pov", "OutPOV.Focus.Y"),
    ("focus_z", "uu", "pov", "OutPOV.Focus.Z"),
    ("rot_pitch", "rotator", "pov", "OutPOV.Rotation.Pitch (65536 = 360 deg)"),
    ("rot_yaw", "rotator", "pov", "OutPOV.Rotation.Yaw"),
    ("rot_roll", "rotator", "pov", "OutPOV.Rotation.Roll"),
    ("distance", "uu", "pov", "OutPOV.Distance (camera distance behind Focus)"),
    ("fov", "deg", "pov", "OutPOV.FOV"),
    # --- car pose -------------------------------------------------------------
    ("car_x", "uu", "car", "car center X"),
    ("car_y", "uu", "car", "car center Y"),
    ("car_z", "uu", "car", "car center Z"),
    ("car_pitch", "rotator", "car", "car rotation Pitch"),
    ("car_yaw", "rotator", "car", "car rotation Yaw"),
    ("car_roll", "rotator", "car", "car rotation Roll"),
    ("car_vx", "uu/s", "car", "car linear velocity X"),
    ("car_vy", "uu/s", "car", "car linear velocity Y"),
    ("car_vz", "uu/s", "car", "car linear velocity Z"),
    # --- ball pose ------------------------------------------------------------
    ("ball_x", "uu", "ball", "ball center X"),
    ("ball_y", "uu", "ball", "ball center Y"),
    ("ball_z", "uu", "ball", "ball center Z"),
    ("ball_vx", "uu/s", "ball", "ball linear velocity X"),
    ("ball_vy", "uu/s", "ball", "ball linear velocity Y"),
    ("ball_vz", "uu/s", "ball", "ball linear velocity Z"),
    # --- player inputs (RL signed convention; see AGENTS.md) ------------------
    ("ball_cam", "flag", "input", "1 = ball cam on"),
    ("swivel_in_x", "-1..1", "input", "right-stick look X (x = right)"),
    ("swivel_in_y", "-1..1", "input", "right-stick look Y (y = up)"),
    ("throttle", "-1..1", "input", "signed throttle (+1 forward)"),
    ("steer", "-1..1", "input", "signed steer (+1 = LEFT, AGENTS.md)"),
    ("pitch_in", "-1..1", "input", "signed pitch (+1 = UP)"),
    ("yaw_in", "-1..1", "input", "signed yaw"),
    ("roll_in", "-1..1", "input", "signed roll"),
    ("boost", "flag", "input", "1 = boost held"),
    ("jump", "flag", "input", "1 = jump held"),
    ("airborne", "flag", "input", "1 = car airborne this frame"),
    ("look_behind", "flag", "input", "1 = held rear view"),
    # --- camera state ---------------------------------------------------------
    ("dt", "s", "state", "frame delta that produced this record"),
    ("current_swivel_yaw", "see header", "state",
     "Camera_TA.CurrentSwivel yaw (units per header swivel_convention)"),
    ("current_swivel_pitch", "see header", "state",
     "Camera_TA.CurrentSwivel pitch (units per header swivel_convention)"),
)

FIELD_NAMES = tuple(f[0] for f in FIELD_SPECS)
FIELD_INDEX = {name: i for i, name in enumerate(FIELD_NAMES)}
NFIELDS = len(FIELD_SPECS)
STRIDE = 4 * NFIELDS
RECORD_STRUCT = struct.Struct("<%df" % NFIELDS)

# ── convention ───────────────────────────────────────────────────────────────
ROT_FULL = 65536.0                  # rotator units per 360 degrees
ROT_PER_RAD = ROT_FULL / (2.0 * math.pi)
#: Cited swivel rate (Rocket Science #2, camel src/camera/Camera.cpp
#: kSwivelRate): 90 deg in 1.3 s at slider 1.0, linear in the slider.
SWIVEL_RATE_PER_UNIT = (math.pi * 0.5) / 1.3
POV_CONVENTION = "ue_rotator_focus_minus_fwd_distance_v1"
SUPPORTED_POV_CONVENTIONS = (POV_CONVENTION,)
SWIVEL_CONVENTIONS = ("radians", "unknown")
DEFAULT_SWIVEL_CONVENTION = "radians"

#: §6.2 scenario deck — one file each when W3 records live. Order is the deck.
SCENARIO_DECK = (
    ("ballcam-toggle", "ball cam on->off mid-drive (transition blend + state swap)"),
    ("target-switch-mid-air", "ball cam target jumps while airborne (target select)"),
    ("swivel-sweep-release", "sweep the right stick to the stop, release (decay)"),
    ("landing-air-to-ground", "descend, touch down, ground branch re-engages"),
    ("supersonic-straight", "0 -> supersonic straight line (distance/FOV kick)"),
    ("wall-contact", "drive into a wall, scrub, rebound (ground clamp)"),
    ("dribble-hold", "ball held on the roof (focus micro-chatter)"),
    ("goal-ballcam-reset", "ball crosses the goal line, resets to center"),
)
SCENARIO_NAMES = tuple(s[0] for s in SCENARIO_DECK)

#: BakkesMod `ProfileCameraSettings` field list, our names + our defaults
#: (src/camera/Camera.h `CameraParams`). `pitch` is the same slider as our
#: `angle` (degrees, negative = looking down).
DEFAULT_SETTINGS = {
    "fov": 110.0,
    "height": 100.0,
    "pitch": -3.0,
    "distance": 270.0,
    "stiffness": 0.45,
    "swivel_speed": 5.0,
    "transition_speed": 1.2,
    "invert_swivel_pitch": False,
    "camera_shake": True,
    # our additive INI key (docs/INI_SCHEMA.md); a live recorder reads it from
    # the INI, not from RL.
    "snap": True,
}

DEFAULT_PACKAGE = {
    "identity": "UE3 868 / licensee 34",
    "engine_version": 10897,
    "cooker_version": 136,
    "licensee": 34,
    "tagame_guid": "66101E814F23F2BA77A621A269A5613B",
    "projectx": "ProjectX.upk",
}

REQUIRED_HEADER_KEYS = (
    "source", "scenario", "tick_rate", "settings", "package",
    "pov_convention", "swivel_convention", "fields", "generator",
)
SOURCE_KINDS = ("synthetic", "live")


#: default trace home — the gitignored local workspace (repo rule 1). Resolved
#: from this file so a run from any CWD still lands in `<repo>/decomp/traces`.
REPO_ROOT_INFER = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
DEFAULT_TRACE_DIR = os.path.join(REPO_ROOT_INFER, "decomp", "traces")


class TraceError(Exception):
    """Base for anything wrong with a trace file."""


class TraceFormatError(TraceError):
    """The bytes are not a v1 RLCT file (magic / version / size / fields)."""


class TraceValidationError(TraceError):
    """Structurally a v1 file, but its content breaks a stated invariant."""


# ── POV <-> eye/center convention (the single implementation) ────────────────
def fwd_from_rotator(pitch_ru, yaw_ru):
    """Unit view direction of a UE rotator. Roll does not change the view axis."""
    p = pitch_ru / ROT_PER_RAD
    y = yaw_ru / ROT_PER_RAD
    cp = math.cos(p)
    return (cp * math.cos(y), cp * math.sin(y), math.sin(p))


def rotator_from_dir(d):
    """UE rotator (pitch, yaw, roll) of a world direction; roll is always 0."""
    x, y, z = d[0], d[1], d[2]
    len2d = math.hypot(x, y)
    return (math.atan2(z, len2d) * ROT_PER_RAD,
            math.atan2(y, x) * ROT_PER_RAD,
            0.0)


def eye_from_pov(focus, rotation, distance):
    """RL: the eye sits `distance` uu behind the focus along the view direction."""
    fx, fy, fz = fwd_from_rotator(rotation[0], rotation[1])
    return (focus[0] - fx * distance,
            focus[1] - fy * distance,
            focus[2] - fz * distance)


def pov_from_eye_center(eye, center, fov, out_time=None):
    """Our camera's (Eye, Center, EffFov) expressed as RL's POV.

    center -> focus, |eye-center| -> distance, direction -> rotation (roll 0).
    Returns None when the pair is degenerate (eye == center: no view axis).
    """
    dx = center[0] - eye[0]
    dy = center[1] - eye[1]
    dz = center[2] - eye[2]
    dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    if dist < 1e-6:
        return None
    rot = rotator_from_dir((dx, dy, dz))
    return {
        "focus": (center[0], center[1], center[2]),
        "rotation": rot,
        "distance": dist,
        "fov": fov,
        "time": out_time,
    }


def wrap_rotator(delta):
    """Shortest signed distance between two rotator values, in (-32768, 32768]."""
    d = math.fmod(delta, ROT_FULL)
    if d > ROT_FULL * 0.5:
        d -= ROT_FULL
    elif d <= -ROT_FULL * 0.5:
        d += ROT_FULL
    return d


def quantize_f32(x):
    """Round a float to the trace's field precision (float32)."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def pov_field_errors(recorded, derived, quantize=True):
    """Per-field error of `derived` POV against the `recorded` one.

    Both arguments are POV dicts (`focus`, `rotation`, `distance`, `fov`). This is
    the ONE error definition the tools share, so "self-consistency is 0.0" and
    "our camera's error table" are measured the same way:

      * units are the ones `§6 P2.4` names — uu for Focus/Distance, **rotator
        units** for Rotation, degrees for FOV;
      * `quantize=True` rounds the derived value to float32 first, i.e. to the
        trace's own field precision. A difference smaller than one float32 ulp is
        not representable in the format, so scoring it would be measuring the
        reader's libm, not the camera. The self-consistency gate therefore reads
        exactly 0.000000, and a real trace's errors are far larger than an ulp
        either way.
      * Rotation is compared per component with rotator wrap (65536 = 360 deg);
        `rotation_mag` is the larger of |pitch| and |yaw| because roll is not
        comparable to our rig (it never rolls) — the caller reports roll as
        UNVERIFIED and must not fold it into a score.

    Returns None when either POV is degenerate (no view axis).
    """
    if not derived or not recorded:
        return None
    q = quantize_f32 if quantize else (lambda v: v)
    rc, dc = recorded["focus"], derived["focus"]
    dx, dy, dz = dc[0] - rc[0], dc[1] - rc[1], dc[2] - rc[2]
    rr, dr = recorded["rotation"], derived["rotation"]
    e_pitch = wrap_rotator(q(dr[0]) - rr[0])
    e_yaw = wrap_rotator(q(dr[1]) - rr[1])
    e_roll = wrap_rotator(q(dr[2]) - rr[2])
    return {
        "focus_uu": math.sqrt(dx * dx + dy * dy + dz * dz),
        "focus_axis_uu": max(abs(dx), abs(dy), abs(dz)),
        "distance_uu": abs(q(derived["distance"]) - recorded["distance"]),
        "rot_pitch": e_pitch,
        "rot_yaw": e_yaw,
        "rot_roll": e_roll,
        "rotation_mag": max(abs(e_pitch), abs(e_yaw)),
        "fov_deg": abs(q(derived["fov"]) - recorded["fov"]),
    }


# ── header ───────────────────────────────────────────────────────────────────
def make_header(source="synthetic", scenario="synthetic", tick_rate=120.0,
                settings=None, package=None, seed=None,
                pov_convention=POV_CONVENTION,
                swivel_convention=DEFAULT_SWIVEL_CONVENTION,
                sample_rate_hz=None, notes="", extra=None):
    """Build the JSON header (package identity, settings, tick rate, field table)."""
    if source not in SOURCE_KINDS:
        raise ValueError("source must be one of %r" % (SOURCE_KINDS,))
    if pov_convention not in SUPPORTED_POV_CONVENTIONS:
        raise ValueError("unsupported pov_convention %r (supported: %r)"
                         % (pov_convention, SUPPORTED_POV_CONVENTIONS))
    if swivel_convention not in SWIVEL_CONVENTIONS:
        raise ValueError("swivel_convention must be one of %r"
                         % (SWIVEL_CONVENTIONS,))
    s = dict(DEFAULT_SETTINGS)
    if settings:
        s.update(settings)
    h = {
        "format": "RLCT",
        "version": VERSION,
        "generator": "tools/decomp/cam_trace.py v1",
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "source": source,
        "scenario": scenario,
        "notes": notes,
        "tick_rate": float(tick_rate),
        "sample_rate_hz": float(sample_rate_hz if sample_rate_hz else tick_rate),
        "seed": seed,
        "seed_pinned": seed is not None,
        "package": dict(package or DEFAULT_PACKAGE),
        "settings": s,
        "pov_convention": pov_convention,
        "swivel_convention": swivel_convention,
        "stride": STRIDE,
        "field_count": NFIELDS,
        "fields": [{"name": n, "unit": u, "group": g, "doc": d}
                   for (n, u, g, d) in FIELD_SPECS],
    }
    if extra:
        h.update(extra)
    return h


def validate_header(header):
    """Raise TraceFormatError/TraceValidationError unless `header` is a v1 header."""
    if not isinstance(header, dict):
        raise TraceFormatError("header is not a JSON object")
    missing = [k for k in REQUIRED_HEADER_KEYS if k not in header]
    if missing:
        raise TraceValidationError("header missing keys: %s" % ", ".join(missing))
    if header.get("format") != "RLCT":
        raise TraceFormatError("header format is %r, expected 'RLCT'"
                               % (header.get("format"),))
    if header.get("version") != VERSION:
        raise TraceFormatError("header version is %r, this reader speaks v%d — "
                               "re-run with a matching tools/decomp/cam_trace.py"
                               % (header.get("version"), VERSION))
    if header.get("source") not in SOURCE_KINDS:
        raise TraceValidationError("header source is %r, expected one of %r"
                                   % (header.get("source"), SOURCE_KINDS))
    if header.get("pov_convention") not in SUPPORTED_POV_CONVENTIONS:
        raise TraceValidationError(
            "header pov_convention is %r; this reader only knows %r"
            % (header.get("pov_convention"), SUPPORTED_POV_CONVENTIONS))
    if header.get("swivel_convention") not in SWIVEL_CONVENTIONS:
        raise TraceValidationError(
            "header swivel_convention is %r, expected one of %r"
            % (header.get("swivel_convention"), SWIVEL_CONVENTIONS))
    if int(header.get("field_count", -1)) != NFIELDS:
        raise TraceFormatError("header field_count %r != %d in this reader"
                               % (header.get("field_count"), NFIELDS))
    if int(header.get("stride", -1)) != STRIDE:
        raise TraceFormatError("header stride %r != %d in this reader"
                               % (header.get("stride"), STRIDE))
    fields = header.get("fields")
    if not isinstance(fields, list) or len(fields) != NFIELDS:
        raise TraceFormatError("header field table has %s entries, expected %d"
                               % (len(fields) if isinstance(fields, list) else "?",
                                  NFIELDS))
    names = [f.get("name") for f in fields]
    if names != list(FIELD_NAMES):
        i = _first_diff(names, list(FIELD_NAMES))
        got = names[i] if i < len(names) else "<missing>"
        want = FIELD_NAMES[i] if i < len(FIELD_NAMES) else "<none>"
        raise TraceFormatError(
            "header field table does not match this reader's FIELD_SPECS "
            "(this is exactly the silent-misread case the version field exists "
            "to prevent); first mismatch at index %d: %r vs %r"
            % (i, got, want))
    if not isinstance(header.get("settings"), dict):
        raise TraceValidationError("header settings is not an object")
    if not isinstance(header.get("package"), dict):
        raise TraceValidationError("header package is not an object")
    return header


def _first_diff(a, b):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    return min(len(a), len(b))


# ── trace object ─────────────────────────────────────────────────────────────
class Trace:
    """A loaded v1 trace: header + fixed-stride float32 records."""

    def __init__(self, header, flat, record_count, path=None, file_size=0):
        self.header = header
        self._a = flat                     # array('f'), native order
        self.record_count = int(record_count)
        self.path = path
        self.file_size = file_size

    # -- access -------------------------------------------------------------
    def __len__(self):
        return self.record_count

    def __getitem__(self, frame):
        base = frame * NFIELDS
        return {n: self._a[base + i] for i, n in enumerate(FIELD_NAMES)}

    def value(self, frame, name):
        return self._a[frame * NFIELDS + FIELD_INDEX[name]]

    def column(self, name):
        """All values of one field, as an array('f')."""
        k = FIELD_INDEX[name]
        return self._a[k:self.record_count * NFIELDS:NFIELDS]

    # -- convenience views --------------------------------------------------
    def focus(self, frame):
        b = frame * NFIELDS
        return (self._a[b + 1], self._a[b + 2], self._a[b + 3])

    def rotation(self, frame):
        b = frame * NFIELDS
        return (self._a[b + 4], self._a[b + 5], self._a[b + 6])

    def car_pos(self, frame):
        b = frame * NFIELDS
        return (self._a[b + 9], self._a[b + 10], self._a[b + 11])

    def car_rot(self, frame):
        b = frame * NFIELDS
        return (self._a[b + 12], self._a[b + 13], self._a[b + 14])

    def car_vel(self, frame):
        b = frame * NFIELDS
        return (self._a[b + 15], self._a[b + 16], self._a[b + 17])

    def ball_pos(self, frame):
        b = frame * NFIELDS
        return (self._a[b + 18], self._a[b + 19], self._a[b + 20])

    @property
    def source(self):
        return self.header.get("source", "?")

    @property
    def synthetic(self):
        return self.source == "synthetic"

    def derived_pov(self, frame, eye, center, fov):
        """Our camera's GL pair expressed as RL's POV (see module docstring)."""
        return pov_from_eye_center(eye, center, fov, self.value(frame, "out_time"))

    def describe(self):
        h = self.header
        s = h.get("settings", {})
        p = h.get("package", {})
        lines = [
            "file        : %s (%d bytes)" % (self.path or "<memory>", self.file_size),
            "magic/version: RLCT v%d, stride %d B, %d fields"
            % (VERSION, STRIDE, NFIELDS),
            "source      : %s%s" % (h.get("source"), "  <-- SYNTHETIC FIXTURE"
                                    if h.get("source") == "synthetic" else ""),
            "scenario    : %s" % h.get("scenario"),
            "seed        : %s (pinned=%s)"
            % (h.get("seed"), h.get("seed_pinned")),
            "rate        : tick_rate %.3f Hz, sample_rate %.3f Hz"
            % (h.get("tick_rate", 0.0), h.get("sample_rate_hz", 0.0)),
            "package     : %s" % (p.get("identity"),),
            "settings    : fov=%.3f height=%.3f pitch=%.3f distance=%.3f "
            "stiffness=%.3f swivel=%.3f transition=%.3f invert_pitch=%s shake=%s "
            "snap=%s"
            % (s.get("fov", float("nan")), s.get("height", float("nan")),
               s.get("pitch", float("nan")), s.get("distance", float("nan")),
               s.get("stiffness", float("nan")), s.get("swivel_speed", float("nan")),
               s.get("transition_speed", float("nan")),
               s.get("invert_swivel_pitch"), s.get("camera_shake"), s.get("snap")),
            "convention  : pov=%s swivel=%s"
            % (h.get("pov_convention"), h.get("swivel_convention")),
            "records     : %d (%.3f s at %.1f Hz)"
            % (self.record_count,
               self.record_count / max(1e-9, h.get("sample_rate_hz") or 1.0),
               h.get("sample_rate_hz") or 0.0),
            "generator   : %s (%s)" % (h.get("generator"), h.get("created_utc")),
        ]
        if h.get("notes"):
            lines.append("notes       : %s" % h.get("notes"))
        return "\n".join(lines)


# ── writer ───────────────────────────────────────────────────────────────────
def _row_to_values(row):
    """dict (partial ok) or sequence -> list of NFIELDS floats."""
    if isinstance(row, dict):
        return [float(row.get(n, 0.0)) for n in FIELD_NAMES]
    vals = [float(v) for v in row]
    if len(vals) != NFIELDS:
        raise TraceFormatError("record has %d values, expected %d"
                               % (len(vals), NFIELDS))
    return vals


def write_trace(path, header, rows, overwrite=True):
    """Write a v1 trace. `rows` is an iterable of dicts or value sequences.

    Returns (bytes_written, record_count, crc32).
    """
    validate_header(header)
    rows = list(rows)
    flat = array("f")
    for r in rows:
        flat.extend(_row_to_values(r))
    blob = flat.tobytes()
    if sys.byteorder != "little":
        swapped = array("f", flat)
        swapped.byteswap()
        blob = swapped.tobytes()
    crc = zlib.crc32(blob) & 0xFFFFFFFF
    hdr_json = json.dumps(header, sort_keys=True,
                          separators=(",", ":")).encode("utf-8")
    prefix = FILE_PREFIX.pack(MAGIC, VERSION, len(hdr_json), STRIDE,
                              NFIELDS, len(rows), crc)
    if not overwrite and os.path.exists(path):
        raise TraceError("refusing to overwrite %s" % path)
    d = os.path.dirname(os.path.abspath(path))
    if d and not os.path.isdir(d):
        os.makedirs(d, exist_ok=True)
    with open(path, "wb") as f:
        f.write(prefix)
        f.write(hdr_json)
        f.write(blob)
    return PAYLOAD_OFFSET + len(hdr_json) + len(blob), len(rows), crc


# ── loader ───────────────────────────────────────────────────────────────────
def load(path, strict=True):
    """Read a v1 trace. Raises TraceFormatError / TraceValidationError."""
    path = os.path.abspath(path)
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < PAYLOAD_OFFSET:
        raise TraceFormatError("%s: %d bytes is shorter than the %d-byte header"
                               % (path, len(raw), PAYLOAD_OFFSET))
    (magic, version, header_len, stride, field_count,
     record_count, crc) = FILE_PREFIX.unpack_from(raw, 0)
    if magic != MAGIC:
        raise TraceFormatError("%s: magic %r, expected %r (not a camera trace?)"
                               % (path, magic, MAGIC))
    if version != VERSION:
        raise TraceFormatError("%s: trace v%d, this reader speaks v%d"
                               % (path, version, VERSION))
    if stride != STRIDE or field_count != NFIELDS:
        raise TraceFormatError(
            "%s: stride/field_count %d/%d != %d/%d for v%d "
            "(a v1 file must match its own field table)"
            % (path, stride, field_count, STRIDE, NFIELDS, VERSION))
    hdr_end = PAYLOAD_OFFSET + header_len
    if hdr_end > len(raw):
        raise TraceFormatError("%s: header_len %d runs past end of file"
                               % (path, header_len))
    try:
        header = json.loads(raw[PAYLOAD_OFFSET:hdr_end].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        raise TraceFormatError("%s: header JSON is unreadable (%s)" % (path, e))
    validate_header(header)
    body = raw[hdr_end:]
    expect = record_count * STRIDE
    if len(body) != expect:
        raise TraceFormatError(
            "%s: record block is %d bytes, expected %d (%d records x %d B) — "
            "truncated or padded file"
            % (path, len(body), expect, record_count, STRIDE))
    crc_calc = zlib.crc32(body) & 0xFFFFFFFF
    if crc_calc != crc:
        raise TraceFormatError("%s: crc32 %08x != stored %08x (corrupt record "
                               "block)" % (path, crc_calc, crc))
    flat = array("f")
    flat.frombytes(body)
    if sys.byteorder != "little":
        flat.byteswap()
    tr = Trace(header, flat, record_count, path=path, file_size=len(raw))
    if strict:
        validate_trace(tr)
    return tr


def validate_trace(tr):
    """Content invariants beyond the byte layout. Returns the list of checks."""
    checks = []
    h = tr.header
    if tr.record_count <= 0:
        raise TraceValidationError("trace holds no records")
    checks.append(("V8", "record_count > 0", "PASS", "%d" % tr.record_count))
    rate = h.get("sample_rate_hz") or h.get("tick_rate")
    if not rate or rate <= 0:
        raise TraceValidationError("header sample_rate_hz/tick_rate is %r" % (rate,))
    checks.append(("V9", "sample_rate_hz > 0", "PASS", "%.3f Hz" % rate))
    dt_col = tr.column("dt")
    bad_dt = [i for i, v in enumerate(dt_col) if not (0.0 < v <= 1.0)]
    if bad_dt:
        raise TraceValidationError(
            "dt is not a plausible frame delta in %d record(s); first at frame %d "
            "= %r" % (len(bad_dt), bad_dt[0], dt_col[bad_dt[0]]))
    checks.append(("V10", "dt in (0, 1] s everywhere", "PASS",
                   "median %.5f s" % _median([abs(v) for v in dt_col])))
    bad = []
    for name in FIELD_NAMES:
        col = tr.column(name)
        for i, v in enumerate(col):
            if v != v or v in (float("inf"), float("-inf")):
                bad.append((i, name, v))
                break
        if bad:
            break
    if bad:
        raise TraceValidationError("non-finite value: frame %d field %r = %r"
                                   % bad[0])
    checks.append(("V11", "all fields finite", "PASS",
                   "%d frames x %d fields" % (tr.record_count, NFIELDS)))
    return checks


def _median(vals):
    if not vals:
        return 0.0
    s = sorted(vals)
    n = len(s)
    mid = n // 2
    return s[mid] if n % 2 else 0.5 * (s[mid - 1] + s[mid])


# ── synthetic fixture generator (NEVER evidence about our camera) ────────────
def _wrap_pi(a):
    """Wrap an angle to (-pi, pi] (the principal value Camera.cpp keeps)."""
    a = math.fmod(a + math.pi, 2.0 * math.pi)
    if a <= 0.0:
        a += 2.0 * math.pi
    return a - math.pi


def _smoothstep(a, b, x):
    if b <= a:
        return 1.0 if x >= b else 0.0
    t = (x - a) / (b - a)
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return t * t * (3.0 - 2.0 * t)


def _scenario_state(name, t, seed):
    """Deterministic world state for one §6.2 scenario at time `t` (seconds).

    Returns a dict of the fields the generator needs. Pure function of
    (name, t, seed) so a trace is reproducible byte-for-byte.
    """
    # stable hash: PYTHONHASHSEED randomises `hash(str)`, zlib.crc32 does not
    rnd = random.Random((seed or 0) * 1000003 + zlib.crc32(name.encode("utf-8")))
    jitter = (rnd.random() - 0.5)
    car = [0.0, 0.0, 17.0]
    car_rot = [0.0, 0.0, 0.0]
    car_vel = [0.0, 0.0, 0.0]
    ball = [1500.0, 200.0, 93.0]
    ball_vel = [0.0, 0.0, 0.0]
    inputs = dict(ball_cam=1.0, swivel_in_x=0.0, swivel_in_y=0.0,
                  throttle=0.0, steer=0.0, pitch_in=0.0, yaw_in=0.0,
                  roll_in=0.0, boost=0.0, jump=0.0, airborne=0.0,
                  look_behind=0.0)
    car_yaw_rad = 0.0

    if name == "ballcam-toggle":
        # rolling start, ball cam off at the halfway mark
        dur = 3.0
        v = 600.0 + 300.0 * _smoothstep(0.0, dur, t)
        car[0] = v * t
        car_vel = [v, 0.0, 0.0]
        ball = [2600.0 + 200.0 * t, 400.0, 300.0 + 40.0 * math.sin(4.0 * t)]
        inputs["ball_cam"] = 0.0 if t >= dur * 0.5 else 1.0
        inputs["throttle"] = 1.0
    elif name == "target-switch-mid-air":
        # airborne from t=1.0; the ball cam target jumps laterally at t=1.6
        air = _smoothstep(1.0, 1.15, t) * (1.0 - _smoothstep(2.4, 2.6, t))
        car[2] = 17.0 + 420.0 * air * math.sin(math.pi * min(1.0, max(0.0, (t - 1.0) / 1.6)))
        car[0] = 500.0 * t
        car_vel = [500.0, 0.0, 0.0]
        ball = [1800.0, 900.0 if t >= 1.6 else -900.0, 700.0]
        ball_vel = [0.0, 0.0, 0.0]
        inputs["airborne"] = 1.0 if air > 0.5 else 0.0
        inputs["jump"] = 1.0 if 1.0 <= t < 1.1 else 0.0
        inputs["ball_cam"] = 1.0
        inputs["throttle"] = 1.0
    elif name == "swivel-sweep-release":
        # sweep right, hold, release at t=2.3 (RL's die-rate lives here)
        car[0] = 300.0 * t
        car_vel = [300.0, 0.0, 0.0]
        ball = [1900.0, 900.0, 260.0]
        if 0.3 <= t < 1.1:
            inputs["swivel_in_x"] = _smoothstep(0.3, 1.1, t)
        elif 1.1 <= t < 1.9:
            inputs["swivel_in_x"] = 1.0
        elif 1.9 <= t < 2.3:
            inputs["swivel_in_x"] = 1.0 - _smoothstep(1.9, 2.3, t)
        inputs["ball_cam"] = 1.0
    elif name == "landing-air-to-ground":
        # fall onto the ground branch: airborne until touchdown at t=1.8
        air = t < 1.8
        h = 600.0 * max(0.0, (1.8 - t) / 1.8)
        car[2] = 17.0 + h
        car[0] = 350.0 * t
        car_vel = [350.0, 0.0, -600.0 if air else 0.0]
        ball = [1700.0, 300.0, 93.0]
        inputs["airborne"] = 1.0 if air else 0.0
        inputs["ball_cam"] = 0.0
    elif name == "supersonic-straight":
        # 0 -> 2300 uu/s in a straight line (distance/FOV kick)
        dur = 3.0
        v = 2300.0 * _smoothstep(0.0, dur * 0.8, t)
        car[0] = 0.5 * v * t
        car_vel = [v, 0.0, 0.0]
        ball = [4000.0, 0.0, 93.0]
        inputs["throttle"] = 1.0
        inputs["boost"] = 1.0
        inputs["ball_cam"] = 0.0
    elif name == "wall-contact":
        # drive into the wall at x=3800, scrub, rebound
        hit = 1.5
        v = 1500.0
        x = v * t
        if t > hit:
            x = 3800.0 - 400.0 * (t - hit)
        car[0] = x
        car_vel = [v if t <= hit else -400.0, 0.0, 0.0]
        ball = [3300.0, 600.0, 300.0]
        ball_vel = [0.0, 400.0, 0.0]
        inputs["ball_cam"] = 1.0
        inputs["throttle"] = 1.0
    elif name == "dribble-hold":
        # ball resting on the roof with contact micro-bounce
        car[0] = 500.0 + 400.0 * t
        car_vel = [400.0, 0.0, 0.0]
        ball = [car[0] + 5.0,
                car[1] + 4.0 * math.sin(9.0 * t) + jitter,
                17.0 + 84.0 + 0.6 * math.sin(23.0 * t)]
        ball_vel = [400.0, 0.0, 0.0]
        inputs["ball_cam"] = 0.0
        inputs["throttle"] = 1.0
    elif name == "goal-ballcam-reset":
        # ball crosses the goal line, then resets to the center spot
        t_goal = 2.0
        ball = [0.0, 0.0, 93.0] if t >= t_goal else [1200.0 + 600.0 * t, 0.0, 93.0]
        ball_vel = [0.0, 0.0, 0.0] if t >= t_goal else [600.0, 0.0, 0.0]
        car[0] = 200.0 * t
        car_vel = [200.0, 0.0, 0.0]
        inputs["ball_cam"] = 1.0
    else:
        raise ValueError("unknown scenario %r (known: %s)"
                         % (name, ", ".join(SCENARIO_NAMES)))

    car_yaw_rad = math.atan2(car_vel[1], car_vel[0]) if (car_vel[0] or car_vel[1]) else 0.0
    car_rot = [0.0, car_yaw_rad * ROT_PER_RAD, 0.0]
    # the swivel *input* is integrated into a die-rate state by the caller
    if name == "swivel-sweep-release":
        swivel_in = (_smoothstep(0.3, 1.1, t) if t < 1.1
                     else (1.0 if t < 1.9 else (1.0 - _smoothstep(1.9, 2.3, t))))
    else:
        swivel_in = inputs["swivel_in_x"]
    return dict(car=car, car_rot=car_rot, car_vel=car_vel, ball=ball,
                ball_vel=ball_vel, inputs=inputs, swivel_in=swivel_in, t=t)


def generate_synthetic(scenario="swivel-sweep-release", frames=480, seed=7,
                       settings=None, sample_rate=120.0, notes=""):
    """Build a **synthetic** v1 trace (header, rows).

    This is a fixture: it exercises the format, the loader and the diff tool with
    no game and no Psyonix data. It is NOT a measurement and any error table run
    against it says nothing about our camera's accuracy — the header says
    `source: "synthetic"` and the diff tool prints that warning itself.
    """
    if scenario not in SCENARIO_NAMES:
        raise ValueError("unknown scenario %r (known: %s)"
                         % (scenario, ", ".join(SCENARIO_NAMES)))
    s = dict(DEFAULT_SETTINGS)
    if settings:
        s.update(settings)
    rate = float(sample_rate)
    dt = 1.0 / rate
    rows = []
    focus_state = None
    eye_state = None
    pitch_lag = 0.0
    swivel_yaw = 0.0   # die-rate integrator state (so CurrentSwivel is not flat)
    swivel_idle = 0.0  # s since the last swivel input (fixture snap-back timer)
    for i in range(int(frames)):
        t = i / rate
        st = _scenario_state(scenario, t, seed)
        car = st["car"]
        ball = st["ball"]
        car_rot = st["car_rot"]
        car_vel = st["car_vel"]
        inputs = st["inputs"]
        speed = math.sqrt(sum(v * v for v in car_vel))
        # ---- RL-ish POV model (a fixture shape, NOT measured truth) --------
        target = ball[:] if inputs["ball_cam"] > 0.5 else [car[0] + 180.0,
                                                          car[1], car[2] + 60.0]
        if focus_state is None:
            focus_state = target[:]
        k_focus = 1.0 - math.exp(-(1.0 / 0.06) / rate)
        for a in range(3):
            focus_state[a] += (target[a] - focus_state[a]) * k_focus
        stiff = float(s["stiffness"])
        frac = min(1.0, speed / 2300.0)
        d_eff = float(s["distance"]) + (1.0 - stiff) * 100.0 * frac
        ang = math.radians(float(s["pitch"])) + math.radians(2.0) * (1.0 - stiff) * frac
        pitch_lag += (-ang - pitch_lag) * 0.25
        back = d_eff * math.cos(pitch_lag)
        up = float(s["height"]) - d_eff * math.sin(pitch_lag)
        # Crude die-rate integrator: the rate is the CITED RL one ("90 deg per
        # 1.3 s at 1.0, linear in the slider", Rocket Science #2 -> camel
        # Camera.cpp kSwivelRate), decayed when the stick is released. A fixture
        # shape only; it is not a claim about RL's SwivelDieRate.
        swivel_rate = SWIVEL_RATE_PER_UNIT * float(s["swivel_speed"])
        swivel_yaw += st["swivel_in"] * swivel_rate * dt
        # The offset is an ANGLE: keep it principal (-pi, pi] exactly as
        # `Camera.cpp UpdateSwivel` wraps yawOff_, so a long hold cannot leave
        # the fixture snapping back the long way round.
        swivel_yaw = _wrap_pi(swivel_yaw)
        swivel_idle = 0.0 if abs(st["swivel_in"]) > 0.004 else swivel_idle + dt
        if swivel_idle > 0.05:
            step = max(swivel_rate * 2.0, 6.0) * dt
            swivel_yaw -= max(-step, min(step, swivel_yaw))
            if abs(swivel_yaw) < 1e-4:
                swivel_yaw = 0.0
        # The eye rides the sphere behind its pivot: the ball-cam pivot is the
        # target bearing, the car-cam pivot is the car facing (this mirrors both
        # the RL states and `Camera.cpp` BallCamPov/CarPov).
        base_yaw = (math.atan2(ball[1] - car[1], ball[0] - car[0])
                    if inputs["ball_cam"] > 0.5 else car_rot[1] / ROT_PER_RAD)
        yaw_c = base_yaw + swivel_yaw
        eye = [car[0] - back * math.cos(yaw_c),
               car[1] - back * math.sin(yaw_c),
               car[2] + up]
        if eye_state is None:
            eye_state = eye[:]
        k_eye = 1.0 - math.exp(-(2.0 + 6.0 * stiff) / rate)
        for a in range(3):
            eye_state[a] += (eye[a] - eye_state[a]) * k_eye
        dx = focus_state[0] - eye_state[0]
        dy = focus_state[1] - eye_state[1]
        dz = focus_state[2] - eye_state[2]
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        # the swivel rotates the VIEW too (`lookYaw = aim + yawOff` in
        # Camera.cpp), so the recorded rotation is off-target by the free-look
        # offset, not aimed at the ball.
        pitch_aim = math.atan2(dz, math.hypot(dx, dy))
        yaw_aim = math.atan2(dy, dx) + swivel_yaw
        fx_, fy_, fz_ = fwd_from_rotator(pitch_aim * ROT_PER_RAD,
                                        yaw_aim * ROT_PER_RAD)
        focus_rec = [eye_state[0] + fx_ * dist,
                     eye_state[1] + fy_ * dist,
                     eye_state[2] + fz_ * dist]
        rot = rotator_from_dir((fx_, fy_, fz_))
        fov = (float(s["fov"]) + 5.0 * frac
               + (5.0 if speed >= 2200.0 else 0.0))
        row = {n: 0.0 for n in FIELD_NAMES}
        row.update({
            "out_time": t,
            "focus_x": focus_rec[0], "focus_y": focus_rec[1],
            "focus_z": focus_rec[2],
            "rot_pitch": rot[0], "rot_yaw": rot[1], "rot_roll": rot[2],
            "distance": dist, "fov": fov,
            "car_x": car[0], "car_y": car[1], "car_z": car[2],
            "car_pitch": car_rot[0], "car_yaw": car_rot[1], "car_roll": car_rot[2],
            "car_vx": car_vel[0], "car_vy": car_vel[1], "car_vz": car_vel[2],
            "ball_x": ball[0], "ball_y": ball[1], "ball_z": ball[2],
            "ball_vx": st["ball_vel"][0], "ball_vy": st["ball_vel"][1],
            "ball_vz": st["ball_vel"][2],
            "dt": dt,
            "current_swivel_yaw": swivel_yaw,
            "current_swivel_pitch": 0.0,
        })
        for k, v in inputs.items():
            row[k] = float(v)
        rows.append(row)
    header = make_header(source="synthetic", scenario=scenario,
                         tick_rate=rate, settings=s, seed=seed,
                         sample_rate_hz=rate,
                         notes=notes or ("deterministic fixture grown by "
                                         "cam_trace.generate_synthetic; NOT a "
                                         "measurement"))
    return header, rows


# ── self-check: format round trip + convention round trip ───────────────────
def selfcheck(path=None, scenario="swivel-sweep-release", frames=240, seed=7):
    """Write -> load -> compare. Prints the format gate and the convention gate.

    Returns (ok, results) where results is a list of (check, detail, maxerr).
    """
    import tempfile
    tmpdir = None
    if path is None:
        tmpdir = tempfile.mkdtemp(prefix="rltrace-selfcheck-")
        path = os.path.join(tmpdir, "%s.rlt" % scenario)
    header, rows = generate_synthetic(scenario=scenario, frames=frames, seed=seed)
    nbytes, nrec, crc = write_trace(path, header, rows)
    tr = load(path)
    results = []

    # 1) byte/field round trip: what went in is what came out, bit for bit.
    worst = {}
    for name in FIELD_NAMES:
        w = 0.0
        k = FIELD_INDEX[name]
        for i in range(nrec):
            want = quantize_f32(float(rows[i].get(name, 0.0)))
            got = tr.value(i, name)
            d = abs(got - want)
            if d > w:
                w = d
        worst[name] = w
    fmt_max = max(worst.values()) if worst else 0.0
    results.append(("SELF1 format round trip (write->load, %d records)" % nrec,
                    "max |loaded - written| over %d fields" % NFIELDS, fmt_max))

    # 2) POV convention round trip: record -> eye -> record, at field precision.
    worst_c = {"focus": 0.0, "distance": 0.0, "rotation": 0.0, "fov": 0.0}
    raw_c = {"focus": 0.0, "distance": 0.0, "rotation": 0.0}
    computed = 0
    for i in range(nrec):
        rec = {"focus": tr.focus(i), "rotation": tr.rotation(i),
               "distance": tr.value(i, "distance"), "fov": tr.value(i, "fov"),
               "time": tr.value(i, "out_time")}
        eye = eye_from_pov(rec["focus"], rec["rotation"], rec["distance"])
        pov = pov_from_eye_center(eye, rec["focus"], rec["fov"], rec["time"])
        if pov is None:
            continue
        computed += 1
        e = pov_field_errors(rec, pov, quantize=True)
        raw = pov_field_errors(rec, pov, quantize=False)
        worst_c["focus"] = max(worst_c["focus"], e["focus_axis_uu"])
        worst_c["distance"] = max(worst_c["distance"], e["distance_uu"])
        worst_c["rotation"] = max(worst_c["rotation"], e["rotation_mag"])
        worst_c["fov"] = max(worst_c["fov"], e["fov_deg"])
        raw_c["focus"] = max(raw_c["focus"], raw["focus_axis_uu"])
        raw_c["distance"] = max(raw_c["distance"], raw["distance_uu"])
        raw_c["rotation"] = max(raw_c["rotation"], raw["rotation_mag"])
    for key in ("focus", "distance", "rotation", "fov"):
        unit = {"focus": "uu", "distance": "uu", "rotation": "rotator units",
                "fov": "degrees"}[key]
        results.append(("SELF2 POV convention round trip (%s)" % key,
                        "record -> eye -> record, %d frames, %s"
                        % (computed, unit), worst_c[key]))
    # the un-quantized fp64 residual, so the 0.0 above is not mistaken for magic
    for key in ("focus", "distance", "rotation"):
        results.append(("SELF3 sub-ulp residual (%s, informational)" % key,
                        "same round trip WITHOUT float32 quantization", raw_c[key]))
    ok = fmt_max == 0.0 and all(v == 0.0 for v in worst_c.values())
    if tmpdir is not None:
        try:
            os.unlink(path)
            os.rmdir(tmpdir)
        except OSError:
            pass
    return ok, results


# ── CLI ─────────────────────────────────────────────────────────────────────
def _cmd_gen(a):
    header, rows = generate_synthetic(scenario=a.scenario, frames=a.frames,
                                      seed=a.seed, sample_rate=a.rate)
    path = a.out or os.path.join(DEFAULT_TRACE_DIR,
                                 "synthetic_%s.rlt" % a.scenario.replace("-", "_"))
    nbytes, nrec, crc = write_trace(path, header, rows)
    print("GEN     synthetic trace: %d records, %d bytes, crc32 %08x"
          % (nrec, nbytes, crc))
    print("        scenario=%s seed=%s rate=%.1f Hz"
          % (a.scenario, a.seed, a.rate))
    print("        out=%s" % path)
    print("        SYNTHETIC FIXTURE — never evidence about the real camera")
    return 0


def _cmd_gen_all(a):
    os.makedirs(a.outdir, exist_ok=True)
    for name in SCENARIO_NAMES:
        header, rows = generate_synthetic(scenario=name, frames=a.frames,
                                          seed=a.seed, sample_rate=a.rate)
        path = os.path.join(a.outdir, "%s.rlt" % name.replace("-", "_"))
        nbytes, nrec, crc = write_trace(path, header, rows)
        print("GEN     %-22s %4d records %7d B crc32 %08x  %s"
              % (name, nrec, nbytes, crc, path))
    print("GEN     all %d scenarios are SYNTHETIC fixtures" % len(SCENARIO_NAMES))
    return 0


def _cmd_info(a):
    tr = load(a.trace)
    print(tr.describe())
    if a.json:
        print(json.dumps(tr.header, indent=2, sort_keys=True))
    return 0


def _cmd_validate(a):
    tr = load(a.trace)
    print(tr.describe())
    print("")
    fails = 0
    with open(a.trace, "rb") as f:
        raw = f.read(PAYLOAD_OFFSET)
    (magic, version, header_len, stride, field_count, record_count,
     crc) = FILE_PREFIX.unpack_from(raw, 0)
    checks = [
        ("V1", "magic", "PASS", "%r" % magic),
        ("V2", "version", "PASS", "v%d (reader v%d)" % (version, VERSION)),
        ("V3", "stride/field_count", "PASS", "%d B / %d fields"
         % (stride, field_count)),
        ("V4", "header JSON keys", "PASS",
         "%d keys, source=%s" % (len(tr.header), tr.header.get("source"))),
        ("V5", "field table matches reader", "PASS",
         "%d names in order" % len(tr.header["fields"])),
        ("V6", "record block size", "PASS",
         "%d == %d x %d" % (record_count * stride, record_count, stride)),
        ("V7", "crc32(record block)", "PASS", "%08x" % crc),
    ]
    try:
        checks.extend(validate_trace(tr))
    except TraceValidationError as e:
        fails += 1
        print("VALIDATE FAIL  content: %s" % e)
    for cid, what, status, detail in checks:
        if status != "PASS":
            fails += 1
        print("VALIDATE %s  %-4s %-32s %s" % (status, cid, what, detail))
    print("VALIDATE %d PASS / %d FAIL  (%s)"
          % (len(checks) - fails, fails, a.trace))
    return 0 if fails == 0 else 2


def _cmd_selfcheck(a):
    ok, results = selfcheck(path=a.out, frames=a.frames)
    for what, detail, err in results:
        if "informational" in what:
            status = "INFO"
        else:
            status = "PASS" if err == 0.0 else "FAIL"
        print("SELFCHECK %-4s %-56s %-24s max_err=%.9g"
              % (status, what, detail, err))
    print("SELFCHECK %s  format+convention self-consistency (exact 0.0 required)"
          % ("PASS" if ok else "FAIL"))
    return 0 if ok else 2


def _cmd_deck(a):
    print("SCENARIO DECK (§6.2) — one trace file each, offline freeplay only")
    for i, (name, desc) in enumerate(SCENARIO_DECK, 1):
        print("  %d. %-22s %s" % (i, name, desc))
    print("")
    print("  live naming: decomp/traces/<scenario>.rlt   (gitignored)")
    print("  seed-pin every sim-driven scenario: header.seed must be non-null")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="RL camera trace format v1 (writer/loader/validator + "
                    "synthetic fixture). See tools/decomp/camera_trace_diff.py "
                    "for the replay/diff side.")
    sub = ap.add_subparsers(dest="cmd")

    g = sub.add_parser("gen", help="write one synthetic scenario trace")
    g.add_argument("--scenario", default="swivel-sweep-release",
                   choices=SCENARIO_NAMES)
    g.add_argument("--frames", type=int, default=480)
    g.add_argument("--seed", type=int, default=7)
    g.add_argument("--rate", type=float, default=120.0)
    g.add_argument("--out")
    g.set_defaults(func=_cmd_gen)

    ga = sub.add_parser("gen-all", help="write the whole synthetic deck")
    ga.add_argument("--outdir",
                    default=os.path.join(DEFAULT_TRACE_DIR, "synthetic"))
    ga.add_argument("--frames", type=int, default=480)
    ga.add_argument("--seed", type=int, default=7)
    ga.add_argument("--rate", type=float, default=120.0)
    ga.set_defaults(func=_cmd_gen_all)

    i = sub.add_parser("info", help="print a trace header")
    i.add_argument("trace")
    i.add_argument("--json", action="store_true")
    i.set_defaults(func=_cmd_info)

    v = sub.add_parser("validate", help="validate a trace (V1..V11)")
    v.add_argument("trace")
    v.set_defaults(func=_cmd_validate)

    sc = sub.add_parser("selfcheck", help="format + convention round trip")
    sc.add_argument("--out")
    sc.add_argument("--frames", type=int, default=240)
    sc.set_defaults(func=_cmd_selfcheck)

    d = sub.add_parser("deck", help="list the §6.2 scenario deck")
    d.set_defaults(func=_cmd_deck)

    a = ap.parse_args(argv)
    if not getattr(a, "func", None):
        ap.print_help()
        return 1
    return a.func(a)


if __name__ == "__main__":
    sys.exit(main())
