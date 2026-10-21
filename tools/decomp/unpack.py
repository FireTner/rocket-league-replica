"""Write a decrypted + inflated copy of an RL package for a UE3 reader.

Our own MIT tool (repo Rule 1): it reads a package from the user's own install
and writes the working copy OUTSIDE this repository, where UELib-style readers
can mmap it as one plaintext image.  No package bytes, no key and no decrypted
copy ever land in the tree — the destination is refused if it resolves inside
this repo (see docs/CAMERA_DECOMP.md for the legal boundary).

What the output is: the plaintext package summary, the AES-256-ECB decrypted
header (`[NameOffset, TotalHeaderSize)`), and every zlib chunk inflated in
place, i.e. `rlupk.Package(...).image`.  Export `SerialOffset` values are
already in that image's coordinate space (offset 0 = file summary).

Usage:
    python3 tools/decomp/unpack.py <src.upk> <dst.upk>
    python3 tools/decomp/unpack.py <src.upk>          # -> <src>_decrypted.upk

The package key is a LOCAL INPUT, never in this repo: put your own copy in
`~/.rl-decomp/upk.key` (64 hex digits) or export `$RL_UPK_KEY`.  Without one
this prints a loud SKIP and exits 3, so a public clone stays inert-but-runnable.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rlupk  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def default_dst(src):
    stem, ext = os.path.splitext(src)
    return f'{stem}_decrypted{ext or ".upk"}'


def inside_repo(path):
    real = os.path.realpath(path)
    root = os.path.realpath(REPO_ROOT) + os.sep
    return real == os.path.realpath(REPO_ROOT) or real.startswith(root)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description='Decrypt + inflate an RL .upk into a reader-friendly image.')
    ap.add_argument('src', help='package from your own install (never in-tree)')
    ap.add_argument('dst', nargs='?',
                    help='output image (default: <src>_decrypted.upk); must be '
                         'outside this repository')
    ap.add_argument('-q', '--quiet', action='store_true', help='no summary line')
    args = ap.parse_args(argv)

    src = os.path.expanduser(args.src)
    dst = os.path.expanduser(args.dst) if args.dst else default_dst(src)

    if not os.path.isfile(src):
        print(f'error: no such package: {src}', file=sys.stderr)
        return 2
    if inside_repo(dst):
        print('error: refusing to write package bytes inside the repository.\n'
              f'  destination: {dst}\n'
              f'  tree:        {REPO_ROOT}\n'
              'Psyonix data never lands in-tree (Rule 1) — write to e.g. '
              '~/rl-decomp/image/ or /decomp/ instead.', file=sys.stderr)
        return 2

    try:
        rlupk.load_key()          # fail with the key SKIP before touching bytes
    except rlupk.MissingKey as exc:
        print(f'SKIP: {exc}')
        return 3

    pkg = rlupk.Package(src, verbose=not args.quiet)
    os.makedirs(os.path.dirname(os.path.abspath(dst)) or '.', exist_ok=True)
    with open(dst, 'wb') as f:
        f.write(pkg.image)
    if not args.quiet:
        print(f'wrote {dst}: {len(pkg.image)} bytes '
              f'({pkg.name_count} names, {pkg.import_count} imports, '
              f'{pkg.export_count} exports, {len(pkg.chunks)} chunks, '
              f'UE3 {pkg.file_version}/licensee {pkg.licensee_version})')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
