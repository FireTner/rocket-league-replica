"""Extract FLightMap2D bindings from Stadium StaticMeshComponents.

Measured layout (UE3 licensee-34 Stadium_Day_P, 2026-10-29): after the
component property block, an FLightMap2D blob begins with the 32-byte
signature below, then:

  i32 NormalizedAverageColor export (1-based)
  float3 scale (usually 1,1,1)
  i32 DirectionalMaxComponent export (1-based)
  float3 directional scale
  float4 coefficient pad (0,2,2,2)
  float2 CoordinateScale
  float2 CoordinateBias

65 of 1176 StaticMeshComponents carry a valid pair. Floor Team1/Team2 and
DFH_Day_StadiumBase2 are among them.

Floor_Team1's only pair is that NormalizedAverage / DirectionalMax binding.
On the licensee-34 Stadium_Day cook the chart is structurally empty
(MEASURED 2026-11-07): directional rect lit 1.19%, average rect lit 1.68%,
uv*s+b vertex lit 8.21%. Team2's quadrant is 1.14% lit. StadiumBase on the
same path is 70.9% lit. The Team1 component's blob references no second
lightmap. Shifting its UVs into the atlas's other populated quadrant samples
a neighbour (45.8% lit, half the vertices still black). Leave that mesh unbound.

Accepts either `rlmat.RLMatPackage` (preferred — has try_properties) or a
raw `rlupk.Package` (falls back to name-only mesh identification).
"""

from __future__ import annotations

import struct

# FLightMap2D lead-in measured on every valid Stadium_Day component.
LIGHTMAP_SIG = bytes.fromhex(
    'c4eb1e1478cf684480bd454e6fee89762d0b90afc6203d4c992441a2ebcce392')


def _upk(pkg):
    """Underlying rlupk.Package (RLMatPackage wraps it as .upk)."""
    return getattr(pkg, 'upk', pkg)


def _mesh_name(pkg, ex):
    upk = _upk(pkg)
    # Prefer tagged StaticMesh property (RLMatPackage).
    if hasattr(pkg, 'try_properties'):
        props, _ = pkg.try_properties(ex)
        if props and 'StaticMesh' in props:
            raw = props['StaticMesh'].value
            if isinstance(raw, (bytes, bytearray)) and len(raw) >= 4:
                idx, = struct.unpack_from('<i', raw, 0)
                if 0 < idx <= len(upk.exports):
                    e = upk.exports[idx - 1]
                    if upk.export_class_name(e) == 'StaticMesh':
                        return upk.export_name(e), idx
    # Fallback: first StaticMesh export index in the head of the component.
    mesh_by_idx = {
        e.idx + 1: upk.export_name(e)
        for e in upk.exports if upk.export_class_name(e) == 'StaticMesh'
    }
    data = upk.data(ex)
    for j in range(0, min(len(data), 800) - 4, 4):
        v, = struct.unpack_from('<i', data, j)
        if v in mesh_by_idx:
            return mesh_by_idx[v], v
    return None, None


def lightmap_exports(pkg):
    """1-based export index -> LightMapTexture2D name."""
    upk = _upk(pkg)
    out = {}
    for e in upk.exports:
        if upk.export_class_name(e) == 'LightMapTexture2D':
            out[e.idx + 1] = upk.export_name(e)
    return out


def extract_component_lightmaps(pkg):
    """Return a list of dicts, one per valid FLightMap2D on a component."""
    upk = _upk(pkg)
    lm = lightmap_exports(pkg)
    hits = []
    for ex in upk.exports:
        if upk.export_class_name(ex) != 'StaticMeshComponent':
            continue
        data = upk.data(ex)
        mesh, mesh_idx = _mesh_name(pkg, ex)
        pos = 0
        while True:
            i = data.find(LIGHTMAP_SIG, pos)
            if i < 0:
                break
            o = i + 32
            if o + 64 > len(data):
                pos = i + 1
                continue
            avg, = struct.unpack_from('<i', data, o)
            dire, = struct.unpack_from('<i', data, o + 16)
            if avg not in lm or dire not in lm:
                pos = i + 1
                continue
            an, dn = lm[avg], lm[dire]
            if not an.startswith('Normalized') or not dn.startswith('Directional'):
                pos = i + 1
                continue
            sa = struct.unpack_from('<3f', data, o + 4)
            sb = struct.unpack_from('<3f', data, o + 20)
            rest = struct.unpack_from('<8f', data, o + 32)
            hits.append({
                'component_export': ex.idx,
                'mesh': mesh,
                'mesh_export': mesh_idx,
                'avg_export': avg,
                'avg_name': an,
                'dir_export': dire,
                'dir_name': dn,
                'avg_scale': sa,
                'dir_scale': sb,
                'coord_scale': (rest[4], rest[5]),
                'coord_bias': (rest[6], rest[7]),
                'coef': rest[0:4],
            })
            pos = i + 1
    return hits


def lightmap_index_by_component(pkg):
    """component_export idx -> first lightmap binding dict."""
    out = {}
    for h in extract_component_lightmaps(pkg):
        out.setdefault(h['component_export'], h)
    return out
