# Local dev speedups. Source this shell; the overrides die with it.
# Usage: source tools/dev-local.sh
# CMake already launches ccache when it is on PATH. This pins the cache dir.

export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/ccache}"
if ! command -v ccache >/dev/null 2>&1; then
  echo "dev-local WARN: ccache not on PATH — builds will be full price" >&2
fi

# Optional mold linker, at configure time:
# cmake -B build -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold
# Left off by default; checksums come from tick math, not link order.

# rtk routing for eligible git subcommands, with fallback to real git.
# Interactive shells only (guarded by caller); pipes/&&/redirects bypass.
git() {
  case "$1" in
    status|log|diff|show) rtk git "$@" || command git "$@" ;;
    *) command git "$@" ;;
  esac
}

echo "dev-local on: ccache launcher, git->rtk wrapper"
