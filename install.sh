#!/usr/bin/env bash
set -euo pipefail

prefix=/usr/local
builddir=build
build_only=0

usage() {
  cat <<'EOF'
Build and install this Noctalia fork.

Usage: ./install.sh [options]

  --prefix DIR     install prefix (default: /usr/local)
  --build-dir DIR  meson build directory (default: build)
  --build-only     compile without installing
  -h, --help       show this help

Noctalia's build dependencies are required; see BUILDING.md upstream.
The default prefix shadows a packaged noctalia through PATH. Restart the
shell after installing with:
  systemctl --user restart noctalia-shell.service
EOF
}

while (($#)); do
  case "$1" in
    --prefix)
      prefix=${2:?install.sh: --prefix needs a directory}
      shift 2
      ;;
    --build-dir)
      builddir=${2:?install.sh: --build-dir needs a directory}
      shift 2
      ;;
    --build-only)
      build_only=1
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "install.sh: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$root"

for tool in meson ninja pkg-config; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "install.sh: $tool is required" >&2
    exit 1
  fi
done

if [[ -d "$builddir" ]]; then
  meson setup --reconfigure "$builddir" . --buildtype=release --prefix="$prefix"
else
  meson setup "$builddir" . --buildtype=release --prefix="$prefix"
fi

ninja -C "$builddir"

if ((build_only)); then
  echo "built: $root/$builddir/noctalia"
  exit 0
fi

sudo_cmd=()
if ((EUID != 0)); then
  sudo_cmd=(sudo)
fi
"${sudo_cmd[@]}" meson install -C "$builddir"

echo
echo "Installed to $prefix."
echo "Restart the shell: systemctl --user restart noctalia-shell.service"
