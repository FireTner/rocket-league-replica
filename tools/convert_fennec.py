#!/usr/bin/env python3
# tools/convert_fennec.py — one-time GLB -> raw mesh converter (MIT).
# Source: RocketLeague2/public/assets/models/fennec.glb (CC-BY-4.0 by Jako,
# https://sketchfab.com/fairlight51 — community recreation, NOT extracted
# game geometry). Attribution ships beside the output.
#
# Output: assets/models/fennec.bin
#   magic "FEN1" | u32 vertexCount | interleaved f32 x,y,z,nx,ny,nz per vertex
#   | u32 indexCount | u32 indices
#
# Baked transform (car-local frame FROZEN by Models.cpp CarFrame:
# local +x = forward, +y = LEFT, +z = up, origin = RocketSim car origin):
#   our = (glb_x, +glb_z, glb_y - Z_OFF)
# GLB is already RL units and, after resolving every node matrix, sits in
#   GLB +X = nose, GLB +Y = up, GLB +Z = LEFT   (FR wheel at glb z = -29.96)
# (verified in RocketLeague2/src/render/FennecModel.ts, "ORIENTATION").
# Since our +x = forward and our +z = up, the only remaining degree of freedom is
# the lateral sign: GLB +Z (left) must map to our +y (left). The previous
# `-glb_z` mirrored the car (VISUAL_BUGS_TODO V3).
#
# Z_OFF and the axes below are MEASURED from the source GLB (row-major node
# composition; reproduces FennecModel.ts's documented world bbox
# 140.42 x 51.65 x 72.47 uu and wheel-contact plane at glb y = -1.50):
#   GLB +X = nose, GLB +Y = up, GLB +Z = LEFT (FR wheel at glb z = -29.96)
# RocketSim parks a spawned car at z = 17.0011 uu (build/headless_sim --dump,
# mesh mode, tick 0). The mesh's lowest point is glb_y = -1.50, so grounding it
# needs Z_OFF = 17.0011 - 1.50 = 15.501. That is exactly the value the source
# documents ("origin sits ~1.43 above the floor"): the mesh min-z becomes
# -17.00, i.e. 1.43 uu above world z = 0. The previous revision used 15.5 but
# paired it with a broken transform, so the mesh actually landed ~32 uu low.
import json, struct, sys, os, math

SRC = "/home/cachyfire/Projects/RocketLeague2/public/assets/models/fennec.glb"
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "assets", "models", "fennec.bin")
Z_OFF = 15.501  # see header (MEASURED: rest z 17.0011 - glb contact -1.50)

def main():
    d = open(SRC, "rb").read()
    magic, ver, _total = struct.unpack("<III", d[:12])
    assert magic == 0x46546C67, "not a glb"
    jlen, jtype = struct.unpack("<II", d[12:20])
    j = json.loads(d[20:20 + jlen].decode("utf8"))
    off = 20 + jlen
    blen, btype = struct.unpack("<II", d[off:off + 8])
    assert btype == 0x004E4942, "no BIN chunk"
    bin_data = d[off + 8:off + 8 + blen]

    CT_SIZE = {5120: 2, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
    NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}

    def read_accessor(idx):
        a = j["accessors"][idx]
        bv = j["bufferViews"][a["bufferView"]]
        comp = NCOMP[a["type"]]
        csz = CT_SIZE[a["componentType"]]
        count = a["count"]
        start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = bv.get("byteStride") or (comp * csz)
        fmt = {5126: "f", 5125: "I", 5123: "H", 5122: "h", 5121: "B", 5120: "b"}[a["componentType"]]
        out = []
        for i in range(count):
            base = start + i * stride
            out.append(struct.unpack_from("<" + fmt * comp, bin_data, base))
        return out

    # --- matrix handling -----------------------------------------------------
    # Everything below is ROW-MAJOR: `mat[r][c]` is row r, column c. glTF stores
    # its `matrix` array column-major, so `to_rows` transposes it on the way in.
    # (The previous revision fed a column-major list into row-major indexing in
    # both `mul` and `xform`, producing an inconsistent transform - the root of
    # the corrupt datum in VISUAL_BUGS_TODO V2.)
    def to_rows(flat):
        return [[flat[r + 4 * c] for c in range(4)] for r in range(4)]

    def node_matrix(n):
        if "matrix" in n:
            return to_rows(n["matrix"])
        t = n.get("translation", [0, 0, 0])
        r = n.get("rotation", [0, 0, 0, 1])
        s = n.get("scale", [1, 1, 1])
        x, y, z, w = r
        rot = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
               [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
               [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        return [[rot[r][0] * s[0], rot[r][1] * s[1], rot[r][2] * s[2], t[r]]
                for r in range(3)] + [[0, 0, 0, 1]]

    def mul(a, b):  # a*b, row-major
        return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)]
                for r in range(4)]

    def xform(m, p):
        """Apply a full 4x4 affine matrix to a POINT (adds translation)."""
        return tuple(m[r][3] + sum(m[r][k] * p[k] for k in range(3))
                     for r in range(3))

    def normal_matrix(m):
        """Inverse-transpose of the 3x3 linear part: the correct normal
        transform under non-uniform scale. Returned row-major, 3x3."""
        a = [[m[r][c] for c in range(3)] for r in range(3)]
        det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
               - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
               + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
        if abs(det) < 1e-20:
            return a
        inv = [[(a[1][1] * a[2][2] - a[1][2] * a[2][1]) / det,
                (a[0][2] * a[2][1] - a[0][1] * a[2][2]) / det,
                (a[0][1] * a[1][2] - a[0][2] * a[1][1]) / det],
               [(a[1][2] * a[2][0] - a[1][0] * a[2][2]) / det,
                (a[0][0] * a[2][2] - a[0][2] * a[2][0]) / det,
                (a[0][2] * a[1][0] - a[0][0] * a[1][2]) / det],
               [(a[1][0] * a[2][1] - a[1][1] * a[2][0]) / det,
                (a[0][1] * a[2][0] - a[0][0] * a[2][1]) / det,
                (a[0][0] * a[1][1] - a[0][1] * a[1][0]) / det]]
        return [[inv[c][r] for c in range(3)] for r in range(3)]  # transpose

    def nrm(nm, v):
        """Transform a normal with the inverse-transpose, then renormalise.
        Translation is NEVER applied to a normal (VISUAL_BUGS_TODO V1)."""
        x = nm[0][0] * v[0] + nm[0][1] * v[1] + nm[0][2] * v[2]
        y = nm[1][0] * v[0] + nm[1][1] * v[1] + nm[1][2] * v[2]
        z = nm[2][0] * v[0] + nm[2][1] * v[1] + nm[2][2] * v[2]
        l = math.sqrt(x * x + y * y + z * z)
        if l < 1e-12:
            return (0.0, 0.0, 1.0)
        return (x / l, y / l, z / l)

    verts = []  # (x,y,z,nx,ny,nz) in OUR frame
    indices = []
    scene = j["scenes"][j.get("scene", 0)]

    def walk(ni, parent, stats):
        n = j["nodes"][ni]
        m = mul(parent, node_matrix(n))
        nm = normal_matrix(m)
        if "mesh" in n:
            mesh = j["meshes"][n["mesh"]]
            for prim in mesh["primitives"]:
                pos = read_accessor(prim["attributes"]["POSITION"])
                nor = read_accessor(prim["attributes"].get("NORMAL", prim["attributes"]["POSITION"]))
                base = len(verts)
                for p, nn in zip(pos, nor):
                    wp = xform(m, p)
                    wn = nrm(nm, nn)
                    vx, vy, vz = wp
                    nx, ny, nz = wn
                    # world -> car-local: (x, z, y) with the lateral sign kept
                    verts.append((vx, vz, vy - Z_OFF, nx, nz, ny))
                idxs = read_accessor(prim["indices"]) if "indices" in prim else []
                for t in range(0, len(idxs), 3):
                    a, b, c = idxs[t][0] + base, idxs[t + 1][0] + base, idxs[t + 2][0] + base
                    # cull degenerate
                    if a == b or b == c or a == c:
                        continue
                    indices.extend((a, b, c))
                    stats["tris"] += 1
                    # degenerate-after-transform check (zero area)
                    pa, pb, pc = verts[a], verts[b], verts[c]
                    e1 = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
                    e2 = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
                    cx = e1[1] * e2[2] - e1[2] * e2[1]
                    cy = e1[2] * e2[0] - e1[0] * e2[2]
                    cz = e1[0] * e2[1] - e1[1] * e2[0]
                    if math.sqrt(cx * cx + cy * cy + cz * cz) < 1e-9:
                        stats["degenerate"] += 1
        for ci in n.get("children", []):
            walk(ci, m, stats)

    ident = [[1 if i == j else 0 for j in range(4)] for i in range(4)]
    stats = {"tris": 0, "degenerate": 0}
    for root in scene["nodes"]:
        walk(root, ident, stats)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"FEN1")
        f.write(struct.pack("<I", len(verts)))
        for v in verts:
            f.write(struct.pack("<6f", *v))
        f.write(struct.pack("<I", len(indices)))
        f.write(struct.pack("<%dI" % len(indices), *indices))
    xs = [v[0] for v in verts]; ys = [v[1] for v in verts]; zs = [v[2] for v in verts]
    nl = [math.sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]) for v in verts]
    bad = sum(1 for l in nl if abs(l - 1.0) > 0.01)
    print("vertices=%d triangles=%d degenerate=%d" % (len(verts), len(indices) // 3, stats["degenerate"]))
    print("bbox x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f] (uu, car-local)"
          % (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
    print("normals: non-unit=%d/%d  |n| min=%.4f max=%.4f" % (bad, len(nl), min(nl), max(nl)))
    print("wrote %s (%d bytes)" % (OUT, os.path.getsize(OUT)))
    if bad or stats["degenerate"]:
        print("FAIL: %d non-unit normals, %d degenerate triangles" % (bad, stats["degenerate"]))
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
