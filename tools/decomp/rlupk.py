"""Minimal Rocket League UE3 (.upk) reader — local-only analysis tool.

Reads the user's OWN Rocket League install, decrypts with Rocket League's
public AES-256-ECB package key, inflates the zlib chunks and exposes names /
imports / exports / object bytes so a camera ground-truth surface can be
extracted.  This file is our own MIT code and contains no Psyonix data: the
package bytes it reads stay on the user's machine (repo Rule 1).  See
docs/CAMERA_DECOMP.md for the legal boundary and docs/THIRD_PARTY.md for the
key provenance (AltimorTASDK/RLUPKTool, MIT).

Format notes (RL = UE3 868, licensee 34):
  * summary is plaintext up to NameOffset;
  * [NameOffset, TotalHeaderSize) is AES-256-ECB encrypted;
  * object data is zlib-chunk compressed (FCompressedChunkHeader + blocks);
  * strings are int32-length (not compact indexed), FName = int64;
  * **export `SerialOffset` is relative to `Summary.DependsOffset`, while the
    object stream physically starts at `TotalHeaderSize`** — so every object
    offset needs the per-package bias `TotalHeaderSize - DependsOffset`
    (MEASURED 2026-10-20: ProjectX 86500, TAGame 562181).  With it, exports
    tile the inflated stream exactly; without it, every object reads as if it
    were a neighbouring object, which is what produced the "inline string
    property names / broken UField" diagnosis in docs/CAMERA_DECOMP.md.
    `Package.data()` applies the bias; `export_offset()` exposes the raw value.
"""
import os
import re
import struct
import zlib
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

TAG = 0x9E2A83C1

# The package key is NEVER stored in this repository (repo Rule 1, publication
# boundary in docs/CAMERA_DECOMP.md). Supply your own copy locally, either as
# `$RL_UPK_KEY` (hex) or in a file outside the tree:
#
#   mkdir -p ~/.rl-decomp
#   printf '%s\n' '<64 hex digits>' > ~/.rl-decomp/upk.key
#
# No game build, launch, or binary scan is involved: the key is a plain local
# input, and nothing here derives it from your install.
KEY_ENV = 'RL_UPK_KEY'
KEY_FILE_ENV = 'RL_UPK_KEY_FILE'
KEY_FILE = os.path.join(os.path.expanduser('~'), '.rl-decomp', 'upk.key')
_KEY_CACHE = None


class MissingKey(RuntimeError):
    """No package key available locally (the public repo carries none)."""


def parse_key(text):
    """Accept 64 hex digits, optionally 0x/:/space/comma separated."""
    digits = re.sub(r'[^0-9a-fA-F]', '', re.sub(r'0[xX]', '', text))
    if len(digits) != 64:
        raise MissingKey(
            f'package key must be 32 bytes / 64 hex digits, got {len(digits)} digits')
    return bytes.fromhex(digits)


def load_key():
    """Return the 32-byte package key from the environment or the local file."""
    global _KEY_CACHE
    if _KEY_CACHE:
        return _KEY_CACHE

    env = os.environ.get(KEY_ENV, '').strip()
    if env:
        _KEY_CACHE = parse_key(env)
        return _KEY_CACHE

    path = os.environ.get(KEY_FILE_ENV) or KEY_FILE
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError as exc:
        raise MissingKey(
            f'no Rocket League package key: ${KEY_ENV} is unset and {path} is '
            f'unreadable ({exc.strerror}).\n'
            'This repository intentionally carries no key. Supply your own '
            'local copy, e.g.:\n'
            f"  mkdir -p {os.path.dirname(path)}\n"
            f"  printf '%s\\n' '<64 hex digits>' > {path}\n"
            f'or: export {KEY_ENV}=<64 hex digits>') from None

    _KEY_CACHE = data if len(data) == 32 else parse_key(data.decode('ascii', 'replace'))
    return _KEY_CACHE


class Cursor:
    def __init__(self, buf, pos=0):
        self.buf = buf
        self.pos = pos

    def i32(self):
        v, = struct.unpack_from('<i', self.buf, self.pos)
        self.pos += 4
        return v

    def u32(self):
        v, = struct.unpack_from('<I', self.buf, self.pos)
        self.pos += 4
        return v

    def u16(self):
        v, = struct.unpack_from('<H', self.buf, self.pos)
        self.pos += 2
        return v

    def i64(self):
        v, = struct.unpack_from('<q', self.buf, self.pos)
        self.pos += 8
        return v

    def u64(self):
        v, = struct.unpack_from('<Q', self.buf, self.pos)
        self.pos += 8
        return v

    def raw(self, n):
        v = self.buf[self.pos:self.pos + n]
        self.pos += n
        return v

    def fstring(self):
        n = self.i32()
        if n == 0:
            return ''
        if n < 0:
            s = self.buf[self.pos:self.pos - 2 * n].decode('utf-16-le')
            self.pos += -2 * n
        else:
            s = self.buf[self.pos:self.pos + n - 1].decode('latin1')
            self.pos += n
        return s


class Export:
    __slots__ = ('idx', 'class_index', 'super_index', 'package_index', 'name_index',
                 'archetype', 'object_flags', 'serial_size', 'serial_offset',
                 'export_flags', 'net_objects', 'guid', 'package_flags')

    def __str__(self):
        return f'{self.idx}:{self.name}'


class Import:
    __slots__ = ('idx', 'class_package', 'class_name', 'package_index', 'name_index')


class Package:
    def __init__(self, path, verbose=False):
        self.path = path
        self.raw = open(path, 'rb').read()
        self.names = []          # list[str]
        self.name_flags = []
        self.imports = []
        self.exports = []
        self._parse_summary()
        self._decrypt_header()
        self._parse_names()
        self._parse_imports()
        self._parse_exports()
        self.image = self._build_image()
        if verbose:
            print(f'{path}: {len(self.names)} names, {len(self.imports)} imports, '
                  f'{len(self.exports)} exports, image {len(self.image)} bytes, '
                  f'{len(self.chunks)} chunks, serial-offset bias {self.serial_bias}')

    @classmethod
    def from_image(cls, path, verbose=False):
        """Read a decrypted + inflated image (e.g. what `unpack.py` wrote).

        The image has the same layout as the package file, but the header is
        already plaintext and every compressed chunk is inflated, so no key is
        involved and `data(export)` indexes bytes that `SerialOffset` already
        points at.  Names/imports/exports parse identically.
        """
        self = cls.__new__(cls)
        self.path = path
        self.raw = open(path, 'rb').read()
        self.names, self.name_flags = [], []
        self.imports, self.exports = [], []
        self.chunks = []
        self._parse_summary()
        self.header = self.raw[self.name_offset:self.header_size]
        self._parse_names()
        self._parse_imports()
        self._parse_exports()
        self.image = self.raw
        if verbose:
            print(f'{path} (image): {len(self.names)} names, '
                  f'{len(self.imports)} imports, {len(self.exports)} exports, '
                  f'{len(self.image)} bytes, serial-offset bias {self.serial_bias}')
        return self

    # ---------------------------------------------------------------- summary
    def _parse_summary(self):
        c = Cursor(self.raw)
        assert c.u32() == TAG, 'not a UE3 package'
        self.file_version = c.u16()
        self.licensee_version = c.u16()
        self.header_size = c.i32()
        self.folder = c.fstring()
        self.package_flags = c.u32()
        self.name_count = c.i32()
        self.name_offset = c.i32()
        self.export_count = c.i32()
        self.export_offset = c.i32()
        self.import_count = c.i32()
        self.import_offset = c.i32()
        self.depends_offset = c.i32()
        c.i32()                      # unk1 == depends_offset
        c.i32(); c.i32(); c.i32()    # unk2..4
        self.guid = c.raw(16).hex()
        gens = c.i32()
        self.generations = [(c.i32(), c.i32(), c.i32()) for _ in range(gens)]
        self.engine_version = c.i32()
        self.cooker_version = c.i32()
        self.compression_flags = c.u32()
        nchunks = c.i32()
        self.summary_chunks = []
        for _ in range(nchunks):
            self.summary_chunks.append((c.i64(), c.i32(), c.i64(), c.i32()))
        self.summary_end = c.pos

        # licensee 34: export SerialOffset is relative to DependsOffset while the
        # object stream starts at TotalHeaderSize (see the module docstring).
        self.serial_bias = (self.header_size - self.depends_offset
                            if 0 < self.depends_offset < self.header_size else 0)

    # --------------------------------------------------------------- decryption
    def _decrypt_header(self):
        start = self.name_offset
        size = ((self.header_size - start) + 15) & ~15
        buf = self.raw[start:start + size]
        dec = Cipher(algorithms.AES(load_key()), modes.ECB()).decryptor()
        self.header = dec.update(buf) + dec.finalize()

    # ------------------------------------------------------------------ names
    def _parse_names(self):
        c = Cursor(self.header)
        for _ in range(self.name_count):
            n = c.i32()
            if n < 0:
                s = c.raw(-2 * n).decode('utf-16-le').rstrip('\x00')
            else:
                s = c.raw(n).decode('latin1').rstrip('\x00')
            self.names.append(s)
            self.name_flags.append(c.u64())

    # ---------------------------------------------------------------- imports
    def _parse_imports(self):
        c = Cursor(self.header, self.import_offset - self.name_offset)
        for i in range(self.import_count):
            imp = Import()
            imp.idx = i
            imp.class_package = c.i32()
            c.i32()                      # class package FName number
            imp.class_name = c.i32()
            c.i32()                      # class name FName number
            imp.package_index = c.i32()
            imp.name_index = c.i64()
            self.imports.append(imp)

    def import_name(self, i):
        imp = self.imports[i]
        return self.fname(imp.name_index)

    # ---------------------------------------------------------------- exports
    def _parse_exports(self):
        c = Cursor(self.header, self.export_offset - self.name_offset)
        for i in range(self.export_count):
            e = Export()
            e.idx = i
            e.class_index = c.i32()
            e.super_index = c.i32()
            e.package_index = c.i32()
            e.name_index = c.i64()
            e.archetype = c.i32()
            e.object_flags = c.u64()
            e.serial_size = c.i32()
            e.serial_offset = c.i64()
            e.export_flags = c.i32()
            n = c.i32()
            e.net_objects = [c.i32() for _ in range(n)]
            e.guid = c.raw(16).hex()
            e.package_flags = c.i32()
            self.exports.append(e)
        self.exports_end = c.pos
        if self.exports_end > len(self.header) * 0.9:
            pass

    # ----------------------------------------------------------------- fnames
    def fname(self, packed):
        idx = struct.unpack('<i', struct.pack('<q', packed)[:4])[0]
        return self.names[idx] if 0 <= idx < len(self.names) else f'<bad name {idx}>'

    @staticmethod
    def fname_parts(packed):
        low = struct.unpack('<i', struct.pack('<q', packed)[:4])[0]
        high = struct.unpack('<i', struct.pack('<q', packed)[4:])[0]
        return low, high

    def export_name(self, e):
        return self.fname(e.name_index)

    def export_class_name(self, e):
        if e.class_index < 0:
            return self.fname(self.imports[-e.class_index - 1].name_index)
        return self.fname(self.exports[e.class_index - 1].name_index)

    def export_outer_name(self, e):
        if e.package_index == 0:
            return None
        if e.package_index < 0:
            return self.import_name(-e.package_index - 1)
        return self.fname(self.exports[e.package_index - 1].name_index)

    # ------------------------------------------------------------------ image
    def _build_image(self):
        """Concatenate inflated chunks onto the plaintext header.

        Uncompressed (image) coordinates: [0, header_size) is the decrypted
        header, [header_size, ...) is the inflated object stream.  Export
        SerialOffset values live in that same coordinate space.
        """
        data = self.raw
        img = bytearray(self.raw[:self.header_size])
        img[self.name_offset:self.name_offset + len(self.header)] = self.header[:self.header_size - self.name_offset]
        pos = self.header_size
        self.chunks = []
        while pos + 16 <= len(data):
            tag, block_size = struct.unpack_from('<Ii', data, pos)
            total_c, total_u = struct.unpack_from('<ii', data, pos + 8)
            if tag != TAG or not (0 < total_u < (64 << 20) and 0 < total_c <= total_u + 4096):
                break
            start = pos
            nblocks = (total_u + block_size - 1) // block_size
            # licensee 34: the block table sits before the block payloads
            blocks = [struct.unpack_from('<ii', data, pos + 16 + 8 * k) for k in range(nblocks)]
            p = pos + 16 + 8 * nblocks
            self.chunks.append((start, block_size, total_c, total_u, nblocks))
            for bc, bu in blocks:
                img += zlib.decompress(data[p:p + bc])
                p += bc
            pos = p
        return bytes(img)

    def export_offset(self, e):
        """Raw `SerialOffset` from the export table (relative to DependsOffset)."""
        return e.serial_offset

    def data(self, e):
        """Bytes of one export, bias-corrected (see the module docstring).

        This is the export's real object data: `self.image` is laid out in FILE
        coordinates (summary at 0, decrypted header, then the inflated object
        stream), so a raw `SerialOffset` — which this build measures relative to
        `DependsOffset` — lands `serial_bias` bytes early, i.e. inside the
        preceding object.  MEASURED: with the bias, 60/61 sampled class default
        objects walk their tagged-property list to exactly `serial_size` and
        100% of the tag names are declared members of that class; without it,
        0-1/61 and 0%.
        """
        off = e.serial_offset + self.serial_bias
        return self.image[off:off + e.serial_size]

    def data_raw(self, e):
        """The un-biased slice at the raw `SerialOffset` (diagnostics only).

        Useful to *show* the shift: it is the tail of whichever object really
        lives `serial_bias` bytes earlier in the stream, which is why it can
        look like a property list while naming another class's members.
        """
        return self.image[e.serial_offset:e.serial_offset + e.serial_size]

    # ------------------------------------------------------------------ query
    def find(self, needle, limit=None):
        needle = needle.lower()
        out = []
        for e in self.exports:
            if needle in self.export_name(e).lower():
                out.append(e)
                if limit and len(out) >= limit:
                    break
        return out


if __name__ == '__main__':
    import sys
    if len(sys.argv) < 2:
        print('usage: rlupk.py <package.upk> [name-needle]')
        raise SystemExit(2)
    try:
        pkg = Package(sys.argv[1], verbose=True)
    except MissingKey as exc:
        print(f'SKIP: {exc}')
        raise SystemExit(3) from None
    needle = sys.argv[2] if len(sys.argv) > 2 else None
    if needle:
        for e in pkg.find(needle):
            print(f'  export {e.idx:6d} {pkg.export_class_name(e):28s} {pkg.export_name(e)} '
                  f'({e.serial_size} b @ {e.serial_offset})')
