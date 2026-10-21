"""Extract Rocket League's camera class/function/property SURFACE as a fixture.

This is the "truth" side of the camera truth gate (docs/CAMERA_DECOMP.md).  It
reads the user's OWN install, keeps only symbol names + shapes, and writes the
result to a gitignored local file (default `decomp/camera_surface.json`).
No package bytes, no decompiled text: names and shapes are facts.

Usage:
    python3 tools/decomp/camera_surface.py --pkgdir "$RL/.../CookedPCConsole" \
        [--out decomp/camera_surface.json] [--show]

Packages read: ProjectX.upk (the base Camera_X / CameraStateBlender_X /
CameraUtils_X) and TAGame.upk (the _TA camera states + ProfileCameraSettings).

The package key is NOT in this repo: set `$RL_UPK_KEY` (64 hex digits) or put it
in `~/.rl-decomp/upk.key`.  Without one this exits 3 with a SKIP, so a public
clone stays runnable-but-inert.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rlupk  # noqa: E402

DEFAULT_PKGDIR = os.path.expanduser(
    '~/Games/Heroic/rocketleague/TAGame/CookedPCConsole')
PACKAGES = ('ProjectX.upk', 'TAGame.upk')


def outer_chain(pkg, e):
    chain = [pkg.export_name(e)]
    while e.package_index > 0:
        e = pkg.exports[e.package_index - 1]
        chain.append(pkg.export_name(e))
    return list(reversed(chain))


def extract(pkgdir):
    rlupk.load_key()          # fail with the key SKIP before touching packages
    paths = []
    for name in PACKAGES:
        p = os.path.join(pkgdir, name)
        if os.path.exists(p):
            paths.append(p)
    if not paths:
        raise SystemExit(f'no RL packages under {pkgdir}')
    pkgs = [rlupk.Package(p) for p in paths]

    classes = {}
    for pkg in pkgs:
        for e in pkg.exports:
            name = pkg.export_name(e)
            is_type = (e.class_index == 0
                       or pkg.export_class_name(e) in ('ScriptStruct', 'Enum'))
            if not is_type or 'camera' not in name.lower():
                continue
            entry = classes.setdefault(name, {
                'package': os.path.basename(pkg.path),
                'kind': 'class' if e.class_index == 0 else pkg.export_class_name(e),
                'super': None,
                'functions': [],
                'properties': [],
            })
            if e.super_index < 0:
                entry['super'] = pkg.import_name(-e.super_index - 1)
            elif e.super_index > 0:
                entry['super'] = pkg.export_name(pkg.exports[e.super_index - 1])

    for pkg in pkgs:
        for e in pkg.exports:
            chain = outer_chain(pkg, e)
            if chain[0] not in classes:
                continue
            cn = pkg.export_class_name(e)
            rec = {'name': pkg.export_name(e), 'size': e.serial_size}
            if cn == 'Function':
                classes[chain[0]]['functions'].append(rec)
            elif cn.endswith('Property'):
                rec['type'] = cn
                classes[chain[0]]['properties'].append(rec)

    head = pkgs[-1]  # TAGame.upk carries the identity we pin
    return {
        'source': [os.path.basename(p.path) for p in pkgs],
        'engine_version': head.engine_version,
        'licensee_version': head.licensee_version,
        'package_guid': head.guid,
        'classes': classes,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkgdir', default=DEFAULT_PKGDIR)
    ap.add_argument('--out', default='decomp/camera_surface.json')
    ap.add_argument('--show', action='store_true',
                    help='print the class table instead of writing a fixture')
    args = ap.parse_args()

    try:
        doc = extract(args.pkgdir)
    except rlupk.MissingKey as exc:
        print(f'SKIP: {exc}')
        raise SystemExit(3) from None
    if args.show:
        for name, c in sorted(doc['classes'].items()):
            print(f'{name:38s} <- {str(c["super"]):24s} '
                  f'fns={len(c["functions"]):3d} props={len(c["properties"]):3d} '
                  f'[{c["package"]}]')
        return

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'w') as f:
        json.dump(doc, f, indent=1, sort_keys=True)
    print(f'{len(doc["classes"])} camera symbols -> {args.out} '
          f'(UE3 {doc["engine_version"]}/{doc["licensee_version"]})')


if __name__ == '__main__':
    main()
