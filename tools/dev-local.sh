# Local-only dev speedups for RocketLeague — source this, don't commit shell globals.
# Usage: source tools/dev-local.sh   (per-shell, dies with the shell)
# Does NOT touch the Makefile (g++ only, no cmake per AGENTS.md).
# CXX ?= in the Makefile means these env overrides win without edits.

export CC="ccache gcc"
export CXX="ccache g++"

# ccache is only a speedup if it actually caches: history showed 0 hits /
# 0.0 GiB (cold /tmp + wiped cache), so every `make all` paid full price.
# Pin a persistent dir and warn once when ccache is missing or cold.
export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/ccache}"
if ! command -v ccache >/dev/null 2>&1; then
  echo "dev-local WARN: ccache not on PATH — builds will be full price" >&2
fi

# Parallel builds by default: `make all` -> `make -j$(nproc) all`
make() {
  if [[ "$*" != *"-j"* ]]; then
    command make -j"$(nproc)" "$@"
  else
    command make "$@"
  fi
}

# Optional mold linker: `make LDFLAGS="-fuse-ld=mold" all`
# Left off by default; checksums come from tick math, not link order,
# but drop mold first if any checksum ever differs.

# rtk routing for eligible git subcommands, with fallback to real git.
# Interactive shells only (guarded by caller); pipes/&&/redirects bypass.
git() {
  case "$1" in
    status|log|diff|show) rtk git "$@" || command git "$@" ;;
    *) command git "$@" ;;
  esac
}

echo "dev-local on: CC=$CC CXX=$CXX, parallel make (-j$(nproc)), git->rtk wrapper"
