#!/usr/bin/env python3
"""rlmat.py — diffuse-texture resolver for cooked Rocket League UE3 packages.

PHASE A3 (round 2) of `MASTER_PROMPT_ARENA_1TO1.md` ("materials + textures").
Given one of the user's own cooked packages (e.g. `Stadium_Day_P.upk`), this
module resolves every `Material` / `MaterialInstanceConstant` object to the
`Texture2D` its surface uses, decodes that texture to exactly `w*h*4` RGBA8
bytes (`tools/arena/rldds.py`) and writes a machine-readable handoff for the
packer (A5).

Nothing here ships or copies a game byte: pixel data only ever goes to a
directory outside the repo tree (`--out`, default `/tmp/rl_a3/tex`), and the
module refuses to write inside the checked-out repo.

===============================================================================
MEASURED FORMAT FACTS (licensee-34 cook, `Stadium_Day_P.upk`)
Every fact below was measured on the package, not assumed.  The full evidence
is in `docs/ARENA_RIP_LOG.md`, rounds A1/A2/A3.

1. TAGGED PROPERTIES.  A 4-byte preamble followed by UE3 tags: `FName` int64
   (name), `FName` int64 (type), `int32 Size`, `int32 ArrayIndex`, value.
   `ByteProperty` and `StructProperty` carry one extra 8-byte `FName`
   (enum / struct name) inside the tag.  `BoolProperty` costs ONE value byte
   after the 24-byte tag and leaves `Size == 0`.  The block ends on an 8-byte
   `None` `FName`; that terminator is sometimes the LAST 8 bytes of the
   export, so a walker that insists on a full 24-byte tag before testing the
   name rejects legal exports.  84/84 `Material`, 58/58
   `MaterialInstanceConstant`, 62/62 `Texture2D`, 18/18 `LightMapTexture2D`,
   53/53 `MaterialExpressionTextureSampleParameter2D` and 186/186
   `MaterialExpressionStaticSwitchParameter` walk at preamble 4.

2. `Material.Expressions` is `int32 count` followed by `count` raw `int32`
   object references (no per-element tags).  MEASURED: for all 84 materials
   the set of non-zero references is EXACTLY the set of exports the material
   owns (outer chain) — 0 mismatches.  The array is mostly zeros: the cook
   strips the graph but keeps the array length.

3. THE MATERIAL GRAPH IS STRIPPED AT COOK TIME.  Every `FExpressionInput`
   survives as a `StructProperty` whose value is a NESTED TAGGED BLOCK
   (`Expression` ObjectProperty, `OutputIndex`/`Mask*` IntProperties), but the
   `Expression` reference is `None` in 46 of the 84 `DiffuseColor` inputs,
   the property is absent in 38, and only 3 materials keep a live
   `DiffuseColor.Expression` — and all three lead to a dead end (a
   `MaterialExpressionStaticSwitchParameter` whose `A`/`B` are `None`).
   `MaterialFunction.FunctionExpressions` is 100% null for all 5 functions.
   => There is NO graph path from a material to its texture in this cook.

4. THE ONLY TEXTURE CARRIERS ARE THE PARAMETER EXPRESSIONS:
   * `MaterialExpressionTextureSampleParameter2D` — 53 exports, each with
     `ParameterName` (NameProperty), `Texture` (ObjectProperty), `Material`
     (ObjectProperty back-pointer to the owning material) and `Coordinates`.
   * `MaterialExpressionTextureSampleParameterCube` — 7 exports.
   * `MaterialInstanceConstant.TextureParameterValues` — 25/58 MICs carry the
     array; its value is `int32 count` followed by, per element, a full nested
     TAGGED BLOCK (`ParameterName` NameProperty, `ParameterValue`
     ObjectProperty, `ExpressionGUID` StructProperty) each ending in its own
     8-byte `None`.  32 entries in total; the walker consumes each array
     exactly.
   All 60 sampler exports are owned by an in-package `Material`.

5. MEASURED PARAMETER NAMES (this package).  Texture-sample parameters:
   `ColorLookup` 16, `Diffuse` 12, `WetMask` 5, `Masks` 3, `DiffuseQuarter` 3,
   `GrassMask` 2, `LightMap` 2, `Normal` 2, `AdTexture` 1, `BallTracker` 1,
   `Detail` 1, `ENV` 1, `LensTexture` 1, `NormalTexture` 1, `TextureDiffuse` 1,
   `TextureNormal` 1; cube samples: `CubeMap` 7.  MIC texture overrides:
   `ColorLookup` 9, `Diffuse` 7, `AdTexture` 6, `LightMap` 3, `CubeMap` 2,
   `BallTracker` 1, `ENV` 1, `FieldSkin` 1, `Normal` 1, `NormalTexture` 1.
   The albedo family is therefore MEASURED as
   {Diffuse, DiffuseQuarter, TextureDiffuse, AdTexture, FieldSkin}; the
   non-albedo families (normal / mask / lookup / lightmap / cubemap / env /
   lens / tracker / detail) are excluded by name because the package itself
   stores that name.

6. EXTERNAL TEXTURES.  47 `Texture2D` imports; their `Outer` names the
   package they live in: `Noise` 12, `Gradients` 6, `FieldSkin_FutureGrid` 2,
   `EngineMaterials` 2, `Stadiums_DetailNormals` 2, `Textures` 2, and one each
   in `Detail_CarbonFiber`, `Detail_Sparkle`, `FieldSkin_Park`,
   `GFX_Shared_Logos`, `Vehicle_Parent_Textures`.  16 further `Texture2D`
   imports have their `Outer` on a `Package` EXPORT of this same file
   (`City_Textures`, `FutureTech_Textures`, `Mat`, `Grass_Textures`,
   `LensFlares_Textures`) — UE3 "linked package groups": the object itself is
   still in another file but is grouped here.

7. `Texture2D` mip chain (after the property block): a 16-byte head
   (`0x00010000`, `0`, `0`, `int32 NumMips`) then one record per mip:
     - `0x00010000` inline:     (`flags`, `DataSize`, `DataSize`) + `DataSize` bytes
     - `0x00010003` file-cache: (`flags`, `DataSize`, `CacheSize`, `CacheOffset`,
                                  `0`, `SizeX`, `SizeY`) with the bytes in
                                  `<TextureFileCacheName>.tfc` next to the package
     - `0x00000021` no payload: (`flags`, `0`, `-1`, `-1`, `-1`, `SizeX`, `SizeY`)
   followed by a 60-byte trailer.

8. A `.tfc` cache entry is the SAME chained compressed-chunk container the
   package uses (`tag 0x9E2A83C1`, `block_size` 0x20000, then per-block
   (compressed, uncompressed) sizes and zlib blocks); inflating it yields
   exactly `DataSize` bytes.

9. THE MATERIAL -> TEXTURE LINK IS ONLY HALF PRESENT, AND THE PACKAGE PROVES
   IT.  `texture_referrers()` scans EVERY export's tagged properties (object
   properties, int32 reference arrays and nested tagged blocks) for references
   to an in-package texture: MEASURED, 55 of the 80 in-package textures have
   NO referrer inside the package at all (`Advert_Strip`, `CityLights_01_Pack`,
   `Statue_D`, `HandRail_D`, `Stairs_Pack`, `LombardyPoplar_Branch`,
   `Park_BannerFlag00_RGB`, `ForcefieldHex`, `LightArray_Arrow_D`,
   `StadiumLight_D`, `CountryFlagsCombined`, `MetalBeamGradient_Pack`,
   `Grate_Square_Pack`, `DustParticles`, ...).  Those textures are the ones
   the arena's materials use; the link that named them lived in a SIBLING
   file of the same UE3 package group and was not cooked into this one.
   => The diffuse of most placed materials is NOT recoverable from this
   package; the honest ceiling is what the report states.  A name-stem
   heuristic would bind only 5 of the 38 untextured used materials uniquely,
   so guessing by name is not even a viable fallback (and is not used).

10. THE SECTION -> MATERIAL JOIN IS THE COMPONENT'S MATERIAL ARRAY.  A
    `StaticMeshComponent` carries the list in `OverrideMaterials` (or
    `Materials`), sometimes on its ARCHETYPE (a prefab instance only
    serialises its diffs), so the archetype chain is walked root-first.
    MEASURED: the array length equals the mesh's section count for 112 of the
    119 meshes (A2's section-count histogram {1:92, 2:19, 3:5, 5:1, 12:2} vs.
    the component-side histogram {1:84, 2:19, 3:5, 5:2, 12:2}; the 7 meshes
    without a list and the one over-long array explain the 1/5 difference), so
    slot `i` of the array is the material of section `i`.

===============================================================================
Public API
    open_package(path)                     -> RLMatPackage
    material_chain(pkg, material_export)   -> list of resolved steps
    diffuse_texture(pkg, material_export)  -> (texture_export, provenance) | None
    decode_texture(pkg, texture_export)    -> {width, height, format, rgba, ...}
    texture_for_material(pkg, export)      -> the integration hook (see below)
    resolve_all(pkg)                       -> summary dict
    used_materials(pkg)                    -> the placed-instance material join
    scan_textures(pkg)                     -> every in-package texture
    main()                                 -> CLI summary

Integration hook for the packer: for every (mesh, section) it already knows the
material name; call

    rlmat.texture_for_material(pkg, pkg.export_by_name(material_name))

which returns `{'material','texture','width','height','format','rgba',
'provenance','reason'}` (rgba is `width*height*4` bytes, reason is None on
success).  `pkg.export_by_name` also accepts a `MaterialInstanceConstant` name.

Usage:
    python3 tools/arena/rlmat.py <pkg> [--out /tmp/rl_a3/tex]
"""
import argparse
import json
import os
import struct
import sys
import zlib
from collections import Counter, OrderedDict, defaultdict

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(os.path.dirname(_HERE))
_DECOMP = os.path.join(_REPO, 'tools', 'decomp')
if _DECOMP not in sys.path:
    sys.path.insert(0, _DECOMP)
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import rlupk                                    # noqa: E402  (path set above)
import rldds                                    # noqa: E402
import rlpkg                                    # noqa: E402  (sibling-package index)

TFC_TAG = 0x9E2A83C1
MAX_ARRAY = 1 << 22

# --- class sets -------------------------------------------------------------
TEXTURE_CLASSES = ('Texture2D',)
TEXTURE_EXPORT_CLASSES = ('Texture2D', 'LightMapTexture2D')
MATERIAL_CLASSES = ('Material', 'MaterialInstanceConstant')
SAMPLER_CLASSES = (
    'MaterialExpressionTextureSampleParameter2D',
    'MaterialExpressionTextureSampleParameterCube',
    'MaterialExpressionTextureSample',
)
# The two sampler classes that carry a 2D albedo.  A cube sample is a cubemap
# (reflection / env probe), never an albedo, so it never designates a diffuse.
DIFFUSE_SAMPLER_CLASSES = (
    'MaterialExpressionTextureSampleParameter2D',
    'MaterialExpressionTextureSample',
)

# --- MEASURED parameter families (see module docstring section 5) ------------
# The albedo family, in the order the package's own parameter names are
# consulted.  The first entry that actually carries a Texture2D wins; the order
# is recorded in the provenance so a consumer can re-derive the pick.
DIFFUSE_PARAMS = ('diffuse', 'diffusequarter', 'texturediffuse', 'adtexture',
                  'fieldskin')
# Families that are measurably NOT an albedo.  They are excluded when the
# "only remaining sampled texture" rule is applied, so that e.g. an ENV map or
# a normal map is never mistaken for the base colour.  `masks` is NOT in this
# list: `Building_A_Pack` (the only texture `Building_Mat` samples besides an
# env map) is its base-colour pack, and the rule keeps it.
NON_ALBEDO_PARAMS = (
    'normal', 'normaltexture', 'texturenormal', 'lightmap', 'cubemap', 'env',
    'col lookup', 'colorlookup', 'wetmask', 'grassmask', 'detail',
    'lenstexture', 'balltracker', 'environment', 'envmap',
)

KNOWN_TYPES = frozenset((
    'BoolProperty', 'ByteProperty', 'IntProperty', 'FloatProperty',
    'ObjectProperty', 'NameProperty', 'StrProperty', 'StructProperty',
    'ArrayProperty', 'MapProperty', 'ComponentProperty', 'ClassProperty',
    'DelegateProperty', 'InterfaceProperty', 'MulticastDelegateProperty',
))
_EXTRA_FNAME_TYPES = ('ByteProperty', 'StructProperty')


# ---------------------------------------------------------------- property walk
class Prop(object):
    """One UE3 tagged property: name/type/size/array index and its raw value."""

    __slots__ = ('name', 'type', 'size', 'array_index', 'meta', 'value', 'offset')

    def __init__(self, name, type_, size, array_index, meta, value, offset):
        self.name = name
        self.type = type_
        self.size = size
        self.array_index = array_index
        self.meta = meta              # enum/struct FName, or None
        self.value = value
        self.offset = offset

    def i32(self, default=0):
        if len(self.value) < 4:
            return default
        return struct.unpack_from('<i', self.value, 0)[0]

    def f32(self, default=0.0):
        if len(self.value) < 4:
            return default
        return struct.unpack_from('<f', self.value, 0)[0]

    def __repr__(self):
        return 'Prop(%s:%s size=%d)' % (self.name, self.type, self.size)


def _fname(pkg, data, off):
    """(name index, number) of the FName at `off`, clamped to the name table."""
    idx, num = struct.unpack_from('<ii', data, off)
    if not (0 <= idx < len(pkg.names)):
        raise ValueError('FName index %d out of range' % idx)
    return idx, num


def walk_tags(pkg, data, start=0, limit=None):
    """Walk UE3 tags from `start`; return ([Prop], offset after the terminator).

    Returns (None, None) if this offset is not a legal tag list. The `None`
    terminator is tested on its 8-byte name FIRST (its type/size words are the
    first bytes of the following data), and an export whose block ends exactly at
    the end of the data is legal.
    """
    props = []
    pos = start
    end_of_data = len(data) if limit is None else min(len(data), limit)
    while pos + 8 <= end_of_data:
        try:
            idx, _num = _fname(pkg, data, pos)
        except (ValueError, struct.error):
            return None, None
        if pkg.names[idx] == 'None':
            return props, pos + 8
        if pos + 24 > end_of_data:
            return None, None
        try:
            tidx, _tnum = _fname(pkg, data, pos + 8)
        except (ValueError, struct.error):
            return None, None
        typ = pkg.names[tidx]
        if typ not in KNOWN_TYPES:
            return None, None
        size, array_index = struct.unpack_from('<ii', data, pos + 16)
        if size < 0 or size > MAX_ARRAY or array_index < 0:
            return None, None
        body = pos + 24
        meta = None
        if typ in _EXTRA_FNAME_TYPES:
            if body + 8 > end_of_data:
                return None, None
            try:
                meta = _fname(pkg, data, body)
            except (ValueError, struct.error):
                return None, None
            body += 8
        if body + size > end_of_data:
            return None, None
        props.append(Prop(pkg.names[idx], typ, size, array_index, meta,
                          data[body:body + size], pos))
        pos = body + size + (1 if typ == 'BoolProperty' else 0)
    return None, None


# ---------------------------------------------------------------- package model
class ParseError(Exception):
    """An export did not yield a legal property block or payload."""


class RLMatPackage(object):
    """A cooked package plus the caches this resolver needs."""

    def __init__(self, path, upk):
        self.path = path
        self.upk = upk
        self.names = upk.names
        self._props = {}         # export idx -> (dict, end offset)
        self._props_fail = set()
        self._tfc = {}           # cache file name -> open handle
        self._by_name = {}
        self._owned = {}         # export idx -> [exports it owns]
        for e in upk.exports:
            self._by_name.setdefault(self.export_name(e), e)
        for e in upk.exports:
            if e.package_index > 0:
                self._owned.setdefault(e.package_index - 1, []).append(e)

    # -- objects ------------------------------------------------------------
    def export_name(self, e):
        return self.upk.export_name(e)

    def export_class_name(self, e):
        return self.upk.export_class_name(e)

    def export_outer_name(self, e):
        return self.upk.export_outer_name(e)

    def export_by_name(self, name):
        return self._by_name.get(name)

    def exports_of_class(self, cls):
        return [e for e in self.upk.exports if self.export_class_name(e) == cls]

    def owned_exports(self, owner):
        """Exports whose `Outer` is `owner` (UE3 `package_index`)."""
        return list(self._owned.get(owner.idx, ()))

    def ref(self, value):
        """Resolve a UE3 object reference (int32 package index) to an export."""
        if not value or len(value) < 4:
            return None
        i = struct.unpack_from('<i', value, 0)[0]
        if 0 < i <= len(self.upk.exports):
            return self.upk.exports[i - 1]
        return None

    def ref_class(self, value):
        e = self.ref(value)
        return self.export_class_name(e) if e is not None else None

    def ref_name(self, value):
        e = self.ref(value)
        if e is not None:
            return self.export_name(e)
        if value and len(value) >= 4:
            i = struct.unpack_from('<i', value, 0)[0]
            if i < 0 and -i - 1 < len(self.upk.imports):
                return 'import:' + self.upk.import_name(-i - 1)
        return None

    def import_outer_package(self, imp):
        """The package an IMPORT lives in, measured from the import table.

        UE3 `FObjectImport::Outer` is an FPackageIndex.  A negative value
        points at another import (a `Package` import) whose NAME is the owning
        package; a positive value points at a `Package` EXPORT of this very
        file — UE3's "linked package group", where the object still lives in
        another file but is grouped here.
        """
        pi = imp.package_index
        if pi < 0:
            j = -pi - 1
            if 0 <= j < len(self.upk.imports):
                return self.upk.import_name(j)
            return None
        if pi > 0 and pi <= len(self.upk.exports):
            return self.export_name(self.upk.exports[pi - 1])
        return None

    def ref_info(self, value):
        """Describe a 4-byte FPackageIndex value.

        Returns `{'kind','name','class','package','export','import'}` with
        `kind` in {none, export, import}.  `package` is the external package an
        import lives in (None for exports).
        """
        if not value or len(value) < 4:
            return {'kind': 'none', 'name': None, 'class': None,
                    'package': None, 'export': None, 'import': None}
        i = struct.unpack_from('<i', value, 0)[0]
        if i > 0 and i <= len(self.upk.exports):
            e = self.upk.exports[i - 1]
            return {'kind': 'export', 'name': self.export_name(e),
                    'class': self.export_class_name(e), 'package': None,
                    'export': e, 'import': None}
        if i < 0 and -i - 1 < len(self.upk.imports):
            imp = self.upk.imports[-i - 1]
            return {'kind': 'import', 'name': self.upk.import_name(-i - 1),
                    'class': self.upk.fname(imp.class_name),
                    'package': self.import_outer_package(imp),
                    'export': None, 'import': imp}
        return {'kind': 'none', 'name': None, 'class': None,
                'package': None, 'export': None, 'import': None}

    # -- properties ---------------------------------------------------------
    def properties(self, e):
        """(dict name -> Prop, offset after the block) for one export, cached.

        Raises `ParseError` when no preamble in 0..16 yields a legal tag list.
        """
        hit = self._props.get(e.idx)
        if hit is not None:
            return hit
        if e.idx in self._props_fail:
            raise ParseError('export %d %s: no legal property block'
                             % (e.idx, self.export_name(e)))
        data = self.upk.data(e)
        for begin in (0, 4, 8, 12, 16):
            if begin + 8 > len(data):
                break
            tags, end = walk_tags(self.upk, data, begin)
            if tags is None:
                continue
            props = {}
            for t in tags:
                props.setdefault(t.name, t)
            out = (props, end)
            self._props[e.idx] = out
            return out
        self._props_fail.add(e.idx)
        raise ParseError('export %d %s: no legal property block'
                         % (e.idx, self.export_name(e)))

    def try_properties(self, e):
        """`properties` without the exception: (dict, end) or ({}, 0)."""
        try:
            return self.properties(e)
        except ParseError:
            return {}, 0

    def data(self, e):
        return self.upk.data(e)


# ------------------------------------------------------------------ package I/O
def open_package(path):
    """Open a cooked `.upk` and return the resolver's package handle."""
    path = os.path.abspath(os.path.expanduser(path))
    upk = rlupk.Package(path)
    return RLMatPackage(path, upk)


class SiblingTextures(object):
    """Resolve an IMPORTED texture to the sibling package that holds it.

    MEASURED (see `tools/arena/rlpkg.py`): a cooked UE3 file that belongs to a
    linked-package group keeps a COPY of every linked package, so a texture
    that the file only imports really exists in another file of the same
    install.  The resolver reads the HEAD of the candidate files only (never a
    41 GB body) and returns the provider file plus the export to decode.

    The index is a JSON file built by `tools/arena/rlpkg.py --index`; it never
    contains a package byte, only offsets and counts.
    """

    def __init__(self, index):
        self.index = index
        self.resolver = rlpkg.SiblingResolver(index)
        self._pkgs = {}
        self.stats = Counter()
        self.provenance = []

    @classmethod
    def from_install(cls, directory, index_path=None, quiet=True):
        """Load the index (or build it) for an install directory."""
        if index_path and os.path.exists(index_path):
            index = rlpkg.load_index(index_path)
        else:
            index = rlpkg.index_install(directory, progress=not quiet, quiet=quiet)
            if index_path:
                rlpkg.save_index(index, index_path)
        return cls(index)

    def open(self, fname):
        pkg = self._pkgs.get(fname)
        if pkg is None:
            pkg = open_package(os.path.join(self.index['root'], fname))
            self._pkgs[fname] = pkg
        return pkg

    def resolve(self, ref, cls=TEXTURE_CLASSES):
        """Resolve an import reference to (file, RLMatPackage, export).

        `ref` is the ref-info dict of an import: `{'kind':'import','name',
        'class','package'}`.  Returns None when the object is not a texture or
        when no provider file holds it.
        """
        name = ref.get('name')
        package = ref.get('package')
        rcls = ref.get('class')
        if not name or not package or rcls not in cls:
            return None
        hit = self.resolver.locate(package, name, cls=rcls)
        if not hit:
            self.stats['not_found'] += 1
            return None
        fname, _head_export = hit
        try:
            spkg = self.open(fname)
        except (rlupk.MissingKey, OSError) as exc:
            self.stats['open_failed'] += 1
            self.provenance.append('package=%s file=%s export=%s OPEN-FAILED %s'
                                   % (package, fname, name, exc))
            return None
        export = self.resolver.export_index(fname, name, cls=rcls)
        if export is None:
            self.stats['export_missing'] += 1
            return None
        self.stats['resolved'] += 1
        self.stats['files_opened'] += 1
        return {'package': package, 'file': os.path.join(self.index['root'], fname),
                'file_name': fname, 'pkg': spkg, 'export': export,
                'name': name, 'class': rcls}

    def resolve_object(self, package, name, cls=None):
        """Resolve ANY imported object (not only textures) in a sibling file."""
        if not name or not package:
            return None
        hit = self.resolver.locate(package, name, cls=cls)
        if not hit:
            self.stats['object_not_found'] += 1
            return None
        fname = hit[0]
        try:
            spkg = self.open(fname)
        except (rlupk.MissingKey, OSError):
            self.stats['open_failed'] += 1
            return None
        export = self.resolver.export_index(fname, name, cls=cls)
        if export is None:
            self.stats['export_missing'] += 1
            return None
        self.stats['objects_resolved'] += 1
        return {'package': package, 'file': os.path.join(self.index['root'], fname),
                'file_name': fname, 'pkg': spkg, 'export': export,
                'name': name, 'class': cls}


def _tfc_handle(pkg, name):
    if name in pkg._tfc:
        return pkg._tfc[name]
    path = os.path.join(os.path.dirname(pkg.path), name + '.tfc')
    if not os.path.exists(path):
        raise ParseError('texture file cache %s.tfc not found next to the package' % name)
    fh = open(path, 'rb')
    pkg._tfc[name] = fh
    return fh


def inflate_chunk_container(blob):
    """Inflate one chained compressed-chunk container (package or .tfc entry)."""
    out = bytearray()
    pos = 0
    while pos + 16 <= len(blob):
        tag, block_size = struct.unpack_from('<Ii', blob, pos)
        total_c, total_u = struct.unpack_from('<ii', blob, pos + 8)
        if tag != TFC_TAG or block_size <= 0:
            raise ParseError('bad chunk header at %d' % pos)
        nblocks = (total_u + block_size - 1) // block_size
        q = pos + 16 + 8 * nblocks
        for k in range(nblocks):
            bc, _bu = struct.unpack_from('<ii', blob, pos + 16 + 8 * k)
            if bc < 0 or q + bc > len(blob):
                raise ParseError('chunk block out of range at %d' % pos)
            out += zlib.decompress(blob[q:q + bc])
            q += bc
        pos = q
    if not out:
        raise ParseError('empty chunk container')
    return bytes(out)


# --------------------------------------------------------------------- textures
def texture_info(pkg, texture_export):
    """Parse the `Texture2D` header: dims, format, mip count, cache, mip0 record."""
    try:
        props, end = pkg.properties(texture_export)
    except ParseError as exc:
        return {'error': str(exc)}
    for need in ('SizeX', 'SizeY', 'Format'):
        if need not in props:
            return {'error': 'Texture2D %s has no %s property'
                             % (pkg.export_name(texture_export), need)}
    data = pkg.data(texture_export)
    # MEASURED: the cooked `Format` is a ByteProperty whose 8-byte value is an
    # FName of the EPixelFormat enumerator (`PF_DXT1`, ...); the tag's extra FName
    # is the enum TYPE name (`EPixelFormat`), not the value. A 1-byte value would
    # be a raw enumerator index, which this cook never uses.
    fmt = None
    fval = props['Format'].value
    if len(fval) >= 8:
        idx = struct.unpack_from('<i', fval, 0)[0]
        if 0 <= idx < len(pkg.names):
            fmt = pkg.names[idx]
    elif len(fval) == 1:
        fmt = 'PF_INDEX_%d' % fval[0]
    info = {
        'width': props['SizeX'].i32(),
        'height': props['SizeY'].i32(),
        'format': fmt,
        'srgb': None,
        'end': end,
        'data_len': len(data),
        'mip_count': None,
        'record_type': None,
        'payload': None,
        'payload_source': None,
        'cache_name': None,
        'error': None,
    }
    if 'SRGB' in props:
        info['srgb'] = True          # BoolProperty stores only presence + 1 value byte
    cn = props.get('TextureFileCacheName')
    if cn is not None and len(cn.value) >= 4:
        idx = struct.unpack_from('<i', cn.value, 0)[0]
        if 0 <= idx < len(pkg.names):
            info['cache_name'] = pkg.names[idx]
    if end + 16 > len(data):
        info['error'] = 'truncated mip head (end=%d len=%d)' % (end, len(data))
        return info
    mip_count, = struct.unpack_from('<i', data, end + 12)
    info['mip_count'] = mip_count
    if not (0 < mip_count <= 32):
        info['error'] = 'implausible mip count %d' % mip_count
        return info
    rec, ds, cs, off, _zero, rx, ry = struct.unpack_from('<7i', data, end + 16)
    info['record_type'] = rec
    info['record'] = {'data_size': ds, 'cache_size': cs, 'cache_offset': off,
                      'size_x': rx, 'size_y': ry}
    if rec == 0x00010000:
        start = end + 28
        if start + ds > len(data):
            info['error'] = 'inline mip0 overruns the export'
            return info
        info['payload'] = data[start:start + ds]
        info['payload_source'] = 'inline@+%d' % (start - end)
    elif rec == 0x00010003:
        if not info['cache_name']:
            info['error'] = 'file-cache mip0 without TextureFileCacheName'
            return info
        try:
            fh = _tfc_handle(pkg, info['cache_name'])
            fh.seek(off)
            blob = fh.read(cs)
            if len(blob) != cs:
                info['error'] = 'short read from %s.tfc at %d (%d/%d)' % (
                    info['cache_name'], off, len(blob), cs)
                return info
            payload = inflate_chunk_container(blob)
        except (ParseError, zlib.error, OSError) as exc:
            info['error'] = 'file-cache inflate failed: %s' % (exc,)
            return info
        info['payload'] = payload
        info['payload_source'] = 'tfc:%s.tfc@%d+%d' % (info['cache_name'], off, cs)
    else:
        info['error'] = 'no mip0 payload (record type 0x%08x)' % (rec & 0xFFFFFFFF)
        return info
    return info


def decode_texture(pkg, texture_export):
    """Decode a texture's mip 0 to RGBA8.

    Returns `{'width','height','format','rgba','mip_count','provenance'}` where
    `rgba` is exactly width*height*4 bytes. Raises `ParseError` when the texture
    has no usable mip 0 or an unsupported format.
    """
    info = texture_info(pkg, texture_export)
    name = pkg.export_name(texture_export)
    prov = ['export=%d %s' % (texture_export.idx, name)]
    if info.get('error'):
        raise ParseError('%s: %s' % (name, info['error']))
    w, h, fmt = info['width'], info['height'], info['format']
    prov.append('format=%s' % fmt)
    prov.append('mip0=%dx%d mips=%d' % (w, h, info['mip_count']))
    prov.append('payload=' + info['payload_source'])
    if info['srgb'] is not None:
        prov.append('srgb=%s (flag reported, no gamma transform applied)'
                    % ('1' if info['srgb'] else '0'))
    payload = info['payload']
    need = None
    if fmt in rldds.SUPPORTED_FORMATS:
        need = rldds.expected_size(w, h, fmt)
    if need is not None and len(payload) != need:
        prov.append('payload_len=%d expected=%d' % (len(payload), need))
    try:
        rgba = rldds.decode(payload, w, h, fmt)
    except ValueError as exc:
        raise ParseError('%s: %s' % (name, exc))
    if len(rgba) != w * h * 4:
        raise ParseError('%s: decoder produced %d bytes, want %d'
                         % (name, len(rgba), w * h * 4))
    return {'width': w, 'height': h, 'format': fmt, 'rgba': rgba,
            'mip_count': info['mip_count'], 'provenance': prov,
            'texture': name, 'export': texture_export}


# ------------------------------------------------------------------ expressions
def _name_value(pkg, props, key):
    """The FName stored in a NameProperty/ByteProperty, or None."""
    pr = props.get(key)
    if pr is None or len(pr.value) < 4:
        return None
    i = struct.unpack_from('<i', pr.value, 0)[0]
    if 0 <= i < len(pkg.names):
        return pkg.names[i]
    return None


def material_samplers(pkg, material_export):
    """The texture-sampler expressions that belong to a `Material`.

    A sampler belongs to the material when its own `Material` property points
    back at it (MEASURED: present on 53/53 + 7/7 sampler exports) — the
    `Outer` chain is used as the fallback when that property is absent.
    """
    out = []
    owned = set(e.idx for e in pkg.owned_exports(material_export))
    for e in pkg.upk.exports:
        cls = pkg.export_class_name(e)
        if cls not in SAMPLER_CLASSES:
            continue
        props, _end = pkg.try_properties(e)
        mat = props.get('Material')
        if mat is not None and len(mat.value) >= 4:
            mi = struct.unpack_from('<i', mat.value, 0)[0]
            if mi > 0 and mi <= len(pkg.upk.exports):
                if pkg.upk.exports[mi - 1].idx != material_export.idx:
                    continue
            elif mi != 0:
                continue
        elif e.idx not in owned:
            continue
        tex = props.get('Texture')
        out.append({
            'export': e,
            'class': cls,
            'parameter': _name_value(pkg, props, 'ParameterName'),
            'texture_ref': tex.value if tex is not None else None,
            'texture': pkg.ref(tex.value) if tex is not None else None,
            'texture_class': pkg.ref_class(tex.value) if tex is not None else None,
            'texture_info': pkg.ref_info(tex.value) if tex is not None
            else {'kind': 'none', 'name': None, 'class': None, 'package': None},
        })
    return out


def mic_texture_overrides(pkg, instance_export):
    """[(parameter name, ref-info dict)] from a MIC's `TextureParameterValues`."""
    props, _end = pkg.try_properties(instance_export)
    arr = props.get('TextureParameterValues')
    if arr is None or len(arr.value) < 4:
        return []
    count, = struct.unpack_from('<i', arr.value, 0)
    if count < 0 or count > 4096:
        return []
    out = []
    off = 4
    for _i in range(count):
        fields, off = walk_tags(pkg.upk, arr.value, off)
        if fields is None:
            break
        fmap = {}
        for f in fields:
            fmap.setdefault(f.name, f)
        pname = _name_value(pkg, fmap, 'ParameterName')
        tref = fmap.get('ParameterValue')
        out.append((pname, pkg.ref_info(tref.value) if tref is not None
                    else {'kind': 'none', 'name': None, 'class': None,
                          'package': None}))
    return out


def parent_of(pkg, instance_export):
    """(Prop|None, ref-info dict) of a MIC's `Parent`."""
    props, _end = pkg.try_properties(instance_export)
    par = props.get('Parent')
    if par is None:
        return None, {'kind': 'none', 'name': None, 'class': None, 'package': None}
    return par, pkg.ref_info(par.value)


# ------------------------------------------------------------------ designation
def _is_diffuse_param(name):
    if not name:
        return False
    n = name.lower()
    return any(n == d or n.startswith(d) for d in DIFFUSE_PARAMS)


def _is_non_albedo_param(name):
    if not name:
        return False
    n = name.lower()
    return any(n == d or n.startswith(d) for d in NON_ALBEDO_PARAMS)


class Resolution(object):
    """The outcome of resolving one material object.

    `reason` is None on success and otherwise one of the closed set
    `untextured-by-design`, `external:<package>`, `param-mask`,
    `unsupported-format:<detail>`, `parse-failure:<detail>`.
    """

    __slots__ = ('material', 'material_class', 'export_index', 'parent_chain',
                 'texture', 'texture_class', 'texture_export', 'width', 'height',
                 'format', 'rgba', 'rule', 'reason', 'detail', 'parameter',
                 'provenance', 'samplers', 'overrides', 'texture_pkg',
                 'texture_source', 'external_package', 'external_file')

    def __init__(self):
        self.material = None
        self.material_class = None
        self.export_index = None
        self.parent_chain = []
        self.texture = None
        self.texture_class = None
        self.texture_export = None
        self.width = 0
        self.height = 0
        self.format = None
        self.rgba = None
        self.rule = None
        self.reason = None
        self.detail = ''
        self.parameter = None
        self.provenance = []
        self.samplers = []
        self.overrides = []
        # A3b: the texture may live in a SIBLING package (an import).
        self.texture_pkg = None        # the RLMatPackage that holds texture_export
        self.texture_source = 'in-package'
        self.external_package = None   # the internal package name of the import
        self.external_file = None      # the file that actually holds the texture

    def as_row(self):
        return {'material': self.material, 'class': self.material_class,
                'texture': self.texture, 'reason': self.reason,
                'rule': self.rule, 'parameter': self.parameter}


def _external_reason(ref):
    pkg_name = ref.get('package') or ref.get('name') or '?'
    return 'external:%s' % pkg_name


def _external_or_mask(res, ref, rule, param, what):
    """Classify a single override/sampler reference that is not a Texture2D."""
    if ref['kind'] == 'import':
        res.rule = rule + '-external'
        res.reason = _external_reason(ref)
    elif ref['kind'] == 'none':
        res.rule = rule + '-null'
        res.reason = 'param-mask'
    else:
        res.rule = rule + '-runtime'
        res.reason = 'param-mask'
    res.parameter = param
    res.detail = what
    res.provenance.append('rule=%s %s' % (res.rule, param))
    return res


def _try_sibling(res, ref, rule, param, sibling, what):
    """Try to resolve an IMPORTED texture in a sibling package (A3b).

    MEASURED: the linked-package group duplicates every linked package into
    each of its files, so an imported texture really exists in another file of
    the same install.  When the sibling package is found, the resolution
    becomes a normal success and the provenance names the package, the file
    and the export.  Returns True when the resolution was made.
    """
    if sibling is None or ref.get('kind') != 'import':
        return False
    if (ref.get('class') or '') not in TEXTURE_CLASSES:
        return False
    hit = sibling.resolve(ref)
    if hit is None:
        return False
    res.rule = rule
    res.parameter = param
    res.texture = hit['name']
    res.texture_class = hit['class']
    res.texture_export = hit['export']
    res.texture_pkg = hit['pkg']
    res.texture_source = 'sibling'
    res.external_package = hit['package']
    res.external_file = hit['file']
    res.reason = None
    res.detail = ('%s; the texture lives in the sibling package %s (%s)'
                  % (what, hit['package'], os.path.basename(hit['file'])))
    res.provenance.append('package=%s file=%s export=%s'
                          % (hit['package'], hit['file'], hit['name']))
    res.provenance.append('rule=%s %s' % (rule, param))
    return True


def _try_sibling_parent(res, pref, sibling, what):
    """Resolve an EXTERNAL parent material through its sibling package (A3b).

    The parent is an import, so the material object itself lives in the linked
    package; the sibling file holds the same object (same package name, same
    object name — the FName is the identity in a UE3 package group).
    """
    if sibling is None or pref.get('kind') != 'import':
        return False
    if (pref.get('class') or '') != 'Material':
        return False
    hit = sibling.resolve_object(pref.get('package'), pref.get('name'), 'Material')
    if hit is None:
        return False
    try:
        sub = resolve_material(hit['pkg'], hit['export'])
    except ParseError:
        return False
    if sub.reason is not None or sub.texture_export is None:
        return False
    res.rule = 'parent-external-sibling'
    res.parameter = sub.parameter
    res.texture = sub.texture
    res.texture_class = sub.texture_class
    res.texture_export = sub.texture_export
    res.texture_pkg = sub.texture_pkg or hit['pkg']
    res.texture_source = 'sibling'
    res.external_package = pref.get('package')
    res.external_file = hit['file']
    res.reason = None
    res.detail = ('%s; the material was resolved in the sibling package %s (%s)'
                  % (what, pref.get('package'), os.path.basename(hit['file'])))
    res.provenance.append('package=%s file=%s export=%s'
                          % (pref.get('package'), hit['file'], pref.get('name')))
    res.provenance.append('rule=parent-external-sibling')
    return True


def resolve_material(pkg, material_export, sibling=None):
    """Resolve one `Material` / `MaterialInstanceConstant` object.

    The chain is the measured one (module docstring sections 3 and 4):

      1. walk the MIC `Parent` chain (nearest instance wins) and collect every
         `TextureParameterValues` override;
      2. collect the owning in-package `Material`'s texture samplers;
      3. designate ONE sampler as the diffuse, using the first rule that
         applies (each rule is recorded in `rule` + `provenance`):
           `mic-override-diffuse`  a diffuse-family parameter of the MIC chain
                                   carries a Texture2D
           `param-diffuse`         a diffuse-family parameter of the material
                                   carries a Texture2D
           `single-sampler`        the material has exactly one Texture2D
                                   sampler
           `single-texture`        several samplers, one distinct Texture2D
           `non-albedo-excluded`   several samplers, but only one distinct
                                   Texture2D once the measured non-albedo
                                   parameter families are excluded
         and a MIC override of the designated parameter always wins over the
         sampler's own default texture.
    """
    res = Resolution()
    res.material = pkg.export_name(material_export)
    res.material_class = pkg.export_class_name(material_export)
    res.export_index = material_export.idx
    res.provenance.append('start=%s(%s) export=%d'
                          % (res.material, res.material_class, material_export.idx))

    overrides = OrderedDict()      # param -> (ref-info, source instance name)
    chain = []
    external_parent = None
    cur = material_export
    depth = 0
    while depth < 8:
        cls = pkg.export_class_name(cur)
        if cls != 'MaterialInstanceConstant':
            break
        chain.append(pkg.export_name(cur))
        for pname, ref in mic_texture_overrides(pkg, cur):
            if pname is None:
                continue
            if pname not in overrides:
                overrides[pname] = (ref, pkg.export_name(cur))
        _par, pref = parent_of(pkg, cur)
        res.provenance.append('parent=%s' % (pref.get('name') or 'None'))
        if pref['kind'] == 'import':
            external_parent = pref
            break
        if pref['kind'] != 'export':
            break
        cur = pref['export']
        depth += 1
    res.parent_chain = list(chain)
    res.overrides = [(k, v[0].get('name'), v[0].get('class'), v[1])
                     for k, v in overrides.items()]

    own_material = cur if pkg.export_class_name(cur) == 'Material' else None
    if own_material is not None:
        chain.append(pkg.export_name(own_material))
        res.parent_chain = list(chain)
        res.samplers = material_samplers(pkg, own_material)
    else:
        res.provenance.append('no in-package parent Material')

    # ---- rule 1: the MIC chain overrides a diffuse-family parameter -------
    for key in DIFFUSE_PARAMS:
        hits = [(pn, ref, src) for pn, (ref, src) in overrides.items()
                if pn and pn.lower() == key]
        if not hits:
            continue
        tex2d = [h for h in hits
                 if h[1]['kind'] == 'export' and h[1]['class'] in TEXTURE_CLASSES]
        if len(tex2d) == 1:
            pn, ref, src = tex2d[0]
            res.rule = 'mic-override-diffuse'
            res.parameter = pn
            res.detail = ('MIC %s overrides the diffuse parameter %s with %s'
                          % (src, pn, ref['name']))
            res.texture_export = ref['export']
            res.texture = ref['name']
            res.texture_class = ref['class']
            res.provenance.append('rule=mic-override-diffuse %s<-%s' % (pn, src))
            return res
        if len(hits) == 1:
            pn, ref, src = hits[0]
            what = ('MIC %s overrides %s with %s (%s)'
                    % (src, pn, ref.get('name'), ref.get('class')))
            if _try_sibling(res, ref, 'mic-override-diffuse', pn, sibling, what):
                return res
            return _external_or_mask(res, ref, 'mic-override-diffuse', pn, what)
        # several overrides of the same family: they must agree
        names = {h[1].get('name') for h in hits}
        if len(names) == 1 and hits[0][1]['kind'] == 'export' \
                and hits[0][1]['class'] in TEXTURE_CLASSES:
            pn, ref, src = hits[0]
            res.rule = 'mic-override-diffuse'
            res.parameter = pn
            res.detail = 'the MIC chain overrides %s with %s' % (pn, ref['name'])
            res.texture_export = ref['export']
            res.texture = ref['name']
            res.texture_class = ref['class']
            res.provenance.append('rule=mic-override-diffuse %s' % pn)
            return res

    # ---- rule 2: the material's own diffuse-family sampler ---------------
    if own_material is not None:
        by_name = {}
        for s in res.samplers:
            if s['class'] not in DIFFUSE_SAMPLER_CLASSES:
                continue
            by_name.setdefault(s['parameter'], []).append(s)
        family_null = None       # a diffuse-family sampler with no default
        family_import = None     # a diffuse-family sampler that is an import
        for key in DIFFUSE_PARAMS:
            hits = []
            for pn, lst in by_name.items():
                if pn and pn.lower() == key:
                    hits += lst
            if not hits:
                continue
            tex2d = [s for s in hits
                     if s['texture_info']['kind'] == 'export'
                     and s['texture_info']['class'] in TEXTURE_CLASSES]
            if not tex2d:
                # the family member exists but carries no in-package texture
                if family_null is None:
                    family_null = hits[0]
                if family_import is None:
                    for s in hits:
                        if s['texture_info']['kind'] == 'import':
                            family_import = s
                            break
                continue
            s = tex2d[0]
            res.rule = 'param-diffuse'
            res.parameter = s['parameter']
            res.detail = ('material %s samples the diffuse parameter %s -> %s'
                          % (pkg.export_name(own_material), s['parameter'],
                             s['texture_info']['name']))
            res.texture_export = s['texture']
            res.texture = s['texture_info']['name']
            res.texture_class = s['texture_info']['class']
            res.provenance.append('rule=param-diffuse %s' % s['parameter'])
            if len(tex2d) > 1:
                res.provenance.append('note=%d diffuse-family samplers, first in '
                                      'export order wins' % len(tex2d))
            return res
        # the family exists but every member has no static texture
        if family_import is not None:
            s = family_import
            what = ('the diffuse parameter %s samples the imported texture '
                    '%s, which lives in package %s'
                    % (s['parameter'], s['texture_info']['name'],
                       s['texture_info'].get('package')))
            if _try_sibling(res, s['texture_info'], 'sampler-external',
                            s['parameter'], sibling, what):
                return res
            res.rule = 'sampler-external'
            res.parameter = s['parameter']
            res.reason = _external_reason(s['texture_info'])
            res.detail = what
            res.provenance.append('rule=sampler-external %s' % s['parameter'])
            return res
        if family_null is not None:
            res.rule = 'param-mask'
            res.parameter = family_null['parameter']
            res.reason = 'param-mask'
            res.detail = ('the diffuse parameter %s has no default texture in the '
                          'package (virtual parameter)' % family_null['parameter'])
            res.provenance.append('rule=param-mask %s' % family_null['parameter'])
            return res

    # ---- rule 2b: the MIC chain overrides exactly one texture -------------
    ov_tex = [(pn, ref, src) for pn, (ref, src) in overrides.items()
              if ref['kind'] == 'export' and ref['class'] in TEXTURE_CLASSES]
    if len(ov_tex) == 1:
        pn, ref, src = ov_tex[0]
        res.rule = 'mic-single-override'
        res.parameter = pn
        res.detail = ('the only texture the MIC chain overrides is %s (%s) from %s'
                      % (pn, ref['name'], src))
        res.texture_export = ref['export']
        res.texture = ref['name']
        res.texture_class = ref['class']
        res.provenance.append('rule=mic-single-override %s<-%s' % (pn, src))
        return res

    # ---- rule 3/4/5: structural picks ------------------------------------
    if own_material is not None:
        twod = [s for s in res.samplers
                if s['class'] in DIFFUSE_SAMPLER_CLASSES
                and s['texture_info']['kind'] == 'export'
                and s['texture_info']['class'] in TEXTURE_CLASSES]
        if twod:
            if len(twod) == 1:
                s = twod[0]
                res.rule = 'single-sampler'
                res.detail = ('the only Texture2D sampler of %s is %s (parameter %s)'
                              % (pkg.export_name(own_material),
                                 s['texture_info']['name'], s['parameter']))
            else:
                distinct = OrderedDict()
                for s in twod:
                    distinct.setdefault(s['texture_info']['name'], []).append(s)
                if len(distinct) == 1:
                    s = list(distinct.values())[0][0]
                    res.rule = 'single-texture'
                    res.detail = ('all Texture2D samplers of %s point at %s'
                                  % (pkg.export_name(own_material),
                                     s['texture_info']['name']))
                else:
                    keep = OrderedDict()
                    for s in twod:
                        if _is_non_albedo_param(s['parameter']):
                            continue
                        keep.setdefault(s['texture_info']['name'], []).append(s)
                    if len(keep) == 1:
                        s = list(keep.values())[0][0]
                        res.rule = 'non-albedo-excluded'
                        res.detail = ('of the %d textures sampled by %s only %s '
                                      'is not in a measured non-albedo parameter '
                                      'family (the others are %s)'
                                      % (len(distinct), pkg.export_name(own_material),
                                         s['texture_info']['name'],
                                         ', '.join(sorted(
                                             {x['parameter'] for lst in distinct.values()
                                              for x in lst
                                              if _is_non_albedo_param(x['parameter'])}))))
                    else:
                        res.rule = 'ambiguous'
                        res.reason = 'parse-failure:ambiguous-diffuse'
                        res.detail = ('material %s samples %d distinct textures (%s) '
                                      'and no measured rule designates the diffuse'
                                      % (pkg.export_name(own_material), len(distinct),
                                         ', '.join(sorted(distinct))))
                        res.provenance.append('rule=ambiguous')
                        return res
            res.parameter = s['parameter']
            res.texture_export = s['texture']
            res.texture = s['texture_info']['name']
            res.texture_class = s['texture_info']['class']
            res.provenance.append('rule=%s %s' % (res.rule, s['parameter']))
            return res

    # ---- the material samples no in-package Texture2D --------------------
    if own_material is not None:
        imp = [s for s in res.samplers
               if s['texture_info']['kind'] == 'import']
        if imp:
            # A3b: an imported texture may live in a sibling package.
            for s in imp:
                ti = s['texture_info']
                what = ('sampler %s references the imported texture %s, which '
                        'lives in package %s'
                        % (s['parameter'], ti['name'], ti.get('package')))
                if _try_sibling(res, ti, 'sampler-external', s['parameter'],
                                sibling, what):
                    return res
            s = imp[0]
            res.rule = 'sampler-external'
            res.parameter = s['parameter']
            res.reason = _external_reason(s['texture_info'])
            res.detail = ('sampler %s references the imported texture %s, which '
                          'lives in package %s'
                          % (s['parameter'], s['texture_info']['name'],
                             s['texture_info'].get('package')))
            res.provenance.append('rule=sampler-external %s' % s['parameter'])
            return res
        nulls = [s for s in res.samplers
                 if s['texture_info']['kind'] == 'none'
                 and s['class'] in DIFFUSE_SAMPLER_CLASSES]
        if nulls:
            res.rule = 'param-mask'
            res.parameter = nulls[0]['parameter']
            res.reason = 'param-mask'
            res.detail = ('sampler %s has no default texture (virtual parameter)'
                          % nulls[0]['parameter'])
            res.provenance.append('rule=param-mask %s' % nulls[0]['parameter'])
            return res
        rts = [s for s in res.samplers
               if s['texture_info']['kind'] == 'export'
               and s['texture_info']['class'] not in TEXTURE_CLASSES]
        if rts:
            s = rts[0]
            res.rule = 'param-mask'
            res.parameter = s['parameter']
            res.reason = 'param-mask'
            res.detail = ('sampler %s references %s (%s): a runtime texture, not a '
                          'static diffuse' % (s['parameter'], s['texture_info']['name'],
                                               s['texture_info']['class']))
            res.provenance.append('rule=param-mask %s' % s['parameter'])
            return res
        if external_parent is not None:
            what = ('the parent material %s lives in another package'
                    % external_parent.get('name'))
            if _try_sibling_parent(res, external_parent, sibling, what):
                return res
            res.rule = 'parent-external'
            res.reason = _external_reason(external_parent)
            res.detail = what
            res.provenance.append('rule=parent-external')
            return res
        res.rule = 'no-sampler'
        res.reason = 'untextured-by-design'
        res.detail = ('material %s owns %d expression(s) and none of them is a '
                      'texture sampler: the cook stores no texture for it'
                      % (pkg.export_name(own_material),
                         len(pkg.owned_exports(own_material))))
        res.provenance.append('rule=no-sampler')
        return res

    # no in-package parent material at all
    if external_parent is not None:
        what = ('the parent material %s lives in another package'
                % external_parent.get('name'))
        if _try_sibling_parent(res, external_parent, sibling, what):
            return res
        res.rule = 'parent-external'
        res.reason = _external_reason(external_parent)
        res.detail = what
        res.provenance.append('rule=parent-external')
        return res
    res.rule = 'no-material'
    res.reason = 'untextured-by-design'
    res.detail = 'no in-package parent material and no texture override'
    res.provenance.append('rule=no-material')
    return res


# --------------------------------------------------------------------- chains
def material_chain(pkg, material_export, sibling=None):
    """Resolve the material chain; return the list of steps (each a dict).

    A step is `{'step','detail','export','name','class'}`.  The chain is the
    measured path: MIC parent hops, MIC texture overrides, the material's
    sampler set, and the designation of one sampler as the diffuse.
    """
    res = resolve_material(pkg, material_export, sibling)
    steps = [{'step': 'start', 'detail': '', 'export': material_export,
              'name': res.material, 'class': res.material_class}]
    for pn, ref, src in res.overrides:
        steps.append({'step': 'texture-override',
                      'detail': '%s = %s (%s) from %s' % (pn, ref.get('name'),
                                                          ref.get('class'), src),
                      'export': ref.get('export'), 'name': ref.get('name'),
                      'class': ref.get('class')})
    for s in res.samplers:
        steps.append({'step': 'sampler',
                      'detail': '%s param=%s text=%s(%s)' % (
                          s['class'], s['parameter'],
                          s['texture_info'].get('name') or 'None',
                          s['texture_info'].get('class')),
                      'export': s['export'], 'name': pkg.export_name(s['export']),
                      'class': s['class']})
    steps.append({'step': 'designate', 'detail': res.rule or 'none',
                  'export': res.texture_export, 'name': res.parameter,
                  'class': res.rule})
    steps.append({'step': 'diffuse' if res.reason is None else 'reject',
                  'detail': res.detail, 'export': res.texture_export,
                  'name': res.texture, 'class': res.texture_class})
    return steps


def diffuse_texture(pkg, material_export, sibling=None):
    """(texture_export, provenance words) for a material/instance, or None."""
    res = resolve_material(pkg, material_export, sibling)
    words = list(res.provenance)
    if res.reason is None and res.texture_export is not None:
        words.append('texture=%s' % res.texture)
        return res.texture_export, words
    words.append('unresolved:%s' % (res.reason or 'no-diffuse'))
    words.append(res.detail)
    return None, words


# ------------------------------------------------------------------- aggregation
def texture_for_material(pkg, material_export, sibling=None):
    """INTEGRATION HOOK: (material) -> decoded diffuse texture or a reason.

    Returns a dict with `material`, `texture`, `width`, `height`, `format`,
    `rgba` (exactly width*height*4 bytes), `provenance` and `reason` (None when
    the texture decoded). Never raises for an unresolvable material.
    """
    out = {'material': pkg.export_name(material_export),
           'material_class': pkg.export_class_name(material_export),
           'texture': None, 'width': 0, 'height': 0, 'format': None,
           'rgba': None, 'provenance': [], 'reason': None}
    res = resolve_material(pkg, material_export, sibling)
    words = list(res.provenance)
    if res.reason is None and res.texture_export is not None:
        words.append('texture=%s' % res.texture)
        tex = res.texture_export
        tex_pkg = res.texture_pkg or pkg
    else:
        words.append('unresolved:%s' % (res.reason or 'no-diffuse'))
        words.append(res.detail)
        out['reason'] = words[-2] if len(words) >= 2 else 'no-diffuse'
        out['provenance'] = words
        return out
    out['texture'] = tex_pkg.export_name(tex)
    try:
        dec = decode_texture(tex_pkg, tex)
    except ParseError as exc:
        out['reason'] = 'unsupported-format: %s' % (exc,)
        out['provenance'] = words
        return out
    out['width'] = dec['width']
    out['height'] = dec['height']
    out['format'] = dec['format']
    out['rgba'] = dec['rgba']
    out['provenance'] = words + dec['provenance']
    return out


def scan_textures(pkg):
    """Every in-package texture export: header, format and decode status.

    Covers all `Texture2D` and `LightMapTexture2D` exports, not only the ones
    a material reaches, so the format histogram is over the whole package.
    """
    rows = []
    for e in pkg.upk.exports:
        cls = pkg.export_class_name(e)
        if cls not in TEXTURE_EXPORT_CLASSES:
            continue
        row = {'name': pkg.export_name(e), 'class': cls, 'export': e.idx,
               'width': 0, 'height': 0, 'format': None, 'mip_count': None,
               'record_type': None, 'payload_source': None,
               'decoded': False, 'bytes': 0, 'reason': None}
        info = texture_info(pkg, e)
        if info.get('error'):
            row['reason'] = info['error']
            # keep the header facts even when the payload is missing
            for key in ('width', 'height', 'format', 'mip_count',
                        'record_type', 'payload_source'):
                if key in info:
                    row[key] = info[key]
            rows.append(row)
            continue
        row.update({'width': info['width'], 'height': info['height'],
                    'format': info['format'], 'mip_count': info['mip_count'],
                    'record_type': info['record_type'],
                    'payload_source': info['payload_source']})
        try:
            dec = decode_texture(pkg, e)
        except ParseError as exc:
            row['reason'] = str(exc)
            rows.append(row)
            continue
        row['decoded'] = True
        row['bytes'] = len(dec['rgba'])
        row['_rgba'] = dec['rgba']
        rows.append(row)
    return rows


def texture_referrers(pkg):
    """Which in-package textures are referenced by which export.

    Scans EVERY export's tagged properties for ObjectProperty values that
    point at an in-package texture export, descending into nested tagged
    blocks (FExpressionInput, `MaterialInstanceConstant.TextureParameterValues`).
    This is the evidence for the round-2 root cause: the cooked package keeps
    a texture reference only where a texture PARAMETER survives, so most
    in-package textures end up with no referrer at all inside the package.

    Returns {'refs': {texture export index: [(owner class, owner name), ...]},
             'orphans': [texture name, ...]}
    """
    tex_ids = set()
    tex_names = {}
    for e in pkg.upk.exports:
        if pkg.export_class_name(e) in TEXTURE_EXPORT_CLASSES:
            # the FPackageIndex of an export is 1-based over the export table
            tex_ids.add(e.idx + 1)
            tex_names[e.idx + 1] = pkg.export_name(e)
    refs = defaultdict(list)

    def note(owner, value):
        if not value or len(value) < 4:
            return
        i = struct.unpack_from('<i', value, 0)[0]
        if i in tex_ids:
            refs[i].append(owner)

    def scan_block(owner, blob, depth=0):
        """Scan a nested tagged block for object references."""
        if depth > 3:
            return
        try:
            fields, _e = walk_tags(pkg.upk, blob, 0)
        except ParseError:
            return
        if not fields:
            return
        for f in fields:
            if f.type == 'ObjectProperty':
                note(owner, f.value)
            elif f.type == 'ArrayProperty':
                scan_array(owner, f.value, depth + 1)
            elif f.type == 'StructProperty':
                scan_block(owner, f.value, depth + 1)

    def scan_array(owner, blob, depth=0):
        """An array property: either int32 object references or tagged blocks."""
        if len(blob) < 4:
            return
        count, = struct.unpack_from('<i', blob, 0)
        if count < 0 or count > 4096:
            return
        if 4 + 4 * count == len(blob):
            # an array of int32 object references (`Material.Expressions`)
            for i in range(count):
                note(owner, blob[4 + 4 * i:8 + 4 * i])
            return
        # an array of tagged blocks (`MaterialInstanceConstant.TextureParameterValues`)
        off = 4
        for i in range(count):
            try:
                fields, off = walk_tags(pkg.upk, blob, off)
            except ParseError:
                fields = None
            if fields is None:
                if i == 0 and 4 + 4 * count <= len(blob):
                    for j in range(count):
                        note(owner, blob[4 + 4 * j:8 + 4 * j])
                return
            for f in fields:
                if f.type == 'ObjectProperty':
                    note(owner, f.value)
                elif f.type == 'ArrayProperty':
                    scan_array(owner, f.value, depth + 1)
                elif f.type == 'StructProperty':
                    scan_block(owner, f.value, depth + 1)

    for e in pkg.upk.exports:
        owner = (pkg.export_class_name(e), pkg.export_name(e))
        props, _end = pkg.try_properties(e)
        for _k, prop in props.items():
            if prop.type == 'ObjectProperty':
                note(owner, prop.value)
            elif prop.type == 'ArrayProperty':
                scan_array(owner, prop.value)
            elif prop.type == 'StructProperty':
                scan_block(owner, prop.value)
    orphans = sorted(tex_names[i] for i in tex_ids if i not in refs)
    return {'refs': dict(refs), 'orphans': orphans}


def used_materials(pkg):
    """The placed-instance material join.

    A `StaticMeshComponent` carries the material list of its mesh; in a cooked
    UE3 package the list is the property `OverrideMaterials` (or `Materials`)
    and it may live on the component's ARCHETYPE instead of on the component
    itself (a prefab instance only serialises what differs from its template),
    so the archetype chain is walked and the first chain member that defines
    the key wins.

    Returns a dict:
        'slots'      [(component name, slot index, ref-info dict), ...]
        'names'      Counter of material object names over all slots
        'slot_names' {slot index: Counter of names}
        'components' number of components that carried a list
        'mesh_slots' {mesh name: [material name per section slot]}
        'mesh_count' {mesh name: number of components using it}
    """
    slots = []
    names = Counter()
    slot_names = {}
    components = 0
    mesh_slots = {}
    mesh_count = Counter()
    for e in pkg.upk.exports:
        if pkg.export_class_name(e) != 'StaticMeshComponent':
            continue
        chain = []
        seen = set()
        cur = e
        depth = 0
        while cur is not None and cur.idx not in seen and depth < 8:
            seen.add(cur.idx)
            props, _end = pkg.try_properties(cur)
            chain.append((cur, props))
            nxt = getattr(cur, 'archetype', 0)
            cur = pkg.upk.exports[nxt - 1] if 0 < nxt <= len(pkg.upk.exports) else None
            depth += 1
        merged = {}
        for _cur, props in reversed(chain):
            merged.update(props)
        mesh = merged.get('StaticMesh')
        mref = pkg.ref_info(mesh.value) if mesh is not None else None
        mname = mref['name'] if mref and mref['kind'] == 'export' else None
        hit = None
        for cur, props in chain:
            for key in ('OverrideMaterials', 'Materials'):
                if key in props:
                    hit = (cur, key, props[key])
                    break
            if hit is not None:
                break
        if hit is None:
            continue
        _cur, key, prop = hit
        if len(prop.value) < 4:
            continue
        count, = struct.unpack_from('<i', prop.value, 0)
        if count < 0 or 4 + 4 * count > len(prop.value):
            continue
        components += 1
        slot_names_list = []
        for i in range(count):
            raw = prop.value[4 + 4 * i:8 + 4 * i]
            ref = pkg.ref_info(raw)
            slots.append((pkg.export_name(e), i, ref))
            label = ref.get('name') or ('<none>' if ref['kind'] == 'none'
                                         else '<bad>')
            names[label] += 1
            slot_names.setdefault(i, Counter())[label] += 1
            slot_names_list.append(label)
        if mname is not None:
            mesh_slots.setdefault(mname, slot_names_list)
            mesh_count[mname] += 1
    return {'slots': slots, 'names': names, 'slot_names': slot_names,
            'components': components, 'mesh_slots': mesh_slots,
            'mesh_count': mesh_count}


def resolve_all(pkg, sibling=None):
    """Resolve every material/instance; return the summary dict (counts + rows).

    `sibling` is an optional `SiblingTextures`: when it is given, a texture
    that the package only IMPORTS is looked up in the sibling package that
    really holds it (A3b) and decoded from there.
    """
    mats = [e for e in pkg.upk.exports
            if pkg.export_class_name(e) in MATERIAL_CLASSES]
    rows = []
    reasons = Counter()
    rules = Counter()
    for e in mats:
        res = resolve_material(pkg, e, sibling)
        if res.reason is None and res.texture_export is not None:
            tex_pkg = res.texture_pkg or pkg
            try:
                dec = decode_texture(tex_pkg, res.texture_export)
            except ParseError as exc:
                res.reason = 'unsupported-format: %s' % (exc,)
                res.detail = str(exc)
            else:
                res.width = dec['width']
                res.height = dec['height']
                res.format = dec['format']
                res.rgba = dec['rgba']
                res.provenance += dec['provenance']
        rows.append(res)
        reasons[res.reason or 'RESOLVED'] += 1
        rules[res.rule or 'none'] += 1

    # the used-material join
    used = used_materials(pkg)
    by_name = {}
    for r in rows:
        by_name.setdefault(r.material, []).append(r)
    used_objects = {}
    for label in used['names']:
        used_objects[label] = by_name.get(label, [])

    # A3b: a used material that this package only IMPORTS lives in a sibling
    # package of the same group; resolve it there (the import names the
    # package, so the link is a reference, never a name guess).
    imported_used = {}
    if sibling is not None:
        import_refs = {}
        for _c, _i, r in used['slots']:
            if r.get('kind') == 'import' and r.get('name'):
                import_refs.setdefault(r['name'], r)
        for label, rlist in list(used_objects.items()):
            if rlist or label not in import_refs:
                continue
            ref = import_refs[label]
            hit = sibling.resolve_object(ref.get('package'), label, 'Material')
            if hit is None:
                continue
            try:
                sub = resolve_material(hit['pkg'], hit['export'], sibling)
            except ParseError:
                continue
            if sub.reason is not None or sub.texture_export is None:
                continue
            row = Resolution()
            row.material = label
            row.material_class = 'Material'
            row.export_index = hit['export'].idx
            row.rule = 'imported-material-sibling'
            row.parameter = sub.parameter
            row.texture = sub.texture
            row.texture_class = sub.texture_class
            row.texture_export = sub.texture_export
            row.texture_pkg = sub.texture_pkg or hit['pkg']
            row.texture_source = 'sibling'
            row.external_package = ref.get('package')
            row.external_file = hit['file']
            row.detail = ('the used material %s is an import; it was resolved in '
                          'the sibling package %s (%s)'
                          % (label, ref.get('package'),
                             os.path.basename(hit['file'])))
            row.provenance = ['package=%s file=%s export=%s'
                              % (ref.get('package'), hit['file'], label)]
            try:
                dec = decode_texture(row.texture_pkg, row.texture_export)
            except ParseError as exc:
                row.reason = 'unsupported-format: %s' % (exc,)
                row.detail = str(exc)
            else:
                row.width = dec['width']
                row.height = dec['height']
                row.format = dec['format']
                row.rgba = dec['rgba']
                row.provenance += dec['provenance']
            used_objects[label] = [row]
            imported_used[label] = row
    used_resolved = 0
    used_classified = 0
    used_named_texture = 0
    for _label, rlist in used_objects.items():
        if not rlist:
            continue                       # an import / an object not in this package
        if any(r.reason is None for r in rlist):
            used_resolved += 1
            used_named_texture += 1
        elif any(r.reason and r.reason.startswith('external:') for r in rlist):
            used_named_texture += 1
        used_classified += 1
    used_total = len([l for l in used_objects if used_objects[l]])

    # the section-level join: a mesh's material array IS its section material
    # list (MEASURED: the array length equals the mesh's section count, see the
    # A3 round-2 log), so every slot is one section's material.
    section_slots = 0
    section_resolved = 0
    section_named = 0
    section_classified = 0
    for mname, slotlist in used['mesh_slots'].items():
        for label in slotlist:
            section_slots += 1
            rlist = used_objects.get(label, [])
            if not rlist:
                continue
            if any(r.reason is None for r in rlist):
                section_resolved += 1
                section_named += 1
            elif any(r.reason and r.reason.startswith('external:') for r in rlist):
                section_named += 1
            section_classified += 1

    tex_rows = scan_textures(pkg)
    fmt_hist = Counter()
    fmt_all = Counter()
    tex_undecoded = []
    for r in tex_rows:
        fmt_all[r['format'] or 'unknown'] += 1
        if r['decoded']:
            fmt_hist[r['format']] += 1
        else:
            tex_undecoded.append(r)
    used_tex = Counter()
    for r in rows:
        if r.reason is None and r.texture:
            used_tex[r.texture] += 1

    # external material->texture links, by the package that holds the texture.
    # Every link is counted ONCE: a `Material` contributes its own samplers, a
    # `MaterialInstanceConstant` contributes its own `TextureParameterValues`.
    ext_links = Counter()
    ext_detail = defaultdict(list)
    for e in mats:
        if pkg.export_class_name(e) == 'Material':
            for s in material_samplers(pkg, e):
                ti = s['texture_info']
                if ti['kind'] == 'import':
                    k = ti.get('package') or '?'
                    ext_links[k] += 1
                    ext_detail[k].append('%s : sampler %s -> %s'
                                         % (pkg.export_name(e), s['parameter'],
                                            ti['name']))
        else:
            for pn, ref in mic_texture_overrides(pkg, e):
                if ref['kind'] == 'import':
                    k = ref.get('package') or '?'
                    ext_links[k] += 1
                    ext_detail[k].append('%s : MIC override %s -> %s'
                                         % (pkg.export_name(e), pn, ref['name']))

    # the referrer census: which in-package textures any export points at
    referrers = texture_referrers(pkg)

    resolved = sum(1 for r in rows if r.reason is None)

    # A3b: the textures that were resolved in a SIBLING package.
    sibling_textures = []
    sibling_seen = set()
    sibling_pkgs = Counter()
    sibling_files = Counter()
    for r in list(rows) + list(imported_used.values()):
        if r.texture_source != 'sibling' or r.rgba is None:
            continue
        key = (r.external_package, r.texture, r.texture_export.idx)
        if key in sibling_seen:
            continue
        sibling_seen.add(key)
        sibling_pkgs[r.external_package] += 1
        sibling_files[os.path.basename(r.external_file or '')] += 1
        sibling_textures.append({
            'texture': r.texture, 'class': r.texture_class,
            'export': r.texture_export.idx, 'package': r.external_package,
            'file': r.external_file, 'width': r.width, 'height': r.height,
            'format': r.format, 'bytes': len(r.rgba), 'rgba': r.rgba,
            'material': r.material, 'parameter': r.parameter,
        })
    sibling_resolved = (sum(1 for r in rows
                             if r.texture_source == 'sibling' and r.reason is None)
                        + sum(1 for r in imported_used.values()
                              if r.reason is None))
    return {
        'package': pkg.path,
        'materials': sum(1 for e in mats
                         if pkg.export_class_name(e) == 'Material'),
        'instances': sum(1 for e in mats
                         if pkg.export_class_name(e) == 'MaterialInstanceConstant'),
        'total': len(rows),
        'resolved': resolved,
        'resolved_pct': (100.0 * resolved / len(rows)) if rows else 0.0,
        'rows': rows,
        'rules': dict(rules),
        'reasons': dict(reasons),
        'textures': tex_rows,
        'textures_total': len(tex_rows),
        'format_histogram': dict(fmt_hist),
        'format_histogram_all': dict(fmt_all),
        'textures_undecoded': tex_undecoded,
        'used': used,
        'used_materials': used_objects,
        'used_total': used_total,
        'used_resolved': used_resolved,
        'used_classified': used_classified,
        'used_named_texture': used_named_texture,
        'used_resolved_pct': (100.0 * used_resolved / used_total) if used_total else 0.0,
        'used_classified_pct': (100.0 * used_classified / used_total) if used_total else 0.0,
        'used_named_pct': (100.0 * used_named_texture / used_total) if used_total else 0.0,
        'section_slots': section_slots,
        'section_resolved': section_resolved,
        'section_named': section_named,
        'section_classified': section_classified,
        'section_resolved_pct': (100.0 * section_resolved / section_slots) if section_slots else 0.0,
        'section_named_pct': (100.0 * section_named / section_slots) if section_slots else 0.0,
        'section_classified_pct': (100.0 * section_classified / section_slots) if section_slots else 0.0,
        'used_texture_histogram': dict(used_tex),
        'external_links': dict(ext_links),
        'external_link_detail': {k: v for k, v in ext_detail.items()},
        'texture_referrers': referrers,
        'orphan_textures': referrers['orphans'],
        # --- A3b: sibling-package resolutions ---------------------------
        'sibling_resolved': sibling_resolved,
        'sibling_textures': sibling_textures,
        'sibling_packages': dict(sibling_pkgs),
        'sibling_files': dict(sibling_files),
        'sibling_stats': (dict(sibling.stats) if sibling is not None else {}),
        'imported_used_materials': sorted(imported_used),
    }


# ---------------------------------------------------------------------------- CLI
def _guard_out_dir(path):
    """Refuse to write decoded pixels anywhere inside the repo tree (Rule 1)."""
    real = os.path.realpath(os.path.abspath(os.path.expanduser(path)))
    repo = os.path.realpath(_REPO)
    if real == repo or real.startswith(repo + os.sep):
        raise SystemExit('refusing to write pixel data inside the repo tree: %s' % real)
    return real


def _safe_name(name):
    return ''.join(c if (c.isalnum() or c in '._-') else '_' for c in name)


def _tex_filename(row):
    """File name of a decoded texture dump.

    The export index is part of the name because the package holds 18
    `LightMapTexture2D` exports with only 6 distinct names (6x
    `DirectionalMaxComponent0`, 6x `NormalizedAverageColor0`, ...), so the
    name alone is not unique.
    """
    return '%s_e%d_%dx%d_%s.rgba' % (_safe_name(row['name']), row['export'],
                                    row['width'], row['height'], row['format'])


def _sib_tex_filename(row):
    """File name of a decoded texture that came from a SIBLING package."""
    return '%s_%s_e%d_%dx%d_%s.rgba' % (_safe_name(row['package']),
                                        _safe_name(row['texture']), row['export'],
                                        row['width'], row['height'], row['format'])


def _write_outputs(summary, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    manifest = []
    wrote = 0
    for r in summary['textures']:
        if r['decoded']:
            fname = _tex_filename(r)
            with open(os.path.join(out_dir, fname), 'wb') as fh:
                fh.write(r['_rgba'])
            wrote += 1
            manifest.append({'texture': r['name'], 'class': r['class'],
                             'export': r['export'], 'file': fname,
                             'width': r['width'], 'height': r['height'],
                             'format': r['format'], 'bytes': r['bytes']})
        else:
            manifest.append({'texture': r['name'], 'class': r['class'],
                             'export': r['export'], 'reason': r['reason']})
    for r in summary.get('sibling_textures', []):
        fname = _sib_tex_filename(r)
        with open(os.path.join(out_dir, fname), 'wb') as fh:
            fh.write(r['rgba'])
        wrote += 1
        manifest.append({'texture': r['texture'], 'class': r['class'],
                         'export': r['export'], 'file': fname,
                         'package': r['package'], 'source_file': r['file'],
                         'width': r['width'], 'height': r['height'],
                         'format': r['format'], 'bytes': r['bytes'],
                         'material': r['material'], 'parameter': r['parameter']})
    with open(os.path.join(out_dir, 'manifest.json'), 'w') as fh:
        json.dump({'package': summary['package'], 'textures': manifest}, fh, indent=1)
    return wrote


def _handoff(summary, out_dir, path):
    """The machine-readable handoff for the packer (A5)."""
    tex_path = {}
    for r in summary['textures']:
        if r['decoded']:
            tex_path[(r['name'], r['export'])] = os.path.join(
                out_dir, _tex_filename(r))
    for r in summary.get('sibling_textures', []):
        tex_path[(r['package'], r['texture'], r['export'])] = os.path.join(
            out_dir, _sib_tex_filename(r))
    mats = []
    for r in summary['rows']:
        if r.texture_source == 'sibling':
            key = (r.external_package, r.texture, r.texture_export.idx)
        else:
            key = (r.texture, r.texture_export.idx) if r.texture_export is not None else None
        mats.append({
            'name': r.material,
            'class': r.material_class,
            'export_index': r.export_index,
            'parent_chain': r.parent_chain,
            'diffuse_texture': r.texture,
            'diffuse_texture_class': r.texture_class,
            'diffuse_parameter': r.parameter,
            'width': r.width,
            'height': r.height,
            'format': r.format,
            'rgba_path': (tex_path.get(key) if key is not None else None),
            'reason': r.reason,
            'rule': r.rule,
            'detail': r.detail,
            'provenance': r.provenance,
            'texture_source': r.texture_source,
            'external_package': r.external_package,
            'external_file': r.external_file,
            'samplers': [{'class': s['class'], 'parameter': s['parameter'],
                          'texture': s['texture_info'].get('name'),
                          'texture_class': s['texture_info'].get('class'),
                          'texture_kind': s['texture_info'].get('kind'),
                          'texture_package': s['texture_info'].get('package')}
                         for s in r.samplers],
            'overrides': [{'parameter': o[0], 'texture': o[1], 'class': o[2],
                           'source': o[3]} for o in r.overrides],
        })
    used = {}
    for label, rlist in summary['used_materials'].items():
        used[label] = {
            'class': rlist[0].material_class if rlist else None,
            'exports': [r.export_index for r in rlist],
            'in_package': bool(rlist),
            'diffuse_texture': next((r.texture for r in rlist
                                     if r.reason is None), None),
            'reason': next((r.reason for r in rlist
                            if r.reason is not None), None),
            'rule': next((r.rule for r in rlist
                          if r.reason is not None), None),
            'slots': sum(1 for _c, _i, ref in summary['used']['slots']
                         if ref.get('name') == label),
        }
    doc = {
        'schema': 'rl-arena-materials/1',
        'generated_by': 'tools/arena/rlmat.py',
        'package': summary['package'],
        'counts': {
            'material_objects': summary['total'],
            'resolved': summary['resolved'],
            'resolved_pct': round(summary['resolved_pct'], 2),
            'used_materials': summary['used_total'],
            'used_resolved': summary['used_resolved'],
            'used_resolved_pct': round(summary['used_resolved_pct'], 2),
            'used_named_texture': summary['used_named_texture'],
            'used_named_pct': round(summary['used_named_pct'], 2),
            'used_classified': summary['used_classified'],
            'used_classified_pct': round(summary['used_classified_pct'], 2),
            'section_slots': summary['section_slots'],
            'section_resolved': summary['section_resolved'],
            'section_resolved_pct': round(summary['section_resolved_pct'], 2),
            'section_named_texture': summary['section_named'],
            'section_named_pct': round(summary['section_named_pct'], 2),
            'section_classified': summary['section_classified'],
            'section_classified_pct': round(summary['section_classified_pct'], 2),
        },
        'rules': summary['rules'],
        'reasons': summary['reasons'],
        'format_histogram': summary['format_histogram'],
        'format_histogram_all': summary['format_histogram_all'],
        'external_links': summary['external_links'],
        'orphan_textures': summary['orphan_textures'],
        'materials': mats,
        'used_materials': used,
        'mesh_sections': {m: {'slots': s, 'components': summary['used']['mesh_count'].get(m, 0)}
                          for m, s in summary['used']['mesh_slots'].items()},
    }
    with open(path, 'w') as fh:
        json.dump(doc, fh, indent=1, sort_keys=False)
    return doc


def main(argv=None):
    ap = argparse.ArgumentParser(
        description='Resolve RL arena materials to decoded diffuse textures (A3).')
    ap.add_argument('package', help='cooked .upk (the user\'s own install)')
    ap.add_argument('--out', default='/tmp/rl_a3/tex',
                    help='directory for decoded RGBA8 dumps (must be outside the repo)')
    ap.add_argument('--handoff', default='/tmp/rl_a3/materials.json',
                    help='machine-readable handoff JSON for the packer (A5)')
    ap.add_argument('--no-write', action='store_true', help='do not write any pixel data')
    ap.add_argument('--quiet', action='store_true', help='summary only, no per-row lines')
    ap.add_argument('--index', default='/tmp/rl_a3b/pkgindex.json',
                    help='sibling-package index JSON (built by tools/arena/rlpkg.py)')
    ap.add_argument('--install', default=None,
                    help='install directory; when the index is missing it is built '
                         'from this directory (the default is the directory of the '
                         'package argument)')
    ap.add_argument('--no-sibling', action='store_true',
                    help='do not resolve imported textures in sibling packages')
    args = ap.parse_args(argv)

    pkg = open_package(args.package)
    sibling = None
    if not args.no_sibling:
        install = args.install or os.path.dirname(pkg.path)
        try:
            sibling = SiblingTextures.from_install(install, args.index,
                                                    quiet=args.quiet)
        except (OSError, ValueError) as exc:
            print('sibling index unavailable (%s); continuing without it' % (exc,))
            sibling = None
    summary = resolve_all(pkg, sibling)

    print('package: %s' % pkg.path)
    print('  %d names, %d imports, %d exports, bias %d'
          % (len(pkg.upk.names), len(pkg.upk.imports), len(pkg.upk.exports),
             pkg.upk.serial_bias))
    print('  materials=%d instances=%d total=%d'
          % (summary['materials'], summary['instances'], summary['total']))
    print('RESOLUTION: %d/%d (%.1f%%) resolve to a diffuse Texture2D'
          % (summary['resolved'], summary['total'], summary['resolved_pct']))
    print('  designation rules: %s' % (', '.join('%s=%d' % kv for kv in
                                                 sorted(summary['rules'].items())) or 'none'))
    print('USED-MATERIAL RATE: %d/%d (%.1f%%) of the %d materials used by placed '
          'instances resolve to an in-package Texture2D; %d (%.1f%%) have a NAMED '
          'texture (in-package or in a named external package); %d (%.1f%%) are '
          'classified'
          % (summary['used_resolved'], summary['used_total'],
             summary['used_resolved_pct'], summary['used_total'],
             summary['used_named_texture'], summary['used_named_pct'],
             summary['used_classified'], summary['used_classified_pct']))
    print('SECTION RATE: %d section slots bound to a material, %d (%.1f%%) resolve '
          'to an in-package Texture2D, %d (%.1f%%) have a named texture, '
          '%d (%.1f%%) are classified'
          % (summary['section_slots'], summary['section_resolved'],
             summary['section_resolved_pct'], summary['section_named'],
             summary['section_named_pct'], summary['section_classified'],
             summary['section_classified_pct']))
    print('  used components with a material list: %d; distinct meshes: %d'
          % (summary['used']['components'], len(summary['used']['mesh_slots'])))
    print('TEXTURES: %d in-package texture exports, %d decoded, %d undecoded'
          % (len(summary['textures']),
             sum(1 for r in summary['textures'] if r['decoded']),
             len(summary['textures_undecoded'])))
    fmt_hist = ', '.join('%s=%d' % kv for kv in sorted(summary['format_histogram'].items()))
    print('  format histogram (all in-package textures): %s' % (fmt_hist or 'none'))
    print('  format histogram (all %d, decoded + undecoded): %s'
          % (summary['textures_total'], summary['format_histogram_all']))
    print('  unresolved reasons:')
    for k, v in sorted(summary['reasons'].items(), key=lambda kv: -kv[1]):
        print('    %4d  %s' % (v, k))
    if summary['textures_undecoded']:
        print('  undecoded textures (explicit):')
        for r in summary['textures_undecoded']:
            print('    %-34s %s' % (r['name'], r['reason']))
    print('EXTERNAL material->texture links (the texture lives in another '
          'package): %d total' % sum(summary['external_links'].values()))
    for k, v in sorted(summary['external_links'].items(), key=lambda kv: -kv[1]):
        print('    %-28s %3d links' % (k, v))
    print('TEXTURE REFERRER CENSUS: %d of %d in-package textures have no '
          'referrer inside the package (the material graph that bound them was '
          'stripped by the cook)'
          % (len(summary['orphan_textures']), summary['textures_total']))
    if sibling is not None:
        print('SIBLING PACKAGES: %d material object(s) resolved to a texture in a '
              'sibling package, %d distinct texture(s) from %d package(s)'
              % (summary['sibling_resolved'], len(summary['sibling_textures']),
                 len(summary['sibling_packages'])))
        for k, v in sorted(summary['sibling_packages'].items()):
            print('    %-28s %3d texture(s)' % (k, v))
        for r in summary['sibling_textures']:
            print('    %-30s %-14s %-24s %dx%d %s  <- %s'
                  % (r['texture'], r['package'], os.path.basename(r['file'] or ''),
                     r['width'], r['height'], r['format'], r['material']))
    if not args.quiet:
        print('  rows:')
        for r in summary['rows']:
            mark = 'OK  ' if r.reason is None else 'MISS'
            print('    %s %-42s %-26s %-18s %s'
                  % (mark, r.material, r.material_class,
                     r.texture or (r.reason or ''), r.rule or ''))
    if not args.no_write:
        out = _guard_out_dir(args.out)
        wrote = _write_outputs(summary, out)
        print('wrote %d decoded RGBA8 textures + manifest.json to %s' % (wrote, out))
        hpath = _guard_out_dir(args.handoff)
        _handoff(summary, out, hpath)
        print('wrote handoff %s' % hpath)
    return 0 if summary['resolved'] and summary['textures'] else 1


if __name__ == '__main__':
    sys.exit(main())
