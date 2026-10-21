"""Rip an arena into a versioned, deterministic pack — **PHASE A5**.

Reads the user's OWN Rocket League install and writes a self-describing pack
(`arena.json` + `arena.bin`) that the C++ loader (phase A6) can read without
knowing anything about UE3.

    python3 tools/arena/rip_arena.py --pack <Stadium_Day_P.upk> \\
        --out arena_assets/stadium_day [--install <CookedPCConsole dir>]
    python3 tools/arena/rip_arena.py --verify arena_assets/stadium_day

RULE 1 (AGENTS.md): the pack lives ONLY under `arena_assets/` (gitignored) or
outside the repository tree.  This module refuses to write anywhere else
inside the repo and never copies a Psyonix byte into the tree.

The pack format is specified normatively in `docs/ARENA_PACK.md`; the format
identifier is `rl-arena-pack/1` and `FORMAT_VERSION` below must be bumped
whenever the layout changes in a way a reader could notice.

The five modules this tool consumes are owned by other agents and are used
read-only: `rlmesh` (geometry), `rlmat` (materials/textures), `rldds`
(texture decoding), `rlplace` (placements), `rlpkg` (sibling packages).
"""
import argparse
import hashlib
import json
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(os.path.dirname(_HERE))
for _p in (os.path.join(_REPO, 'tools', 'decomp'), _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import rlupk                                                   # noqa: E402
import rldds                                                   # noqa: E402
import rlmat                                                   # noqa: E402
import rlmesh                                                  # noqa: E402
import rlplace                                                 # noqa: E402
import rlpkg                                                   # noqa: E402

# ------------------------------------------------------------------ constants
PACK_FORMAT = 'rl-arena-pack/1'
FORMAT_VERSION = 1
GENERATOR = 'tools/arena/rip_arena.py'

BLOB_MAGIC = b'RLARENAP'      # 8 bytes
BLOB_HEADER_SIZE = 32
BLOB_ALIGN = 4                # every array starts on a 4-byte boundary
BLOB_FILE = 'arena.bin'
MANIFEST_FILE = 'arena.json'

DEFAULT_INDEX = '/tmp/rl_a3b/pkgindex.json'
PACK_DIR_NAME = 'arena_assets'

# blob array kinds (the order they are laid down in the blob)
ARRAY_KINDS = ('positions', 'normals', 'tangents', 'uvs', 'indices', 'matrices')
# the stride of every group array, in BYTES per record, and the number of
# values one record holds (a matrix is 16 doubles = 128 bytes)
ARRAY_STRIDE = {'positions': 12, 'normals': 12, 'tangents': 12, 'uvs': 8,
                'indices': 2, 'matrices': 128}
ARRAY_VALUES = {'positions': 3, 'normals': 3, 'tangents': 3, 'uvs': 2,
                'indices': 1, 'matrices': 16}
ARRAY_TYPE = {'positions': 'f32', 'normals': 'f32', 'tangents': 'f32',
              'uvs': 'f32', 'indices': 'u16', 'matrices': 'f64'}

# mesh section / vertex stream layout (measured by tools/arena/rlmesh.py)
SECTION_REC = 24


class RipError(RuntimeError):
    """A fatal, user-facing error.  The CLI turns it into exit code 2."""


# ------------------------------------------------------------------- helpers
def _f(x):
    """Normalise a float for the manifest.

    `-0.0` becomes `+0.0` and non-finite values are rejected, so the JSON
    number is always the shortest round-trip repr of a finite IEEE-754 double
    (the determinism rule of docs/ARENA_PACK.md).
    """
    v = float(x)
    if v != v or v in (float('inf'), float('-inf')):
        raise RipError('non-finite float in the manifest: %r' % (x,))
    return v + 0.0


def sha256_file(path):
    """sha256 of a file, streamed.  Returns the hex digest."""
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def _guard_out_dir(path):
    """Refuse to write a pack anywhere in the repo except under `arena_assets/`.

    RULE 1: the repository tree must never hold a Psyonix byte.  A pack is a
    Psyonix byte container, so it may only live under `<repo>/arena_assets/`
    (gitignored) or completely outside the repository.
    """
    real = os.path.realpath(os.path.abspath(os.path.expanduser(path)))
    repo = os.path.realpath(_REPO)
    if real == repo or not real.startswith(repo + os.sep):
        return real                              # outside the repo: fine
    allowed = os.path.join(repo, PACK_DIR_NAME)
    if not (real == allowed or real.startswith(allowed + os.sep)):
        raise RipError('refusing to write a pack inside the repository tree at '
                       '%s (Rule 1: only <repo>/%s/ is allowed)'
                       % (real, PACK_DIR_NAME))
    return real


# ------------------------------------------------------------------- the blob
class Blob(object):
    """Accumulates the binary blob; every array is 4-byte aligned."""

    def __init__(self):
        self.buf = bytearray(BLOB_HEADER_SIZE)
        self.arrays = {}          # kind -> {'offset','length','count','stride'}

    def align(self):
        pad = (-len(self.buf)) % BLOB_ALIGN
        if pad:
            self.buf += b'\x00' * pad

    def add(self, kind, data):
        """Append one array; returns (absolute_offset, length)."""
        self.align()
        off = len(self.buf)
        self.buf += data
        return off, len(data)

    def finish(self, array_count):
        """Write the header and return the finished bytes."""
        struct.pack_into('<8sIIQII', self.buf, 0, BLOB_MAGIC, FORMAT_VERSION,
                         BLOB_HEADER_SIZE, len(self.buf), array_count, 0)
        return bytes(self.buf)


# ------------------------------------------------------------------ package IO
def _load_index(install_dir, index_path, quiet=False):
    """Load (or build) the install-wide sibling index."""
    if index_path and os.path.exists(index_path):
        index = rlpkg.load_index(index_path)
        return index
    if not install_dir:
        raise RipError('no sibling index at %s and no --install directory given'
                       % (index_path or '<none>'))
    index = rlpkg.index_install(install_dir, progress=not quiet, quiet=quiet)
    if index_path:
        rlpkg.save_index(index, index_path)
    return index


def _package_identity(pkg, path):
    """The build identity of a cooked package (no absolute paths)."""
    return {
        'file': os.path.basename(path),
        'name': os.path.splitext(os.path.basename(path))[0],
        'file_version': int(pkg.file_version),
        'licensee_version': int(pkg.licensee_version),
        'guid': pkg.guid,
        'bytes': os.path.getsize(path),
        'sha256': sha256_file(path),
        'export_count': len(pkg.exports),
        'import_count': len(pkg.imports),
        'name_count': len(pkg.names),
    }


# ---------------------------------------------------------------- mesh tables
def _mesh_arrays(geo):
    """The float/uint arrays of one parsed mesh."""
    n = geo['num_verts']
    positions = struct.pack('<%df' % (3 * n), *geo['positions'])
    normals = geo.get('normals')
    tangents = geo.get('tangents')
    uvs = geo.get('uvs')
    normals = struct.pack('<%df' % (3 * n), *[c for v in normals for c in v]) \
        if normals else b'\x00' * (12 * n)
    tangents = struct.pack('<%df' % (3 * n), *[c for v in tangents for c in v]) \
        if tangents else b'\x00' * (12 * n)
    uvs = struct.pack('<%df' % (2 * n), *[c for v in uvs for c in v]) \
        if uvs else b'\x00' * (8 * n)
    indices = struct.pack('<%dH' % len(geo['indices']), *geo['indices'])
    return positions, normals, tangents, uvs, indices


def _mesh_entry(name, geo, source, package, file_name, export_index, ranges,
                lod):
    """One manifest mesh record (ranges are filled in later)."""
    lo, hi = geo['position_bbox']
    return {
        'name': name,
        'source': source,
        'package': package,
        'file': file_name,
        'export': export_index,
        'num_verts': geo['num_verts'],
        'num_tris': geo['num_tris'],
        'num_indices': len(geo['indices']),
        'uv_channels': int(geo.get('uv_channels') or 0),
        'uv_stored_channels': 1,
        'bounds': {'min': [_f(v) for v in lo], 'max': [_f(v) for v in hi]},
        'arrays': ranges,
        'sections': [
            {'material_index': int(s['material']),
             'flags': int(s['flags']),
             'first_index': int(s['first_index']),
             'num_triangles': int(s['num_faces']),
             'min_vertex': int(s['min_vertex']),
             'max_vertex': int(s['max_vertex'])}
            for s in (geo.get('sections') or [])],
        'lod': lod,
    }


def _lod_of(geo):
    """LOD bookkeeping: the sections are LOD0, the rest of the index buffer
    is a further LOD (MEASURED: 114 of 119 meshes are exactly tiled)."""
    secs = geo.get('sections') or []
    lod0 = sum(int(s['num_faces']) for s in secs)
    total = int(geo['num_tris'])
    info = geo.get('section_info') or {}
    return {
        'section_triangles': lod0,
        'buffer_triangles': total,
        'extra_lod_triangles': total - lod0,
        'tiled': bool(info.get('tiled')),
        'reason': info.get('reason') or '',
    }


def _collect_in_package_meshes(pkg):
    """Every `StaticMesh` export of the package, sorted by name."""
    out = []
    for e in pkg.exports:
        if pkg.export_class_name(e) != 'StaticMesh' or not e.serial_size:
            continue
        geo = rlmesh.resolve_geometry(pkg, e)
        if geo is None:
            continue
        out.append((pkg.export_name(e), geo, e.idx))
    out.sort(key=lambda t: t[0])
    return out


def _collect_external_meshes(pkg, index):
    """The `StaticMesh` objects this package only IMPORTS, resolved in their
    sibling package (A3b proved `rlmesh` parses them unchanged)."""
    resolver = rlpkg.SiblingResolver(index)
    out = []
    for i, imp in enumerate(pkg.imports):
        if pkg.fname(imp.class_name) != 'StaticMesh':
            continue
        name = pkg.import_name(i)
        package = rlpkg.import_package_name(pkg, i)
        hit = resolver.locate(package, name, cls='StaticMesh')
        if hit is None:
            continue
        fname = hit[0]
        path = os.path.join(index.get('root') or '', fname)
        try:
            spkg = rlupk.Package(path)
        except Exception as exc:                              # noqa: BLE001
            raise RipError('cannot open the sibling package %s: %s' % (fname, exc))
        export = None
        for se in spkg.exports:
            if spkg.export_name(se) == name and \
                    spkg.export_class_name(se) == 'StaticMesh' and se.serial_size:
                export = se
                break
        if export is None:
            continue
        geo = rlmesh.parse_static_mesh(spkg, export)
        if geo is None:
            continue
        out.append((name, geo, export.idx, package, os.path.basename(fname),
                    path))
    out.sort(key=lambda t: t[0])
    return out


# ------------------------------------------------------------------ materials
class MaterialResolver(object):
    """Resolve a material object to its diffuse texture (name + package).

    Uses `rlmat.resolve_material` (A3/A3b) and resolves an IMPORTED material in
    the sibling package that holds it.  Every outcome carries a reason from a
    closed set.
    """

    def __init__(self, mpkg, pkg, sibling):
        self.mpkg = mpkg
        self.pkg = pkg
        self.sibling = sibling
        self.by_name = {}
        self.by_name_all = {}
        for e in pkg.exports:
            if pkg.export_class_name(e) in rlmat.MATERIAL_CLASSES:
                nm = pkg.export_name(e)
                self.by_name.setdefault(nm, e)
                self.by_name_all.setdefault(nm, []).append(e)
        self.imports_by_name = {}
        for i, imp in enumerate(pkg.imports):
            cls = pkg.fname(imp.class_name)
            if cls in rlmat.MATERIAL_CLASSES:
                self.imports_by_name.setdefault(pkg.import_name(i), i)
        self._cache = {}

    # -- one resolution -----------------------------------------------------
    def _export_at(self, index, name):
        """The material export a reference points at.

        A UE3 export NAME is not unique (`BoostPad_Mat` exists twice in this
        package, MEASURED), so a reference is resolved through its export
        INDEX, never through its name.  The name lookup is only a fallback.
        """
        if 0 <= index < len(self.pkg.exports):
            e = self.pkg.exports[index]
            if self.pkg.export_name(e) == name and \
                    self.pkg.export_class_name(e) in rlmat.MATERIAL_CLASSES:
                return e
        return self.by_name.get(name)

    def resolve_ref(self, kind, name, index):
        """Resolve one material reference.

        `kind` is 'export' | 'import' | 'none'.  Returns a dict:
        `{status, material, class, source, package, file, rule, parameter,
          texture, texture_package, texture_file, texture_class, texture_export}`
        """
        key = (kind, name, index)
        hit = self._cache.get(key)
        if hit is not None:
            return hit
        if kind == 'none' or name in (None, 'None'):
            out = {'status': 'null-slot', 'material': None, 'class': None,
                   'source': 'none', 'package': None, 'file': None,
                   'rule': None, 'parameter': None, 'texture': None,
                   'texture_package': None, 'texture_file': None,
                   'texture_class': None, 'texture_export': None}
            self._cache[key] = out
            return out
        if kind == 'export':
            e = self._export_at(index, name)
            if e is None:
                out = {'status': 'not-a-material-export', 'material': name,
                       'class': None, 'source': 'package', 'package': None,
                       'file': None, 'rule': None, 'parameter': None,
                       'texture': None, 'texture_package': None,
                       'texture_file': None, 'texture_class': None,
                       'texture_export': None}
                self._cache[key] = out
                return out
            res = rlmat.resolve_material(self.mpkg, e, self.sibling)
            out = self._from_resolution(name, self.mpkg.export_class_name(e),
                                        res, res.texture_pkg or self.mpkg,
                                        'package', e.idx)
        else:
            package = rlpkg.import_package_name(self.pkg, index)
            found = None
            if self.sibling is not None and package:
                found = self.sibling.resolve_object(package, name, 'Material')
            if found is None:
                out = {'status': 'external:not-found', 'material': name,
                       'class': None, 'source': 'sibling', 'package': package,
                       'file': None, 'rule': None, 'parameter': None,
                       'texture': None, 'texture_package': None,
                       'texture_file': None, 'texture_class': None,
                       'texture_export': None}
                self._cache[key] = out
                return out
            res = rlmat.resolve_material(found['pkg'], found['export'],
                                         self.sibling)
            out = self._from_resolution(name, found['pkg'].export_class_name(
                found['export']), res, res.texture_pkg or found['pkg'],
                'sibling', found['export'].idx)
            out['package'] = package
            out['file'] = os.path.basename(found['file'])
        self._cache[key] = out
        return out

    def _from_resolution(self, name, cls, res, tex_pkg, source,
                         material_export=None):
        return {
            'status': res.reason or 'RESOLVED',
            'material': name,
            'class': cls,
            'source': source,
            'package': None,
            'file': os.path.basename(tex_pkg.path),
            'rule': res.rule,
            'parameter': res.parameter,
            'material_export': material_export,
            'texture': res.texture,
            'texture_package': (tex_pkg.export_outer_name(res.texture_export)
                                if res.texture_export is not None else None),
            'texture_file': os.path.basename(tex_pkg.path),
            'texture_class': (tex_pkg.export_class_name(res.texture_export)
                              if res.texture_export is not None else None),
            'texture_export': (res.texture_export.idx
                               if res.texture_export is not None else None),
            '_res': res,
            '_tex_pkg': tex_pkg,
            '_tex_path': tex_pkg.path,
        }

    def resolve_name(self, name):
        """Resolve a material by NAME — the mesh-slot-list join.

        A name is NOT a reference: `BoostPad_Mat` exists as two exports in
        this package (MEASURED), only one of which owns a diffuse.  The
        mesh-slot-list join of A3/A3b therefore counts a name as resolved when
        ANY export of that name resolves, and this method does the same, so
        the two numbers are comparable.  The binding chain of the pack itself
        never uses this path: it resolves the export INDEX.
        """
        exports = self.by_name_all.get(name) or []
        if exports:
            best = None
            for e in exports:
                res = rlmat.resolve_material(self.mpkg, e, self.sibling)
                if res.reason is None:
                    best = (e, res)
                    break
                if best is None:
                    best = (e, res)
            e, res = best
            out = self._from_resolution(name, self.mpkg.export_class_name(e),
                                        res, res.texture_pkg or self.mpkg,
                                        'package', e.idx)
            out['ambiguous_exports'] = len(exports)
            return out
        idx = self.imports_by_name.get(name)
        if idx is not None:
            out = self.resolve_ref('import', name, idx)
            out['ambiguous_exports'] = 1
            return out
        return self.resolve_ref('none', None, 0)


# ------------------------------------------------------------------- textures
def _texture_payload(tex_pkg, tex_export):
    """The ORIGINAL compressed mip-0 payload of a texture, plus its header.

    Returns `(info, payload)` where `info` is `rlmat.texture_info`'s dict and
    `payload` is the raw (still compressed) bytes.
    """
    info = rlmat.texture_info(tex_pkg, tex_export)
    if info.get('error'):
        return info, None
    return info, info.get('payload')


# ------------------------------------------------------------------ the build
def build_pack(pack_path, install_dir=None, index_path=DEFAULT_INDEX,
               want_textures=True, store='compressed', quiet=False):
    """Build the pack for one stadium package.  Returns the manifest dict."""
    pack_path = os.path.abspath(os.path.expanduser(pack_path))
    if not os.path.exists(pack_path):
        raise RipError('package not found: %s' % pack_path)
    if not quiet:
        print('package      : %s' % os.path.basename(pack_path))
    pkg = rlupk.Package(pack_path)
    mpkg = rlmat.open_package(pack_path)

    index = None
    sibling = None
    if install_dir or os.path.exists(index_path or ''):
        index = _load_index(install_dir, index_path, quiet=quiet)
        sibling = rlmat.SiblingTextures(index)

    # ---- meshes ------------------------------------------------------------
    in_pkg_meshes = _collect_in_package_meshes(pkg)
    ext_meshes = _collect_external_meshes(pkg, index) if index is not None else []

    # ---- placements --------------------------------------------------------
    pl = rlplace.placements(pkg)
    instances = pl['instances']
    external_instances = pl['external']
    sec_count = {name: len(geo.get('sections') or [])
                 for name, geo, _idx in in_pkg_meshes}
    ext_sec_count = {name: len(geo.get('sections') or [])
                     for name, geo, _idx, _p, _f, _path in ext_meshes}

    # ---- material resolution ----------------------------------------------
    resolver = MaterialResolver(mpkg, pkg, sibling)

    def ref_of(index_value):
        ref = rlplace.resolve_object(pkg, index_value)
        return ref['kind'], ref['name'], ref['index']

    # the binding chain: instance -> component material array (slot i is
    # section i) -> MIC/Material -> diffuse texture
    bindings = []
    used_materials = {}
    coverage_reasons = {}
    ext_reasons = {}
    total_slots = 0
    resolved_slots = 0
    named_slots = 0
    ext_total = 0
    ext_resolved = 0

    def bind(inst, mesh, sections, scope, reasons):
        """Walk one instance's sections and bind each one to its material."""
        nonlocal total_slots, resolved_slots, named_slots
        nonlocal ext_total, ext_resolved
        overrides = inst.get('material_indices') or []
        for slot in range(sections):
            if scope == 'placement':
                total_slots += 1
            else:
                ext_total += 1
            if slot < len(overrides):
                kind, name, idx = ref_of(overrides[slot])
            else:
                kind, name, idx = 'none', None, 0
            res = resolver.resolve_ref(kind, name, idx)
            status = res['status']
            if status == 'RESOLVED':
                if scope == 'placement':
                    resolved_slots += 1
                else:
                    ext_resolved += 1
            if status == 'RESOLVED' or status.startswith('external:'):
                if scope == 'placement':
                    named_slots += 1
            reasons[status] = reasons.get(status, 0) + 1
            used_materials.setdefault(
                (kind, name, idx), {'ref': res, 'slots': 0})['slots'] += 1
            bindings.append({
                'scope': scope,
                'instance': inst['export'],
                'mesh': mesh,
                'section': slot,
                'material': name,
                'material_kind': kind,
                'status': status,
                'texture': res['texture'],
                'texture_file': res['texture_file'],
            })

    for inst in instances:
        mesh = inst['mesh']
        bind(inst, mesh, sec_count.get(mesh, 0), 'placement', coverage_reasons)
    for inst in external_instances:
        mesh = inst['mesh']
        bind(inst, mesh, ext_sec_count.get(mesh, 0), 'external_placement',
             ext_reasons)

    # the comparison number: the MESH's own material slot list (what A3/A3b
    # measured: 171 slots over the distinct placed meshes)
    used = rlmat.used_materials(mpkg)
    mesh_slot_slots = 0
    mesh_slot_resolved = 0
    mesh_slot_named = 0
    for mesh, labels in sorted(used['mesh_slots'].items()):
        for label in labels:
            mesh_slot_slots += 1
            res = resolver.resolve_name(label)
            if res['status'] == 'RESOLVED':
                mesh_slot_resolved += 1
                mesh_slot_named += 1
            elif res['status'].startswith('external:'):
                mesh_slot_named += 1

    # ---- textures ----------------------------------------------------------
    textures = []
    texture_index = {}
    texture_bytes = 0
    texture_formats = {}
    texture_rgba8 = 0
    if want_textures:
        for (kind, name, idx), info in sorted(
                used_materials.items(),
                key=lambda kv: (kv[1]['ref']['texture'] or '',
                                kv[1]['ref']['texture_file'] or '')):
            res = info['ref']
            if res['status'] != 'RESOLVED' or res['texture'] is None:
                continue
            tpkg = res['_tex_pkg']
            tex_export = res['_res'].texture_export
            tkey = (res['texture_file'], res['texture'])
            if tkey in texture_index:
                continue
            tinfo, payload = _texture_payload(tpkg, tex_export)
            if payload is None:
                continue
            entry = {
                'name': res['texture'],
                'class': res['texture_class'],
                'package': res['texture_package'],
                'file': res['texture_file'],
                'width': int(tinfo['width']),
                'height': int(tinfo['height']),
                'format': tinfo['format'],
                'mip_count': int(tinfo['mip_count'] or 0),
                'srgb': bool(tinfo.get('srgb')),
                'storage': 'compressed',
                'rgba8_bytes': int(tinfo['width']) * int(tinfo['height']) * 4,
                'payload_sha256': '',
                '_payload': payload,
            }
            # the payload is always DECODED once, to prove that the stored
            # bytes really are the diffuse the manifest claims (the pack
            # keeps the compressed form; the loader decodes it again)
            try:
                rgba = rldds.decode(payload, entry['width'], entry['height'],
                                    entry['format'])
            except ValueError as exc:
                entry['decoded'] = False
                entry['decode_error'] = str(exc)
            else:
                if len(rgba) != entry['rgba8_bytes']:
                    entry['decoded'] = False
                    entry['decode_error'] = ('decoder produced %d bytes, want %d'
                                             % (len(rgba), entry['rgba8_bytes']))
                else:
                    entry['decoded'] = True
            if store == 'rgba':
                if not entry['decoded']:
                    raise RipError('cannot decode %s: %s'
                                   % (entry['name'],
                                      entry.get('decode_error', 'unknown')))
                entry['storage'] = 'rgba8'
                entry['_payload'] = rgba
            # the hash is of the bytes that are actually STORED
            entry['payload_sha256'] = hashlib.sha256(entry['_payload']).hexdigest()
            texture_index[tkey] = len(textures)
            textures.append(entry)
            texture_bytes += len(entry['_payload'])
            texture_formats[entry['format']] = \
                texture_formats.get(entry['format'], 0) + 1
            texture_rgba8 += entry['rgba8_bytes']

    # how many of the resolved slots point at a texture whose pixels really
    # decode (the reference resolution and the decoded pixels are two
    # different claims; the pack reports both)
    decoded_keys = set((t['file'], t['name']) for t in textures if t.get('decoded'))
    decoded_slots = sum(1 for b in bindings
                        if b['scope'] == 'placement' and b['status'] == 'RESOLVED'
                        and (b['texture_file'], b['texture']) in decoded_keys)

    # ---- the blob ----------------------------------------------------------
    blob = Blob()
    kind_buffers = {k: bytearray() for k in ARRAY_KINDS}
    mesh_entries = []
    all_meshes = ([(n, g, i, 'package', 'Stadium_Day_P', 'Stadium_Day_P.upk')
                    for n, g, i in in_pkg_meshes] +
                   [(n, g, i, 'sibling', p, f)
                    for n, g, i, p, f, _path in ext_meshes])
    for name, geo, export_index, source, package, file_name in all_meshes:
        positions, normals, tangents, uvs, indices = _mesh_arrays(geo)
        ranges = {}
        for kind, data in (('positions', positions), ('normals', normals),
                           ('tangents', tangents), ('uvs', uvs),
                           ('indices', indices)):
            first = len(kind_buffers[kind]) // ARRAY_STRIDE[kind]
            kind_buffers[kind] += data
            ranges[kind] = {
                'first': first,
                'count': len(data) // ARRAY_STRIDE[kind],
                'stride': ARRAY_STRIDE[kind],
            }
        mesh_entries.append((name, geo, export_index, source, package,
                             file_name, ranges))

    # the matrices of every instance (column-major, 16 doubles)
    all_instances = list(instances) + list(external_instances)
    matrix_meta = []
    for inst in all_instances:
        m = inst.get('matrix') or []
        data = struct.pack('<16d', *[_f(v) for v in m])
        first = len(kind_buffers['matrices']) // ARRAY_STRIDE['matrices']
        kind_buffers['matrices'] += data
        matrix_meta.append({'first': first, 'count': 1,
                             'stride': ARRAY_STRIDE['matrices'],
                             'length': len(data)})

    array_table = {}
    for kind in ARRAY_KINDS:
        data = bytes(kind_buffers[kind])
        off, length = blob.add(kind, data)
        array_table[kind] = {
            'offset': off,
            'length': length,
            'count': length // ARRAY_STRIDE[kind],
            'stride': ARRAY_STRIDE[kind],
            'type': ARRAY_TYPE[kind],
        }

    # absolute byte offsets per mesh
    meshes_json = []
    for (name, geo, export_index, source, package, file_name,
         ranges) in mesh_entries:
        entry = _mesh_entry(name, geo, source, package, file_name, export_index,
                            {}, _lod_of(geo))
        for kind in ('positions', 'normals', 'tangents', 'uvs', 'indices'):
            r = ranges[kind]
            base = array_table[kind]['offset']
            entry['arrays'][kind] = {
                'offset': base + r['first'] * r['stride'],
                'length': r['count'] * r['stride'],
                'count': r['count'],
                'stride': r['stride'],
            }
        meshes_json.append(entry)

    # the external meshes keep their own table
    n_in = len(in_pkg_meshes)
    mesh_table = meshes_json[:n_in]
    external_mesh_table = meshes_json[n_in:]

    # ---- instances ---------------------------------------------------------
    def instance_json(inst, matrix_meta_i, mesh_is_external):
        overrides = inst.get('material_indices') or []
        slots = []
        for i, raw in enumerate(overrides):
            kind, name, idx = ref_of(raw)
            res = resolver.resolve_ref(kind, name, idx)
            slots.append({
                'slot': i,
                'material': name,
                'kind': kind,
                'status': res['status'],
                'texture': res['texture'],
            })
        mm = matrix_meta_i
        return {
            'component_export': inst['export'],
            'mesh': inst['mesh'],
            'mesh_external': bool(mesh_is_external),
            'owner': inst.get('owner'),
            'owner_class': inst.get('owner_class'),
            'source': inst.get('source'),
            'slot': inst.get('slot'),
            'matrix': {
                'offset': (array_table['matrices']['offset'] +
                           mm['first'] * ARRAY_STRIDE['matrices']),
                'length': mm['length'],
                'count': mm['count'],
                'stride': mm['stride'],
                'values': ARRAY_VALUES['matrices'],
                'layout': 'column-major',
            },
            'material_slots': slots,
        }

    instance_table = [instance_json(inst, matrix_meta[i], False)
                      for i, inst in enumerate(instances)]
    external_instance_table = [
        instance_json(inst, matrix_meta[len(instances) + i], True)
        for i, inst in enumerate(external_instances)]

    # ---- materials ---------------------------------------------------------
    material_table = []
    for (kind, name, idx), info in sorted(
            used_materials.items(),
            key=lambda kv: (kv[1]['ref']['material'] or '',
                             kv[1]['ref']['file'] or '')):
        res = info['ref']
        tidx = None
        if res['status'] == 'RESOLVED' and res['texture'] is not None:
            tidx = texture_index.get((res['texture_file'], res['texture']))
        material_table.append({
            'name': res['material'],
            'class': res['class'],
            'source': res['source'],
            'package': res['package'],
            'file': res['file'],
            'status': res['status'],
            'rule': res['rule'],
            'parameter': res['parameter'],
            'export': res.get('material_export'),
            'diffuse_texture': res['texture'],
            'texture_index': tidx,
            'section_slots': info['slots'],
        })

    # ---- textures into the blob -------------------------------------------
    texture_table = []
    for t in textures:
        payload = t.pop('_payload')
        off, length = blob.add('texture', payload)
        t['offset'] = off
        t['length'] = length
        t['bytes'] = length
        texture_table.append(t)

    # ---- siblings ----------------------------------------------------------
    # every package the pack borrowed from: its internal package name, the
    # file basename and the content hash (no absolute path, no timestamp)
    sibling_files = {}

    def note_sibling(file_name, path, package):
        row = sibling_files.setdefault(file_name, {'path': path,
                                                   'packages': set()})
        if package:
            row['packages'].add(package)

    for (kind, name, idx), info in sorted(used_materials.items()):
        res = info['ref']
        if res['status'] != 'RESOLVED' or res['texture'] is None:
            continue
        note_sibling(res['texture_file'], res['_tex_path'],
                     res['texture_package'])
    for name, geo, export_index, package, file_name, path in ext_meshes:
        note_sibling(file_name, path, package)
    siblings = []
    for file_name in sorted(sibling_files):
        row = sibling_files[file_name]
        path = row['path']
        if file_name == os.path.basename(pack_path):
            continue          # the source package is described by `source`
        if not path or not os.path.exists(path):
            continue
        siblings.append({
            'file': file_name,
            'packages': sorted(row['packages']),
            'bytes': os.path.getsize(path),
            'sha256': sha256_file(path),
        })

    # ---- manifest ----------------------------------------------------------
    blob_bytes = blob.finish(len(array_table) + len(texture_table))
    manifest = {
        'format': PACK_FORMAT,
        'format_version': FORMAT_VERSION,
        'generator': GENERATOR,
        'source': _package_identity(pkg, pack_path),
        'counts': {
            'meshes': len(mesh_table),
            'external_meshes': len(external_mesh_table),
            'instances': len(instance_table),
            'external_instances': len(external_instance_table),
            'sections': sum(len(m['sections']) for m in mesh_table) +
                        sum(len(m['sections']) for m in external_mesh_table),
            'materials': len(material_table),
            'textures': len(texture_table),
            'vertices': sum(m['num_verts'] for m in mesh_table) +
                        sum(m['num_verts'] for m in external_mesh_table),
            'triangles': sum(m['num_tris'] for m in mesh_table) +
                         sum(m['num_tris'] for m in external_mesh_table),
            'used_section_slots': total_slots,
        },
        'coverage': {
            'chain': ('instance -> component material array (slot i = section i) '
                      '-> MIC/Material -> diffuse texture'),
            'used_section_slots': total_slots,
            'resolved_diffuse': resolved_slots,
            'resolved_pct': _f(round(100.0 * resolved_slots / total_slots, 4))
                            if total_slots else 0.0,
            'named_texture': named_slots,
            'named_pct': _f(round(100.0 * named_slots / total_slots, 4))
                         if total_slots else 0.0,
            'decoded_diffuse': decoded_slots,
            'decoded_pct': _f(round(100.0 * decoded_slots / total_slots, 4))
                           if total_slots else 0.0,
            'reasons': dict(sorted(coverage_reasons.items())),
            'external_placements': {
                'note': ('the 26 placements whose mesh is an IMPORT (a sibling '
                         'package); they are not part of the 451 placements'),
                'used_section_slots': ext_total,
                'resolved_diffuse': ext_resolved,
                'reasons': dict(sorted(ext_reasons.items())),
            },
            'mesh_slot_list': {
                'note': ('the MESH material slot list (one list per distinct '
                         'placed mesh) — the A3/A3b comparison number, not the '
                         'binding the pack uses'),
                'slots': mesh_slot_slots,
                'resolved_diffuse': mesh_slot_resolved,
                'resolved_pct': _f(round(100.0 * mesh_slot_resolved /
                                         mesh_slot_slots, 4))
                                if mesh_slot_slots else 0.0,
                'named_texture': mesh_slot_named,
            },
        },
        'texture_storage': {
            'mode': store,
            'compressed_bytes': texture_bytes,
            'rgba8_bytes': texture_rgba8,
            'formats': dict(sorted(texture_formats.items())),
        },
        'arrays': array_table,
        'meshes': mesh_table,
        'external_meshes': external_mesh_table,
        'instances': instance_table,
        'external_instances': external_instance_table,
        'materials': material_table,
        'textures': texture_table,
        'section_bindings': bindings,
        'siblings': siblings,
        'blob': {
            'file': BLOB_FILE,
            'bytes': len(blob_bytes),
            'header_bytes': BLOB_HEADER_SIZE,
            'sha256': hashlib.sha256(blob_bytes).hexdigest(),
            'array_count': len(array_table) + len(texture_table),
        },
    }
    return manifest, blob_bytes


def write_pack(out_dir, manifest, blob_bytes):
    """Write `arena.json` + `arena.bin` atomically into `out_dir`."""
    out_dir = _guard_out_dir(out_dir)
    os.makedirs(out_dir, exist_ok=True)
    bin_path = os.path.join(out_dir, BLOB_FILE)
    json_path = os.path.join(out_dir, MANIFEST_FILE)
    tmp = bin_path + '.tmp'
    with open(tmp, 'wb') as fh:
        fh.write(blob_bytes)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, bin_path)
    text = json.dumps(manifest, sort_keys=True, indent=2, ensure_ascii=True) + '\n'
    tmp = json_path + '.tmp'
    with open(tmp, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(text)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, json_path)
    return bin_path, json_path


# --------------------------------------------------------------------- verify
class Checker(object):
    """A named list of checks; every check prints one PASS/FAIL line."""

    def __init__(self):
        self.rows = []
        self.failed = 0

    def check(self, name, ok, detail=''):
        ok = bool(ok)
        if not ok:
            self.failed += 1
        self.rows.append((name, ok, detail))
        print('%s %s: %s' % ('PASS' if ok else 'FAIL', name, detail))
        return ok


def verify_pack(pack_dir):
    """Re-read a pack and check every manifest/blob agreement."""
    pack_dir = os.path.abspath(os.path.expanduser(pack_dir))
    json_path = os.path.join(pack_dir, MANIFEST_FILE)
    bin_path = os.path.join(pack_dir, BLOB_FILE)
    c = Checker()
    if not os.path.exists(json_path):
        raise RipError('no %s in %s' % (MANIFEST_FILE, pack_dir))
    if not os.path.exists(bin_path):
        raise RipError('no %s in %s' % (BLOB_FILE, pack_dir))
    with open(json_path, 'r', encoding='utf-8') as fh:
        m = json.load(fh)
    with open(bin_path, 'rb') as fh:
        blob = fh.read()

    c.check('manifest-format', m.get('format') == PACK_FORMAT
            and m.get('format_version') == FORMAT_VERSION,
            'format=%s version=%s' % (m.get('format'), m.get('format_version')))
    c.check('blob-size', m['blob']['bytes'] == len(blob),
            'manifest=%d file=%d' % (m['blob']['bytes'], len(blob)))
    digest = hashlib.sha256(blob).hexdigest()
    c.check('blob-sha256', digest == m['blob']['sha256'],
            'manifest=%s file=%s' % (m['blob']['sha256'], digest))

    magic, version, hsize, total, acount, flags = struct.unpack_from(
        '<8sIIQII', blob, 0)
    c.check('blob-header', magic == BLOB_MAGIC and version == FORMAT_VERSION
            and hsize == BLOB_HEADER_SIZE and total == len(blob)
            and acount == m['blob']['array_count'],
            'magic=%s version=%d header=%d total=%d arrays=%d'
            % (magic.decode('ascii', 'replace'), version, hsize, total, acount))

    # every declared array lies inside the blob and is 4-byte aligned
    bad = []
    for kind, a in sorted(m['arrays'].items()):
        if a['offset'] % BLOB_ALIGN or a['offset'] < BLOB_HEADER_SIZE \
                or a['offset'] + a['length'] > len(blob) \
                or a['length'] != a['count'] * a['stride']:
            bad.append(kind)
    c.check('array-offsets', not bad,
            '%d arrays, all aligned and in range%s'
            % (len(m['arrays']), '' if not bad else ' (bad: %s)' % ','.join(bad)))

    # per-mesh array lengths against the manifest's own counts
    all_meshes = m['meshes'] + m['external_meshes']
    bad = []
    for mesh in all_meshes:
        n = mesh['num_verts']
        if mesh['arrays']['positions']['count'] != n or \
                mesh['arrays']['normals']['count'] != n or \
                mesh['arrays']['tangents']['count'] != n or \
                mesh['arrays']['uvs']['count'] != n:
            bad.append(mesh['name'] + ':vertex-count')
        if mesh['arrays']['indices']['count'] != mesh['num_indices'] or \
                mesh['num_indices'] != mesh['num_tris'] * 3:
            bad.append(mesh['name'] + ':index-count')
    c.check('mesh-array-lengths', not bad,
            '%d meshes, vertex and index counts agree%s'
            % (len(all_meshes), '' if not bad else ' (bad: %s)'
               % ','.join(bad[:4])))

    # the index values really address the mesh's own vertices
    bad = []
    for mesh in all_meshes:
        a = mesh['arrays']['indices']
        data = blob[a['offset']:a['offset'] + a['length']]
        vals = struct.unpack('<%dH' % (len(data) // 2), data)
        if vals and max(vals) >= mesh['num_verts']:
            bad.append(mesh['name'])
    c.check('index-range', not bad,
            'every index < its mesh vertex count%s'
            % ('' if not bad else ' (bad: %s)' % ','.join(bad[:4])))

    # the section table tiles the LOD0 part of the index buffer
    bad = []
    nsec = 0
    for mesh in all_meshes:
        secs = mesh['sections']
        nsec += len(secs)
        if not secs:
            continue
        fi = 0
        for s in secs:
            if s['first_index'] != fi:
                bad.append(mesh['name'])
                break
            fi += 3 * s['num_triangles']
        else:
            if fi > mesh['num_indices']:
                bad.append(mesh['name'] + ':overrun')
    c.check('section-tiling', not bad,
            '%d sections, first_index chains tile the index buffer%s'
            % (nsec, '' if not bad else ' (bad: %s)' % ','.join(bad[:4])))
    c.check('section-count', nsec == m['counts']['sections'],
            'manifest=%d tables=%d' % (m['counts']['sections'], nsec))

    # textures: length and in-range
    bad = []
    for t in m['textures']:
        a, l = t['offset'], t['length']
        if a % BLOB_ALIGN or a < BLOB_HEADER_SIZE or a + l > len(blob):
            bad.append(t['name'])
            continue
        data = blob[a:a + l]
        if hashlib.sha256(data).hexdigest() != t['payload_sha256']:
            bad.append(t['name'] + ':hash')
            continue
        if t['storage'] == 'compressed':
            need = rldds.expected_size(t['width'], t['height'], t['format'])
            if need is not None and need != l:
                bad.append('%s:len(%d!=%d)' % (t['name'], l, need))
        else:
            if l != t['width'] * t['height'] * 4:
                bad.append('%s:rgba-len' % t['name'])
    c.check('texture-lengths', not bad,
            '%d textures, byte ranges and payload hashes agree%s'
            % (len(m['textures']), '' if not bad else ' (bad: %s)'
               % ','.join(bad[:4])))

    # instances
    c.check('instance-count', len(m['instances']) == m['counts']['instances'],
            'manifest=%d table=%d' % (m['counts']['instances'],
                                      len(m['instances'])))
    bad = []
    for inst in m['instances']:
        mm = inst['matrix']
        if mm['length'] != 128 or mm['offset'] % BLOB_ALIGN or \
                mm['offset'] + mm['length'] > len(blob):
            bad.append(inst['component_export'])
    c.check('instance-matrices', not bad,
            '%d instance matrices are 16 doubles in range%s'
            % (len(m['instances']), '' if not bad else ' (bad: %s)'
               % ','.join(str(b) for b in bad[:4])))

    # every placement owns exactly one matrix, the matrices are contiguous in
    # the blob and each one is a column-major matrix with m[15] == 1
    n_pl = len(m['instances'])
    n_ext = len(m['external_instances'])
    mat = m['arrays']['matrices']
    bad = []
    seen = set()
    for inst in list(m['instances']) + list(m['external_instances']):
        mm = inst['matrix']
        if mm['offset'] in seen:
            bad.append('dup-matrix@%d' % mm['offset'])
        seen.add(mm['offset'])
        if mm['count'] != 1 or mm['stride'] != 128 or mm['length'] != 128:
            bad.append('shape@%d' % mm['offset'])
            continue
        vals = struct.unpack_from('<16d', blob, mm['offset'])
        if vals[15] != 1.0 or not all(v == v for v in vals):
            bad.append('values@%d' % mm['offset'])
    c.check('matrix-array', not bad and len(seen) == n_pl + n_ext and
            mat['count'] == n_pl + n_ext and mat['stride'] == 128,
            '%d placements own %d distinct matrices, the array holds %d '
            'elements, every matrix is column-major with m[15]=1%s'
            % (n_pl + n_ext, len(seen), mat['count'],
               '' if not bad else ' (bad: %s)' % ','.join(bad[:4])))

    # orphans: every instance must name a mesh the pack carries
    known = set(x['name'] for x in m['meshes'])
    known_ext = set(x['name'] for x in m['external_meshes'])
    orphans = []
    for inst in m['instances']:
        if inst['mesh'] not in known:
            orphans.append(inst['mesh'])
    for inst in m['external_instances']:
        if inst['mesh'] not in known_ext:
            orphans.append(inst['mesh'])
    c.check('instance-orphans', not orphans,
            'orphan count=%d (%d in-package + %d external instances)'
            % (len(orphans), len(m['instances']), len(m['external_instances'])))

    # the coverage table
    cov = m['coverage']
    c.check('coverage-total', cov['used_section_slots'] ==
            m['counts']['used_section_slots'],
            'slots=%d' % cov['used_section_slots'])
    c.check('coverage-reasons', sum(cov['reasons'].values()) ==
            cov['used_section_slots'],
            'reasons sum=%d slots=%d' % (sum(cov['reasons'].values()),
                                          cov['used_section_slots']))
    c.check('coverage-resolved', cov['resolved_diffuse'] <= cov['used_section_slots'],
            'resolved=%d of %d (%.2f%%)'
            % (cov['resolved_diffuse'], cov['used_section_slots'],
               cov['resolved_pct']))
    c.check('binding-count', len(m['section_bindings']) ==
            cov['used_section_slots'] +
            cov['external_placements']['used_section_slots'],
            'bindings=%d slots=%d' % (len(m['section_bindings']),
                                      cov['used_section_slots'] +
                                      cov['external_placements']
                                      ['used_section_slots']))

    # the material table points at real textures
    bad = []
    for mat in m['materials']:
        ti = mat['texture_index']
        if ti is not None and not (0 <= ti < len(m['textures'])):
            bad.append(mat['name'])
        if mat['status'] == 'RESOLVED' and ti is None and m['textures']:
            bad.append(mat['name'] + ':unbound')
    c.check('material-texture-links', not bad,
            '%d materials, resolved ones point at a texture%s'
            % (len(m['materials']), '' if not bad else ' (bad: %s)'
               % ','.join(bad[:4])))

    print('')
    print('verify: %d checks, %d PASS, %d FAIL'
          % (len(c.rows), len(c.rows) - c.failed, c.failed))
    return c.failed == 0


# ------------------------------------------------------------------------ CLI
def main(argv=None):
    ap = argparse.ArgumentParser(
        description='Rip a Rocket League stadium into a deterministic arena '
                    'pack (arena.json + arena.bin).')
    ap.add_argument('--pack', metavar='UPK',
                    help='the stadium package to rip')
    ap.add_argument('--out', metavar='DIR',
                    help='the pack output directory (must be under '
                         'arena_assets/ when inside the repository)')
    ap.add_argument('--verify', metavar='DIR',
                    help='verify an existing pack instead of building one')
    ap.add_argument('--install', metavar='DIR',
                    help='the CookedPCConsole directory (enables sibling '
                         'package resolution)')
    ap.add_argument('--index', metavar='JSON', default=DEFAULT_INDEX,
                    help='the sibling package index (default %s)'
                         % DEFAULT_INDEX)
    ap.add_argument('--no-textures', action='store_true',
                    help='skip the texture payloads')
    ap.add_argument('--store', choices=('compressed', 'rgba'), default='compressed',
                    help='texture storage: the original compressed bytes '
                         '(default) or the expanded RGBA8')
    ap.add_argument('--quiet', action='store_true', help='less chatter')
    args = ap.parse_args(argv)

    try:
        if args.verify:
            ok = verify_pack(args.verify)
            return 0 if ok else 1
        if not args.pack or not args.out:
            ap.error('--pack and --out are required (or --verify DIR)')
        manifest, blob_bytes = build_pack(
            args.pack, install_dir=args.install, index_path=args.index,
            want_textures=not args.no_textures, store=args.store,
            quiet=args.quiet)
        bin_path, json_path = write_pack(args.out, manifest, blob_bytes)
        if not args.quiet:
            counts = manifest['counts']
            cov = manifest['coverage']
            print('pack         : %s' % os.path.dirname(bin_path))
            print('meshes       : %d (+%d external)  instances: %d (+%d external)'
                  % (counts['meshes'], counts['external_meshes'],
                     counts['instances'], counts['external_instances']))
            print('sections     : %d   materials: %d   textures: %d'
                  % (counts['sections'], counts['materials'],
                     counts['textures']))
            print('coverage     : %d/%d used section slots resolve to a '
                  'diffuse texture (%.2f%%), %d of them decode to pixels '
                  '(%.2f%%)'
                  % (cov['resolved_diffuse'], cov['used_section_slots'],
                     cov['resolved_pct'], cov['decoded_diffuse'],
                     cov['decoded_pct']))
            print('arena.json   : %d B  sha256 %s'
                  % (os.path.getsize(json_path),
                     hashlib.sha256(open(json_path, 'rb').read()).hexdigest()))
            print('arena.bin    : %d B  sha256 %s'
                  % (os.path.getsize(bin_path), manifest['blob']['sha256']))
        return 0
    except RipError as exc:
        print('rip_arena: error: %s' % exc, file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print('rip_arena: interrupted', file=sys.stderr)
        return 130


if __name__ == '__main__':
    sys.exit(main())
