"""UE3 (Rocket League) arena-object reader — local-only analysis.  **SPIKE.**

Reads the user's OWN Rocket League packages. Carries no Psyonix bytes; writes
nothing into the repository tree (docs/ARENA_RIP.md, AGENTS.md Rule 1).

STATUS (2026-10-20, round A1) — read docs/ARENA_RIP.md / ARENA_RIP_LOG.md:

  VERIFIED   `read_properties` / `_walk` — UE3 tagged properties for this cook.
             Type-driven walk reaches the `None` terminator on all 119
             StaticMesh exports of `Stadium_Day_P.upk`.
             `BoolProperty` carries ONE value byte after its 24-byte tag (Size
             stays 0), so its record is 25 bytes; the `None` terminator is an
             8-byte FName and the render data starts right after it.

  VERIFIED   `parse_static_mesh` — the STRUCTURAL path, and the shipped one.
             Resolves **119 of 119** StaticMesh exports.  The measured layout
             is documented at the `# raster stream parse` comment below:
             tagged properties, 56-byte bounds head (FBoxSphereBounds + flag +
             FBox), int32 version 6, an opaque middle, `[N][12][N]` + N float3
             positions, a 12-byte-per-vertex tangent/UV block, then
             `int32 count` + `uint16[count]` indices.
             The historic 12/119 was NOT a second layout: the BoolProperty value
             byte shifts the render stream by one byte per bool, and the old
             locator scanned 4-byte-aligned offsets only.

  MEASURED   `find_vertices` / `find_indices` — the *heuristic* locator, kept as
             the fallback path (`resolve_geometry` reports which path ran; it is
             used by 0 of 119 meshes). It assumes 4-byte alignment and a
             per-vertex magnitude window (`is_coord`):
               * positions are plain float3, preceded by `[count=N][12][N]`;
               * the index buffer is `int32 count` + `uint16[count]`, every
                 index < N, count divisible by 3;
               * the stream after the positions holds FPackedNormal bytes
                 (0x7f / 0x80 / 0xff) — the normal+UV stream.

  NOT DECODED  the opaque middle between the bounds head and the position
             declaration (sections / per-LOD bookkeeping), the tangent/UV block
             contents, materials/textures, actor+component transforms, the pack
             format, and `--arenadir` (the flag does not exist yet).

So: positions + indices are now complete for this package and validated against
the arena oracle; the rest of the rip is still ahead.
"""
import struct

# ---------------------------------------------------------------- primitives
FMT_I32 = struct.Struct('<i')
FMT_F32 = struct.Struct('<f')

# A coordinate triple is "plausible" if every component is finite, inside the
# arena's scale, and not a denormal.  Denormals matter: RL writes small ints
# (12, 150) into float slots, and they read as 1e-44-ish values that a naive
# |v| < 30000 test happily accepts — that is what produced the false "position
# buffers" in the first pass.
COORD_LIMIT = 30000.0
DENORMAL_FLOOR = 1e-30


class Reader:
    """Little-endian cursor over one export's bytes."""

    __slots__ = ('buf', 'pos', 'end')

    def __init__(self, buf, pos=0):
        self.buf = buf
        self.pos = pos
        self.end = len(buf)

    def i32(self):
        v, = FMT_I32.unpack_from(self.buf, self.pos)
        self.pos += 4
        return v

    def f32(self):
        v, = FMT_F32.unpack_from(self.buf, self.pos)
        self.pos += 4
        return v

    def vec3(self):
        return (self.f32(), self.f32(), self.f32())

    def skip(self, n):
        self.pos += n

    @property
    def left(self):
        return self.end - self.pos


def _is_denormal(x):
    return x != 0.0 and abs(x) < DENORMAL_FLOOR


def is_coord(v):
    """True if v looks like an arena-space FVector."""
    return all(x == x and abs(x) < COORD_LIMIT and not _is_denormal(x) for x in v)


# ------------------------------------------------------------ property tags
# UE3 tagged properties, licensee 34: FName is int64 (idx + number).  Two types
# carry an extra 8-byte FName inside the tag before the value (Byte: the enum
# name; Struct: the struct name).  The block ends on a `None` tag whose type
# and size fields are garbage, so the name must be tested FIRST.  MEASURED on
# 510 components / 119 meshes / 76 actors of Stadium_Day_P.upk.
_PAYLOAD_EXTRA = ('ByteProperty', 'StructProperty')
_KNOWN_TYPES = {
    'BoolProperty', 'ByteProperty', 'IntProperty', 'FloatProperty',
    'ObjectProperty', 'NameProperty', 'StrProperty', 'StructProperty',
    'ArrayProperty', 'MapProperty', 'ComponentProperty', 'ClassProperty',
    'DelegateProperty', 'InterfaceProperty', 'MulticastDelegateProperty',
}


def read_properties(pkg, data, start=0, candidates=None):
    """Parse the tagged-property block; return (dict, offset-after-block).

    Cooked RL objects carry a small preamble (4 bytes for actors and meshes,
    8 for components), so several candidate starts are tried and the first one
    whose walk reaches a `None` terminator wins.  `candidates()` lets a caller
    reject a start whose post-block bytes are not a valid render-data head
    (see `resolve_geometry`); it is called as `candidates(props, end)`.
    """
    for begin in range(start, start + 16, 4):
        try:
            props, end = _walk(pkg, data, begin)
        except (ValueError, KeyError, IndexError, struct.error):
            continue
        if props is None:
            continue
        if candidates is None or candidates(props, end):
            return props, end
    raise ValueError('no valid property block')


def _walk(pkg, data, pos):
    """Walk UE3 tags; return (props, offset of the first render-data byte).

    MEASURED, licensee 34, `Stadium_Day_P.upk` (all 119 `StaticMesh` exports):
      * `BoolProperty` carries its value as ONE byte AFTER the 24-byte tag and
        leaves `Size = 0` — so its record is 25 bytes, not 24.  Missing that
        byte is what made every mesh with a `UseSimple*Collision` bool fail the
        walk (and, transitively, made the old locator scan only 4-byte-aligned
        offsets while the real stream was shifted by one byte per bool).
      * The `None` terminator is an 8-byte FName (index + number); the render
        data starts immediately after it.  The tag's type/size words are the
        first bytes of that data, so the NAME must be tested first.
    """
    props = {}
    while pos + 24 <= len(data):
        ni, _nn, ti, _tn = struct.unpack_from('<iiii', data, pos)
        size, arr = struct.unpack_from('<ii', data, pos + 16)
        name = pkg.names[ni] if 0 <= ni < len(pkg.names) else None
        typ = pkg.names[ti] if 0 <= ti < len(pkg.names) else None
        if name == 'None':
            return props, pos + 8
        if name is None or typ not in _KNOWN_TYPES:
            return None, pos
        if size < 0 or size > (8 << 20):
            return None, pos
        body = pos + 24
        extra = 8 if typ in _PAYLOAD_EXTRA else 0
        props[name] = True if typ == 'BoolProperty' else data[body + extra:body + extra + size]
        pos = body + extra + size + (1 if typ == 'BoolProperty' else 0)
    return None, pos


def prop_i32(props, name, default=None):
    v = props.get(name)
    return struct.unpack_from('<i', v)[0] if v and len(v) >= 4 else default


def prop_f32(props, name, default=None):
    v = props.get(name)
    return struct.unpack_from('<f', v)[0] if v and len(v) >= 4 else default


def prop_vec(props, name, default=(0.0, 0.0, 0.0)):
    v = props.get(name)
    return struct.unpack_from('<3f', v) if v and len(v) >= 12 else default


# ------------------------------------------------------- raster stream parse
# MEASURED layout of the bytes that follow the tagged-property block of a
# `StaticMesh` export in this cook (all 119 meshes of `Stadium_Day_P.upk`):
#
#   +0   float3  Bounds.Origin
#   +12  float3  Bounds.BoxExtent
#   +24  float   Bounds.SphereRadius
#   +28  int32   flag (always 0 in the measured set)
#   +32  float3  FBox.Min
#   +44  float3  FBox.Max
#   +56  int32   6                  (stream version — the same value everywhere)
#   +60  int32   opaque             (a power of two; per-mesh, not decoded)
#   ...  opaque middle: the SECTION TABLE lives here (24-byte records) ...
#   q    int32 N, int32 12, int32 N      <- position-buffer declaration
#   q+12 float3[N]                       <- POSITIONS
#   pe   int32 A, int32 stride, int32 N, int32 0, int32 stride, int32 N
#   pe+24  vertex records, N x stride bytes:
#            +0  packed TANGENT (3 bytes + W byte)
#            +4  packed NORMAL  (3 bytes + W byte)
#            +8  A-1 UV channels, each two IEEE-754 binary16
#   ...  int32 0, int32 0, int32 N, int32 T   (index-block descriptor)
#   ...  int32 count, uint16[count]       <- INDICES, count % 3 == 0
#
# `A` (the number of 4-byte groups after the two packed normals) and the
# record `stride` are 1..4 / 8+4*A in the measured set; the 24-byte header
# plus the 16-byte index-block descriptor are the "40 bytes" that the A1
# round saw as `stream_gap == 12*N + 40`.
#
# The declaration used to be located by scanning *4-byte-aligned* offsets for
# `[N][12][N]`.  That is exactly why only 12 of 119 meshes resolved: the
# `BoolProperty` value byte (see `_walk`) shifts the whole render stream by one
# byte per bool, so most declarations sit at offsets ≡ 1, 2 or 3 (mod 4).
#
# The middle of the stream is NOT decoded, so the declaration is pinned by a
# measurement-derived structural invariant instead of by alignment: the mesh's
# own bounds header says where the geometry must be.  `Origin - BoxExtent` and
# `Origin + BoxExtent` (tolerance 1e-3 relative) must contain every one of the
# N vertices, and in the measured set they equal the position bbox EXACTLY.
BOUNDS_HEAD = 56
DECL_SIZE = 12
POS_STRIDE = 12
BOUNDS_TOL = 1e-3


def read_bounds(data, off):
    """Parse the `FBoxSphereBounds` + FBox head; return dict or None.

    Returns None unless the head is self-consistent: origin/extents/radius
    finite, every extent non-negative, radius non-negative.
    """
    if off < 0 or off + BOUNDS_HEAD > len(data):
        return None
    origin = struct.unpack_from('<3f', data, off)
    extent = struct.unpack_from('<3f', data, off + 12)
    radius, = struct.unpack_from('<f', data, off + 24)
    box_min = struct.unpack_from('<3f', data, off + 32)
    box_max = struct.unpack_from('<3f', data, off + 44)
    for v in origin + extent + (radius,) + box_min + box_max:
        if v != v or v in (float('inf'), float('-inf')):
            return None
    if any(e < 0.0 for e in extent) or radius < 0.0:
        return None
    lo = tuple(o - e for o, e in zip(origin, extent))
    hi = tuple(o + e for o, e in zip(origin, extent))
    return {'origin': origin, 'extent': extent, 'radius': radius,
            'box_min': box_min, 'box_max': box_max, 'lo': lo, 'hi': hi}


def _in_box(v, lo, hi, tol):
    for a, l, h in zip(v, lo, hi):
        span = max(1.0, abs(l), abs(h))
        if a < l - tol * span or a > h + tol * span:
            return False
    return True


def _position_bbox(data, start, n):
    """Exact (min, max) of the n float3s at `start`; None if any is not finite."""
    mn = [float('inf')] * 3
    mx = [float('-inf')] * 3
    for i in range(n):
        v = struct.unpack_from('<3f', data, start + 12 * i)
        for k in range(3):
            x = v[k]
            if x != x or x in (float('inf'), float('-inf')):
                return None
            if x < mn[k]:
                mn[k] = x
            if x > mx[k]:
                mx[k] = x
    return tuple(mn), tuple(mx)


def find_position_buffer(data, lo, box):
    """Locate the position declaration; return (num_verts, offset) or None.

    Structural, not a hunt for a plausible triple: the candidate must be
    `[N][12][N]` and all N vertices must lie inside the bounds header's own
    box.  Candidates are then ranked by N (the largest wins); the number of
    passing candidates is reported in the result as `decl_candidates` so a
    regression shows up as ambiguity instead of a silent wrong pick.
    """
    blo, bhi = box['lo'], box['hi']
    best = None
    n_cand = 0
    for q in range(lo, len(data) - DECL_SIZE):
        n1, stride, n2 = struct.unpack_from('<3i', data, q)
        if n1 != n2 or stride != POS_STRIDE or n1 < 3 or n1 >= 400000:
            continue
        start = q + DECL_SIZE
        if start + n1 * POS_STRIDE > len(data):
            continue
        # cheap reject on the first four vertices before the full sweep
        if not all(_in_box(struct.unpack_from('<3f', data, start + 12 * i),
                           blo, bhi, BOUNDS_TOL) for i in range(4)):
            continue
        bbox = _position_bbox(data, start, n1)
        if bbox is None or not _in_box(bbox[0], blo, bhi, BOUNDS_TOL) \
                or not _in_box(bbox[1], blo, bhi, BOUNDS_TOL):
            continue
        n_cand += 1
        if best is None or n1 > best[0]:
            best = (n1, start)
    if best is not None:
        best = (best[0], best[1], n_cand)
    return best


def find_index_buffer(data, from_offset, num_verts):
    """Locate the index buffer after the vertex streams; (count, offset) or None.

    MEASURED shape: int32 count, then `count` uint16 values, all < num_verts,
    count divisible by 3.  The window starts after the positions.  Among the
    well-formed candidates the one that references the LAST vertex wins (a real
    index buffer does; MEASURED: `max(index) == N-1` for 119/119 meshes), then
    the one with the most indices.  The coverage comes back to the caller so it
    can report the pick instead of trusting it.
    """
    best = None
    for q in range(max(0, from_offset), len(data) - 8):
        count, = struct.unpack_from('<i', data, q)
        if count < 3 or count % 3 or count > 6 * num_verts:
            continue
        if q + 4 + count * 2 > len(data):
            continue
        # cheap reject on the first/last few indices before the full sweep
        k = min(6, count)
        if max(struct.unpack_from(f'<{k}H', data, q + 4)) >= num_verts:
            continue
        if max(struct.unpack_from(f'<{k}H', data, q + 4 + (count - k) * 2)) >= num_verts:
            continue
        indices = struct.unpack_from(f'<{count}H', data, q + 4)
        mx = max(indices)
        if mx >= num_verts:
            continue
        score = (mx == num_verts - 1, count)
        if best is None or score > best[0]:
            best = (score, count, q + 4, mx)
    return None if best is None else best[1:]


# ------------------------------------------------------- vertex-stream decode
# MEASURED layout of the per-vertex stream that follows the position buffer
# (all 119 meshes of `Stadium_Day_P.upk`):
#
#   pe+0   int32   A            (number of extra 4-byte groups: 1..4)
#   pe+4   int32   stride       (== 8 + 4*A, i.e. 12/16/20/24 for A=1..4)
#   pe+8   int32   N            (vertex count == the position count)
#   pe+12  int32   0
#   pe+16  int32   stride       (repeat)
#   pe+20  int32   N            (repeat)
#   pe+24  vertex records, N of them, `stride` bytes each:
#            +0  4 bytes  packed TANGENT  (x,y,z as bytes + a W byte)
#            +4  4 bytes  packed NORMAL   (x,y,z as bytes + a W byte)
#            +8  4 bytes  UV channel 0    (two IEEE-754 binary16)
#            +12 ...      UV channel 1..A-1 (two IEEE-754 binary16 each)
#
# The packed-normal map is MEASURED, not assumed: `v = b/255*2-1` (identical to
# `(b-127.5)/127.5`) makes 315046 of 315056 channel-1 vectors unit-length to
# within 1.2e-2, while the classic UE3 `(b-128)/127` leaves 384 of them
# outside [0.98, 1.02] and `(b-128)/128` leaves 450.  The remaining outliers are
# the all-zero source vectors (`7f 7f 7f 80`), not a decode error.
#
# The UV map is MEASURED the same way: `Billboard_Simple`'s four channel-0
# values decode as exactly the quad corners (1,0) (0,0) (0,1) (1,1) under the
# binary16 map, and 91.5 % of all triangles keep a non-degenerate triangle in
# UV space.  The unsigned 16-bit map instead puts the same quad at
# (0.234, 0.063) and the half map is the only one that yields 0/1 corners.
PACKED_NORM_SCALE = 255.0
SECTION_REC = 24          # bytes of one FStaticMeshSection record
SECTION_SCAN = 1 << 16    # how far back the section table is searched


def packed_normal(b):
    """Decode 3 packed-normal bytes to (-1..1) floats: v = b/255*2-1."""
    return (b[0] / 255.0 * 2.0 - 1.0,
            b[1] / 255.0 * 2.0 - 1.0,
            b[2] / 255.0 * 2.0 - 1.0)


def _ranges(idx):
    """Compress a sorted list of indices into 'a-b,c' ranges."""
    out = []
    for i in idx:
        if out and i == out[-1][1] + 1:
            out[-1][1] = i
        else:
            out.append([i, i])
    return ','.join(f'{a}' if a == b else f'{a}-{b}' for a, b in out)


def decode_vertex_stream(data, pe, num_verts):
    """Decode the vertex stream that follows the positions.

    Returns None when the header does not match the measured shape.  The
    returned dict carries the raw decoded values (no renormalisation: the
    validation summary needs the measured |n| as it sits in the file).
    """
    if pe + 24 + 4 > len(data):
        return None
    a, stride, n1, zero, stride2, n2 = struct.unpack_from('<6i', data, pe)
    if not (1 <= a <= 8) or stride != 8 + 4 * a:
        return None
    if n1 != num_verts or n2 != num_verts or zero != 0 or stride2 != stride:
        return None
    base = pe + 24
    if base + num_verts * stride > len(data):
        return None
    normals = []
    tangents = []
    uvs = []
    for v in range(num_verts):
        o = base + v * stride
        t = struct.unpack_from('<4B', data, o)
        n = struct.unpack_from('<4B', data, o + 4)
        tangents.append(packed_normal(t[:3]))
        normals.append(packed_normal(n[:3]))
        uvs.append(struct.unpack_from('<2e', data, o + 8))
    return {'channels': a, 'stride': stride, 'offset': base,
            'tangents': tuple(tangents), 'normals': tuple(normals),
            'uvs': tuple(uvs)}


# --------------------------------------------------------------- section table
# MEASURED layout of the section table (the opaque middle immediately before
# the position declaration).  Each section contributes TWO 24-byte records
# with the same 6-int32 shape:
#
#   +0   int32  MinVertexIndex
#   +4   int32  MaxVertexIndex
#   +8   int32  Flags           (collision-primitive style bitmask)
#   +12  int32  MaterialIndex
#   +16  int32  FirstIndex      (in INDICES, not triangles)
#   +20  int32  NumTriangles
#
# The FIRST record of every section carries the degenerate vertex range
# (0, 1) and the SECOND one carries the real range; both agree on
# (Flags, MaterialIndex, FirstIndex, NumTriangles).  The two records are 24
# or 25 bytes apart (the stride alternates), so the table is located by
# CONTENT, never by a fixed offset.
#
# Locator (v3):
#   * the last section record always ends 5 bytes before the position
#     declaration, i.e. its first field is at decl-29 and its `NumTriangles`
#     field is at decl-9  ->  that is the hard anchor;
#   * a candidate 24-byte record must satisfy the field shape
#     (0 < NumTriangles <= T, FirstIndex % 3 == 0, FirstIndex + 3*NumTriangles
#     <= num_indices, 0 <= MinVertexIndex <= MaxVertexIndex < N,
#     0 <= MaterialIndex < 64);
#   * candidates are grouped by (FirstIndex, NumTriangles); the member with
#     the LARGEST offset is the authoritative one (the degenerate twin always
#     precedes it by 24 or 25 bytes);
#   * the chain is walked BACKWARDS from the anchor with the tiling rule
#     FirstIndex[i] + 3*NumTriangles[i] == FirstIndex[i+1], which is what
#     makes the table self-verifying.
#
# Note: the stored (MinVertexIndex, MaxVertexIndex) is NOT always the exact
# min/max of the index slice (LombardyPoplar_Mini stores (110, 865) for a
# slice that uses vertices 2..880), so it is NOT used as a locator test.


def _section_candidates(data, lo, hi, num_verts, num_tris, indices):
    """24-byte windows that satisfy the section field shape."""
    out = []
    ni = len(indices)
    for o in range(lo, hi):
        mn, mx, fl, mat, fi, nt = struct.unpack_from('<6i', data, o)
        if not (0 < nt <= num_tris):
            continue
        if fi < 0 or fi % 3 or fi + nt * 3 > ni:
            continue
        if not (0 <= mn <= mx < num_verts):
            continue
        if not (0 <= mat < 64):
            continue
        out.append({'offset': o, 'material': mat, 'flags': fl,
                    'first_index': fi, 'num_faces': nt,
                    'min_vertex': mn, 'max_vertex': mx})
    return out


def find_sections(data, decl_off, num_verts, num_tris, indices):
    """Locate the per-section table; return (sections, info) or (None, info).

    `sections` is ordered by `first_index` and every entry tiles the index
    buffer: `first_index[0] == 0` and
    `first_index[i+1] == first_index[i] + 3*num_faces[i]`.
    """
    ni = len(indices)
    info = {'candidates': 0, 'cover': 0, 'tiled': False, 'reason': ''}
    anchor = decl_off - 29
    if anchor < 0 or anchor + SECTION_REC > len(data):
        info['reason'] = 'no room for the section table anchor'
        return None, info
    lo = max(0, decl_off - SECTION_SCAN)
    hi = decl_off - SECTION_REC + 1
    cands = _section_candidates(data, lo, hi, num_verts, num_tris, indices)
    info['candidates'] = len(cands)
    groups = {}
    for c in cands:
        groups.setdefault((c['first_index'], c['num_faces']), []).append(c)

    def pick(fi, nt):
        g = groups.get((fi, nt))
        if not g:
            return None
        return max(g, key=lambda c: c['offset'])

    a_mn, a_mx, a_fl, a_mat, a_fi, a_nt = struct.unpack_from('<6i', data, anchor)
    if not (0 < a_nt <= num_tris and a_fi >= 0 and a_fi % 3 == 0
            and a_fi + a_nt * 3 <= ni and 0 <= a_mn <= a_mx < num_verts
            and 0 <= a_mat < 64):
        info['reason'] = ('the section anchor at decl-29 does not hold a '
                          'section-shaped record')
        return None, info
    chain = []
    cur_fi, cur_nt = a_fi, a_nt
    cur_off = anchor
    seen = set()
    while True:
        rec = pick(cur_fi, cur_nt)
        if rec is None or rec['offset'] > cur_off:
            break
        chain.append(rec)
        cur_off = rec['offset']
        if cur_fi == 0:
            break
        key = (cur_fi, cur_nt)
        if key in seen:
            break
        seen.add(key)
        # find the predecessor: fi + 3*nt == cur_fi, located BEFORE the current
        # record (the table is written front to back, so a candidate that
        # sits after the current record cannot be its predecessor)
        prev = None
        for (pfi, pnt), g in groups.items():
            if pfi + pnt * 3 != cur_fi or pfi >= cur_fi:
                continue
            cand = max(g, key=lambda c: c['offset'])
            if cand['offset'] >= cur_off:
                continue
            if prev is None or cand['offset'] > prev['offset']:
                prev = cand
        if prev is None:
            info['reason'] = (f'section chain ends at first_index {cur_fi}: '
                              f'no predecessor record with '
                              f'FirstIndex + 3*NumTriangles == {cur_fi}')
            break
        cur_fi, cur_nt = prev['first_index'], prev['num_faces']
    chain.reverse()
    if not chain:
        info['reason'] = 'no section record found'
        return None, info
    if chain[0]['first_index'] != 0:
        info['reason'] = (f'section chain starts at first_index '
                          f'{chain[0]["first_index"]}, not 0')
        return None, info
    cover = chain[-1]['first_index'] + chain[-1]['num_faces'] * 3
    info['cover'] = cover
    info['tiled'] = (cover == ni)
    if not info['tiled']:
        info['reason'] = (f'sections cover {cover} of {ni} indices; the index '
                          f'buffer also carries {ni - cover} indices of a '
                          f'further LOD (ni / cover = {ni / cover:.4f})')
    return chain, info


# ------------------------------------------------------- heuristic (fallback)
# The original locator, kept as the secondary path: it assumes 4-byte alignment
# and a per-vertex magnitude window, so it resolves only meshes whose property
# block happens to leave the stream aligned.  `resolve_geometry` reports which
# path produced each mesh; the shipped path is the structural one above.
def find_vertices(data, min_verts=3, sample=24):
    """Locate the position stream; return (num_verts, data_offset) or None.

    The measured header is `[count=N][12][N]` immediately before N float3s.
    `sample` is how many leading vertices are checked to reject false hits
    cheaply (checking all N on a 1.6 MB mesh is not worth it).
    """
    best = None
    for p in range(0, len(data) - 24, 4):
        n1, stride, n2 = struct.unpack_from('<3i', data, p)
        if stride != 12 or n1 != n2 or not (min_verts <= n1 < 400000):
            continue
        start = p + 12
        if start + n1 * 12 > len(data):
            continue
        if all(is_coord(struct.unpack_from('<3f', data, start + 12 * i))
               for i in range(min(n1, sample))):
            if best is None or n1 > best[0]:
                best = (n1, start)
    return best


def find_indices(data, num_verts, from_offset=0):
    """Locate the index buffer; return (count, data_offset) or None.

    MEASURED shape: int32 count, then `count` uint16 values all < num_verts,
    count divisible by 3.  Picks the candidate with the most indices.
    """
    best = None
    for q in range(max(0, from_offset), len(data) - 8):
        count, = struct.unpack_from('<i', data, q)
        if count < 3 or count % 3 or count > 6 * num_verts:
            continue
        if q + 4 + count * 2 > len(data):
            continue
        indices = struct.unpack_from(f'<{count}H', data, q + 4)
        if max(indices) < num_verts and (best is None or count > best[0]):
            best = (count, q + 4)
    return best


def extract_geometry(pkg, export):
    """Best-effort positions+indices for one StaticMesh export.

    Returns a dict or None when this mesh's layout is not one we can locate yet
    (the majority — see the module status).
    """
    data = pkg.data(export)
    verts = find_vertices(data)
    if verts is None:
        return None
    num_verts, vstart = verts
    idx = find_indices(data, num_verts, vstart + num_verts * 12)
    if idx is None:
        return None
    num_indices, istart = idx
    positions = struct.unpack_from(f'<{num_verts * 3}f', data, vstart)
    indices = struct.unpack_from(f'<{num_indices}H', data, istart)
    return {
        'name': pkg.export_name(export),
        'export_bytes': len(data),
        'num_verts': num_verts,
        'num_tris': num_indices // 3,
        'positions': positions,
        'indices': indices,
        'vertex_offset': vstart,
        'index_offset': istart,
    }


def mesh_bounds(positions):
    """Axis-aligned bounds of a flat (x,y,z,...) position tuple."""
    xs = positions[0::3]
    ys = positions[1::3]
    zs = positions[2::3]
    return ((min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs)))


def _head_ok(data, ds):
    """Property-walk gate: the bytes at `ds` must be a valid bounds head."""
    box = read_bounds(data, ds)
    if box is None:
        return False
    if ds + BOUNDS_HEAD + 8 > len(data):
        return False
    version, = struct.unpack_from('<i', data, ds + BOUNDS_HEAD)
    return version == 6


def parse_static_mesh(pkg, export):
    """Structural parse of one `StaticMesh` export; dict or None.

    Path: tagged properties -> bounds head -> position declaration pinned by the
    bounds invariant -> positions -> index buffer.  Reports the byte extent it
    actually consumed (`bytes_read`) so a caller can assert it fits
    `export.serial_size`.
    """
    data = pkg.data(export)
    try:
        props, ds = read_properties(
            pkg, data, candidates=lambda _p, end: _head_ok(data, end))
    except ValueError:
        return None
    box = read_bounds(data, ds)
    pos = find_position_buffer(data, ds + BOUNDS_HEAD, box)
    if pos is None:
        return None
    num_verts, vstart, n_cand = pos
    idx = find_index_buffer(data, vstart + num_verts * POS_STRIDE, num_verts)
    if idx is None:
        return None
    num_indices, istart, max_index = idx
    bbox = _position_bbox(data, vstart, num_verts)
    # Measured: between the positions and the index buffer sits the
    # per-vertex stream (packed tangent + packed normal + A binary16 UV
    # channels) and, before the positions, the section table.  Both are
    # decoded here.
    pos_end = vstart + num_verts * POS_STRIDE
    indices = struct.unpack_from(f'<{num_indices}H', data, istart)
    stream = decode_vertex_stream(data, pos_end, num_verts)
    decl_off = vstart - DECL_SIZE
    sections, sec_info = find_sections(data, decl_off, num_verts,
                                       num_indices // 3, indices)
    return {
        'name': pkg.export_name(export),
        'export_bytes': len(data),
        'properties': props,
        'data_start': ds,
        'bounds_head': box,
        'decl_candidates': n_cand,
        'num_verts': num_verts,
        'num_tris': num_indices // 3,
        'positions': struct.unpack_from(f'<{num_verts * 3}f', data, vstart),
        'indices': indices,
        'max_index': max_index,
        'vertex_offset': vstart,
        'index_offset': istart,
        'position_end': pos_end,
        # bytes between the last position byte and the index buffer
        'stream_gap': istart - 4 - pos_end,
        # 12-byte-per-vertex block present?  (measured: gap == 12*N + 40)
        'tangent_block': istart - 4 - pos_end >= num_verts * POS_STRIDE,
        'position_bbox': bbox,
        # ---- round A2: normals, UVs, sections ----
        'vertex_stream': stream,
        'normals': stream['normals'] if stream else None,
        'tangents': stream['tangents'] if stream else None,
        'uvs': stream['uvs'] if stream else None,
        'uv_channels': stream['channels'] if stream else 0,
        'sections': sections,
        'section_info': sec_info,
        # bytes actually parsed, from the first render byte to the last index
        'bytes_read': istart + num_indices * 2 - ds,
        'bytes_read_end': istart + num_indices * 2,
        'path': 'structural',
    }


def resolve_geometry(pkg, export):
    """Structural parse first, heuristic locator only as a reported fallback."""
    geo = parse_static_mesh(pkg, export)
    if geo is not None:
        return geo
    geo = extract_geometry(pkg, export)
    if geo is not None:
        geo['path'] = 'heuristic-fallback'
        geo['bytes_read'] = geo['index_offset'] + len(geo['indices']) * 2
        geo['position_bbox'] = mesh_bounds(geo['positions'])
    return geo


# Arena truth this extraction is checked against (docs/ARENA_RIP.md): field
# half-length 4096, goal mouth ±2688, centre circle radius ~707, the DFH base
# spans ±31000.  These five meshes are the oracle that says the extraction is
# real geometry and not noise.
ORACLE = frozenset({
    'Field_Center', 'Goal_STD_Collision', 'Field_STD_Collision_SideBot',
    'Field_STD_Collision_Corner', 'DFH_Day_StadiumBase2',
})


def _oracle_ok(name, lo, hi):
    """True when a mesh's measured bounds hit the documented RL truth."""
    if name == 'Field_Center':
        r = max(-lo[0], hi[0], -lo[1], hi[1])
        return abs(r - 707.0) < 1.5 and abs(lo[2]) < 0.01 and abs(hi[2]) < 0.01
    if name == 'Goal_STD_Collision':
        return abs(-lo[0] - 2688.0) < 0.5 and abs(hi[0] - 2688.0) < 0.5
    if name == 'Field_STD_Collision_SideBot':
        return abs(lo[0] - 3713.0) < 1.0 and abs(hi[0] - 4106.0) < 1.5
    if name == 'Field_STD_Collision_Corner':
        return abs(lo[0] - 2426.0) < 1.0 and abs(hi[0] - 4107.0) < 1.5
    if name == 'DFH_Day_StadiumBase2':
        return abs(lo[0] + 31073.0) < 2.0 and abs(hi[0] - 27472.0) < 2.0
    return True


if __name__ == '__main__':
    # Usage: python3 tools/arena/rlmesh.py <Stadium_Day_P.upk>
    import hashlib
    import sys
    sys.path.insert(0, __file__.rsplit('/tools/', 1)[0] + '/tools/decomp')
    import rlupk                                     # noqa: E402  (local tool)

    NORM_LO, NORM_HI = 0.98, 1.02
    UV_LO, UV_HI = -64.0, 64.0

    pkg = rlupk.Package(sys.argv[1])
    ok = miss = fallback = ambig = coverage = tangent = 0
    worst_ratio = (0.0, '')
    head_equal = 0
    oracle = [0, 0]
    worst_dev = 0.0
    exact_bbox = 0
    # ---- round A2 accumulators ----
    nverts_total = 0
    n_len = 0
    n_len_lo = 9.0
    n_len_hi = 0.0
    n_out = 0
    n_out_meshes = []
    n_out_verts = 0
    uv_n = 0
    uv_u_lo = uv_v_lo = float('inf')
    uv_u_hi = uv_v_hi = float('-inf')
    uv_out = 0
    uv_out_meshes = []
    sec_meshes = 0
    sec_tiled = 0
    sec_none = []
    sec_fail = []
    sec_hist = {}
    sec_mat = {}
    table = []
    for e in pkg.exports:
        if pkg.export_class_name(e) != 'StaticMesh' or not e.serial_size:
            continue
        geo = resolve_geometry(pkg, e)
        if geo is None:
            miss += 1
            continue
        ok += 1
        if geo['path'] != 'structural':
            fallback += 1
        lo, hi = mesh_bounds(geo['positions'])
        ratio = geo['bytes_read'] / max(1, e.serial_size)
        if ratio > worst_ratio[0]:
            worst_ratio = (ratio, geo['name'])
        if 'decl_candidates' in geo:
            if geo['decl_candidates'] > 1:
                ambig += 1
            if geo['max_index'] == geo['num_verts'] - 1:
                coverage += 1
            if geo['tangent_block']:
                tangent += 1
            box = geo['bounds_head']
            plo, phi = geo['position_bbox']
            dev = max(max(abs(a - b) for a, b in zip(plo, box['lo'])),
                      max(abs(a - b) for a, b in zip(phi, box['hi'])))
            span = max(1.0, max(abs(v) for v in box['lo'] + box['hi']))
            worst_dev = max(worst_dev, dev / span)
            if dev == 0.0:
                exact_bbox += 1
            head_equal += 1
        # ---- round A2: normals / UVs / sections ----
        nverts = geo['num_verts']
        nverts_total += nverts
        normals = geo.get('normals')
        uvs = geo.get('uvs')
        n_ok = 1
        if normals is None or len(normals) != nverts:
            n_ok = 0
            n_out_meshes.append((geo['name'], 'no normal stream', ''))
        else:
            bad = []
            for vi, v in enumerate(normals):
                L = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) ** 0.5
                n_len += 1
                if L < n_len_lo:
                    n_len_lo = L
                if L > n_len_hi:
                    n_len_hi = L
                if not (NORM_LO <= L <= NORM_HI):
                    bad.append((vi, L))
            n_out += len(bad)
            if bad:
                n_ok = 0
                n_out_verts += len(bad)
                n_out_meshes.append((geo['name'],
                                     f'{len(bad)} vertices with |n| outside '
                                     f'[{NORM_LO},{NORM_HI}]',
                                     _ranges([b[0] for b in bad])))
        u_ok = 1
        if uvs is None or len(uvs) != nverts:
            u_ok = 0
            uv_out_meshes.append((geo['name'], 'no uv stream', ''))
        else:
            bad = []
            for vi, (u, v) in enumerate(uvs):
                uv_n += 1
                if u < uv_u_lo:
                    uv_u_lo = u
                if u > uv_u_hi:
                    uv_u_hi = u
                if v < uv_v_lo:
                    uv_v_lo = v
                if v > uv_v_hi:
                    uv_v_hi = v
                if not (UV_LO <= u <= UV_HI) or not (UV_LO <= v <= UV_HI):
                    bad.append(vi)
            uv_out += len(bad)
            if bad:
                u_ok = 0
                uv_out_meshes.append((geo['name'],
                                      f'{len(bad)} of {nverts} vertices with u '
                                      f'or v outside [{UV_LO:g},{UV_HI:g}]',
                                      _ranges(bad)))
        secs = geo.get('sections')
        info = geo.get('section_info') or {}
        s_ok = 1
        if secs:
            sec_meshes += 1
            sec_hist[len(secs)] = sec_hist.get(len(secs), 0) + 1
            for s in secs:
                sec_mat[s['material']] = sec_mat.get(s['material'], 0) + 1
            if info.get('tiled'):
                sec_tiled += 1
            else:
                s_ok = 0
                sec_fail.append((geo['name'], info.get('reason', 'not tiled')))
        else:
            sec_none.append((geo['name'], info.get('reason', 'no section table')))
        table.append(f"{geo['name']},{nverts},{geo['num_tris']},{n_ok},{u_ok},{s_ok}")
        if geo['name'] in ORACLE:
            if _oracle_ok(geo['name'], lo, hi):
                oracle[0] += 1
            else:
                oracle[1] += 1
                print(f'ORACLE-FAIL {geo["name"]} bounds={lo}..{hi}')
        print(f"{geo['name'][:34]:34s} verts={geo['num_verts']:6d} "
              f"tris={geo['num_tris']:6d} bounds={tuple(round(v) for v in lo)}"
              f"..{tuple(round(v) for v in hi)} "
              f"bytes_read={geo['bytes_read']}/{e.serial_size}")
    print(f'\nrender stream version 6 head accepted: {head_equal}/{ok} meshes')
    print(f'index buffer references the last vertex (max == N-1): '
          f'{coverage}/{ok} meshes')
    print(f'12-byte-per-vertex tangent/UV block present: {tangent}/{ok} meshes')
    print(f'declaration candidates > 1: {ambig} meshes (ambiguity, 0 is clean)')
    print(f'heuristic fallback used: {fallback} meshes')
    print(f'max bytes_read / export serial_size: {worst_ratio[0]:.4f} '
          f'({worst_ratio[1]})')
    print(f'oracle bounds: {oracle[0]}/{len(ORACLE)} named meshes match RL '
          f'truth, {oracle[1]} fail')
    print(f'position bbox vs bounds header: worst deviation {worst_dev:.2e} '
          f'relative ({exact_bbox}/{ok} bit-exact)')
    print(f'\ngeometry resolved {ok}, unresolved {miss}')

    # ---------------------------------------------------------------- A2 ----
    digest = hashlib.sha256('\n'.join(sorted(table)).encode()).hexdigest()
    print('\n--- A2 normals / UVs / sections ---')
    print(f'normals: {n_len} decoded over {ok} meshes, '
          f'|n| range [{n_len_lo:.6f}, {n_len_hi:.6f}], '
          f'outside [{NORM_LO},{NORM_HI}]: {n_out} '
          f'({n_out_verts} vertices, {len(n_out_meshes)} meshes)')
    print(f'uvs: {uv_n} components decoded (channel 0 of A), '
          f'u range [{uv_u_lo:.6g}, {uv_u_hi:.6g}], '
          f'v range [{uv_v_lo:.6g}, {uv_v_hi:.6g}], '
          f'outside [{UV_LO:g},{UV_HI:g}]: {uv_out} '
          f'({len(uv_out_meshes)} meshes)')
    print(f'sections: {sec_meshes} meshes with a section table, '
          f'{sec_tiled} tile the index buffer, {len(sec_none)} without, '
          f'{len(sec_fail)} non-tiling')
    print(f'section count histogram: {dict(sorted(sec_hist.items()))}')
    print(f'section material index histogram: {dict(sorted(sec_mat.items()))}')
    print(f'validation table sha256 (name,verts,tris,normals_ok,uvs_ok,'
          f'sections_ok): {digest}')
    for name, why, why2 in n_out_meshes:
        print(f'NORMAL-FAIL {name}: {why}'
              + (f' [vertex indices {why2}]' if why2 else ''))
    for name, why, why2 in uv_out_meshes:
        print(f'UV-FAIL {name}: {why}'
              + (f' [vertex indices {why2}]' if why2 else ''))
    for name, why in sec_fail:
        print(f'SECTION-FAIL {name}: {why}')
    for name, why in sec_none:
        print(f'SECTION-ABSENT {name}: {why}')
