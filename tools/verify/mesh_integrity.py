#!/usr/bin/env python3
"""tools/verify/mesh_integrity.py — baked Fennec asset integrity (VG13 backend).

Asserts, on `assets/models/fennec.bin`:
  (a) every baked normal is unit length (| |n| - 1 | <= 0.01);
  (b) the bbox min-z is within +/-1.0 uu of -17.00 (ground plane datum, see
      tools/convert_fennec.py header);
  (c) the footprint matches the source GLB's documented MEASURED dimensions
      140.42 (length) x 72.47 (width) x 51.65 (height) within +/-1 uu;
  (d) there are no zero-area (degenerate) triangles.

The hitbox (Octane, src/physics/CarPresets.h kCarPresets[0]) is reported as a
delta for the record but NOT asserted: the mesh is a detailed recreation that
includes wheels and a wing, so it is legitimately larger than the collision box.

Evidence for the bug this exists to catch: docs/VISUAL_BUGS_TODO.md V1/V2.
Before the converter fix this reported 58851/58851 non-unit normals
(|n| median 100.0) and bbox z[-48.91,-1.67].

Usage:
  python3 tools/verify/mesh_integrity.py            # human output
  python3 tools/verify/mesh_integrity.py --json      # machine output (VG13)
Exit code 0 = all assertions pass, 1 = at least one failed, 2 = cannot read.
"""
import json
import math
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MESH = os.path.join(ROOT, "assets", "models", "fennec.bin")

# Tolerances (see header).
NORMAL_TOL = 0.01
DATUM_TARGET = -17.00
DATUM_TOL = 1.0
# Authored footprint, from FennecModel.ts's documented world bbox
# "140.42 x 51.65 x 72.47 uu" (x, height, width) and re-measured from the GLB
# with correct node-matrix composition. Order matches mesh (x=length, y=width,
# z=height); tolerance +/-1.0 uu.
SRC_SIZE = (140.42, 72.47, 51.65)
SRC_TOL = 1.0
# Octane hitbox FULL size in uu -- src/physics/CarPresets.h kCarPresets[0].
HITBOX = (120.507, 86.6994, 38.6591)


def load(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"FEN1":
        raise ValueError("bad magic %r (expected FEN1)" % d[:4])
    nv = struct.unpack_from("<I", d, 4)[0]
    off = 8
    need = off + nv * 24 + 4
    if len(d) < need:
        raise ValueError("truncated: %d bytes, need >= %d" % (len(d), need))
    verts = [struct.unpack_from("<6f", d, off + i * 24) for i in range(nv)]
    off += nv * 24
    ni = struct.unpack_from("<I", d, off)[0]
    off += 4
    if len(d) < off + ni * 4:
        raise ValueError("truncated index block")
    idx = struct.unpack_from("<%dI" % ni, d, off)
    return verts, idx


def check():
    verts, idx = load(MESH)
    nv = len(verts)
    res = {"mesh": MESH, "vertices": nv, "indices": len(idx),
           "triangles": len(idx) // 3, "failures": []}

    # (a) unit normals
    lens = [math.sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]) for v in verts]
    bad = [l for l in lens if abs(l - 1.0) > NORMAL_TOL]
    res["normals"] = {"non_unit": len(bad), "min": min(lens), "max": max(lens)}
    if bad:
        res["failures"].append(
            "normals: %d/%d non-unit (|n| min %.4f max %.4f, tol %.2f)"
            % (len(bad), nv, min(lens), max(lens), NORMAL_TOL))

    # (b)/(c) datum and size
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    size = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    res["bbox"] = {"min": [min(xs), min(ys), min(zs)],
                   "max": [max(xs), max(ys), max(zs)],
                   "size": list(size)}
    if abs(min(zs) - DATUM_TARGET) > DATUM_TOL:
        res["failures"].append(
            "datum: bbox min-z %.2f, expected %.2f +/- %.2f"
            % (min(zs), DATUM_TARGET, DATUM_TOL))
    # Footprint vs the source GLB's documented dimensions.
    res["hitbox_delta"] = [size[i] - HITBOX[i] for i in range(3)]
    res["src_delta"] = [size[i] - SRC_SIZE[i] for i in range(3)]
    for i, name in enumerate(("length x", "width y", "height z")):
        if abs(res["src_delta"][i]) > SRC_TOL:
            res["failures"].append(
                "size %s: %.2f, expected %.2f +/- %.1f (source FennecModel.ts)"
                % (name, size[i], SRC_SIZE[i], SRC_TOL))

    # (d) degenerate triangles
    degen = 0
    for t in range(0, len(idx), 3):
        a, b, c = verts[idx[t]], verts[idx[t + 1]], verts[idx[t + 2]]
        e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        cx = e1[1] * e2[2] - e1[2] * e2[1]
        cy = e1[2] * e2[0] - e1[0] * e2[2]
        cz = e1[0] * e2[1] - e1[1] * e2[0]
        if math.sqrt(cx * cx + cy * cy + cz * cz) < 1e-9:
            degen += 1
    res["degenerate_triangles"] = degen
    if degen:
        res["failures"].append("degenerate triangles: %d" % degen)

    res["pass"] = not res["failures"]
    return res


def main():
    want_json = "--json" in sys.argv
    try:
        res = check()
    except (OSError, ValueError) as e:
        out = {"pass": False, "failures": ["cannot read mesh: %s" % e]}
        print(json.dumps(out) if want_json else "MESH-INTEGRITY ERROR: %s" % e)
        return 2
    if want_json:
        print(json.dumps(res))
    else:
        print("V1/V2 mesh integrity: %s" % ("PASS" if res["pass"] else "FAIL"))
        print("  verts=%d tris=%d" % (res["vertices"], res["triangles"]))
        print("  normals non-unit=%d (|n| %.4f..%.4f)"
              % (res["normals"]["non_unit"], res["normals"]["min"], res["normals"]["max"]))
        print("  bbox min-z=%.2f (target %.2f +/- %.1f)  size=%.2f x %.2f x %.2f"
              % (res["bbox"]["min"][2], DATUM_TARGET, DATUM_TOL, *res["bbox"]["size"]))
        print("  source delta L/W/H = %.2f / %.2f / %.2f uu (tol %.1f)"
              % (*res["src_delta"], SRC_TOL))
        print("  hitbox delta L/W/H = %.2f / %.2f / %.2f uu (informational)"
              % tuple(res["hitbox_delta"]))
        print("  degenerate triangles=%d" % res["degenerate_triangles"])
        for f in res["failures"]:
            print("  FAIL %s" % f)
    return 0 if res["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
