#!/usr/bin/env python3
"""verify_vs_rocketsim.py — A3 headless check: our tables vs RocketSim authority.

Asserts every value in src/physics/CarPresets.h and src/physics/Tuning.h
against third_party/RocketSim sources. MIT only. RocketSim (c) 2022 ZealanL.

Usage:
    python3 src/physics/verify_vs_rocketsim.py [--rocketsim third_party/RocketSim]
Exit code 0 = all checks pass, 1 = any MISMATCH / missing file.
"""
import argparse
import math
import re
import sys
from pathlib import Path

WS = Path(__file__).resolve().parents[2]  # workspace root (…/RocketLeague)
TOL = 1e-3  # absolute tolerance in uu (hitbox literals have 4 decimals)


def strip_comments(src: str) -> str:
    out = []
    for line in src.splitlines():
        # cut // comments (no string literals contain // in these sources)
        i = line.find("//")
        out.append(line[:i] if i != -1 else line)
    return "\n".join(out)


def cxx_eval(expr: str, env: dict) -> float:
    e = expr.strip()
    e = e.replace("M_SQRT1_2", repr(math.sqrt(0.5)))
    e = e.replace("M_PI_4", repr(math.pi / 4))
    e = e.replace("M_PI", repr(math.pi))
    e = re.sub(r"(\d[\d.]*)f\b", r"\1", e)  # strip float suffixes
    return float(eval(e, {"__builtins__": {}}, dict(env)))


def split_top_commas(s: str):
    parts, depth, cur = [], 0, ""
    for ch in s:
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


def brace_blocks(s: str, start: int):
    """Return (inner_text, end_index) for the {…} block opening at/after start."""
    i = s.index("{", start)
    depth, j = 0, i
    while True:
        if s[j] == "{":
            depth += 1
        elif s[j] == "}":
            depth -= 1
            if depth == 0:
                return s[i + 1:j], j + 1
        j += 1


failures = []
checked = {"scalar": 0, "curve": 0, "preset": 0}


def check_scalar(label, got, want, cite):
    checked["scalar"] += 1
    ok = abs(got - want) <= TOL * max(1.0, abs(want))
    print(f"  [{'PASS' if ok else 'MISMATCH'}] {label}: ours={got:.6g} "
          f"authority={want:.6g} ({cite})")
    if not ok:
        failures.append(label)


def check_curve(label, got_pts, want_pts, cite):
    checked["curve"] += 1
    ok = (len(got_pts) == len(want_pts) and all(
        abs(g - w) <= TOL * max(1.0, abs(w))
        for gp, wp in zip(got_pts, want_pts) for g, w in zip(gp, wp)))
    print(f"  [{'PASS' if ok else 'MISMATCH'}] {label}: {len(got_pts)} pts ({cite})")
    if not ok:
        print(f"      ours={got_pts}")
        print(f"      auth={want_pts}")
        failures.append(label)


# ---------------------------------------------------------------- CLI / load
ap = argparse.ArgumentParser()
ap.add_argument("--rocketsim", default=str(WS / "third_party" / "RocketSim"))
args = ap.parse_args()
rs = Path(args.rocketsim)
rlconst_p = rs / "src" / "RLConst.h"
carcfg_p = rs / "src" / "Sim" / "Car" / "CarConfig" / "CarConfig.cpp"
ours_presets = WS / "src" / "physics" / "CarPresets.h"
ours_tuning = WS / "src" / "physics" / "Tuning.h"
for p in (rlconst_p, carcfg_p, ours_presets, ours_tuning):
    if not p.is_file():
        print(f"FATAL: missing {p}")
        sys.exit(1)

rl = strip_comments(rlconst_p.read_text())
cc = strip_comments(carcfg_p.read_text())
hp = ours_presets.read_text()
ht = strip_comments(ours_tuning.read_text())

# ------------------------------------------------- parse RLConst.h scalars
env = {}
m = re.search(r"constexpr\s+float\b", rl)
decl = rl[m.start():]
decl = decl[:decl.index(";", decl.index("DEMO_RESPAWN_TIME")) + 1]
for stmt in split_top_commas(decl):
    mm = re.match(r"\s*(?:constexpr\s+float\s+)?([A-Za-z_]\w*)\s*=\s*(.*?);?\s*$",
                  stmt, re.S)
    if not mm:
        continue
    name, expr = mm.group(1), mm.group(2)
    try:
        env[name] = cxx_eval(expr, env)
    except Exception as e:  # noqa: BLE001 — report and continue
        print(f"  [SKIP] could not eval {name}: {e}")

# namespaces used by checks
ns_block = re.search(r"namespace BoostPads \{(.*?)\n\t\}", rl, re.S).group(1)
for n in ["CYL_HEIGHT", "CYL_RAD_BIG", "CYL_RAD_SMALL", "BOX_HEIGHT",
          "BOX_RAD_BIG", "BOX_RAD_SMALL", "COOLDOWN_BIG", "COOLDOWN_SMALL",
          "BOOST_AMOUNT_BIG", "BOOST_AMOUNT_SMALL"]:
    mm = re.search(rf"{n}\s*=\s*([^,;]+)", ns_block)
    env[n] = cxx_eval(mm.group(1), env)
for ns, names in (
    ("BTVehicle", ["SUSPENSION_FORCE_SCALE_FRONT", "SUSPENSION_FORCE_SCALE_BACK",
                   "SUSPENSION_STIFFNESS", "WHEELS_DAMPING_COMPRESSION",
                   "WHEELS_DAMPING_RELAXATION", "MAX_SUSPENSION_TRAVEL",
                   "SUSPENSION_SUBTRACTION"]),
):
    for n in names:
        mm = re.search(rf"{n}\s*=\s*([^,;]+)", rl)
        env[f"{ns}::{n}"] = cxx_eval(mm.group(1), env)


def rs_curve(name):
    i = rl.index(name)
    inner, _ = brace_blocks(rl, rl.index("{", i))
    pts = []
    for pm in re.finditer(r"\{\s*([^,{}]+),\s*([^,{}]+)\}", inner):
        pts.append((cxx_eval(pm.group(1), env), cxx_eval(pm.group(2), env)))
    return pts


# ------------------------------------------------- parse CarConfig.cpp
def array_rows(text, name, per_row):
    i = text.index(name)
    inner, _ = brace_blocks(text, text.index("{", i))
    rows = []
    for rm in re.finditer(r"\{([^{}]*)\}", inner):
        vals = [cxx_eval(p, {}) for p in split_top_commas(rm.group(1).strip())
                if p.strip()]
        if len(vals) == per_row:
            rows.append(vals)
    return rows


def flat_array(text, name):
    i = text.index(name)
    inner, _ = brace_blocks(text, text.index("{", i))
    return [cxx_eval(p, {}) for p in split_top_commas(inner) if p.strip()]


SIZES = array_rows(cc, "HITBOX_SIZES", 3)
OFFS = array_rows(cc, "HITBOX_OFFSETS", 3)
FOFF = array_rows(cc, "FRONT_WHEELS_OFFSET", 3)
BOFF = array_rows(cc, "BACK_WHEELS_OFFSET", 3)
FRAD = flat_array(cc, "FRONT_WHEEL_RADS")
BRAD = flat_array(cc, "BACK_WHEEL_RADS")
FSUS = flat_array(cc, "FRONT_WHEEL_SUS_REST")
BSUS = flat_array(cc, "BACK_WHEEL_SUS_REST")
MACROS = re.findall(r"MAKE_CAR_CONFIG\(\s*(\w+)\s*,\s*(\d+)\s*,\s*(\w+)\s*\)", cc)

# ------------------------------------------------- parse our headers
our_rows = {}  # name -> (16 floats, threeWheels)
for pm in re.finditer(r'\{"(\w+)",(.*?)\},\s*(true|false),\s*0\.5f\}',
                      hp, re.S):
    floats = [float(v.rstrip("f")) for v in
              re.findall(r"[-+]?\d[\d.]*f?", pm.group(2))]
    our_rows[pm.group(1).upper()] = (floats, pm.group(3) == "true")

our_scalars = {}  # kName -> value
for mm in re.finditer(r"k([A-Za-z_]\w*)\s*=\s*([^;{}]+);", ht):
    try:
        our_scalars["k" + mm.group(1)] = cxx_eval(mm.group(2), {
            "kCarMass": 180.0})
    except Exception:  # noqa: BLE001
        pass


def our_curve(name):
    i = ht.index(name)
    inner, _ = brace_blocks(ht, ht.index("{", i))
    return [(cxx_eval(a, {}), cxx_eval(b, {}))
            for a, b in re.findall(r"\{\s*([^,{}]+),\s*([^,{}]+)\}", inner)]


# ================================================================= checks
print("== presets vs CarConfig.cpp ==")
PRESET_ORDER = ["OCTANE", "DOMINUS", "PLANK", "BREAKOUT", "HYBRID", "MERC",
                "PSYCLOPS"]
assert [m[0] for m in MACROS] == PRESET_ORDER, "macro order changed!"
for idx, pname in enumerate(PRESET_ORDER):
    checked["preset"] += 1
    got, three = our_rows.get(pname, (None, None))
    want = (SIZES[idx] + OFFS[idx] + [FRAD[idx], FSUS[idx]] + FOFF[idx]
            + [BRAD[idx], BSUS[idx]] + BOFF[idx])
    want_three = MACROS[idx][2] == "true"
    ok = (got is not None and len(got) == 16
          and all(abs(g - w) <= TOL * max(1.0, abs(w))
                  for g, w in zip(got, want))
          and three == want_three)
    print(f"  [{'PASS' if ok else 'MISMATCH'}] {pname}: 16 fields + "
          f"threeWheels={want_three} (CarConfig.cpp:20-82,100)")
    if not ok:
        print(f"      ours={got} three={three}")
        print(f"      auth={want} three={want_three}")
        failures.append(f"preset {pname}")

print("== default preset ==")
checked["scalar"] += 1
ok = "kCarPresets[0]" in hp and our_rows.get("OCTANE") is not None
print(f"  [{'PASS' if ok else 'MISMATCH'}] kDefaultPreset == Octane")
if not ok:
    failures.append("default preset")

print("== Tuning.h vs RLConst.h ==")
SCALARS = [  # (ours, authority, citation)
    ("kGravityZ", "GRAVITY_Z", "RLConst.h:12"),
    ("kCarMass", "CAR_MASS_BT", "RLConst.h:28"),
    ("kBallMass", "BALL_MASS_BT", "RLConst.h:29"),
    ("kBallRadiusSoccar", "BALL_COLLISION_RADIUS_SOCCAR", "RLConst.h:117"),
    ("kBallRadiusHoops", "BALL_COLLISION_RADIUS_HOOPS", "RLConst.h:118"),
    ("kBallRadiusDropshot", "BALL_COLLISION_RADIUS_DROPSHOT", "RLConst.h:119"),
    ("kBallRestZ", "BALL_REST_Z", "RLConst.h:43"),
    ("kBallMaxAngSpeed", "BALL_MAX_ANG_SPEED", "RLConst.h:44"),
    ("kBallDrag", "BALL_DRAG", "RLConst.h:45"),
    ("kBallFriction", "BALL_FRICTION", "RLConst.h:46"),
    ("kBallRestitution", "BALL_RESTITUTION", "RLConst.h:47"),
    ("kBallMaxSpeed", "BALL_MAX_SPEED", "RLConst.h:54"),
    ("kCarMaxSpeed", "CAR_MAX_SPEED", "RLConst.h:53"),
    ("kBoostMax", "BOOST_MAX", "RLConst.h:56"),
    ("kBoostUsedPerSecond", "BOOST_USED_PER_SECOND", "RLConst.h:57"),
    ("kBoostMinTime", "BOOST_MIN_TIME", "RLConst.h:58"),
    ("kBoostAccelGround", "BOOST_ACCEL_GROUND", "RLConst.h:59"),
    ("kBoostAccelAir", "BOOST_ACCEL_AIR", "RLConst.h:60"),
    ("kBoostSpawnAmount", "BOOST_SPAWN_AMOUNT", "RLConst.h:61"),
    ("kRechargeBoostPerSecond", "RECHARGE_BOOST_PER_SECOND", "RLConst.h:63"),
    ("kRechargeBoostDelay", "RECHARGE_BOOST_DELAY", "RLConst.h:64"),
    ("kCarMaxAngSpeed", "CAR_MAX_ANG_SPEED", "RLConst.h:66"),
    ("kSupersonicStartSpeed", "SUPERSONIC_START_SPEED", "RLConst.h:69"),
    ("kSupersonicMaintainMinSpeed", "SUPERSONIC_MAINTAIN_MIN_SPEED",
     "RLConst.h:73"),
    ("kSupersonicMaintainMaxTime", "SUPERSONIC_MAINTAIN_MAX_TIME",
     "RLConst.h:76"),
    ("kPowerslideRiseRate", "POWERSLIDE_RISE_RATE", "RLConst.h:81"),
    ("kPowerslideFallRate", "POWERSLIDE_FALL_RATE", "RLConst.h:82"),
    ("kThrottleTorqueAmount", "THROTTLE_TORQUE_AMOUNT", "RLConst.h:84"),
    ("kBrakeTorqueAmount", "BRAKE_TORQUE_AMOUNT", "RLConst.h:85"),
    ("kStoppingForwardVel", "STOPPING_FORWARD_VEL", "RLConst.h:87"),
    ("kCoastingBrakeFactor", "COASTING_BRAKE_FACTOR", "RLConst.h:88"),
    ("kBrakingNoThrottleSpeedThresh", "BRAKING_NO_THROTTLE_SPEED_THRESH",
     "RLConst.h:89"),
    ("kThrottleDeadzone", "THROTTLE_DEADZONE", "RLConst.h:90"),
    ("kThrottleAirAccel", "THROTTLE_AIR_ACCEL", "RLConst.h:92"),
    ("kJumpAccel", "JUMP_ACCEL", "RLConst.h:94"),
    ("kJumpImmediateForce", "JUMP_IMMEDIATE_FORCE", "RLConst.h:95"),
    ("kJumpMinTime", "JUMP_MIN_TIME", "RLConst.h:96"),
    ("kJumpResetTimePad", "JUMP_RESET_TIME_PAD", "RLConst.h:97"),
    ("kJumpMaxTime", "JUMP_MAX_TIME", "RLConst.h:98"),
    ("kDoubleJumpMaxDelay", "DOUBLEJUMP_MAX_DELAY", "RLConst.h:99"),
    ("kFlipZDamp120", "FLIP_Z_DAMP_120", "RLConst.h:102"),
    ("kFlipZDampStart", "FLIP_Z_DAMP_START", "RLConst.h:103"),
    ("kFlipZDampEnd", "FLIP_Z_DAMP_END", "RLConst.h:104"),
    ("kFlipTorqueTime", "FLIP_TORQUE_TIME", "RLConst.h:105"),
    ("kFlipTorqueMinTime", "FLIP_TORQUE_MIN_TIME", "RLConst.h:106"),
    ("kFlipPitchLockTime", "FLIP_PITCHLOCK_TIME", "RLConst.h:107"),
    ("kFlipPitchLockExtraTime", "FLIP_PITCHLOCK_EXTRA_TIME", "RLConst.h:108"),
    ("kFlipInitialVelScale", "FLIP_INITIAL_VEL_SCALE", "RLConst.h:109"),
    ("kFlipTorqueX", "FLIP_TORQUE_X", "RLConst.h:110"),
    ("kFlipTorqueY", "FLIP_TORQUE_Y", "RLConst.h:111"),
    ("kFlipFwdImpulseMaxSpeedScale", "FLIP_FORWARD_IMPULSE_MAX_SPEED_SCALE",
     "RLConst.h:112"),
    ("kFlipSideImpulseMaxSpeedScale", "FLIP_SIDE_IMPULSE_MAX_SPEED_SCALE",
     "RLConst.h:113"),
    ("kFlipBackImpulseMaxSpeedScale", "FLIP_BACKWARD_IMPULSE_MAX_SPEED_SCALE",
     "RLConst.h:114"),
    ("kFlipBackImpulseScaleX", "FLIP_BACKWARD_IMPULSE_SCALE_X",
     "RLConst.h:115"),
    ("kBallCarExtraImpulseZScale", "BALL_CAR_EXTRA_IMPULSE_Z_SCALE",
     "RLConst.h:135"),
    ("kBallCarExtraImpulseZScaleHoopsGround",
     "BALL_CAR_EXTRA_IMPULSE_Z_SCALE_HOOPS_GROUND", "RLConst.h:136"),
    ("kBallCarExtraImpulseForwardScale",
     "BALL_CAR_EXTRA_IMPULSE_FORWARD_SCALE", "RLConst.h:137"),
    ("kBallCarExtraImpulseMaxDeltaVel",
     "BALL_CAR_EXTRA_IMPULSE_MAXDELTAVEL_UU", "RLConst.h:138"),
    ("kBallCarExtraImpulseHoopsNormalZThresh",
     "BALL_CAR_EXTRA_IMPULSE_Z_SCALE_HOOPS_NORMAL_Z_THRESH", "RLConst.h:139"),
    ("kCarSpawnRestZ", "CAR_SPAWN_REST_Z", "RLConst.h:141"),
    ("kCarRespawnZ", "CAR_RESPAWN_Z", "RLConst.h:142"),
    ("kBumpCooldownTime", "BUMP_COOLDOWN_TIME", "RLConst.h:144"),
    ("kBumpMinForwardDist", "BUMP_MIN_FORWARD_DIST", "RLConst.h:145"),
    ("kDemoRespawnTime", "DEMO_RESPAWN_TIME", "RLConst.h:146"),
    ("kCarTorqueScale", "CAR_TORQUE_SCALE", "RLConst.h:124"),
    ("kAutoFlipImpulse", "CAR_AUTOFLIP_IMPULSE", "RLConst.h:126"),
    ("kAutoFlipTorque", "CAR_AUTOFLIP_TORQUE", "RLConst.h:127"),
    ("kAutoFlipTime", "CAR_AUTOFLIP_TIME", "RLConst.h:128"),
    ("kAutoRollForce", "CAR_AUTOROLL_FORCE", "RLConst.h:132"),
    ("kAutoRollTorque", "CAR_AUTOROLL_TORQUE", "RLConst.h:133"),
    ("kPadAmountBig", "BOOST_AMOUNT_BIG", "RLConst.h:266"),
    ("kPadAmountSmall", "BOOST_AMOUNT_SMALL", "RLConst.h:267"),
    ("kPadCooldownBig", "COOLDOWN_BIG", "RLConst.h:263"),
    ("kPadCooldownSmall", "COOLDOWN_SMALL", "RLConst.h:264"),
]
for ours, auth, cite in SCALARS:
    if ours not in our_scalars:
        checked["scalar"] += 1
        print(f"  [MISMATCH] {ours}: missing from Tuning.h ({cite})")
        failures.append(ours)
    elif auth not in env:
        checked["scalar"] += 1
        print(f"  [SKIP] {auth}: not parsed from RLConst.h")
    else:
        check_scalar(f"{ours} == {auth}", our_scalars[ours], env[auth], cite)

# air-control vecs + autoflip thresholds (parsed separately — Vec / macro exprs)
for ours, auth, cite in [
    ("kCarAirTorqueX", 130.0, "RLConst.h:150"),
    ("kCarAirTorqueY", 95.0, "RLConst.h:150"),
    ("kCarAirTorqueZ", 400.0, "RLConst.h:150"),
    ("kCarAirDampingX", 30.0, "RLConst.h:151"),
    ("kCarAirDampingY", 20.0, "RLConst.h:151"),
    ("kCarAirDampingZ", 50.0, "RLConst.h:151"),
    ("kAutoFlipNormZThresh", math.sqrt(0.5), "RLConst.h:129"),
    ("kAutoFlipRollThresh", 2.8, "RLConst.h:130"),
]:
    if ours not in our_scalars:
        checked["scalar"] += 1
        print(f"  [MISMATCH] {ours}: missing from Tuning.h ({cite})")
        failures.append(ours)
    else:
        check_scalar(f"{ours}", our_scalars[ours], auth, cite)

print("== curves vs RLConst.h ==")
CURVES = [
    ("kSteerAngleFromSpeed", "STEER_ANGLE_FROM_SPEED_CURVE", "RLConst.h:418"),
    ("kSteerAngleFromSpeedThreeWheel",
     "STEER_ANGLE_FROM_SPEED_CURVE_THREEWHEEL", "RLConst.h:429"),
    ("kPowerslideSteerAngleFromSpeed",
     "POWERSLIDE_STEER_ANGLE_FROM_SPEED_CURVE", "RLConst.h:438"),
    ("kDriveSpeedTorqueFactor", "DRIVE_SPEED_TORQUE_FACTOR_CURVE",
     "RLConst.h:447"),
    ("kNonStickyFrictionFactor", "NON_STICKY_FRICTION_FACTOR_CURVE",
     "RLConst.h:455"),
    ("kLatFriction", "LAT_FRICTION_CURVE", "RLConst.h:463"),
    ("kLatFrictionThreeWheel", "LAT_FRICTION_CURVE_THREEWHEEL",
     "RLConst.h:470"),
    ("kHandbrakeLatFrictionFactor", "HANDBRAKE_LAT_FRICTION_FACTOR_CURVE",
     "RLConst.h:483"),
    ("kHandbrakeLongFrictionFactor", "HANDBRAKE_LONG_FRICTION_FACTOR_CURVE",
     "RLConst.h:489"),
    ("kBallCarExtraImpulseFactor", "BALL_CAR_EXTRA_IMPULSE_FACTOR_CURVE",
     "RLConst.h:496"),
    ("kBumpVelGround", "BUMP_VEL_AMOUNT_GROUND_CURVE", "RLConst.h:505"),
    ("kBumpVelAir", "BUMP_VEL_AMOUNT_AIR_CURVE", "RLConst.h:513"),
    ("kBumpUpwardVel", "BUMP_UPWARD_VEL_AMOUNT_CURVE", "RLConst.h:521"),
]
for ours, auth, cite in CURVES:
    check_curve(f"{ours} == {auth}", our_curve(ours), rs_curve(auth), cite)

print("== suspension (BTVehicle) ==")
for ours, auth, cite in [
    ("kSuspForceScaleFront", "BTVehicle::SUSPENSION_FORCE_SCALE_FRONT",
     "RLConst.h:158"),
    ("kSuspForceScaleBack", "BTVehicle::SUSPENSION_FORCE_SCALE_BACK",
     "RLConst.h:159"),
    ("kSuspStiffness", "BTVehicle::SUSPENSION_STIFFNESS", "RLConst.h:161"),
    ("kWheelsDampingCompression",
     "BTVehicle::WHEELS_DAMPING_COMPRESSION", "RLConst.h:162"),
    ("kWheelsDampingRelaxation",
     "BTVehicle::WHEELS_DAMPING_RELAXATION", "RLConst.h:163"),
    ("kMaxSuspensionTravel", "BTVehicle::MAX_SUSPENSION_TRAVEL",
     "RLConst.h:164"),
    ("kSuspensionSubtraction", "BTVehicle::SUSPENSION_SUBTRACTION",
     "RLConst.h:165"),
]:
    check_scalar(f"{ours} == {auth}", our_scalars[ours], env[auth], cite)

print()
total = sum(checked.values())
print(f"checked: {total} "
      f"(presets={checked['preset']} scalars={checked['scalar']} "
      f"curves={checked['curve']})")
if failures:
    print(f"RESULT: FAIL — {len(failures)} mismatch(es): {failures}")
    sys.exit(1)
print("RESULT: ALL CHECKS PASSED")
print("NOTE: G1-G7 dynamic gates (1410±1, 2300 cap, 2.55m/4.82m jumps, "
      "turn rates, ~350° flip, supersonic) need a live sim — see FeelNotes.md.")
