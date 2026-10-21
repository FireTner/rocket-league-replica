#!/usr/bin/env python3
"""rldds.py — pure-Python DXT1/DXT3/DXT5 + uncompressed texture decoders to RGBA8.

Part of PHASE A3 of `MASTER_PROMPT_ARENA_1TO1.md` (materials + textures). This
module holds NO game data and touches no package: it converts a byte string plus
a UE3 pixel-format name into exactly `w * h * 4` RGBA8 bytes.

Formats (UE3 `EPixelFormat`, as spelled by the cooked `Format` property of a
`Texture2D` export in the licensee-34 cook):

    PF_DXT1      S3TC BC1 — 8 B / 4x4 block, 3 or 4 colours + optional 1-bit alpha
    PF_DXT3      S3TC BC2 — 16 B / 4x4 block, explicit 4-bit alpha + 4-colour block
    PF_DXT5      S3TC BC3 — 16 B / 4x4 block, interpolated alpha + 4-colour block
    PF_A8R8G8B8  uncompressed, little-endian DWORD 0xAARRGGBB -> bytes B,G,R,A
    PF_B8G8R8A8  uncompressed, little-endian DWORD 0xBBGGRRAA -> bytes A,R,G,B
    PF_G8        one greyscale byte per pixel
    PF_V8U8      two signed bytes per pixel (X, Y); not a colour format

Channel-order rule for the uncompressed formats (MEASURED, see
`docs/ARENA_RIP_LOG.md` round A3): a UE3 `PF_A8R8G8B8` texel is the 32-bit value
`0xAARRGGBB`, and the cook stores it little-endian, so the four bytes on disk are
`B, G, R, A`. The field test used the one A8R8G8B8 texture whose subject colour
is physically constrained — `Grass_Strip` (2048x128), whose byte means are
`(13.9, 145.3, 73.9, 255.0)`: read as `B,G,R,A` that is `R=74 G=145 B=14`, a
yellow-green turf; the swapped reading would be `R=14 B=74`, a teal that no
sunlit stadium turf is. `Step_D` (a grey ramp) is invariant under the swap and
its 4th byte is 255 everywhere, which pins byte 3 = alpha.

`SRGB` is a cooked `BoolProperty` on the `Texture2D` export (`True` for 44 of the
62 textures here). The decode below is a byte-exact texel unpack and never
gamma-transforms: the flag is reported by `rlmat.decode_texture`, not applied, so
a consumer that wants linear light does the transform itself and knows why.

Self-test: `python3 tools/arena/rldds.py` (exit 0 = every case passed).
"""
import struct
import sys

__all__ = ['decode', 'expected_size', 'SUPPORTED_FORMATS', 'ROLL', 'TG', 'TB']

BPP = {  # bytes per pixel, uncompressed formats only
    'PF_A8R8G8B8': 4,
    'PF_B8G8R8A8': 4,
    'PF_G8': 1,
    'PF_V8U8': 2,
}
SUPPORTED_FORMATS = tuple(sorted(BPP)) + ('PF_DXT1', 'PF_DXT3', 'PF_DXT5')

# RGB565 expansion: replicate the high bits into the low ones so that 0x1F -> 255
# and 0x00 -> 0 (the S3TC reference expansion).
_ROLL = tuple((i << 3) | (i >> 2) for i in range(32))       # 5 -> 8 bits
ROLL = _ROLL
TG = tuple((i << 2) | (i >> 4) for i in range(64))          # 6 -> 8 bits
TB = _ROLL


def block_size(fmt):
    """Bytes of one 4x4 block (DXT) or of one pixel (uncompressed)."""
    if fmt == 'PF_DXT1':
        return 8
    if fmt in ('PF_DXT3', 'PF_DXT5'):
        return 16
    if fmt in BPP:
        return BPP[fmt]
    raise ValueError('unsupported pixel format %r' % (fmt,))


def block_dims(w, h):
    """(blocks_x, blocks_y) for a DXT image; a partial block still costs a block."""
    return max(1, (w + 3) // 4), max(1, (h + 3) // 4)


def expected_size(w, h, fmt):
    """Exact byte length of a mip of this format (the size a real mip must have)."""
    if fmt in ('PF_DXT1', 'PF_DXT3', 'PF_DXT5'):
        bx, by = block_dims(w, h)
        return bx * by * block_size(fmt)
    if fmt in BPP:
        return w * h * BPP[fmt]
    raise ValueError('unsupported pixel format %r' % (fmt,))


# --------------------------------------------------------------------- DXT colour
def _rgb565(c):
    return _ROLL[(c >> 11) & 0x1F], TG[(c >> 5) & 0x3F], _ROLL[c & 0x1F]


def _colour_block(block, off, out, w, h, bx, by, allow_1bit_alpha, write_alpha=True):
    """Decode one 8-byte S3TC colour block into the RGBA image at pixel (bx, by).

    `allow_1bit_alpha` picks the 3-colour + transparent-black mode when
    c0 <= c1 (BC1 only). `write_alpha=False` is used by DXT3/DXT5, whose alpha
    arrives in the separate 8-byte alpha block and must not be overwritten.
    """
    c0, c1 = struct.unpack_from('<HH', block, off)
    bits, = struct.unpack_from('<I', block, off + 4)
    r0, g0, b0 = _rgb565(c0)
    r1, g1, b1 = _rgb565(c1)
    if c0 > c1 or not allow_1bit_alpha:
        # four-colour mode
        pal = (
            (r0, g0, b0, 255),
            (r1, g1, b1, 255),
            ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255),
            ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255),
        )
    else:
        # three-colour mode + transparent black (BC1's 1-bit alpha)
        pal = (
            (r0, g0, b0, 255),
            (r1, g1, b1, 255),
            ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255),
            (0, 0, 0, 0),
        )
    for py in range(4):
        y = by * 4 + py
        row = bits >> (8 * py)
        for px in range(4):
            x = bx * 4 + px
            if x >= w or y >= h:
                continue
            r, g, b, a = pal[(row >> (2 * px)) & 3]
            i = (y * w + x) * 4
            out[i] = r
            out[i + 1] = g
            out[i + 2] = b
            if write_alpha:
                out[i + 3] = a


def _alpha_dxt3(block, off, out, w, h, bx, by):
    """DXT3: 64 bits of explicit 4-bit alpha, one nibble per pixel, row-major."""
    lo = struct.unpack_from('<Q', block, off)[0]
    for py in range(4):
        y = by * 4 + py
        for px in range(4):
            x = bx * 4 + px
            if x >= w or y >= h:
                continue
            a = (lo >> (4 * (py * 4 + px))) & 0xF
            out[(y * w + x) * 4 + 3] = a * 17     # 0xF -> 255


def _alpha_dxt5(block, off, out, w, h, bx, by):
    """DXT5: a0, a1 endpoints + 16 3-bit indices (alpha ramp of 8 steps)."""
    a0, a1 = struct.unpack_from('<BB', block, off)
    bits = int.from_bytes(block[off + 2:off + 8], 'little')
    if a0 > a1:
        ramp = (a0, a1,
                (6 * a0 + 1 * a1) // 7, (5 * a0 + 2 * a1) // 7,
                (4 * a0 + 3 * a1) // 7, (3 * a0 + 4 * a1) // 7,
                (2 * a0 + 5 * a1) // 7, (1 * a0 + 6 * a1) // 7)
    else:
        ramp = (a0, a1,
                (4 * a0 + 1 * a1) // 5, (3 * a0 + 2 * a1) // 5,
                (2 * a0 + 3 * a1) // 5, (1 * a0 + 4 * a1) // 5,
                0, 255)
    for py in range(4):
        y = by * 4 + py
        for px in range(4):
            x = bx * 4 + px
            if x >= w or y >= h:
                continue
            idx = (bits >> (3 * (py * 4 + px))) & 7
            out[(y * w + x) * 4 + 3] = ramp[idx]


def _decode_dxt(data, w, h, fmt):
    need = expected_size(w, h, fmt)
    if len(data) < need:
        raise ValueError('%s %dx%d needs %d bytes, got %d' % (fmt, w, h, need, len(data)))
    out = bytearray(w * h * 4)
    bs = block_size(fmt)
    bx_n, by_n = block_dims(w, h)
    off = 0
    for by in range(by_n):
        for bx in range(bx_n):
            if fmt == 'PF_DXT1':
                _colour_block(data, off, out, w, h, bx, by, True)
            elif fmt == 'PF_DXT3':
                _alpha_dxt3(data, off, out, w, h, bx, by)
                _colour_block(data, off + 8, out, w, h, bx, by, False, write_alpha=False)
            else:  # PF_DXT5
                _alpha_dxt5(data, off, out, w, h, bx, by)
                _colour_block(data, off + 8, out, w, h, bx, by, False, write_alpha=False)
            off += bs
    return bytes(out)


# ------------------------------------------------------------------- uncompressed
def _decode_uncompressed(data, w, h, fmt):
    need = expected_size(w, h, fmt)
    if len(data) < need:
        raise ValueError('%s %dx%d needs %d bytes, got %d' % (fmt, w, h, need, len(data)))
    n = w * h
    out = bytearray(n * 4)
    if fmt == 'PF_A8R8G8B8':          # disk bytes: B, G, R, A
        out[0::4] = data[2:n * 4:4]
        out[1::4] = data[1:n * 4:4]
        out[2::4] = data[0:n * 4:4]
        out[3::4] = data[3:n * 4:4]
    elif fmt == 'PF_B8G8R8A8':        # disk bytes: A, R, G, B
        out[0::4] = data[1:n * 4:4]
        out[1::4] = data[2:n * 4:4]
        out[2::4] = data[3:n * 4:4]
        out[3::4] = data[0:n * 4:4]
    elif fmt == 'PF_G8':
        out[0::4] = data[:n]
        out[1::4] = data[:n]
        out[2::4] = data[:n]
        out[3::4] = b'\xff' * n
    elif fmt == 'PF_V8U8':            # two signed bytes (X, Y): not colour
        out[0::4] = data[0:n * 2:2]
        out[1::4] = data[1:n * 2:2]
        out[2::4] = b'\x80' * n
        out[3::4] = b'\xff' * n
    else:
        raise ValueError('unsupported pixel format %r' % (fmt,))
    return bytes(out)


def decode(data, w, h, fmt):
    """Decode `data` to exactly w*h*4 RGBA8 bytes. Raises ValueError on mismatch."""
    if w <= 0 or h <= 0:
        raise ValueError('bad dimensions %dx%d' % (w, h))
    if w > 32768 or h > 32768:
        raise ValueError('implausible dimensions %dx%d' % (w, h))
    if fmt in ('PF_DXT1', 'PF_DXT3', 'PF_DXT5'):
        return _decode_dxt(data, w, h, fmt)
    return _decode_uncompressed(data, w, h, fmt)


# ---------------------------------------------------------------------- self-test
def _dxt1_block(c0, c1, indices):
    """One 8-byte DXT1 block; `indices` is a list of 16 2-bit values, row-major."""
    bits = 0
    for i, v in enumerate(indices):
        bits |= (v & 3) << (2 * i)
    return struct.pack('<HHI', c0, c1, bits)


def _dxt5_block(a0, a1, aidx, c0=0, c1=0):
    """One 16-byte DXT5 block: alpha endpoints + 16 3-bit indices + colour block."""
    bits = 0
    for i, v in enumerate(aidx):
        bits |= (v & 7) << (3 * i)
    return struct.pack('<BB', a0, a1) + bits.to_bytes(6, 'little') + \
        _dxt1_block(c0, c1, [0] * 16)


def _dxt3_block(anib, c0=0, c1=0):
    bits = 0
    for i, v in enumerate(anib):
        bits |= (v & 0xF) << (4 * i)
    return struct.pack('<Q', bits) + _dxt1_block(c0, c1, [0] * 16)


def _px(rgba, w, x, y):
    i = (y * w + x) * 4
    return tuple(rgba[i:i + 4])


def _selftest():
    failures = []
    passes = []

    def check(name, got, want):
        if got == want:
            passes.append(name)
        else:
            failures.append('%s: got %r want %r' % (name, got, want))

    # 1. solid colour: c0 == c1 -> three-colour mode, every pixel index 0 -> that RGB.
    red565 = (255 >> 3) << 11 | (0 >> 2) << 5 | (0 >> 3)      # 0xF800
    blk = _dxt1_block(red565, red565, [0] * 16)
    rgba = decode(blk, 4, 4, 'PF_DXT1')
    colors = {_px(rgba, 4, x, y) for y in range(4) for x in range(4)}
    check('DXT1 solid c0==c1 -> every pixel (255,0,0,255)', colors, {(255, 0, 0, 255)})
    check('DXT1 solid -> 4*4*4 bytes', len(rgba), 64)

    # 1b. solid colour with c0 > c1 too (different code path)
    blk = _dxt1_block(red565, 0x001F, [0] * 16)
    rgba = decode(blk, 4, 4, 'PF_DXT1')
    colors = {_px(rgba, 4, x, y) for y in range(4) for x in range(4)}
    check('DXT1 solid c0>c1 index0 -> every pixel (255,0,0,255)', colors, {(255, 0, 0, 255)})

    # 2. c0 > c1 four-colour pattern: red vs blue endpoints -> exact 4 steps.
    idx = [0, 1, 2, 3] * 4
    rgba = decode(_dxt1_block(0xF800, 0x001F, idx), 4, 4, 'PF_DXT1')
    want = [(255, 0, 0, 255), (0, 0, 255, 255), (170, 0, 85, 255), (85, 0, 170, 255)]
    check('DXT1 c0>c1 4-colour row 0', [_px(rgba, 4, x, 0) for x in range(4)], want)
    check('DXT1 c0>c1 4-colour row 3', [_px(rgba, 4, x, 3) for x in range(4)], want)

    # 3. DXT1 1-bit alpha (bonus): c0 <= c1, index 3 -> fully transparent black.
    rgba = decode(_dxt1_block(0x001F, 0xF800, [3] * 16), 4, 4, 'PF_DXT1')
    colors = {_px(rgba, 4, x, y) for y in range(4) for x in range(4)}
    check('DXT1 1-bit alpha index3 -> (0,0,0,0)', colors, {(0, 0, 0, 0)})
    # and index 2 in that mode is the average of the endpoints (c0 = blue here,
    # so the block really is in the c0 <= c1 branch)
    rgba = decode(_dxt1_block(0x001F, 0xF800, [2] * 16), 4, 4, 'PF_DXT1')
    check('DXT1 3-colour midpoint index2 -> (127,0,127,255)',
          _px(rgba, 4, 0, 0), (127, 0, 127, 255))

    # 4. DXT5 alpha ramp, a0 > a1, endpoints chosen for exact integer steps:
    #    252/0 -> 252, 0, 216, 180, 144, 108, 72, 36
    want_a = [252, 0, 216, 180, 144, 108, 72, 36]
    for k in range(8):
        blk = _dxt5_block(252, 0, [k] * 16)
        rgba = decode(blk, 4, 4, 'PF_DXT5')
        check('DXT5 a0>a1 alpha index %d -> %d' % (k, want_a[k]),
              rgba[3], want_a[k])
    # and every pixel of the block carries that alpha (the block is uniform)
    check('DXT5 uniform alpha ramp pixel set',
          {_px(rgba, 4, x, y)[3] for y in range(4) for x in range(4)}, {36})

    # 5. DXT5 8-step mode (a0 < a1): endpoints 0/252, then four interpolants over
    #    5 and the two hard-coded steps 0 and 255:
    #    (4*0+1*252)/5=50, (3*0+2*252)/5=100, (2*0+3*252)/5=151, (1*0+4*252)/5=201
    want_b = [0, 252, 50, 100, 151, 201, 0, 255]
    got_b = [decode(_dxt5_block(0, 252, [k] * 16), 4, 4, 'PF_DXT5')[3] for k in range(8)]
    check('DXT5 a0<a1 8-step alpha ramp', got_b, want_b)
    # index 6 -> 0 and index 7 -> 255 are the two hard-coded steps
    check('DXT5 a0<a1 index6 -> 0', got_b[6], 0)
    check('DXT5 a0<a1 index7 -> 255', got_b[7], 255)

    # 6. DXT3 explicit alpha nibbles: 0x0, 0x1, 0x8, 0xF -> 0, 17, 136, 255
    blk = _dxt3_block([[0, 1, 8, 15][i % 4] for i in range(16)])
    rgba = decode(blk, 4, 4, 'PF_DXT3')
    check('DXT3 nibble 0 -> 0', _px(rgba, 4, 0, 0)[3], 0)
    check('DXT3 nibble 1 -> 17', _px(rgba, 4, 1, 0)[3], 17)
    check('DXT3 nibble 8 -> 136', _px(rgba, 4, 2, 0)[3], 136)
    check('DXT3 nibble 15 -> 255', _px(rgba, 4, 3, 0)[3], 255)

    # 7. non-square, partial-block geometry: 6x10 DXT1 -> 2x3 blocks, exact length
    data = b''.join(_dxt1_block(0x07E0, 0x07E0, [0] * 16) for _ in range(6))
    rgba = decode(data, 6, 10, 'PF_DXT1')
    check('DXT1 6x10 -> %d bytes' % (6 * 10 * 4), len(rgba), 6 * 10 * 4)
    check('DXT1 6x10 green', _px(rgba, 6, 5, 9), (0, 255, 0, 255))

    # 8. uncompressed channel order, one texel of each format.
    rgba = decode(bytes([0x10, 0x20, 0x30, 0x40]), 1, 1, 'PF_A8R8G8B8')
    check('PF_A8R8G8B8 B,G,R,A -> (0x30,0x20,0x10,0x40)', rgba, bytes([0x30, 0x20, 0x10, 0x40]))
    rgba = decode(bytes([0x10, 0x20, 0x30, 0x40]), 1, 1, 'PF_B8G8R8A8')
    check('PF_B8G8R8A8 A,R,G,B -> (0x20,0x30,0x40,0x10)', rgba, bytes([0x20, 0x30, 0x40, 0x10]))
    rgba = decode(bytes([0x77]), 1, 1, 'PF_G8')
    check('PF_G8 0x77 -> (0x77,0x77,0x77,255)', rgba, bytes([0x77, 0x77, 0x77, 0xFF]))
    rgba = decode(bytes([0xC0, 0x40]), 1, 1, 'PF_V8U8')
    check('PF_V8U8 two signed bytes -> (X,Y,0x80,255)', rgba, bytes([0xC0, 0x40, 0x80, 0xFF]))

    # 9. short buffer is a hard error, never a short decode.
    try:
        decode(b'\x00' * 4, 4, 4, 'PF_DXT1')
        failures.append('short DXT1 buffer did not raise')
    except ValueError:
        passes.append('short DXT1 buffer raises ValueError')

    # 10. expected_size agrees with the decoder for every supported format.
    for fmt in SUPPORTED_FORMATS:
        n = expected_size(17, 9, fmt)
        want = expected_size(17, 9, fmt)
        rgba = decode(b'\x00' * n, 17, 9, fmt)
        check('decode(%s, 17x9) -> %d bytes' % (fmt, want), len(rgba), 17 * 9 * 4)

    for line in passes:
        print('PASS ' + line)
    for line in failures:
        print('FAIL ' + line)
    print('rldds self-test: %d PASS, %d FAIL' % (len(passes), len(failures)))
    return 0 if not failures else 1


if __name__ == '__main__':
    sys.exit(_selftest())
