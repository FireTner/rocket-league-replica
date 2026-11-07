#!/usr/bin/env python3
"""Map a material's compiled texture parameters onto its ReferencedTextures array.
Reads a local RefShaderCache package. Needs a local Oodle library. Writes outside the repo.
"""

from __future__ import annotations

import argparse
import json
import mmap
import os
import struct
import sys
from collections import Counter

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(os.path.dirname(_HERE))
sys.path[:0] = [os.path.join(_REPO, 'tools', 'decomp'), _HERE]

import rlmat  # noqa: E402
import rlupk  # noqa: E402

TAG = 0x9E2A83C1
ALBEDO_PARAMS = frozenset({
    'Diffuse', 'TextureDiffuse', 'DiffuseQuarter', 'AdTexture',
    'FieldSkin', 'DiffuseTexture',
})
NORMAL_PARAMS = frozenset({
    'Normal', 'TextureNormal', 'NormalTexture',
})


def _find_oo2core():
    env = os.environ.get('OO2CORE_SO')
    candidates = [
        env,
        '/tmp/arena_h/liboo2corelinux64.so.9',
        os.path.join(_REPO, 'arena_assets', 'liboo2corelinux64.so.9'),
    ]
    for p in candidates:
        if p and os.path.isfile(p):
            return p
    raise FileNotFoundError(
        'Oodle shared library not found. Set OO2CORE_SO or place '
        'liboo2corelinux64.so.9 under /tmp/arena_h/')


def _oodle_decompress(lib, src: bytes, dst_len: int) -> bytes:
    import ctypes
    src_buf = (ctypes.c_ubyte * len(src)).from_buffer_copy(src)
    dst_buf = (ctypes.c_ubyte * dst_len)()
    ret = lib.OodleLZ_Decompress(
        ctypes.addressof(src_buf), len(src),
        ctypes.addressof(dst_buf), dst_len,
        1, 0, 0, None, 0, None, None, None, 0, 3)
    if ret != dst_len:
        raise RuntimeError(f'OodleLZ_Decompress returned {ret}, want {dst_len}')
    return bytes(dst_buf)


def _load_oodle():
    import ctypes
    path = _find_oo2core()
    lib = ctypes.CDLL(path)
    lib.OodleLZ_Decompress.restype = ctypes.c_size_t
    lib.OodleLZ_Decompress.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_int, ctypes.c_int, ctypes.c_int,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_void_p,
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int,
    ]
    return lib


def _inflate_ue3_oodle(lib, raw: bytes, pos: int):
    tag, block_size = struct.unpack_from('<Ii', raw, pos)
    if tag != TAG:
        raise ValueError(f'bad chunk tag at {pos}: {hex(tag)}')
    total_c, total_u = struct.unpack_from('<ii', raw, pos + 8)
    nblocks = (total_u + block_size - 1) // block_size
    blocks = [struct.unpack_from('<ii', raw, pos + 16 + 8 * k)
              for k in range(nblocks)]
    p = pos + 16 + 8 * nblocks
    out = bytearray()
    for bc, bu in blocks:
        out += _oodle_decompress(lib, raw[p:p + bc], bu)
        p += bc
    return bytes(out), p


def inflate_shader_cache(cache_path: str, image_path: str | None = None) -> str:
    """Inflate RefShaderCache to a flat image file; return the image path."""
    lib = _load_oodle()
    with open(cache_path, 'rb') as fh:
        raw = fh.read()

    # Summary chunk[0] describes the encrypted/compressed header region.
    # Layout measured: CompressionFlags @109, nchunks @113, first chunk @117
    # with uoff=NameOffset, coff pointing at an FCompressedChunkHeader.
    uoff, usize, coff, csize = struct.unpack_from('<qiqi', raw, 117)
    header, end0 = _inflate_ue3_oodle(lib, raw, coff)
    if len(header) != usize:
        raise RuntimeError(f'header size drift: got {len(header)} want {usize}')

    name_offset = uoff  # 141
    image = bytearray(raw[:name_offset]) + bytearray(header)

    # Body: sequential Oodle FCompressedChunkHeaders after the header container.
    pos = end0
    while pos + 16 <= len(raw):
        tag, = struct.unpack_from('<I', raw, pos)
        if tag != TAG:
            break
        chunk, pos = _inflate_ue3_oodle(lib, raw, pos)
        image += chunk

    if image_path is None:
        image_path = '/tmp/arena_h/sc_image.bin'
    os.makedirs(os.path.dirname(image_path) or '.', exist_ok=True)
    with open(image_path, 'wb') as fh:
        fh.write(image)
    return image_path


def _parse_names(mm, name_offset: int, name_count: int):
    c = rlupk.Cursor(mm)
    c.pos = name_offset
    names = []
    for _ in range(name_count):
        n = c.i32()
        if n < 0:
            s = c.raw(-2 * n).decode('utf-16-le').rstrip('\x00')
        else:
            s = c.raw(n).decode('latin1').rstrip('\x00')
        c.u64()
        names.append(s)
    return names


def material_resource_tail(pkg, export):
    """Return (guid_hex, referenced_texture_names) from a Material export tail."""
    _props, end = pkg.try_properties(export)
    data = pkg.data(export)
    tail = data[end:]
    if len(tail) < 44:
        return None, []
    # Measured layout (ArVer>=858): unkMask, f10count=0, f1Ccount=0, f58,
    # FGuid, f80, ReferencedTextures count + refs.
    guid = tail[16:32].hex()
    nref = struct.unpack_from('<i', tail, 36)[0]
    if nref < 0 or nref > 64 or 40 + 4 * nref > len(tail):
        return guid, []
    refs = []
    for i in range(nref):
        ref = struct.unpack_from('<i', tail, 40 + 4 * i)[0]
        if ref > 0:
            refs.append(pkg.export_name(pkg.upk.exports[ref - 1]))
        elif ref < 0:
            refs.append(pkg.upk.import_name(-ref - 1))
        else:
            refs.append(None)
    return guid, refs


def _texture_param_bindings(mm, names, guid_hex, lookback=50000):
    """Find Diffuse/Normal TextureParameter indices ahead of a material GUID."""
    tex_cls = names.index('FMaterialUniformExpressionTextureParameter')
    guid = bytes.fromhex(guid_hex)
    h = mm.find(guid)
    if h < 0:
        return None
    start = max(0, h - lookback)
    params = []
    off = start
    while off + 20 < h:
        cls, z1, nm, z2, tidx = struct.unpack_from('<iiiii', mm, off)
        if cls == tex_cls and z1 == 0 and z2 == 0 and 0 <= nm < len(names):
            params.append((off - h, names[nm], tidx))
            off += 20
        else:
            off += 4

    best = None
    i = 0
    while i < len(params):
        j = i
        while j + 1 < len(params) and params[j + 1][0] - params[j][0] < 64:
            j += 1
        cluster = params[i:j + 1]
        names_in = {p[1] for p in cluster}
        has_d = bool(names_in & ALBEDO_PARAMS)
        has_n = bool(names_in & NORMAL_PARAMS)
        if has_d or has_n:
            key = (has_d and has_n, has_d, -abs(cluster[0][0]))
            if best is None or key > best[0]:
                best = (key, cluster)
        i = j + 1
    if not best:
        return {'guid_off': h, 'diffuse_index': None, 'normal_index': None,
                'diffuse_param': None, 'normal_param': None, 'cluster': []}
    cluster = best[1]
    out = {'guid_off': h, 'diffuse_index': None, 'normal_index': None,
           'diffuse_param': None, 'normal_param': None,
           'cluster': [(n, t) for _, n, t in cluster]}
    for _, nm, tidx in cluster:
        if nm in ALBEDO_PARAMS and out['diffuse_index'] is None:
            out['diffuse_param'] = nm
            out['diffuse_index'] = tidx
        if nm in NORMAL_PARAMS and out['normal_index'] is None:
            out['normal_param'] = nm
            out['normal_index'] = tidx
    return out


def is_colour_texture(pkg, tex_name, sibling_resolver=None):
    """True when a Texture2D looks like a colour map (not normal/mask/gray)."""
    # Prefer in-package export.
    try:
        ex = pkg.export_by_name(tex_name)
    except Exception:
        ex = None
    if ex is None:
        return True  # unknown import: allow as colour candidate
    cls = pkg.export_class_name(ex)
    if cls not in ('Texture2D', 'LightMapTexture2D'):
        return False
    if cls == 'LightMapTexture2D':
        return False
    props, _end = pkg.try_properties(ex)
    # props may be dict-like from try_properties
    if isinstance(props, dict):
        items = props.items() if hasattr(props, 'items') else []
        # rlmat try_properties returns (dict_or_list, end)
    # Walk tagged props for CompressionSettings
    prop_list = props if isinstance(props, list) else []
    if isinstance(props, dict):
        # some versions return name->value
        cs = props.get('CompressionSettings')
        if isinstance(cs, str) and ('Normal' in cs or 'Grayscale' in cs or 'Mask' in cs):
            return False
        return True
    for p in prop_list:
        if not isinstance(p, dict):
            continue
        if p.get('name') == 'CompressionSettings':
            val = str(p.get('value', p.get('enum', '')))
            if any(x in val for x in ('Normal', 'Grayscale', 'Mask', 'TC_Normalmap')):
                return False
    # Name heuristic only as last resort on the texture object's own
    # CompressionSettings absence — do NOT stem-guess material names.
    lower = tex_name.lower()
    if lower.endswith('_n') or lower.endswith('_normal') or '_n_' in lower:
        # Still require object fields when possible; without CompressionSettings
        # treat trailing _N as normal (MEASURED Stairs_N).
        return False
    return True


def build_bindings(cache_image_path: str, stadium_path: str) -> dict:
    with open(cache_image_path, 'rb') as fh:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)

    # Summary fields from the inflated image head.
    name_count = struct.unpack_from('<i', mm, 33)[0]  # fragile — reparse summary
    # Re-parse summary properly.
    c = rlupk.Cursor(mm)
    assert c.u32() == TAG
    c.u16(); c.u16()
    header_size = c.i32()
    c.fstring()
    c.u32()
    name_count = c.i32()
    name_offset = c.i32()
    export_count = c.i32()
    export_offset = c.i32()
    import_count = c.i32()
    import_offset = c.i32()
    names = _parse_names(mm, name_offset, name_count)

    pkg = rlmat.open_package(stadium_path)

    # Collect every Material / MIC in the stadium package.
    materials = []
    for ex in pkg.upk.exports:
        cls = pkg.export_class_name(ex)
        if cls not in ('Material', 'MaterialInstanceConstant'):
            continue
        if getattr(ex, 'class_index', None) == 0 and getattr(ex, 'serial_size', 1) == 0:
            continue
        name = pkg.export_name(ex)
        if cls == 'Material':
            guid, refs = material_resource_tail(pkg, ex)
        else:
            # MIC: follow parent Material for ReferencedTextures / GUID.
            guid, refs = None, []
            props, end = pkg.try_properties(ex)
            # Parent may be ObjectProperty 'Parent'
            parent = None
            prop_list = props if isinstance(props, list) else []
            # try_properties for rlmat returns dict-like in some paths
            data = pkg.data(ex)
            # Fall back: use texture_for_material path later
            guid, refs = None, []
        materials.append({
            'name': name,
            'class': cls,
            'guid': guid,
            'refs': refs,
            'export_idx': ex.idx if hasattr(ex, 'idx') else None,
        })

    # Build GUID -> sampler binding from the shader cache image.
    mat_bindings = {}
    stairs_proof = None
    for mat in materials:
        if mat['class'] != 'Material' or not mat['guid']:
            continue
        bind = _texture_param_bindings(mm, names, mat['guid'])
        if not bind:
            continue
        di = bind.get('diffuse_index')
        ni = bind.get('normal_index')
        refs = mat['refs']
        colour = refs[di] if di is not None and 0 <= di < len(refs) else None
        normal = refs[ni] if ni is not None and 0 <= ni < len(refs) else None
        # Reject a "colour" that is clearly the normal/mask slot of the same
        # material (Measured: some clusters bind Diffuse to a later index that
        # is out of range, and the closest albedo-named param can still point
        # at Stairs_N-style maps).  Use the texture object's own fields /
        # known normal-map pixel formats — never stem-guess a material name.
        if colour:
            low = colour.lower()
            if (low.endswith('_n') or low.endswith('_normal') or
                    low.endswith('_nrm') or 'normal' in low):
                colour = None
            else:
                try:
                    tex_ex = pkg.export_by_name(colour)
                    if tex_ex is not None:
                        info = rlmat.texture_info(pkg, tex_ex)
                        if (info.get('format') or '') in (
                                'PF_V8U8', 'PF_BC5', 'PF_BC5UNORM'):
                            colour = None
                except Exception:
                    pass
        entry = {
            'material': mat['name'],
            'guid': mat['guid'],
            'refs': refs,
            'diffuse_param': bind.get('diffuse_param'),
            'diffuse_index': di,
            'normal_param': bind.get('normal_param'),
            'normal_index': ni,
            'colour': colour,
            'normal': normal,
            'cluster': bind.get('cluster'),
        }
        mat_bindings[mat['name']] = entry
        if mat['name'] == 'Stairs_Mat':
            stairs_proof = entry

    mm.close()

    if not stairs_proof:
        raise RuntimeError('Stairs_Mat GUID not found in ShaderCache — stop')
    if stairs_proof.get('colour') != 'Stairs_Pack' or stairs_proof.get('normal') != 'Stairs_N':
        raise RuntimeError(
            f"Stairs_Mat proof FAILED: got colour={stairs_proof.get('colour')} "
            f"normal={stairs_proof.get('normal')} (want Stairs_Pack / Stairs_N)")

    return {
        'stairs_proof': {
            'colour': stairs_proof['colour'],
            'normal': stairs_proof['normal'],
            'diffuse_param': stairs_proof['diffuse_param'],
            'normal_param': stairs_proof['normal_param'],
            'ok': True,
        },
        'materials': mat_bindings,
        'name_count': name_count,
        'header_size': header_size,
    }


def recount_sections(stadium_path: str, bindings: dict, cook_dir: str | None = None) -> dict:
    """Recount how many of 606 non-CustomColors sections gain a named diffuse."""
    # Reuse rlmat's used-materials / placement join when available.
    pkg = rlmat.open_package(stadium_path)
    # Prefer the pack's existing section_bindings if present.
    pack_json = os.path.join(_REPO, 'arena_assets', 'stadium_day', 'arena.json')
    if os.path.isfile(pack_json):
        with open(pack_json) as fh:
            pack = json.load(fh)
        rows = pack.get('section_bindings', [])
    else:
        rows = []

    mat_map = bindings.get('materials', {})
    total = 0
    identified = 0
    split = Counter()
    for row in rows:
        if row.get('scope') != 'placement':
            continue
        status = row.get('status', '')
        if status == 'external:CustomColors':
            continue
        total += 1
        mat = row.get('material')
        tex = row.get('texture')
        # Already resolved by graph
        if status == 'RESOLVED' and tex:
            identified += 1
            split['graph_or_prior'] += 1
            continue
        # Shader-cache named diffuse
        b = mat_map.get(mat) if mat else None
        if b and b.get('colour'):
            identified += 1
            split['shader_cache'] += 1
            continue
        # Single colour candidate in ReferencedTextures
        if b and b.get('refs'):
            colours = [r for r in b['refs'] if r and not str(r).endswith('_N')
                       and 'Normal' not in str(r)]
            # Prefer CompressionSettings when we can; here use name filter only
            # for the recount of "array isolates one colour".
            if len(colours) == 1:
                identified += 1
                split['single_colour_array'] += 1
                continue
            if len(colours) == 0:
                split['no_colour_texture'] += 1
            else:
                split['multi_colour_unresolved'] += 1
            continue
        if b is None:
            split['cache_miss_or_no_material'] += 1
        else:
            split['no_colour_texture'] += 1

    return {
        'denominator': total,
        'identified': identified,
        'pct': round(100.0 * identified / total, 2) if total else 0.0,
        'split': dict(split),
        'bar_90pct': int(0.9 * total) if total else 0,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cache', required=True, help='RefShaderCache-PC-D3D-SM5.upk')
    ap.add_argument('--stadium', required=True, help='Stadium_Day_P.upk')
    ap.add_argument('--image', default='/tmp/arena_h/sc_image.bin',
                    help='inflated image path (reused if present + --reuse-image)')
    ap.add_argument('--reuse-image', action='store_true')
    ap.add_argument('--out', default='/tmp/arena_h/bindings.json')
    args = ap.parse_args()

    if args.reuse_image and os.path.isfile(args.image):
        image_path = args.image
        print(f'reusing image {image_path}')
    else:
        print(f'inflating {args.cache} ...')
        image_path = inflate_shader_cache(args.cache, args.image)
        print(f'wrote {image_path} ({os.path.getsize(image_path)} bytes)')

    print('building material bindings ...')
    bindings = build_bindings(image_path, args.stadium)
    proof = bindings['stairs_proof']
    print(f"Stairs_Mat PROOF: colour={proof['colour']} normal={proof['normal']} OK")

    print('recounting sections ...')
    recount = recount_sections(args.stadium, bindings)
    bindings['recount'] = recount
    print(f"identified diffuse: {recount['identified']}/{recount['denominator']} "
          f"({recount['pct']}%)")
    print('split:', json.dumps(recount['split'], sort_keys=True))

    os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)
    # Drop bulky cluster lists from the published table.
    slim = {
        'stairs_proof': bindings['stairs_proof'],
        'recount': recount,
        'materials': {
            k: {kk: vv for kk, vv in v.items() if kk != 'cluster'}
            for k, v in bindings['materials'].items()
        },
    }
    with open(args.out, 'w') as fh:
        json.dump(slim, fh, indent=2, sort_keys=True)
        fh.write('\n')
    print(f'wrote {args.out} ({len(slim["materials"])} materials)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
