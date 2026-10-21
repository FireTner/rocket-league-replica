#!/usr/bin/env python3
"""rlpkg.py — install-wide package index and sibling-package access (A3b).

PHASE A3b of `MASTER_PROMPT_ARENA_1TO1.md`.  A3 round 2 measured that the
stadium package (`Stadium_Day_P.upk`) is missing 55 of its 80 textures and
that the surviving external links point at **sibling packages of the same UE3
package group**.  This module finds those siblings in the user's own install
and opens them.

===============================================================================
MEASURED FACTS (all measured on the install, not assumed)
===============================================================================
1. **A cooked `.upk` is ONE flat file**: the summary is plaintext at offset 0,
   the *name table* starts at `NameOffset` and is **AES-256-ECB encrypted**
   with the public Rocket League package key (the ECB stream starts exactly at
   `NameOffset`, NOT at a 16-byte-aligned file offset — MEASURED: decrypting
   `Stadium_Day_P.upk` from `NameOffset = 1699` yields the first name
   `2x1_A_MIC`, while decrypting from the aligned 1683 does not).

2. **The package's own name is NOT in the name table.**  MEASURED on
   `Stadium_Day_P.upk`: the 2007 name-table entries are *object* names
   (alphabetically sorted: `2x1_A_MIC`, `2x1_B_MIC`, ... `WarmupMesh`) and
   the string `Stadium_Day_P` does **not** occur.  UE3 derives the package
   name from the FILE name at load time; the cooked file stores the *linked*
   packages instead, as `Package`-class exports.

3. **THE LINKED-PACKAGE MECHANISM IS THE KEY TO THE WHOLE TASK.**  A cooked
   file contains, besides its own objects, `UPackage` exports whose names are
   the names of the *linked packages* the group spans.  MEASURED on
   `Stadium_Day_P.upk`: 143 `Package` exports / 99 distinct names
   (`Adverts`, `City`, `City_Textures`, `Grass_Textures`, `Mat`, `Meshes`,
   `Noise`-style groups, `SM`, `Textures`, `TiledPatterns`, ...).  An object
   that belongs to a linked package has that `Package` export as its `Outer`
   — MEASURED: `Noise_Generic_01_Pack` has outer `Noise`, `Curve_Fire_01_Pack`
   outer `Gradients`, `CarbonFiber_N` outer `Detail_CarbonFiber`,
   `Logo_RocketLeague` outer `GFX_Shared_Logos`.

4. **A linked package is DUPLICATED into every file of the group.**  MEASURED:
   the export `Noise_Generic_01_Pack` exists in `Startup.upk`, `TAGame.upk`
   and `BG_Park_Snowy.upk`; `Curve_Fire_01_Pack` in `Startup.upk`,
   `TAGame.upk`, `BG_Park_Snowy.upk` and `BG_UtopiaSnow.upk`;  the same for
   `Logo_RocketLeague`, `StadiumFloorLightMap`, `CarbonFiber_N`.  So the
   provider of a package is **not unique**; the index maps a package name to
   *all* files that declare it and the resolver picks one.

5. **HOW AN IMPORT NAMES ITS PACKAGE.**  `FObjectImport.PackageIndex` is the
   import's OUTER:
     * **negative** -> another *import* of the same file, and that import's
       `ObjectName` (an FName index into the *importing* file's name table) is
       the package's name — MEASURED: `Noise` 12, `Gradients` 6,
       `EngineMaterials` 2, `FieldSkin_FutureGrid` 2, `Stadiums_DetailNormals`
       2, `Textures`(export) 2, one each in `Detail_CarbonFiber`,
       `Detail_Sparkle`, `FieldSkin_Park`, `GFX_Shared_Logos`,
       `Vehicle_Parent_Textures`, `ENV`, `EngineMeshes`;
     * **positive** -> a `Package` **export** of the same file (the linked
       package group) — MEASURED: `City_Textures` 2, `FutureTech_Textures` 2,
       `Mat` 4, `Grass_Textures` 4, `TiledPatterns` 2, `LensFlares_Textures`
       1, `Events` 1.
   Either way the import names the package by its own name, and that name is
   what the index is keyed by.

6. **THE HEADER IS NOT ALWAYS ENCRYPTED.**  MEASURED: `EngineMaterials.upk`
   (file_version 867, licensee 0) has a **plaintext** header — its name table
   starts with `A\0`, `ArrayProperty\0`, `Base\0`, ...  The licensee-34 files
   (`file_version` 868) are encrypted.  The reader therefore *auto-detects*
   both and validates the result.

7. **THE EXPORT ENTRY IS 72 BYTES FOR LICENSEE >= 1 AND 68 FOR LICENSEE 0.**
   MEASURED: `Stadium_Day_P.upk` (licensee 34) has
   `(DependsOffset - ExportOffset) / ExportCount = 234112 / 3251 = 72.0`
   and the 72-byte layout parses every export name and class correctly;
   `EditorMaterials.upk` (licensee 0) has `20740 / 305 = 68.0`.  The reader
   therefore auto-detects the layout by validating the parsed table.

8. **650 OF THE 3251 EXPORTS OF THE STADIUM PACKAGE ARE ZEROED PLACEHOLDER
   SLOTS.**  MEASURED: exactly 650 exports have `ClassIndex == 0` (and a
   zero name and `SerialSize == 0`).  A census that only looks at
   `export_class_name()` is therefore off by 650 — `rlupk`'s class resolution
   maps `ClassIndex == 0` onto the *last* export, which is why it reports
   1160 `StaticMeshComponent` where the truth is 510.  This module reports
   those slots as `class == None` and NEVER counts them as objects.

9. **SOME PACKAGES CANNOT BE READ AT ALL** (the header does not decrypt with
   the public key).  MEASURED: 24 of the 344 non-sound packages of the
   install (`BB_P`, `CHN_Stadium_P`, `KO_*`, `Labs_*`, `Music_P`,
   `Park_Bman_P`, `Park_Snowy_P`, `Stadium_Race_Day_P`,
   `ThrowbackHockey_P`, `Woods_Night_P`, `FNI_Stadium_P`, `Farm_Night_P`,
   `RefShaderCache-PC-D3D-SM5`, ...).  Their summary parses, their plaintext
   pre-load table parses, but **no 16-byte block of the region before
   `NameOffset` decrypts to a legal name entry** under any ECB origin
   (MEASURED: 1298 origins x 3 blocks x 13 offsets searched, 0 hits), so the
   name table is not recoverable with this key.  They are reported as
   `unreadable` with that reason — never silently skipped.  A3b re-measured
   this with the *decoded* key over **every** origin in the first 350 000
   bytes of `Music_P.upk`, `Park_Snowy_P.upk` and `Stadium_Race_Day_P.upk`:
   **0 candidate origins** (the 6134 unreadable `_SF` sound packages are the
   rest of the unreadable set; they carry no textures or meshes).

10. **A COPY OF A LINKED-PACKAGE OBJECT IS IDENTICAL IN EVERY FILE THAT
    HOLDS IT.**  MEASURED for the stadium's materials: `Stairs_Mat` exists in
    **41** files, all with `Outer == Materials` and all with the same export
    body size (974 B); `Advert_Strip_Mat` in **27** files (712 B);
    `Statue_MAT` in **10** files.  A head-only sweep of all 11 998 readable
    files finds **zero** copies of those materials that own a
    `MaterialExpressionTextureSample*` expression, and **zero** copies with
    `Outer == None` (no file in the install owns the `Materials` package as
    its own package).  So a material whose graph the cook stripped is
    stripped **install-wide**: a sibling package cannot restore it, and only
    an *import* (a reference to a texture that lives in another package) can
    be followed across the group.

11. **A TEXTURE WITH THE `0x00000021` MIP-0 RECORD HAS NO PIXELS ANYWHERE.**
    MEASURED: the record is `[0x21, 0, -1, -1, -1, size_x, size_y]` — the
    uncompressed size is 0 and the cache offset is -1, while the *mip tail*
    records that follow it are normal `0x00010003` file-cache records with
    real offsets.  The same-named copies in the sibling files carry the same
    `0x21` record (MEASURED for `Stairs_Pack` in `CHN_Stadium_Rocks.upk`,
    `BrushedMetal_N` in `antenna_classic_SF.upk`, `ENV_PartlyCloudy` in
    `Swoosh_Lights.upk`, `Grate_Square_Pack` in `TrainStation_P.upk`), and
    the `.tfc` entry that holds the mip tail has exactly the tail's
    uncompressed size (MEASURED: `Stairs_Pack` mip1 -> `Textures5.tfc`
    entry `total_u == 262144 == 128*512*4`), so the mip 0 is not in the
    cache either.  Such a texture is **not** recoverable from the group.

===============================================================================
Public API
    index_install(directory)        -> index dict (package -> files) + report
    resolve_package(name, index)   -> the file path(s) that provide `name`
    open_sibling(name, index)      -> an opened `rlupk.Package` for a sibling
    import_package_name(pkg, i)    -> the internal package name of an import
    read_head(path)                -> the head-only view of one `.upk`
    SiblingResolver(index)         -> locate an object in the sibling packages
                                      (`locate`, `locate_many`, `open_file`)

Rule 1: no package byte, no texture pixel and no index file may enter the
repository tree.  The index is written to `/tmp/rl_a3b/pkgindex.json` by
default and the tool refuses any output path inside the repo.
"""
import argparse
import json
import os
import struct
import sys
import time
from collections import Counter, defaultdict

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(os.path.dirname(_HERE))
_DECOMP = os.path.join(_REPO, 'tools', 'decomp')
for _p in (_DECOMP, _HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import rlupk                                    # noqa: E402  (path set above)
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes   # noqa: E402

TAG = 0x9E2A83C1
INDEX_VERSION = 3

# The name entry is `int32 length` + `length` bytes (the null terminator is
# included) + 8 bytes of name flags.  MEASURED on both the licensee-0
# (plaintext) and licensee-34 (encrypted) headers.
NAME_FLAGS_BYTES = 8

# The import entry is `int32 classPackage` + `int32 classPackageName` +
# `int32 className` + `int32 classNameNum` + `int32 packageIndex` +
# `int64 objectName` = 28 bytes.  MEASURED on every readable package.
IMPORT_ENTRY = 28

# The export entry is 72 bytes for licensee >= 1 and 68 for licensee 0.
EXPORT_ENTRY_64 = 68
EXPORT_ENTRY_72 = 72


class HeadError(Exception):
    """The head of a package could not be parsed."""


def _clip(text, limit=120):
    """Shorten a diagnostic so the index stays small."""
    text = ' '.join(str(text).split())
    return text if len(text) <= limit else text[:limit] + '...'


# --------------------------------------------------------------------- summary
class Summary(object):
    """The plaintext `FPackageFileSummary` at the start of a cooked package."""

    __slots__ = ('file_version', 'licensee', 'header_size', 'folder',
                 'package_flags', 'name_count', 'name_offset', 'export_count',
                 'export_offset', 'import_count', 'import_offset',
                 'depends_offset', 'summary_end')

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)

    def as_dict(self):
        return {k: getattr(self, k) for k in self.__slots__}


def _parse_summary(raw):
    """Parse the plaintext summary.  Raises `HeadError` on a non-UE3 file."""
    p = 0

    def u32():
        nonlocal p
        v, = struct.unpack_from('<I', raw, p)
        p += 4
        return v

    def i32():
        nonlocal p
        v, = struct.unpack_from('<i', raw, p)
        p += 4
        return v

    def u16():
        nonlocal p
        v, = struct.unpack_from('<H', raw, p)
        p += 2
        return v

    def fstring():
        nonlocal p
        n = i32()
        if n == 0:
            return ''
        if n < 0:
            s = raw[p:p - 2 * n].decode('utf-16-le', 'replace')
            p += -2 * n
        else:
            s = raw[p:p + n].decode('latin1', 'replace')
            p += n
        return s

    if len(raw) < 64:
        raise HeadError('file shorter than a package summary')
    if u32() != TAG:
        raise HeadError('not a UE3 package (magic 0x%08X)' % TAG)
    file_version = u16()
    licensee = u16()
    header_size = i32()
    folder = fstring()
    package_flags = u32()
    name_count = i32()
    name_offset = i32()
    export_count = i32()
    export_offset = i32()
    import_count = i32()
    import_offset = i32()
    depends_offset = i32()
    i32()                              # unk1 (== depends_offset)
    i32(); i32(); i32()                # unk2..4
    p += 16                            # guid
    gens = i32()
    for _ in range(gens):
        i32(); i32(); i32()
    i32()                              # engine version
    i32()                              # cooker version
    u32()                              # compression flags
    nchunks = i32()
    p += 24 * nchunks
    return Summary(file_version=file_version, licensee=licensee,
                   header_size=header_size, folder=folder,
                   package_flags=package_flags, name_count=name_count,
                   name_offset=name_offset, export_count=export_count,
                   export_offset=export_offset, import_count=import_count,
                   import_offset=import_offset, depends_offset=depends_offset,
                   summary_end=p)


# ------------------------------------------------------------------ name table
def _parse_names(buf, count):
    """(names, bytes consumed) of a name table; raises `HeadError`."""
    names = []
    p = 0
    for _ in range(count):
        if p + 4 > len(buf):
            raise HeadError('name table overruns the header')
        n, = struct.unpack_from('<i', buf, p)
        p += 4
        if n == 0:
            raise HeadError('empty name entry')
        if n < 0:
            s = buf[p:p - 2 * n].decode('utf-16-le', 'replace').rstrip('\x00')
            p += -2 * n
        else:
            s = buf[p:p + n].decode('latin1', 'replace').rstrip('\x00')
            p += n
        if not s or len(s) > 1024:
            raise HeadError('illegal name entry (length %d)' % len(s))
        for c in s:
            if ord(c) < 32 or ord(c) > 126:
                raise HeadError('non-printable name entry %r' % s)
        if p + NAME_FLAGS_BYTES > len(buf):
            raise HeadError('name table overruns the header')
        p += NAME_FLAGS_BYTES
        names.append(s)
    return names, p


# ------------------------------------------------------------------ head package
class HeadPackage(object):
    """The head-only view of a cooked package (summary + the three tables)."""

    def __init__(self, path, summary, names, imports, exports, encrypted,
                 export_entry):
        self.path = path
        self.summary = summary
        self.names = names
        self.imports = imports
        self.exports = exports
        self.encrypted = encrypted
        self.export_entry = export_entry

    # -- accessors ---------------------------------------------------------
    def fname(self, packed):
        """The name of an int64 FName (index into this file's name table)."""
        idx = struct.unpack('<i', struct.pack('<q', packed)[:4])[0]
        if 0 <= idx < len(self.names):
            return self.names[idx]
        return None

    def fname32(self, idx):
        if 0 <= idx < len(self.names):
            return self.names[idx]
        return None

    def import_name(self, i):
        """The object name of import `i` (an FName in THIS file's table)."""
        if 0 <= i < len(self.imports):
            return self.fname(self.imports[i]['name_index'])
        return None

    def import_class(self, i):
        if 0 <= i < len(self.imports):
            return self.fname32(self.imports[i]['class_name'])
        return None

    def import_outer(self, i):
        """The package an import lives in (see module docstring section 5)."""
        if not (0 <= i < len(self.imports)):
            return None
        pi = self.imports[i]['package_index']
        if pi < 0:
            return self.import_name(-pi - 1)
        if 0 < pi <= len(self.exports):
            return self.export_name(self.exports[pi - 1])
        return None

    def export_name(self, e):
        return self.fname(e['name_index'])

    def export_class(self, e):
        """The class of an export; **None for a zeroed placeholder slot**."""
        ci = e['class_index']
        if ci < 0:
            return self.import_name(-ci - 1)
        if 0 < ci <= len(self.exports):
            return self.export_name(self.exports[ci - 1])
        return None

    def export_outer(self, e):
        pi = e['package_index']
        if pi < 0:
            return self.import_name(-pi - 1)
        if 0 < pi <= len(self.exports):
            return self.export_name(self.exports[pi - 1])
        return None

    def linked_packages(self):
        """The distinct names of the `Package` exports (the linked packages)."""
        out = []
        seen = set()
        for e in self.exports:
            if self.export_class(e) != 'Package':
                continue
            n = self.export_name(e)
            if n and n not in seen:
                seen.add(n)
                out.append(n)
        return out

    def objects(self, name, cls=None, outer=None):
        """The exports named `name` (optionally of class `cls` / outer)."""
        out = []
        for e in self.exports:
            if self.export_name(e) != name:
                continue
            if cls is not None and self.export_class(e) != cls:
                continue
            if outer is not None and self.export_outer(e) != outer:
                continue
            out.append(e)
        return out

    def placeholders(self):
        """The zeroed export slots (ClassIndex == 0) — not objects at all."""
        return [e for e in self.exports if e['class_index'] == 0]


# ------------------------------------------------------------------- head read
def _decrypt(buf, key):
    size = (len(buf) + 15) & ~15
    blob = buf[:size] + b'\x00' * (size - len(buf))
    dec = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    return dec.update(blob) + dec.finalize()


def _parse_imports(buf, count):
    out = []
    p = 0
    for _ in range(count):
        cp, _cpn, cname, _cnn, pkg, name = struct.unpack_from('<iiiiiq', buf, p)
        p += IMPORT_ENTRY
        out.append({'class_package': cp, 'class_name': cname,
                    'package_index': pkg, 'name_index': name})
    return out


def _parse_exports72(buf, count):
    """The licensee >= 1 export entry (72 bytes + a variable GUID array)."""
    out = []
    p = 0
    for _ in range(count):
        ci, si, pi, ni, arch = struct.unpack_from('<iiiqi', buf, p)
        oflags, ssize, soff = struct.unpack_from('<qiq', buf, p + 24)
        eflags, nnet = struct.unpack_from('<ii', buf, p + 44)
        p += 52 + 4 * max(nnet, 0) + 16 + 4
        out.append({'class_index': ci, 'super_index': si,
                    'package_index': pi, 'name_index': ni, 'archetype': arch,
                    'object_flags': oflags, 'serial_size': ssize,
                    'serial_offset': soff, 'export_flags': eflags})
    return out


def _parse_exports68(buf, count):
    """The licensee 0 export entry (68 bytes, fixed)."""
    out = []
    p = 0
    for _ in range(count):
        ci, si, pi, ni, arch = struct.unpack_from('<iiiqi', buf, p)
        oflags, ssize, soff = struct.unpack_from('<iii', buf, p + 24)
        eflags = struct.unpack_from('<i', buf, p + 36)[0]
        p += 68
        out.append({'class_index': ci, 'super_index': si,
                    'package_index': pi, 'name_index': ni, 'archetype': arch,
                    'object_flags': oflags, 'serial_size': ssize,
                    'serial_offset': soff, 'export_flags': eflags})
    return out


def _validate_exports(head_like_names, name_count, exports, import_count):
    """A cheap consistency check of a parsed export table."""
    if not exports:
        return False
    for e in exports:
        ni = struct.unpack('<i', struct.pack('<q', e['name_index'])[:4])[0]
        if not (0 <= ni < name_count):
            return False
        ci = e['class_index']
        if ci < -import_count or ci > len(exports):
            return False
    return True


def read_head(path, key=None):
    """Read ONLY the head of a cooked `.upk`.

    Reads the plaintext summary, then the encrypted (or plaintext) name /
    import / export tables.  The object *body* is never touched.  Raises
    `HeadError` with a measured reason when the head cannot be read.
    """
    if key is None:
        key = rlupk.load_key()
    with open(path, 'rb') as fh:
        head = fh.read(1 << 16)
        if len(head) < 64:
            raise HeadError('file too small')
        summary = _parse_summary(head)
        hs = summary.header_size
        no = summary.name_offset
        # only the three tables are needed: [NameOffset, DependsOffset)
        end = summary.depends_offset
        if not (no < end <= hs):
            end = hs
        if end > len(head):
            fh.seek(0)
            head = fh.read(end + 16)
            if len(head) < end:
                raise HeadError('truncated header (want %d, got %d)'
                                % (end, len(head)))
    raw_hdr = head[no:end]
    names = None
    encrypted = None
    errors = []
    for enc in (True, False):
        buf = _decrypt(raw_hdr, key) if enc else raw_hdr
        try:
            names, _used = _parse_names(buf, summary.name_count)
        except HeadError as exc:
            # the reason can carry a decrypted garbage name (a few kB): keep
            # the index small, the detail is worthless anyway.
            errors.append('%s: %s' % ('aes-ecb' if enc else 'plaintext',
                                      _clip(str(exc))))
            continue
        encrypted = enc
        break
    if names is None:
        raise HeadError('name table not readable (%s)' % '; '.join(errors))
    hdr = _decrypt(raw_hdr, key) if encrypted else raw_hdr

    imports = _parse_imports(hdr[summary.import_offset - no:],
                             summary.import_count)
    exports = None
    entry = None
    for entry_size, parser in ((EXPORT_ENTRY_72, _parse_exports72),
                               (EXPORT_ENTRY_64, _parse_exports68)):
        try:
            cand = parser(hdr[summary.export_offset - no:], summary.export_count)
        except struct.error:
            continue
        if _validate_exports(names, summary.name_count, cand,
                              summary.import_count):
            exports = cand
            entry = entry_size
            break
    if exports is None:
        raise HeadError('export table not readable (neither the 72-byte nor '
                        'the 68-byte licensee layout parses)')
    return HeadPackage(path, summary, names, imports, exports, encrypted, entry)


# ------------------------------------------------------------------- the index
def _guard_out(path):
    """Refuse to write the index inside the repository tree (Rule 1)."""
    real = os.path.realpath(os.path.abspath(os.path.expanduser(path)))
    repo = os.path.realpath(_REPO)
    if real == repo or real.startswith(repo + os.sep):
        raise SystemExit('refusing to write the package index inside the repo '
                         'tree: %s' % real)
    return real


def index_install(directory, workers=8, progress=True, quiet=False):
    """Scan every `.upk` of `directory` (head only) and build the index.

    Returns a dict:
        'version'      index format version
        'root'         the scanned directory
        'files'        {file path: per-file metadata}
        'packages'     {package name: [file paths that declare it]}
        'report'       counters + the list of unreadable / non-UE3 files
    """
    directory = os.path.abspath(os.path.expanduser(directory))
    names = sorted(n for n in os.listdir(directory) if n.lower().endswith('.upk'))
    t0 = time.time()
    files = {}
    unreadable = []
    non_ue3 = []
    packages = defaultdict(list)
    key = rlupk.load_key()
    done = [0]

    def one(fname):
        path = os.path.join(directory, fname)
        try:
            head = read_head(path, key)
        except HeadError as exc:
            return fname, None, str(exc)
        except (OSError, struct.error, ValueError) as exc:
            return fname, None, '%s: %s' % (type(exc).__name__, exc)
        lp = head.linked_packages()
        meta = {
            'file_version': head.summary.file_version,
            'licensee': head.summary.licensee,
            'encrypted': head.encrypted,
            'header_size': head.summary.header_size,
            'name_count': head.summary.name_count,
            'import_count': head.summary.import_count,
            'export_count': head.summary.export_count,
            'export_entry': head.export_entry,
            'placeholders': len(head.placeholders()),
            'packages': lp,
        }
        return fname, meta, None

    results = []
    if workers and workers > 1:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for res in pool.map(one, names):
                results.append(res)
                done[0] += 1
                if progress and done[0] % 2000 == 0:
                    print('  scanned %d/%d (%.1fs)' % (done[0], len(names),
                                                       time.time() - t0),
                          flush=True)
    else:
        for n in names:
            results.append(one(n))
            done[0] += 1
            if progress and done[0] % 2000 == 0:
                print('  scanned %d/%d (%.1fs)' % (done[0], len(names),
                                                   time.time() - t0), flush=True)

    for fname, meta, err in results:
        if meta is None:
            if err and 'not a UE3 package' in err:
                non_ue3.append({'file': fname, 'reason': err})
            else:
                unreadable.append({'file': fname, 'reason': err})
            continue
        files[fname] = meta
        for p in meta['packages']:
            packages[p].append(fname)

    elapsed = time.time() - t0
    report = {
        'files_scanned': len(names),
        'files_indexed': len(files),
        'unreadable': len(unreadable),
        'non_ue3': len(non_ue3),
        'packages_indexed': len(packages),
        'duplicate_package_names': sum(1 for v in packages.values() if len(v) > 1),
        'elapsed_seconds': round(elapsed, 2),
        'unreadable_files': unreadable,
        'non_ue3_files': non_ue3,
    }
    return {
        'version': INDEX_VERSION,
        'root': directory,
        'files': files,
        'packages': {k: sorted(v) for k, v in sorted(packages.items())},
        'report': report,
    }


def save_index(index, path):
    path = _guard_out(path)
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(path, 'w') as fh:
        json.dump(index, fh, indent=1, sort_keys=True)
    return path


def load_index(path):
    with open(path) as fh:
        return json.load(fh)


# --------------------------------------------------------------- sibling access
def resolve_package(internal_name, index):
    """The file path(s) that declare the package `internal_name`."""
    pkgs = index.get('packages') or {}
    root = index.get('root') or ''
    return [os.path.join(root, f) for f in pkgs.get(internal_name, [])]


def provider_files(internal_name, index):
    """The file names (relative to the install root) that declare a package."""
    return list((index.get('packages') or {}).get(internal_name, []))


_OPENED = {}


def open_sibling(name, index, prefer=None):
    """Open a sibling package that provides `name`; returns (path, Package).

    The provider is NOT unique (a linked package is duplicated into every
    file of the group — module docstring section 4), so the *smallest*
    provider is opened: it is the cheapest to read and inflates fastest.
    `prefer` forces a specific file name.
    """
    if name in _OPENED:
        return _OPENED[name]
    root = index.get('root') or ''
    cands = provider_files(name, index)
    if prefer:
        cands = [prefer] + [c for c in cands if c != prefer]
    if not cands:
        raise KeyError('no file in the install declares the package %r' % name)
    cands = sorted(cands, key=lambda f: os.path.getsize(os.path.join(root, f))
                   if os.path.exists(os.path.join(root, f)) else 0)
    last = None
    for fname in cands:
        path = os.path.join(root, fname)
        try:
            pkg = rlupk.Package(path)
        except Exception as exc:                       # noqa: BLE001
            last = exc
            continue
        _OPENED[name] = (path, pkg)
        return path, pkg
    raise KeyError('could not open a provider of %r (%s)' % (name, last))


def import_package_name(pkg, import_index):
    """The *internal* package name of an import of `pkg`.

    MEASURED (module docstring section 5): the import's `PackageIndex` is its
    OUTER.  A negative value points at another import of the same file, whose
    `ObjectName` (an FName index into the importing file's name table) is the
    package's own name; a positive value points at a `Package` export of the
    same file (the linked-package group) and its name is the package's name.
    """
    if not (0 <= import_index < len(pkg.imports)):
        return None
    imp = pkg.imports[import_index]
    pi = imp.package_index
    if pi < 0:
        j = -pi - 1
        if 0 <= j < len(pkg.imports):
            return pkg.import_name(j)
        return None
    if 0 < pi <= len(pkg.exports):
        return pkg.export_name(pkg.exports[pi - 1])
    return None


def find_object(name, index, cls=None, outer=None, package=None):
    """Locate an export by name across the install (head only).

    Returns (file name, export dict, HeadPackage) for the first provider that
    matches, preferring the smallest file.  Only the HEAD of the candidate
    files is read; the body is left alone.
    """
    root = index.get('root') or ''
    if package:
        cands = provider_files(package, index)
    else:
        cands = list((index.get('files') or {}).keys())
    cands = sorted(cands, key=lambda f: os.path.getsize(os.path.join(root, f))
                   if os.path.exists(os.path.join(root, f)) else 0)
    for fname in cands:
        path = os.path.join(root, fname)
        try:
            head = read_head(path)
        except (HeadError, OSError):
            continue
        hits = head.objects(name, cls=cls, outer=outer)
        if hits:
            return fname, hits[0], head
    return None, None, None


# ------------------------------------------------------------- sibling resolver
class SiblingResolver(object):
    """Resolve an IMPORTED object to the sibling package that holds it.

    MEASURED (module docstring sections 3-5): an import names its package,
    and the linked package is DUPLICATED into every file of the group.  The
    resolver therefore scans the candidate providers **in increasing file
    size** (the cheapest read) and stops as soon as every requested name is
    found.  Only the HEAD of a candidate is read; the body is opened later,
    and only for the file that actually holds the object.
    """

    def __init__(self, index):
        self.index = index
        self.root = index.get('root') or ''
        self._heads = {}            # file name -> HeadPackage (LRU-ish)
        self._opened = {}           # file name -> rlupk.Package
        self._cache = {}            # (package, name) -> (file, export) or None
        self.stats = Counter()

    # -- head access -------------------------------------------------------
    def head(self, fname):
        h = self._heads.get(fname)
        if h is None:
            try:
                h = read_head(os.path.join(self.root, fname))
            except (HeadError, OSError):
                h = False
            self._heads[fname] = h
            if len(self._heads) > 12:
                for k in list(self._heads)[:4]:
                    if k != fname:
                        self._heads.pop(k, None)
        return h or None

    def _candidates(self, package):
        if package:
            cands = provider_files(package, self.index)
        else:
            cands = list((self.index.get('files') or {}).keys())
        def size(f):
            p = os.path.join(self.root, f)
            try:
                return os.path.getsize(p)
            except OSError:
                return 0
        return sorted(cands, key=size)

    # -- lookup ------------------------------------------------------------
    def locate_many(self, package, names, cls=None):
        """{name: (file name, export dict)} for the objects of a package.

        Scans the providers of `package` (smallest file first) and stops as
        soon as every name in `names` has been found.
        """
        want = set(n for n in names if n)
        out = {}
        if not want:
            return out
        for n in want:
            hit = self._cache.get((package, n))
            if hit:
                out[n] = hit
        missing = want - set(out)
        if not missing:
            return out
        for fname in self._candidates(package):
            head = self.head(fname)
            if head is None:
                continue
            self.stats['files_scanned'] += 1
            for n in list(missing):
                hits = head.objects(n, cls=cls, outer=package)
                if not hits and package:
                    hits = head.objects(n, cls=cls)
                if hits:
                    out[n] = (fname, hits[0])
                    self._cache[(package, n)] = (fname, hits[0])
                    missing.discard(n)
            if not missing:
                break
        for n in missing:
            self._cache[(package, n)] = None
        return out

    def locate(self, package, name, cls=None):
        hit = self._cache.get((package, name))
        if hit is not None:
            return hit
        got = self.locate_many(package, [name], cls=cls)
        return got.get(name)

    # -- body access -------------------------------------------------------
    def open_file(self, fname):
        """Open the FULL package (body included) of a provider file."""
        pkg = self._opened.get(fname)
        if pkg is None:
            pkg = rlupk.Package(os.path.join(self.root, fname))
            self._opened[fname] = pkg
        return pkg

    def export_index(self, fname, name, cls=None):
        """The export index of `name` inside the opened package `fname`."""
        pkg = self.open_file(fname)
        for e in pkg.exports:
            if pkg.export_name(e) != name:
                continue
            if cls is not None and pkg.export_class_name(e) != cls:
                continue
            return e
        return None



# ------------------------------------------------------------------------- CLI
def main(argv=None):
    ap = argparse.ArgumentParser(
        description='Index the user\'s cooked Rocket League packages (A3b).')
    ap.add_argument('--index', metavar='DIR',
                    help='scan this install directory and write the index')
    ap.add_argument('--json', default='/tmp/rl_a3b/pkgindex.json',
                    help='where to write the index JSON (must be outside the repo)')
    ap.add_argument('--load', metavar='JSON',
                    help='load a previously written index JSON')
    ap.add_argument('--lookup', metavar='NAME',
                    help='print the files that declare the package NAME')
    ap.add_argument('--object', metavar='NAME',
                    help='print the files that hold an export named NAME')
    ap.add_argument('--workers', type=int, default=8,
                    help='parallel readers (default 8)')
    ap.add_argument('--quiet', action='store_true', help='no progress lines')
    args = ap.parse_args(argv)

    index = None
    if args.index:
        print('indexing %s ...' % os.path.abspath(os.path.expanduser(args.index)))
        index = index_install(args.index, workers=args.workers,
                              progress=not args.quiet)
        rep = index['report']
        print('files scanned      : %d' % rep['files_scanned'])
        print('packages indexed   : %d' % rep['packages_indexed'])
        print('files indexed      : %d' % rep['files_indexed'])
        print('unreadable         : %d' % rep['unreadable'])
        print('non-UE3 files      : %d' % rep['non_ue3'])
        print('package names seen : %d (%.1f%% declare more than one file)'
              % (rep['packages_indexed'], 100.0 * rep['duplicate_package_names']
                 / max(1, rep['packages_indexed'])))
        print('elapsed            : %.1fs' % rep['elapsed_seconds'])
        if rep['unreadable']:
            print('unreadable files (first 10):')
            for r in rep['unreadable_files'][:10]:
                print('    %-36s %s' % (r['file'], r['reason']))
        path = save_index(index, args.json)
        print('wrote index %s' % path)
    elif args.load:
        index = load_index(args.load)
        print('loaded index %s (%d files, %d packages)'
              % (args.load, len(index.get('files') or {}),
                 len(index.get('packages') or {})))
    else:
        ap.error('one of --index or --load is required')

    if args.lookup:
        paths = resolve_package(args.lookup, index)
        if not paths:
            print('package %r: NOT FOUND in the index' % args.lookup)
        else:
            print('package %r -> %d file(s):' % (args.lookup, len(paths)))
            for p in paths:
                print('    %s' % p)
    if args.object:
        fname, export, head = find_object(args.object, index)
        if fname is None:
            print('object %r: NOT FOUND' % args.object)
        else:
            print('object %r -> %s export#%d class=%s outer=%s'
                  % (args.object, fname, export['class_index'],
                     head.export_class(export), head.export_outer(export)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
