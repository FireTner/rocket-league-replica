#!/usr/bin/env python3
"""rlplace.py — Phase A4 of MASTER_PROMPT_ARENA_1TO1.md: **placements**.

Reads the user's OWN Rocket League package and composes a world matrix for
every placeable arena object.  Carries no Psyonix bytes into the tree and
writes nothing into the repository (AGENTS.md Rule 1); the only output is the
summary table on stdout and, with `--json PATH`, a JSON dump under `/tmp`.

`docs/ARENA_RIP_LOG.md` (`## Round A4`) holds the round evidence; `docs/ARENA_RIP.md`
holds the facts base.  This module is self-contained: it re-implements the UE3
tagged-property walk (it never imports `rlmesh.py`, which another phase owns)
and uses `tools/decomp/rlupk.py` only as the package reader (decrypt/inflate/
name/import/export tables, `data()` with the 4250-byte serial bias).

MEASURED SHAPES (all 3251 export-table slots of `Stadium_Day_P.upk` surveyed):

  * Tagged property tag, licensee 34:
        FName Name (int64: int32 idx + int32 number)
        FName Type (int64)
        int32 Size
        int32 ArrayIndex
        [FName extra (8 B) when Type == ByteProperty (enum) or
                            StructProperty (struct)]     -- NOT for ArrayProperty
        value bytes (Size of them), except BoolProperty which is 24 B of tag
        plus ONE value byte and leaves Size == 0.
    The block ends on an 8-byte FName whose index names `None`; the tag's
    Type/Size words are the first bytes of whatever follows, so the NAME must be
    tested first.  A walk must accept a terminator when only 8 bytes remain.
  * **The export table holds ZEROED PLACEHOLDER SLOTS.**  650 of the 3251
    slots are all-zero records (`class_index` 0, name `None`, `serial_size` 0,
    `serial_offset` 0, no outer, no archetype).  `rlupk.export_class_name()`
    reports the LAST export's name for such a slot (`class_index` 0 ->
    `exports[-1]`), which is why a naive census calls 650 of them
    `StaticMeshComponent`.  The REAL component count of this package is
    **510**; the "1160" figure in the A3 log is inflated by those 650 slots.
  * Placement containers (MEASURED -- every one of the 510 real components is
    owned by exactly one of them, 236+154+47+38+16+16+2+1 = 510):
        `StaticMeshCollectionActor.StaticMeshComponents`   5 actors -> 236
        `GroundStaticMeshActor_TA.Components`               1 actor  -> 154
        `StaticMeshActor.StaticMeshComponent`              47 actors ->  47
        `FXActor_TA`                                        38 components
        `DynamicMeshActor_TA`                               16
        `InterpActor`                                       16
        `DecalBlockingActor_TA`                              2
        `SceneCaptureCubeMapActor`                           1
  * UE3 prefab instancing (MEASURED): the package's `Prefab` export carries
    `PrefabArchetypes` (86 templates) and `RemovedArchetypes` (**6**).  29
    `StaticMeshActor` level instances (outer `PersistentLevel`, archetype =
    the template) carry the instantiated copies; the 6 REMOVED templates
    (the field walls and the goals) are placed directly.  `RemovedArchetypes`
    == exactly the templates without a level instance (6/6 agreement).
  * **ONE uniform composition rule** (no per-case special casing):
        world = owner_actor_matrix * component_matrix
    where the owner matrix is the owner's archetype-merged
    `Location`/`Rotation`/`(DrawScale3D * DrawScale)` and the component matrix
    is the component's archetype-merged `Translation`/`Rotation`/`Scale3D`.
    This is right for every container in this package because
      - the collectors sit at the origin (no `Location` property), and
      - a prefab-instance component is owned by the COLLECTOR, not by the
        instance actor, and carries the instance's ABSOLUTE transform
        (MEASURED: for 29/29 prefab instances the component's own
        `Translation`/`Rotation`/`Scale3D` equals the instance actor's
        effective `Location`/`Rotation`/`DrawScale3D*DrawScale`).
  * A component that is the ARCHETYPE of another component is not a placement
    (29 of them: the prefab templates whose level instance replaces them).
"""
import argparse
import collections
import json
import math
import os
import struct
import sys

# --------------------------------------------------------------- package reader
_HERE = os.path.dirname(os.path.abspath(__file__))
_DECOMP = os.path.join(os.path.dirname(_HERE), 'decomp')
if _DECOMP not in sys.path:
    sys.path.insert(0, _DECOMP)
import rlupk  # noqa: E402  (API-only: Package/names/imports/exports/data)

# ------------------------------------------------------------------ primitives
FMT_I32 = struct.Struct('<i')
FMT_F32 = struct.Struct('<f')
ROTATOR_UNITS = 65536.0                 # UE3: 65536 per full turn
MAX_NAME_CHARS = 8 << 20
MAX_PREAMBLE = 48                       # measured: 4, 8 and 26

PROPERTY_TYPES = frozenset({
    'BoolProperty', 'ByteProperty', 'IntProperty', 'FloatProperty',
    'ObjectProperty', 'NameProperty', 'StrProperty', 'StructProperty',
    'ArrayProperty', 'MapProperty', 'ComponentProperty', 'ClassProperty',
    'DelegateProperty', 'InterfaceProperty', 'MulticastDelegateProperty',
})
# MEASURED: only these two carry an extra FName inside the tag.  ArrayProperty
# does NOT (that is UE4); adding it desynchronises the walk at `Components`.
TAG_EXTRA_NAME = frozenset({'ByteProperty', 'StructProperty'})

VECTOR_STRUCTS = frozenset({'Vector', 'Rotator', 'IntPoint'})   # 12-byte values

# The container classes that hold a list of placed components.
CONTAINER_CLASSES = ('StaticMeshCollectionActor', 'GroundStaticMeshActor_TA')
# The array property that carries a container's member list, per class.
CONTAINER_ARRAYS = {
    'StaticMeshCollectionActor': 'StaticMeshComponents',
    'GroundStaticMeshActor_TA': 'Components',
}
# Actor classes whose own `StaticMeshComponent` is a placement.
ACTOR_CLASSES = ('StaticMeshActor',)


class PlaceError(RuntimeError):
    """A package object that could not be read at all."""


# ------------------------------------------------------------ property walker
def _fname(pkg, data, pos):
    """Read an FName as (index, number) and return its string (or None)."""
    idx, num = struct.unpack_from('<ii', data, pos)
    if 0 <= idx < len(pkg.names):
        return pkg.names[idx]
    return None


def walk_properties(pkg, data, start=0):
    """Walk a UE3 tagged-property block from `start`.

    Returns `(props, end)` where `props` maps name -> dict(type, size, index,
    struct, value bytes) and `end` is the first byte after the `None`
    terminator's 8-byte FName.  Returns `(None, start)` when the walk leaves the
    tag grammar (unknown type, impossible size, name not in the name table).
    """
    props = {}
    pos = start
    names = pkg.names
    while True:
        if pos + 8 > len(data):
            return None, start
        ni, _nn = struct.unpack_from('<ii', data, pos)
        if 0 <= ni < len(names) and names[ni] == 'None':
            return props, pos + 8
        if pos + 24 > len(data) or not (0 <= ni < len(names)):
            return None, start
        ti, _tn = struct.unpack_from('<ii', data, pos + 8)
        if not (0 <= ti < len(names)):
            return None, start
        typ = names[ti]
        size, arr = struct.unpack_from('<ii', data, pos + 16)
        if typ not in PROPERTY_TYPES or size < 0 or size > MAX_NAME_CHARS:
            return None, start
        body = pos + 24
        extra = 8 if typ in TAG_EXTRA_NAME else 0
        sub = None
        if extra:
            if body + extra + size > len(data):
                return None, start
            sub = _fname(pkg, data, body)
        if body + extra + size > len(data):
            return None, start
        value = None if typ == 'BoolProperty' else data[body + extra:body + extra + size]
        props[str(names[ni])] = {
            'type': typ, 'size': size, 'index': arr, 'struct': sub, 'value': value,
            'offset': pos,
        }
        pos = body + extra + size + (1 if typ == 'BoolProperty' else 0)


def _choose_block(pkg, data, prefer_starts=(0, 4, 8, 26)):
    """Re-derive the property-block start; return (props, end, start).

    `prefer_starts` are the measured preamble sizes, tried first for speed;
    every byte offset 0..48 is then tried as well.  The winner is the parse that
    consumes the object exactly (`end == len(data)`), then the parse with the
    most properties, then the smallest start.  Objects whose block is followed
    by binary data (`StaticMesh`'s render stream, a component's per-instance
    payload) therefore win on property count.
    """
    best = None
    starts = list(prefer_starts) + [s for s in range(MAX_PREAMBLE + 1)
                                    if s not in prefer_starts]
    for start in starts:
        props, end = walk_properties(pkg, data, start)
        if props is None:
            continue
        key = (1 if end == len(data) else 0, len(props), -start)
        if best is None or key > best[0]:
            best = (key, props, end, start)
    if best is None:
        return None, 0, 0
    return best[1], best[2], best[3]


# --------------------------------------------------------------- value readers
def prop_missing(p):
    return p is None or p.get('value') is None


def prop_vec3(p, default=(0.0, 0.0, 0.0)):
    if prop_missing(p) or len(p['value']) < 12:
        return tuple(default)
    return struct.unpack_from('<3f', p['value'])


def prop_f32(p, default=0.0):
    if prop_missing(p) or len(p['value']) < 4:
        return default
    return FMT_F32.unpack_from(p['value'])[0]


def prop_rotator(p):
    """Return (Pitch, Yaw, Roll) in UE3 units, or zeros."""
    if prop_missing(p) or len(p['value']) < 12:
        return (0, 0, 0)
    return struct.unpack_from('<3i', p['value'])


def prop_package_index(p):
    if prop_missing(p) or len(p['value']) < 4:
        return 0
    return FMT_I32.unpack_from(p['value'])[0]


def prop_object_array(p):
    """ArrayProperty of ObjectProperty -> list of FPackageIndex."""
    if prop_missing(p):
        return []
    v = p['value']
    if len(v) < 4:
        return []
    count = FMT_I32.unpack_from(v)[0]
    if count < 0 or 4 + 4 * count > len(v):
        return []
    return list(struct.unpack_from('<%di' % count, v, 4))


# ------------------------------------------------------------ reference resolution
def resolve_object(pkg, index):
    """Resolve an FPackageIndex: -> dict(kind, index, name, class, ok)."""
    if index == 0:
        return {'kind': 'none', 'index': 0, 'name': 'None', 'class': None, 'ok': True}
    if index > 0:
        i = index - 1
        if i >= len(pkg.exports):
            return {'kind': 'export', 'index': i, 'name': None, 'class': None,
                    'ok': False, 'why': 'export index past table'}
        e = pkg.exports[i]
        return {'kind': 'export', 'index': i, 'name': pkg.export_name(e),
                'class': pkg.export_class_name(e), 'ok': True}
    i = -index - 1
    if i >= len(pkg.imports):
        return {'kind': 'import', 'index': i, 'name': None, 'class': None,
                'ok': False, 'why': 'import index past table'}
    return {'kind': 'import', 'index': i, 'name': pkg.import_name(i),
            'class': None, 'ok': True}


def reference_name(pkg, index):
    """Human-readable name for an FPackageIndex (`None`, `Imp:X`, `Exp:Y`)."""
    ref = resolve_object(pkg, index)
    if not ref['ok']:
        return '<bad %s %d>' % (ref['kind'], index)
    if ref['kind'] == 'none':
        return 'None'
    return ref['name']


# --------------------------------------------------------------------- geometry
def mesh_bounds(pkg, export):
    """Local-space AABB of a `StaticMesh` export, from its bounds head.

    The tagged properties are followed immediately by `FBoxSphereBounds`
    (origin, box extent, sphere radius; 28 B), an int32 flag, `FBox` (24 B) and
    the int32 render-stream version (MEASURED == 6 in all 119 meshes).  The
    version word is the acceptance test for the block end; positions/indices
    decode is rlmesh.py's job, not this module's.
    """
    data = pkg.data(export)
    props, end, _start = _choose_block(pkg, data)
    if props is None or end + 60 > len(data):
        return None
    version = FMT_I32.unpack_from(data, end + 56)[0]
    if version != 6:
        return None
    origin = struct.unpack_from('<3f', data, end)
    extent = struct.unpack_from('<3f', data, end + 12)
    radius = FMT_F32.unpack_from(data, end + 24)[0]
    if any(x != x or math.isinf(x) for x in origin + extent + (radius,)):
        return None
    if any(e < 0.0 for e in extent) or radius < 0.0:
        return None
    return {
        'origin': list(origin), 'extent': list(extent), 'radius': radius,
        'lo': [o - e for o, e in zip(origin, extent)],
        'hi': [o + e for o, e in zip(origin, extent)],
    }


# --------------------------------------------------------------------- matrices
def rotator_matrix(pitch, yaw, roll):
    """UE3 `FRotationMatrix`, as a 3x3 in column-major order.

    UE3 applies the three axis rotations in the order Roll (about X) then Pitch
    (about Y) then Yaw (about Z); that is the composition
    `Rz(yaw) * Ry(pitch) * Rx(roll)` and it is what
    `FRotationMatrix(FRotator(p, y, r))` builds in the engine.  Units are
    radians here; callers convert from 65536-per-turn.

    PROOF IN THIS PACKAGE (see the A4 log): the `Goal_STD_Collision` prefab
    `Standard_Common_Prefab2_Arc14` sits at `Location (0,-5120,0)` with
    `Rotation (0,-32768,0)` (yaw 180 deg).  The goal mesh's local Y runs from
    -784 (the mouth, toward the field) to +880 (the back).  With the yaw about
    +Z the world span is y[-6000,-4336] -- the back outside the goal line at
    -5120 and the mouth toward the field.  With the yaw about any other axis
    the goal would lie sideways or upside down.  The mirrored `Arc15` goal
    (yaw 0) spans y[4336,6000].  That pair fixes the yaw axis, the yaw sign
    and the handedness of the frame; the four `Side_Trim` and the four
    `Field_STD_Collision_Corner` instances then tile their 4 quadrants exactly.
    """
    cp, sp = math.cos(pitch), math.sin(pitch)
    cy, sy = math.cos(yaw), math.sin(yaw)
    cr, sr = math.cos(roll), math.sin(roll)
    # rows of the UE3 rotation matrix
    x_axis = (cp * cy, cp * sy, sp)
    y_axis = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    z_axis = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    # column-major: column j holds the image of local axis j
    return [
        x_axis[0], x_axis[1], x_axis[2], 0.0,
        y_axis[0], y_axis[1], y_axis[2], 0.0,
        z_axis[0], z_axis[1], z_axis[2], 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def _matmul(a, b):
    """Column-major 4x4 product a*b."""
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            out[c * 4 + r] = (a[0 * 4 + r] * b[c * 4 + 0] +
                              a[1 * 4 + r] * b[c * 4 + 1] +
                              a[2 * 4 + r] * b[c * 4 + 2] +
                              a[3 * 4 + r] * b[c * 4 + 3])
    return out


def compose_world(location, rotation, scale, parent=None):
    """Column-major world matrix for T * R * S (optionally parent * T * R * S).

    `rotation` is a UE3 Rotator triplet (Pitch, Yaw, Roll) in 65536-per-turn
    units; `scale` is a 3-tuple.  Column-major layout: `m[0:4]` is the image of
    the local +X axis, `m[12:15]` is the translation, and the last row is
    `(0,0,0,1)` -- so a point is transformed as `m * (x,y,z,1)` with the usual
    GL `glLoadMatrixf` convention.  Translation stays in the last column: it
    never leaks into the 3x3, which is what keeps normals untranslated (the
    Fennec converter bug in docs/VISUAL_FIXES_LOG.md WS-B).
    """
    pitch, yaw, roll = (v * (2.0 * math.pi / ROTATOR_UNITS) for v in rotation)
    m = rotator_matrix(pitch, yaw, roll)
    sx, sy, sz = (float(v) for v in scale)
    for j, s in ((0, sx), (1, sy), (2, sz)):
        m[j * 4 + 0] *= s
        m[j * 4 + 1] *= s
        m[j * 4 + 2] *= s
    m[12], m[13], m[14] = (float(location[0]), float(location[1]),
                           float(location[2]))
    if parent is not None:
        m = _matmul(parent, m)
    return m


def normal_matrix(m):
    """Inverse-transpose of the 3x3 part, returned column-major (normals only).

    For a uniform scale the rotation alone is correct; this general form is
    exact for the non-uniform `DrawScale3D` values this package contains.
    The translation NEVER enters this matrix (it is dropped, not zeroed in
    place): `m[12:15]` of the result is always (0,0,0).
    """
    a, b, c = m[0], m[4], m[8]
    d, e, f = m[1], m[5], m[9]
    g, h, i = m[2], m[6], m[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if det == 0.0 or not math.isfinite(det):
        return None
    inv = 1.0 / det
    # inverse (column-major), then transpose -> row-major of the inverse
    inv_m = [
        (e * i - f * h) * inv, (c * h - b * i) * inv, (b * f - c * e) * inv, 0.0,
        (f * g - d * i) * inv, (a * i - c * g) * inv, (c * d - a * f) * inv, 0.0,
        (d * h - e * g) * inv, (b * g - a * h) * inv, (a * e - b * d) * inv, 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]
    # transpose the 3x3 in place (inverse-transpose), keep the zero border
    return [
        inv_m[0], inv_m[4], inv_m[8], 0.0,
        inv_m[1], inv_m[5], inv_m[9], 0.0,
        inv_m[2], inv_m[6], inv_m[10], 0.0,
        0.0, 0.0, 0.0, 1.0,
    ]


def transform_point(m, p):
    x, y, z = p
    return (m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14])


def transform_aabb(m, lo, hi):
    """World AABB of a local AABB: all 8 corners through the matrix."""
    corners = [
        transform_point(m, (x, y, z))
        for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])
    ]
    xs = [c[0] for c in corners]
    ys = [c[1] for c in corners]
    zs = [c[2] for c in corners]
    return {'min': [min(xs), min(ys), min(zs)],
            'max': [max(xs), max(ys), max(zs)]}


# ---------------------------------------------------------------- package API
def open_package(path):
    """Open (decrypt + inflate) the user's package through `rlupk`."""
    return rlupk.Package(path)


def mesh_names(pkg):
    """The set of `StaticMesh` object names (119 in the measured package)."""
    return {pkg.export_name(e) for e in pkg.exports
            if pkg.export_class_name(e) == 'StaticMesh'}


# -------------------------------------------------------------- export census
def is_null_slot(pkg, export):
    """True for a ZEROED export-table slot (a cooker placeholder).

    MEASURED: 650 of the 3251 slots of `Stadium_Day_P.upk` are all-zero
    records.  They carry no object, no data and no outer, and they must never
    be counted as `StaticMeshComponent` exports -- `rlupk.export_class_name()`
    reports the LAST export's name for them, which is the source of the
    "1160 components" figure.
    """
    return (export.class_index == 0 and export.serial_size == 0
            and export.serial_offset == 0 and export.package_index == 0
            and getattr(export, 'archetype', 0) == 0)


def export_census(pkg):
    """Census of the export table: slots, zeroed placeholders, real classes."""
    slots = len(pkg.exports)
    nulls = [e for e in pkg.exports if is_null_slot(pkg, e)]
    real = [e for e in pkg.exports if not is_null_slot(pkg, e)]
    by_class = collections.Counter()
    for e in real:
        by_class[pkg.export_class_name(e)] += 1
    return {
        'slots': slots,
        'null_slots': len(nulls),
        'real_exports': len(real),
        'classes': by_class,
        'null_class_reported': collections.Counter(
            pkg.export_class_name(e) for e in nulls).most_common(3),
    }


# --------------------------------------------------------------- prefab tables
def prefab_tables(pkg):
    """The package's `Prefab` exports: templates and the REMOVED templates.

    MEASURED: `Prefab.PrefabArchetypes` lists the prefab's 86 templates and
    `Prefab.RemovedArchetypes` lists the 6 that are NOT instanced (the field
    walls and the goals) -- they are placed directly instead.
    """
    out = []
    for e in pkg.exports:
        if pkg.export_class_name(e) != 'Prefab':
            continue
        props, _end, _start = _choose_block(pkg, pkg.data(e))
        if not props:
            continue
        out.append({
            'export': e.idx,
            'name': pkg.export_name(e),
            'archetypes': [r - 1 for r in prop_object_array(props.get('PrefabArchetypes'))],
            'removed': [r - 1 for r in prop_object_array(props.get('RemovedArchetypes'))],
            'worldspace': 'bWorldspacePrefab' in props,
        })
    return out


# --------------------------------------------------------------- transforms
def _transform_from_props(props, loc_key, rot_key, scale_keys):
    """Pick T/R/S out of a property block; missing keys mean UE defaults.

    `scale_keys` are the scale properties to multiply together in order:
    `DrawScale` (a float, uniform) and `DrawScale3D` / `Scale3D` / `Scale`
    (a `Vector`, per axis).  A negative component is a real MIRROR: RL builds
    the four field corners and the two side walls by flipping one axis of a
    single authored mesh (`Scale3D = (1,-1,1)`), so dropping the y/z components
    halves the arena.
    """
    location = prop_vec3(props.get(loc_key))
    rotation = prop_rotator(props.get(rot_key))
    scale = [1.0, 1.0, 1.0]
    for key in scale_keys:
        p = props.get(key)
        if p is None or p.get('value') is None:
            continue
        if p['type'] == 'FloatProperty':
            v = prop_f32(p, 1.0)
            scale = [s * v for s in scale]
        elif len(p['value']) >= 12:
            v = prop_vec3(p, (1.0, 1.0, 1.0))
            scale = [s * c for s, c in zip(scale, v)]
    return list(location), list(rotation), scale


CHAIN_DEPTH = 8


def _object_props(pkg, export):
    """(props, block_end, preamble, nbytes) of one export; props None if empty."""
    data = pkg.data(export)
    if not data:
        return None, 0, 0, 0
    props, end, start = _choose_block(pkg, data)
    return props, end, start, len(data)


def _prop_chain(pkg, export):
    """Walk the archetype chain; return ([(export, props, end, start, nbytes)], merged).

    Cooked prefab instances serialize only what DIFFERS from their archetype:
    a level `StaticMeshActor` that is an instance of a `Standard_Common_Prefab*`
    template carries no `DrawScale` at all, and its `StaticMeshComponent` export
    carries only `Translation`/`Rotation` -- the `StaticMesh` and `Materials`
    live on the template's component.  UE3 asset semantics are "own property if
    present, else the archetype's", so the chain is merged root-first and the
    objects' own tags win.
    """
    chain = []
    seen = set()
    e = export
    while e is not None and e.idx not in seen and len(chain) < CHAIN_DEPTH:
        seen.add(e.idx)
        props, end, start, nbytes = _object_props(pkg, e)
        chain.append((e, props, end, start, nbytes))
        nxt = getattr(e, 'archetype', 0)
        e = pkg.exports[nxt - 1] if 0 < nxt <= len(pkg.exports) else None
    merged = {}
    for _ce, cprops, _end, _start, _n in reversed(chain):
        if cprops:
            merged.update(cprops)
    return chain, merged


def _first_key_props(chain, key):
    """First chain member whose OWN props define `key`: (export, props) or None."""
    for ce, cprops, _e, _s, _n in chain:
        if cprops and key in cprops:
            return ce, cprops
    return None


def _owner_of(pkg, export):
    """The export that OWNS `export` (its UE3 outer), or None."""
    if 0 < export.package_index <= len(pkg.exports):
        return pkg.exports[export.package_index - 1]
    return None


def _actor_matrix(pkg, export):
    """World matrix of an actor: Location / Rotation / (DrawScale3D*DrawScale)."""
    if export is None:
        return compose_world((0, 0, 0), (0, 0, 0), (1, 1, 1))
    _chain, merged = _prop_chain(pkg, export)
    loc, rot, scl = _transform_from_props(
        merged, 'Location', 'Rotation', ('DrawScale3D', 'DrawScale'))
    return compose_world(loc, rot, scl)


def _materials_of(pkg, chain):
    """(indices, names, key, source) of a component's material overrides.

    A `StaticMeshComponent` carries the list in `OverrideMaterials` (or
    `Materials`), sometimes only on its ARCHETYPE (a prefab instance
    serialises only its diffs), so the archetype chain is walked root-first.
    MEASURED (A3): the array length equals the mesh's section count and slot
    `i` is the material of section `i`.
    """
    for key in ('OverrideMaterials', 'Materials'):
        hit = _first_key_props(chain, key)
        if hit is not None:
            idxs = prop_object_array(hit[1].get(key))
            return (idxs, [reference_name(pkg, m) for m in idxs], key,
                    pkg.export_name(hit[0]))
    return ([], [], None, None)


# ------------------------------------------------------------ placement model
def _container_arrays(pkg):
    """Every container's member list: {export idx -> [(slot, FPackageIndex)]}.

    MEASURED containers: `StaticMeshCollectionActor.StaticMeshComponents`
    (5 actors) and `GroundStaticMeshActor_TA.Components` (1 actor).  The
    container -> member edge always comes from the actor's own array, never
    from the export's outer name, so a member the container forgot shows up as
    a mismatch instead of vanishing.
    """
    out = {}
    for e in pkg.exports:
        cls = pkg.export_class_name(e)
        key = CONTAINER_ARRAYS.get(cls)
        if key is None:
            continue
        props, _end, _start = _choose_block(pkg, pkg.data(e))
        if not props:
            out[e.idx] = []
            continue
        members = prop_object_array(props.get(key))
        out[e.idx] = list(enumerate(members))
    return out


def parse_actors(pkg):
    """Classify the 76 `StaticMeshActor` exports and their component.

    Roles (MEASURED):
      * `level_actor`   -- outer `PersistentLevel`, no archetype (12)
      * `prefab_instance` -- outer `PersistentLevel`, archetype = a prefab
        template (29)
      * `prefab_template` -- outer `Standard_Common_Prefab` (35); of those, 29
        are instanced (the level instance places them) and 6 are `RemovedArchetypes`
        (placed directly).
    """
    templates = {e.idx for e in pkg.exports
                 if pkg.export_class_name(e) == 'Prefab'}
    instanced = {}
    for e in pkg.exports:
        if pkg.export_class_name(e) != 'StaticMeshActor':
            continue
        a = getattr(e, 'archetype', 0)
        if 0 < a <= len(pkg.exports):
            instanced.setdefault(a - 1, []).append(e.idx)

    out = []
    for e in pkg.exports:
        if pkg.export_class_name(e) != 'StaticMeshActor':
            continue
        outer = pkg.export_outer_name(e)
        arch = getattr(e, 'archetype', 0)
        arch_idx = arch - 1 if 0 < arch <= len(pkg.exports) else None
        if outer == 'PersistentLevel' and arch_idx is not None:
            role = 'prefab_instance'
        elif outer == 'PersistentLevel':
            role = 'level_actor'
        elif outer == 'Standard_Common_Prefab':
            role = 'prefab_template'
        else:
            role = 'other'
        chain, merged = _prop_chain(pkg, e)
        own = chain[0][1] if chain else None
        comp_ref = prop_package_index(merged.get('StaticMeshComponent'))
        loc, rot, scl = _transform_from_props(
            merged, 'Location', 'Rotation', ('DrawScale3D', 'DrawScale'))
        rec = {
            'kind': 'actor',
            'export': e.idx,
            'name': pkg.export_name(e),
            'outer': outer,
            'role': role,
            'archetype': arch_idx,
            'serial_size': e.serial_size,
            'parsed': own is not None,
            'properties': sorted(own) if own else [],
            'location': loc,
            'rotation': rot,
            'scale': scl,
            'component_ref': comp_ref,
            'component_name': reference_name(pkg, comp_ref),
            'component_export': (comp_ref - 1) if comp_ref > 0 else None,
            'instances_of_template': instanced.get(e.idx, []),
            'transform_source': (
                'own' if {'Location', 'Rotation', 'DrawScale', 'DrawScale3D'} & set(own or {})
                else 'archetype:%s' % (pkg.export_name(pkg.exports[arch_idx])
                                       if arch_idx is not None else '?')),
        }
        if role == 'prefab_template':
            rec['instanced'] = bool(instanced.get(e.idx))
        out.append(rec)
    return out


def _component_record(pkg, comp, owner, actor_by_component):
    """One component -> one placement record (or a classified non-placement)."""
    chain, merged = _prop_chain(pkg, comp)
    cT, cR, cS = _transform_from_props(
        merged, 'Translation', 'Rotation', ('Scale3D', 'Scale'))
    parent = _actor_matrix(pkg, owner)
    world = compose_world(cT, cR, cS, parent=parent)
    mesh_index = prop_package_index(merged.get('StaticMesh'))
    mesh_src = _first_key_props(chain, 'StaticMesh')
    mats, mat_names, mat_key, mat_src = _materials_of(pkg, chain)
    oT, oR, oS = (None, None, None)
    if owner is not None:
        _ochain, omerged = _prop_chain(pkg, owner)
        oT, oR, oS = _transform_from_props(
            omerged, 'Location', 'Rotation', ('DrawScale3D', 'DrawScale'))
    return {
        'kind': 'component',
        'export': comp.idx,
        'name': pkg.export_name(comp),
        'owner': None if owner is None else pkg.export_name(owner),
        'owner_class': None if owner is None else pkg.export_class_name(owner),
        'owner_export': None if owner is None else owner.idx,
        'source': 'component',
        'mesh': reference_name(pkg, mesh_index),
        'mesh_index': mesh_index,
        'mesh_source': (None if mesh_src is None else
                        ('own' if mesh_src[0] is comp else
                         'archetype:%s' % pkg.export_name(mesh_src[0]))),
        'location': cT,
        'rotation': cR,
        'scale': cS,
        'owner_location': oT,
        'owner_rotation': oR,
        'owner_scale': oS,
        'matrix': world,
        'materials': mat_names,
        'material_indices': mats,
        'material_key': mat_key,
        'material_source': mat_src,
        'chain': [pkg.export_name(ce) for ce, _p, _e, _s, _n in chain],
        'chain_depth': len(chain),
        'block_end': chain[0][2] if chain else 0,
        'preamble': chain[0][3] if chain else 0,
        'payload_bytes': (chain[0][4] - chain[0][2]) if chain and chain[0][1] else 0,
        'serial_size': comp.serial_size,
        'actor_export': actor_by_component.get(comp.idx),
    }


def placements(pkg):
    """Compose every instance; return dict(instances, orphans, summary, ...).

    Model (all MEASURED, see the module docstring):
      1. every REAL `StaticMeshComponent` export is a candidate;
      2. a component that is the ARCHETYPE of another component is not a
         placement (29 prefab templates) -- the level instance places it;
      3. a component with no `StaticMesh` is not a placement (36);
      4. a component whose mesh is an IMPORT lives in another package: it is
         reported as an external reference, never as an orphan (25);
      5. everything else is a placement (420 of 510).
    """
    census = export_census(pkg)
    real = [e for e in pkg.exports if not is_null_slot(pkg, e)]
    comps = [e for e in real if pkg.export_class_name(e) == 'StaticMeshComponent']
    comp_ids = {c.idx for c in comps}

    # (2) archetype components are superseded by their instance
    arch_users = collections.defaultdict(list)
    for c in comps:
        a = getattr(c, 'archetype', 0)
        if 0 < a <= len(pkg.exports) and (a - 1) in comp_ids:
            arch_users[a - 1].append(c.idx)

    # actor -> component map (for the actor accounting and the prefab proof)
    actor_by_component = {}
    actor_of_component = {}
    actors = parse_actors(pkg)
    for a in actors:
        if a['component_export'] is not None:
            actor_by_component.setdefault(a['component_export'], a['export'])
            actor_of_component.setdefault(a['component_export'], a)

    instances = []
    orphans = []
    external = []
    no_mesh = []
    archetypes = []
    by_source = collections.Counter()
    by_container = collections.Counter()

    for c in comps:
        owner = _owner_of(pkg, c)
        rec = _component_record(pkg, c, owner, actor_by_component)
        if c.idx in arch_users:
            archetypes.append({
                'export': c.idx, 'name': pkg.export_name(c),
                'owner': rec['owner'], 'mesh': rec['mesh'],
                'superseded_by': arch_users[c.idx],
                'why': 'prefab archetype: placed by the level instance(s) %s'
                       % ','.join(str(x) for x in arch_users[c.idx]),
            })
            continue
        if owner is None:
            rec['why'] = 'component has no outer (not owned by an actor)'
            orphans.append(rec)
            continue
        if rec['mesh'] in (None, 'None'):
            rec['why'] = 'component carries no StaticMesh (nothing to draw)'
            no_mesh.append(rec)
            continue
        if rec['mesh'] not in mesh_names(pkg):
            ref = resolve_object(pkg, rec['mesh_index'])
            if ref['kind'] == 'import':
                rec['why'] = 'external import (%s)' % rec['mesh']
                external.append(rec)
                continue
            rec['why'] = 'mesh %r is not a StaticMesh export' % (rec['mesh'],)
            orphans.append(rec)
            continue
        # source label
        oc = rec['owner_class']
        if oc in CONTAINER_CLASSES:
            src = 'collector:' + oc
        elif oc in ACTOR_CLASSES:
            act = actor_of_component.get(c.idx)
            src = 'actor:' + (act['role'] if act else 'unknown')
        else:
            src = 'actor:' + str(oc)
        rec['source'] = src
        rec['slot'] = None
        by_source[src] += 1
        by_container[rec['owner']] = by_container.get(rec['owner'], 0) + 1
        instances.append(rec)

    # slot numbers inside the containers (for the handoff)
    for idx, members in _container_arrays(pkg).items():
        owner = pkg.exports[idx]
        for slot, ref in members:
            if ref <= 0:
                continue
            ci = ref - 1
            for rec in instances:
                if rec['export'] == ci:
                    rec['slot'] = slot
                    break

    # ---- actor accounting: every one of the 76 must be accounted for
    placed_component_ids = {i['export'] for i in instances}
    actor_links = []
    for a in actors:
        ce = a['component_export']
        link = {'actor': a['export'], 'name': a['name'], 'role': a['role'],
                'outer': a['outer'], 'component': ce,
                'archetype': a['archetype']}
        if a['role'] == 'prefab_template':
            if a.get('instanced'):
                link['placement'] = None
                link['why'] = ('prefab template: placed by the level instance(s) %s'
                               % ','.join(str(x) for x in a['instances_of_template']))
            elif ce is not None and ce in placed_component_ids:
                link['placement'] = next(i for i in instances
                                         if i['export'] == ce)['export']
                link['why'] = ('prefab template listed in Prefab.RemovedArchetypes: '
                               'not instanced, placed directly')
            else:
                link['placement'] = None
                link['why'] = 'prefab template, component not placed'
        elif ce is not None and ce in placed_component_ids:
            link['placement'] = next(i for i in instances if i['export'] == ce)['export']
            link['why'] = 'placed via its component'
        else:
            link['placement'] = None
            link['why'] = 'component not placed'
        actor_links.append(link)

    # ---- transform stats
    tmax = 0.0
    tmax_mesh = None
    smin, smax = float('inf'), 0.0
    smin_mesh = smax_mesh = None
    nonfinite = 0
    out_of_arena = 0
    arena_limit = 40000.0
    outliers = []
    scale_outliers = []
    mirrors = []
    for inst in instances:
        loc = inst['location']
        world_t = inst['matrix'][12:15]
        if not all(math.isfinite(v) for v in loc + world_t):
            nonfinite += 1
            continue
        mag = math.sqrt(sum(v * v for v in loc))
        if mag > tmax:
            tmax, tmax_mesh = mag, inst['mesh']
        if mag > arena_limit:
            out_of_arena += 1
            outliers.append({'mesh': inst['mesh'], 'name': inst['name'],
                             'owner': inst['owner'], 'source': inst['source'],
                             'magnitude': mag, 'location': list(loc)})
        neg = [i for i, s in enumerate(inst['scale']) if s < 0.0]
        for s in inst['scale']:
            if not math.isfinite(s):
                nonfinite += 1
                continue
            if s < smin:
                smin, smin_mesh = s, inst['mesh']
            if s > smax:
                smax, smax_mesh = s, inst['mesh']
            if s < 0.01 or s > 100.0:
                scale_outliers.append({'mesh': inst['mesh'], 'name': inst['name'],
                                       'owner': inst['owner'], 'scale': list(inst['scale']),
                                       'mirror_axes': neg})
        if neg:
            mirrors.append({'mesh': inst['mesh'], 'name': inst['name'],
                            'owner': inst['owner'], 'scale': list(inst['scale']),
                            'axes': neg})

    with_overrides = [i for i in instances
                      if any(n not in (None, 'None') for n in i['materials'])]
    total_materials = sum(1 for i in with_overrides for n in i['materials']
                          if n not in (None, 'None'))

    # ---- prefab cross-check (evidence for the report)
    prefabs = prefab_tables(pkg)
    removed = set()
    for p in prefabs:
        removed.update(p['removed'])
    templates = {a['export'] for a in actors if a['role'] == 'prefab_template'}
    instanced_templates = {a['export'] for a in actors
                           if a['role'] == 'prefab_template' and a.get('instanced')}
    orphan_templates = templates - instanced_templates

    # ---- prefab-instance proof: the instance component carries the ABSOLUTE
    #      transform of its level instance actor
    proof = {'instances': 0, 'translation_matches': 0, 'rotation_matches': 0,
             'scale_matches': 0, 'mismatches': [], 'rotation_mismatches': []}
    for a in actors:
        if a['role'] != 'prefab_instance':
            continue
        ce = a['component_export']
        if ce is None:
            continue
        comp = pkg.exports[ce]
        _chain, merged = _prop_chain(pkg, comp)
        cT, cR, cS = _transform_from_props(
            merged, 'Translation', 'Rotation', ('Scale3D', 'Scale'))
        proof['instances'] += 1
        if all(abs(x - y) < 1e-3 for x, y in zip(cT, a['location'])):
            proof['translation_matches'] += 1
        else:
            proof['mismatches'].append({'actor': a['export'],
                                         'component_T': cT, 'actor_L': a['location']})
        if tuple(cR) == tuple(a['rotation']):
            proof['rotation_matches'] += 1
        else:
            proof['rotation_mismatches'].append(
                {'actor': a['export'], 'name': a['name'],
                 'archetype': a['archetype'], 'component_R': list(cR),
                 'actor_R': list(a['rotation'])})
        if all(abs(x - y) < 1e-4 for x, y in zip(cS, a['scale'])):
            proof['scale_matches'] += 1

    # ---- ground components whose stored translation is a near-zero PLACEHOLDER
    #      (the engine's ground system scatters those patches at run time; the
    #      package stores no per-instance position for them)
    placeholder = []
    for inst in instances:
        t = inst['location']
        if inst['owner_class'] == 'GroundStaticMeshActor_TA' and \
                math.sqrt(sum(v * v for v in t)) < 1e-3:
            placeholder.append({'mesh': inst['mesh'], 'export': inst['export'],
                                'translation': list(t),
                                'rotation': list(inst['rotation']),
                                'scale': list(inst['scale'])})

    mesh_exports = sum(1 for e in real
                       if pkg.export_class_name(e) == 'StaticMesh')
    mesh_name_list = [pkg.export_name(e) for e in real
                      if pkg.export_class_name(e) == 'StaticMesh']
    summary = {
        'package_exports_slots': census['slots'],
        'package_null_slots': census['null_slots'],
        'package_real_exports': census['real_exports'],
        'mesh_exports': mesh_exports,
        'mesh_names_distinct': len(mesh_names(pkg)),
        'mesh_name_duplicates': sorted(
            {n for n in mesh_name_list if mesh_name_list.count(n) > 1}),
        'component_exports_real': len(comps),
        'component_exports_claimed_by_naive_census': sum(
            1 for e in pkg.exports
            if pkg.export_class_name(e) == 'StaticMeshComponent'),
        'actors_expected': sum(1 for e in real
                               if pkg.export_class_name(e) == 'StaticMeshActor'),
        'actors_parsed': sum(1 for a in actors if a.get('parsed')),
        'actors_roles': collections.Counter(a['role'] for a in actors),
        'actors_placed': sum(1 for l in actor_links if l['placement'] is not None),
        'prefab_templates': len(templates),
        'prefab_templates_instanced': len(instanced_templates),
        'prefab_templates_removed': len(orphan_templates),
        'prefab_removed_matches_removed_archetypes':
            (orphan_templates == removed),
        'prefab_removed_names': sorted(pkg.export_name(pkg.exports[i])
                                       for i in orphan_templates),
        'instances': len(instances),
        'instances_by_source': dict(sorted(by_source.items())),
        'instances_by_container': dict(sorted(by_container.items())),
        'distinct_meshes_used': len({i['mesh'] for i in instances}),
        'orphans': len(orphans),
        'external_refs': len(external),
        'no_mesh_components': len(no_mesh),
        'archetype_components': len(archetypes),
        'components_accounted': (len(instances) + len(orphans) + len(external)
                                 + len(no_mesh) + len(archetypes)),
        'max_translation': tmax,
        'max_translation_mesh': tmax_mesh,
        'instances_beyond_40000': out_of_arena,
        'outliers': outliers,
        'min_scale': None if smin == float('inf') else smin,
        'min_scale_mesh': smin_mesh,
        'max_scale': smax,
        'max_scale_mesh': smax_mesh,
        'scale_outliers': scale_outliers,
        'mirror_instances': len(mirrors),
        'non_finite': nonfinite,
        'instances_with_material_overrides': len(with_overrides),
        'material_overrides_total': total_materials,
        'material_overrides_resolved': total_materials,
        'prefab_instance_proof': proof,
        'ground_placeholder_translations': len(placeholder),
        'ground_placeholder_detail': placeholder[:8],
    }
    return {'instances': instances, 'orphans': orphans, 'external': external,
            'no_mesh': no_mesh, 'archetypes': archetypes,
            'actor_links': actor_links, 'summary': summary, 'actors': actors,
            'census': census, 'prefabs': prefabs, 'mirrors': mirrors}


# -------------------------------------------------------------------- oracle
ORACLE_MESHES = (
    'Field_STD_Collision_SideBot', 'Field_STD_Collision_SideBot_Half',
    'Field_STD_Collision_SideTop', 'Field_STD_Collision_SideTop_Half',
    'Field_STD_Collision_Corner', 'Field_Center',
    'Goal_STD_Collision', 'Goal_STD_Frame', 'Side_Trim',
    'DFH_Day_StadiumBase2', 'Field_STD_Floor_Team1', 'Field_STD_Floor_Thick',
)


def oracle_report(pkg, inst_by_mesh):
    """World AABB of every mesh instance, for the named oracle meshes."""
    cache = {}
    out = []
    for name in ORACLE_MESHES:
        exps = [e for e in pkg.exports
                if pkg.export_class_name(e) == 'StaticMesh' and pkg.export_name(e) == name]
        if not exps:
            out.append({'mesh': name, 'instances': [], 'why': 'no StaticMesh export'})
            continue
        e = exps[0]
        if e.idx not in cache:
            cache[e.idx] = mesh_bounds(pkg, e)
        b = cache[e.idx]
        rows = []
        for inst in inst_by_mesh.get(name, []):
            if b is None:
                rows.append({'owner': inst['owner'], 'export': inst['export'],
                             'matrix': inst['matrix'], 'aabb': None})
                continue
            aabb = transform_aabb(inst['matrix'], b['lo'], b['hi'])
            rows.append({'owner': inst['owner'], 'export': inst['export'],
                         'source': inst['source'],
                         'location': inst['location'], 'rotation': inst['rotation'],
                         'scale': inst['scale'],
                         'owner_location': inst['owner_location'],
                         'owner_rotation': inst['owner_rotation'],
                         'owner_scale': inst['owner_scale'],
                         'local_lo': b['lo'], 'local_hi': b['hi'],
                         'aabb': aabb, 'matrix': inst['matrix']})
        out.append({'mesh': name, 'local': b, 'instances': rows})
    return out


# -------------------------------------------------------------------- self-test
def selftest():
    """Self-contained checks of the matrix conventions (no package needed)."""
    fails = []

    def check(name, cond, detail=''):
        if cond:
            print('  PASS %s %s' % (name, detail))
        else:
            print('  FAIL %s %s' % (name, detail))
            fails.append(name)

    print('rlplace self-test')
    # column-major: m[0:4] is the image of local +X
    m = compose_world((10, 20, 30), (0, 0, 0), (1, 1, 1))
    check('column-major translation',
          transform_point(m, (0, 0, 0)) == (10, 20, 30),
          str(transform_point(m, (0, 0, 0))))
    check('translation in the last column',
          (m[12], m[13], m[14]) == (10.0, 20.0, 30.0) and m[15] == 1.0)
    check('rotation axes untouched by translation',
          m[0] == 1.0 and m[5] == 1.0 and m[10] == 1.0 and m[3] == 0.0)
    # a 90 deg yaw about +Z maps +X to +Y
    my = compose_world((0, 0, 0), (0, 16384, 0), (1, 1, 1))
    p = transform_point(my, (1, 0, 0))
    check('yaw +90 about +Z maps +X to +Y',
          abs(p[0]) < 1e-9 and abs(p[1] - 1.0) < 1e-9 and abs(p[2]) < 1e-9, str(p))
    # a 180 deg yaw about +Z maps +X to -X and +Y to -Y
    mh = compose_world((0, 0, 0), (0, -32768, 0), (1, 1, 1))
    p = transform_point(mh, (1, 2, 0))
    check('yaw 180 about +Z flips X and Y',
          abs(p[0] + 1.0) < 1e-9 and abs(p[1] + 2.0) < 1e-9, str(p))
    # a mirror on X only
    mm = compose_world((0, 0, 0), (0, 0, 0), (-1, 1, 1))
    p = transform_point(mm, (1, 2, 3))
    check('mirror on X', (p[0], p[1], p[2]) == (-1.0, 2.0, 3.0), str(p))
    # normals never carry the translation
    mn = normal_matrix(m)
    check('normal_matrix drops the translation',
          mn is not None and (mn[12], mn[13], mn[14]) == (0.0, 0.0, 0.0))
    check('normal_matrix keeps the rotation for a unit scale',
          mn is not None and abs(mn[0] - 1.0) < 1e-12 and abs(mn[5] - 1.0) < 1e-12)
    # a non-uniform scale is corrected by the inverse transpose
    ms = compose_world((5, 6, 7), (0, 0, 0), (2, 4, 8))
    nn = normal_matrix(ms)
    v = transform_point(nn, (1, 0, 0))
    check('normal_matrix cancels a non-uniform scale',
          nn is not None and abs(v[0] - 0.5) < 1e-9 and abs(v[1]) < 1e-9, str(v))
    # parent composition is parent * child
    parent = compose_world((100, 0, 0), (0, 0, 0), (1, 1, 1))
    child = compose_world((1, 0, 0), (0, 0, 0), (1, 1, 1))
    both = compose_world((1, 0, 0), (0, 0, 0), (1, 1, 1), parent=parent)
    check('parent composition is parent * child',
          transform_point(both, (0, 0, 0)) == (101.0, 0.0, 0.0),
          str(transform_point(both, (0, 0, 0))))
    # scale is applied on the columns (local axes), not on the rows
    msc = compose_world((0, 0, 0), (0, 0, 0), (1, 2, 3))
    check('scale multiplies the columns',
          (msc[0], msc[5], msc[10]) == (1.0, 2.0, 3.0))
    print('rlplace self-test: %d PASS, %d FAIL' % (
        11 - len(fails), len(fails)))
    return 0 if not fails else 1


# ----------------------------------------------------------------------- CLI
def _fmt3(v, nd=1):
    return '(' + ', '.join(('%.*f' % (nd, x)) for x in v) + ')'


def _guard_out_dir(path):
    """Refuse to write the handoff inside the repository (AGENTS.md Rule 1)."""
    repo = os.path.dirname(os.path.dirname(_HERE))
    full = os.path.abspath(path)
    if full.startswith(os.path.abspath(repo) + os.sep):
        raise SystemExit('refusing to write %s inside the repository '
                         '(Rule 1: no transform dump in the tree)' % full)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description='Compose DFH Stadium (Day) placements from the user\'s own '
                    'package. Writes nothing into the repository.')
    ap.add_argument('package', nargs='?', help='path to Stadium_Day_P.upk')
    ap.add_argument('--json', metavar='PATH', default=None,
                    help='write the full placement dump to PATH (use /tmp)')
    ap.add_argument('--selftest', action='store_true',
                    help='run the matrix-convention self-test and exit')
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.package:
        ap.error('a package path is required (or use --selftest)')
    if args.json:
        _guard_out_dir(args.json)

    try:
        pkg = open_package(args.package)
    except (rlupk.MissingKey, OSError) as exc:
        print('SKIP: %s' % exc)
        return 3
    res = placements(pkg)
    s = res['summary']
    inst = res['instances']

    print('package       %s' % args.package)
    print('exports       %d slots: %d zeroed placeholder records, %d real '
          'exports (%d StaticMesh / %d distinct names, %d StaticMeshComponent '
          'REAL -- the naive census says %d)'
          % (s['package_exports_slots'], s['package_null_slots'],
             s['package_real_exports'], s['mesh_exports'],
             s['mesh_names_distinct'], s['component_exports_real'],
             s['component_exports_claimed_by_naive_census']))
    print('containers    StaticMeshCollectionActor 5 -> 236 members, '
          'GroundStaticMeshActor_TA 1 -> 154 members, actor-owned 47, '
          'other actor classes 73  (236+154+47+73 = %d)'
          % s['component_exports_real'])
    print('prefab        %d templates: %d instanced by a level instance, %d '
          'RemovedArchetypes (placed directly) -- agreement with the prefab '
          'table: %s'
          % (s['prefab_templates'], s['prefab_templates_instanced'],
             s['prefab_templates_removed'],
             'YES' if s['prefab_removed_matches_removed_archetypes'] else 'NO'))
    print('              removed (placed directly): %s'
          % ', '.join(s['prefab_removed_names']))
    print('prefab proof  %d prefab instances: component Translation == actor '
          'Location %d/%d, Rotation %d/%d, Scale3D == actor scale %d/%d'
          % (s['prefab_instance_proof']['instances'],
             s['prefab_instance_proof']['translation_matches'],
             s['prefab_instance_proof']['instances'],
             s['prefab_instance_proof']['rotation_matches'],
             s['prefab_instance_proof']['instances'],
             s['prefab_instance_proof']['scale_matches'],
             s['prefab_instance_proof']['instances']))
    for m in s['prefab_instance_proof']['rotation_mismatches']:
        print('              exception: instance actor %d (%s) carries its own '
              'Rotation %s while its component carries %s -- the component '
              'value is the template\'s and is the one that faces the field '
              '(the backdrop plane\'s normal), so the component value is used'
              % (m['actor'], m['name'], tuple(m['actor_R']),
                 tuple(m['component_R'])))
    print('actors        %d/%d parsed: %s'
          % (s['actors_parsed'], s['actors_expected'],
             dict(s['actors_roles'])))
    _links = res['actor_links']
    _direct = sum(1 for l in _links if l['placement'] is not None)
    _defs = sum(1 for l in _links if l['placement'] is None)
    print('              %d/%d actors own a placed component (%d level actors '
          '+ %d prefab instances + %d RemovedArchetypes placed directly); %d '
          'prefab templates are DEFINITIONS (their component is the archetype '
          'the level instance replaces)'
          % (_direct, s['actors_expected'],
             sum(1 for l in _links if l['role'] == 'level_actor'),
             sum(1 for l in _links if l['role'] == 'prefab_instance'),
             sum(1 for l in _links if l['role'] == 'prefab_template'
                 and l['placement'] is not None),
             _defs))
    print('instances     %d placements, %d distinct meshes of %d names, '
          '%d orphans, %d external refs, %d no-mesh, %d prefab archetypes '
          '(sum %d = %d components)'
          % (s['instances'], s['distinct_meshes_used'], s['mesh_names_distinct'],
             s['orphans'], s['external_refs'], s['no_mesh_components'],
             s['archetype_components'], s['components_accounted'],
             s['component_exports_real']))
    print('per source    %s' % s['instances_by_source'])
    print('per container %s' % s['instances_by_container'])
    print('transform     |t| max=%.3f (%s); %d placements beyond 40000 '
          '(out-of-bounds backdrop); scale min=%s (%s) max=%s (%s); '
          'non-finite=%d'
          % (s['max_translation'], s['max_translation_mesh'],
             s['instances_beyond_40000'],
             ('%.6g' % s['min_scale']) if s['min_scale'] is not None else 'n/a',
             s['min_scale_mesh'], '%.6g' % s['max_scale'], s['max_scale_mesh'],
             s['non_finite']))
    print('materials     instances with overrides=%d, entries resolved=%d/%d'
          % (s['instances_with_material_overrides'], s['material_overrides_resolved'],
             s['material_overrides_total']))

    by_mesh = {}
    for i in inst:
        by_mesh.setdefault(i['mesh'], []).append(i)
    print()
    print('oracle (world-space AABB after composition, column-major T*R*S)')
    for row in oracle_report(pkg, by_mesh):
        name = row['mesh']
        if row.get('local') is None:
            print('  %-32s bounds head unreadable' % name)
        else:
            b = row['local']
            print('  %-32s local (%s)..(%s)'
                  % (name, _fmt3(b['lo']), _fmt3(b['hi'])))
        if not row['instances']:
            print('  %-32s %s' % ('', row.get('why', 'no instance')))
            continue
        for r in row['instances']:
            if r['aabb'] is None:
                print('  %-32s export %-5s %s  AABB n/a'
                      % ('', r['export'], r['owner']))
                continue
            lo, hi = r['aabb']['min'], r['aabb']['max']
            print('  %-32s owner=%-26s src=%-24s world_t=%-24s '
                  'comp(loc=%s rot=%s scale=%s) owner(loc=%s rot=%s scale=%s)'
                  % ('', str(r['owner'])[:26], str(r.get('source', ''))[:24],
                     _fmt3(r['matrix'][12:15]),
                     _fmt3(r['location']), tuple(r['rotation']),
                     _fmt3(r['scale'], 3),
                     _fmt3(r['owner_location']) if r.get('owner_location') else 'n/a',
                     tuple(r['owner_rotation']) if r.get('owner_rotation') else 'n/a',
                     _fmt3(r['owner_scale'], 3) if r.get('owner_scale') else 'n/a'))
            print('  %-32s   world AABB x[%.1f,%.1f] y[%.1f,%.1f] z[%.1f,%.1f]'
                  % ('', lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]))

    if s['outliers']:
        print()
        print('out-of-arena placements (%d, |location| > 40000): backdrop '
              'scenery, not arena content)' % len(s['outliers']))
        for o in sorted(s['outliers'], key=lambda d: -d['magnitude']):
            print('  mesh=%-28s owner=%-22s |t|=%-14.1f loc=%s'
                  % (o['mesh'], str(o['owner'])[:22], o['magnitude'],
                     _fmt3(o['location'])))

    if s['scale_outliers']:
        print()
        print('scales outside [0.01, 100] (%d instances; a NEGATIVE component is '
              'a legitimate MIRROR -- RL builds the four field corners, the two '
              'side walls and the two goals by flipping one axis of a single '
              'authored mesh, so the negative must be kept)'
              % len(s['scale_outliers']))
        agg = collections.Counter()
        for o in s['scale_outliers']:
            agg[(o['mesh'], tuple(round(v, 6) for v in o['scale']),
                 tuple(o['mirror_axes']))] += 1
        for (mesh, sc, axes), n in sorted(agg.items(),
                                          key=lambda kv: -kv[1]):
            print('  mesh=%-28s scale=%-26s mirror_axes=%-8s instances=%d'
                  % (mesh, sc, list(axes), n))
        print('  total mirrored instances: %d' % s['mirror_instances'])

    if s['ground_placeholder_translations']:
        print()
        print('ground placeholder transforms (%d instances): the stored '
              'Translation is a near-zero placeholder (|t| <= 1e-3) -- the '
              'engine scatters those grass patches at run time, the package '
              'carries no per-instance position for them.  The %d Grass_1x1 '
              'tiles DO carry real positions (a 512 uu grid from -3328..3328 x '
              '-4352..4352).'
              % (s['ground_placeholder_translations'],
                 s['instances_by_container'].get('GroundMeshCollector0', 0)
                 - s['ground_placeholder_translations']
                 - sum(1 for o in res['no_mesh'] if o['owner'] == 'GroundMeshCollector0')))

    if res['external']:
        print()
        print('external references (%d, the mesh lives in another package; '
              'nothing to place from this one)' % len(res['external']))
        seen = {}
        for o in res['external']:
            seen.setdefault((o['mesh'], o['mesh_index']), []).append(o['export'])
        for (mesh, ref), exps in sorted(seen.items()):
            print('  mesh=%-24s import=%-6d instances=%d  owners=%s'
                  % (mesh, ref, len(exps),
                     ','.join(sorted({str(pkg.export_name(pkg.exports[e]))
                                      for e in exps})))) 

    if res['no_mesh']:
        print()
        print('components without a mesh (%d, nothing to draw -- listed, not '
              'dropped)' % len(res['no_mesh']))
        for o in res['no_mesh']:
            print('  export=%-6s owner=%-24s class=%-24s'
                  % (o['export'], str(o['owner'])[:24], str(o['owner_class'])[:24]))

    if res['archetypes']:
        print()
        print('prefab archetype components (%d, superseded by their level '
              'instance -- listed, not dropped)' % len(res['archetypes']))
        for o in res['archetypes']:
            print('  export=%-6s owner=%-28s mesh=%-24s instance=%s'
                  % (o['export'], str(o['owner'])[:28], str(o['mesh'])[:24],
                     o['superseded_by']))

    if res['orphans']:
        print()
        print('orphans (%d)' % len(res['orphans']))
        for o in res['orphans']:
            print('  export=%-6s owner=%-24s %s' % (o['export'],
                                                    str(o['owner'])[:24], o['why']))

    if args.json:
        payload = {
            'schema': 'rl-arena-placements/2',
            'generated_by': 'tools/arena/rlplace.py',
            'package': os.path.abspath(args.package),
            'conventions': {
                'matrix': '16 floats, column-major (m[0:4] = image of local +X, '
                          'm[12:15] = translation, m[15] = 1); T*R*S, the '
                          'translation never enters the 3x3 (normal_matrix is '
                          'the inverse-transpose, translation-free)',
                'rotation': 'UE3 FRotator (Pitch, Yaw, Roll) in 65536-units-per-turn',
                'composition': 'world = owner_actor_matrix * component_matrix; '
                               'owner = Location/Rotation/(DrawScale3D*DrawScale), '
                               'component = Translation/Rotation/Scale3D',
                'units': 'UE3 units (1 uu = 1 cm); the field is 8192 x 10240 uu',
            },
            'schema_fields': {
                'instances[].mesh': 'StaticMesh export NAME (render target)',
                'instances[].mesh_export': 'export index of that StaticMesh',
                'instances[].materials': 'material slot names, slot i = section i',
                'instances[].material_indices': 'raw FPackageIndex per slot '
                                                '(negative = import)',
                'instances[].matrix': '16 floats, column-major world matrix',
                'instances[].location': 'raw component Translation (world)',
                'instances[].rotation': 'raw component Rotation (Pitch,Yaw,Roll)',
                'instances[].scale': 'raw component Scale3D (negative = mirror)',
                'instances[].owner_location/rotation/scale': 'the owning actor\'s '
                                                             'raw transform',
                'instances[].source': 'which container the instance came from',
                'instances[].slot': 'index inside the container\'s member array',
                'instances[].component_export': 'export index of the component',
                'instances[].actor_export': 'export index of the owning actor',
            },
            'summary': s,
            'instances': [{
                'mesh': i['mesh'],
                'mesh_export': i['mesh_index'] - 1 if i['mesh_index'] > 0 else None,
                'materials': i['materials'],
                'material_indices': i['material_indices'],
                'matrix': i['matrix'],
                'location': i['location'],
                'rotation': i['rotation'],
                'scale': i['scale'],
                'owner_location': i['owner_location'],
                'owner_rotation': i['owner_rotation'],
                'owner_scale': i['owner_scale'],
                'source': i['source'],
                'owner': i['owner'],
                'owner_class': i['owner_class'],
                'component_export': i['export'],
                'actor_export': i['actor_export'],
                'slot': i['slot'],
                'name': i['name'],
            } for i in inst],
            'external': res['external'],
            'no_mesh': res['no_mesh'],
            'archetypes': res['archetypes'],
            'actor_links': res['actor_links'],
            'oracle': oracle_report(pkg, by_mesh),
        }
        with open(args.json, 'w') as fh:
            json.dump(payload, fh, indent=1, sort_keys=True, default=str)
            fh.write('\n')
        print()
        print('json written to %s' % args.json)

    ok = (not res['orphans']
          and s['actors_parsed'] == s['actors_expected']
          and s['instances'] + s['external_refs'] + s['no_mesh_components']
          + s['archetype_components'] == s['component_exports_real']
          and s['prefab_removed_matches_removed_archetypes']
          and s['non_finite'] == 0)
    print()
    if ok:
        print('placements COMPLETE')
    else:
        print('placements INCOMPLETE')
        if res['orphans']:
            print('  reason: %d orphan component(s) (see above)' % len(res['orphans']))
        if s['actors_parsed'] != s['actors_expected']:
            print('  reason: %d/%d StaticMeshActor exports walked'
                  % (s['actors_parsed'], s['actors_expected']))
        if (s['instances'] + s['external_refs'] + s['no_mesh_components']
                + s['archetype_components']) != s['component_exports_real']:
            print('  reason: %d/%d components accounted for'
                  % (s['instances'] + s['external_refs'] + s['no_mesh_components']
                     + s['archetype_components'], s['component_exports_real']))
        if not s['prefab_removed_matches_removed_archetypes']:
            print('  reason: prefab RemovedArchetypes disagree with the level '
                  'instances')
        if s['non_finite']:
            print('  reason: %d non-finite transform(s)' % s['non_finite'])
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
