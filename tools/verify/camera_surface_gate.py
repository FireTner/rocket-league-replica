#!/usr/bin/env python3
"""CS1-CS5 — camera truth-surface gate (LOCAL ONLY, SKIPs loudly).

Ground truth = the camera class/function/property surface extracted from the
user's own Rocket League install by `tools/decomp/camera_surface.py`.  That
fixture is Psyonix-derived data, so it NEVER enters the repo: it lives in the
gitignored `decomp/` directory.  Without it this gate SKIPs (exit 0), exactly
like `mesh_gates.py` without a mesh dump.

It answers one question for docs/CAMERA_DECOMP.md: does the real game still
have the mechanism-bearing symbols our `src/camera/` port claims?  It never
grades feel or numbers — that stays with measured.py / visual_gates.py.

Usage:
    python3 tools/verify/camera_surface_gate.py            # default fixture
    python3 tools/verify/camera_surface_gate.py --fixture X.json
    python3 tools/verify/camera_surface_gate.py --regen    # re-extract first
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_FIXTURE = os.path.join(REPO, 'decomp', 'camera_surface.json')

# Identity pinned in docs/CAMERA_DECOMP.md; a change means RL patched its
# package format and the fixture/analysis must be re-baselined.
EXPECTED_ENGINE = 10897

CLASSES = {
    'Camera_X': 0,                # base chase camera (ProjectX.upk)
    'Camera_TA': 0,               # TAGame camera: swivel, FOV, field clip
    'CameraState_TA': 0,          # free look / deadzone / input scales
    'CameraState_Car_TA': 0,      # car rig: air/ground blend, distance, focus
    'CameraState_BallCam_TA': 0,  # ball target selection + pitch scaling
    'CameraStateBlender_X': 0,    # state transition blending
}

FUNCTIONS = {
    'Camera_X': ['UpdateCamera', 'ProcessCameraState', 'ClampPOV',
                 'BlendCameraOrientations', 'UpdateCameraState',
                 'UpdateCameraKnocks', 'SnapTransition'],
    'Camera_TA': ['UpdateSwivel', 'GetDesiredSwivel', 'ApplySwivel', 'UpdateFOV',
                  'ProcessCameraState', 'PostProcessCameraState',
                  'ApplyRearCameraView', 'ClipToField', 'GetCameraSettings'],
    'CameraState_TA': ['UpdateFreeLook', 'ApplyFreeLook', 'UpdateFreeLookInput'],
    'CameraState_Car_TA': ['UpdatePOV', 'UpdateValidPOV', 'UpdateAirGroundBlend',
                           'UpdateFocus', 'UpdateFocusWorldOffset',
                           'UpdateAirAndGroundCamera', 'UpdateGroundPOV',
                           'UpdateAirPOV', 'CalculateDesiredAirRotation',
                           'UpdateDistance', 'UpdateFOV',
                           'UpdateRotationModifiers'],
    'CameraState_BallCam_TA': ['UpdateFocus', 'UpdateRotationModifiers',
                               'UpdateValidPOV', 'TargetSelect', 'ChooseTarget',
                               'FindBestTarget', 'ShouldExecute'],
    'CameraStateBlender_X': ['BlendCameraState', 'UpdatePOV', 'TransitionToState',
                             'Tick', 'IsTransitioning'],
}

PROPERTIES = {
    'Camera_TA': ['CameraSettings', 'SwivelDieRate', 'SwivelFastSpeed',
                  'SwivelExtentFast', 'SwivelExtentSlow', 'CurrentSwivel',
                  'ClipRate', 'bGroundClampTrace', 'GroundClampZOffset'],
    'CameraState_TA': ['MaxFreeLookPitchDegrees', 'FreeLookRateYaw',
                       'FreeLookRatePitch', 'FreeLookOffset', 'bFreeLookActive'],
    'CameraState_Car_TA': ['AirVelocityInfluence', 'AirVelocityInfluenceMaxSpeed',
                           'SupersonicFOV', 'SupersonicFOVInterpSpeed',
                           'FOVInterpSpeed', 'MaxSpeedFOV', 'DistanceSpeedScale',
                           'DistanceOffsetMin', 'DistanceInterp', 'FocusInterp',
                           'GroundRotationInterpRate', 'InterpToGroundRate',
                           'InterpToAirRate', 'AirGroundBlend'],
    'CameraState_BallCam_TA': ['PitchExtentMax', 'PitchExtentMin',
                               'PitchFocusZFactor', 'PitchScale', 'RotationRate'],
    'CameraStateBlender_X': ['SnapshotPOV', 'RemainingTime', 'BlendParams',
                             'TransitionDelta', 'Transition'],
}

# Our implemented mechanisms (src/camera/Camera.h) -> the real symbols that
# justify them.  Every mechanism needs at least one live symbol.
MECHANISMS = [
    ('ball-aim slew', ['CameraState_BallCam_TA.UpdateFocus',
                       'CameraState_BallCam_TA.RotationRate']),
    ('air heading', ['CameraState_Car_TA.UpdateAirGroundBlend',
                     'CameraState_Car_TA.AirVelocityInfluence',
                     'CameraState_Car_TA.CalculateDesiredAirRotation']),
    ('stiffness lag', ['Camera_X.UpdateCamera',
                       'CameraState_Car_TA.DistanceInterp',
                       'CameraState_Car_TA.FocusInterp']),
    ('swivel + snap-back', ['CameraState_TA.UpdateFreeLook',
                            'Camera_TA.UpdateSwivel',
                            'Camera_TA.SwivelDieRate']),
    ('target filter', ['CameraState_Car_TA.FocusInterp',
                       'Camera_X.ProcessCameraState']),
    ('transition blend', ['CameraStateBlender_X.BlendCameraState',
                          'CameraStateBlender_X.TransitionToState']),
    ('dynamic FOV/distance', ['CameraState_Car_TA.UpdateFOV',
                              'CameraState_Car_TA.SupersonicFOV',
                              'CameraState_Car_TA.DistanceSpeedScale']),
    ('shake/knocks', ['Camera_X.UpdateCameraKnocks',
                      'Camera_X.ModifyCameraShakeScale']),
    ('look-behind', ['Camera_TA.ApplyRearCameraView']),
    ('ground clamp / field clip', ['Camera_TA.bGroundClampTrace',
                                   'Camera_TA.ClipToField']),
]


def fail(msg):
    print(f'  FAIL {msg}')
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default=DEFAULT_FIXTURE)
    ap.add_argument('--regen', action='store_true',
                    help='re-extract the fixture from the local install first')
    args = ap.parse_args()

    if args.regen:
        r = subprocess.run([sys.executable,
                            os.path.join(REPO, 'tools', 'decomp', 'camera_surface.py'),
                            '--out', args.fixture])
        if r.returncode != 0:
            print('camera_surface_gate: SKIP (re-extract failed)')
            return 0

    if not os.path.exists(args.fixture):
        print('camera_surface_gate: SKIP — no local decomp fixture.')
        print(f'  expected {args.fixture}')
        print('  The fixture is Psyonix-derived data and is gitignored by design')
        print('  (repo Rule 1). Re-create it from YOUR install with:')
        print('    python3 tools/decomp/camera_surface.py --out decomp/camera_surface.json')
        print('  See docs/CAMERA_DECOMP.md.')
        return 0

    with open(args.fixture) as f:
        doc = json.load(f)
    classes = doc.get('classes', {})
    fails = 0
    passes = 0

    print(f'camera_surface_gate: fixture={os.path.relpath(args.fixture, REPO)} '
          f'UE3 {doc.get("engine_version")}/{doc.get("licensee_version")}')

    # CS1 — identity
    eng = doc.get('engine_version')
    if eng != EXPECTED_ENGINE:
        fails += fail(f'CS1 package identity: engine {eng} != pinned {EXPECTED_ENGINE}; '
                      're-baseline docs/CAMERA_DECOMP.md')
    else:
        print(f'  PASS CS1 identity engine={eng} licensee={doc.get("licensee_version")}')
        passes += 1

    def has(cls, kind, name):
        return any(x['name'] == name for x in classes.get(cls, {}).get(kind, []))

    # CS2 — classes
    missing = [c for c in CLASSES if c not in classes]
    if missing:
        fails += fail(f'CS2 classes missing: {", ".join(missing)}')
    else:
        print(f'  PASS CS2 {len(CLASSES)} camera classes present')
        passes += 1

    # CS3 — functions
    miss = [f'{c}.{n}' for c, names in FUNCTIONS.items() for n in names if not has(c, 'functions', n)]
    if miss:
        fails += fail(f'CS3 functions missing: {", ".join(miss)}')
    else:
        total = sum(len(v) for v in FUNCTIONS.values())
        print(f'  PASS CS3 {total} mechanism-bearing functions present')
        passes += 1

    # CS4 — properties
    miss = [f'{c}.{n}' for c, names in PROPERTIES.items() for n in names if not has(c, 'properties', n)]
    if miss:
        fails += fail(f'CS4 properties missing: {", ".join(miss)}')
    else:
        total = sum(len(v) for v in PROPERTIES.values())
        print(f'  PASS CS4 {total} mechanism-bearing properties present')
        passes += 1

    def live_symbol(ref):
        cls, name = ref.split('.', 1)
        return has(cls, 'functions', name) or has(cls, 'properties', name)

    # CS5 — our mechanism list still maps onto live symbols
    dark = []
    for label, refs in MECHANISMS:
        live = [r for r in refs if live_symbol(r)]
        if not live:
            dark.append(label)
        else:
            print(f'    {label:28s} -> {len(live)}/{len(refs)} symbols live')
    if dark:
        fails += fail(f'CS5 mechanisms with no live symbol: {", ".join(dark)}')
    else:
        print(f'  PASS CS5 all {len(MECHANISMS)} ported mechanisms map to live RL symbols')
        passes += 1

    print(f'camera_surface_gate: {passes} PASS / {fails} FAIL')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
