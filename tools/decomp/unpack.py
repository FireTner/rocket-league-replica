"""Write a decrypted, inflated copy of a local .upk outside this repo.
Refuses a destination inside the tree. Without a local key, exits 3.
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
