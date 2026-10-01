#!/usr/bin/env bash
# Install Debian/Ubuntu packages required to configure and compile Illumo.
# CMake runs this on Linux when ILLUMO_INSTALL_LINUX_DEPS is ON (the default).
# Safe to re-run: already-installed packages are skipped.

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: install-linux-deps.sh [--tidy] [--docs] [--dry-run] [--help]

Installs apt packages Illumo needs on Debian/Ubuntu:
  compiler/toolchain, pkg-config, OpenGL/GLX (CMake 4 FindOpenGL),
  X11 development libraries for vendored GLFW, and gtkmm-3 for
  native dialogs/clipboard.

  --tidy     also install clang-tidy (CMake default gate)
  --docs     also install latexmk (PDF docs; does not pull a full TeX live)
  --dry-run  print missing packages without installing
EOF
}

WANT_TIDY=0
WANT_DOCS=0
DRY_RUN=0
for argument in "$@"; do
  case "$argument" in
    --tidy) WANT_TIDY=1 ;;
    --docs) WANT_DOCS=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $argument" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! command -v apt-get >/dev/null 2>&1; then
  echo "Illumo Linux deps: apt-get not found; skipping (not Debian/Ubuntu)."
  exit 0
fi

# Concrete package names so dpkg-query is reliable (avoid virtual packages).
PACKAGES=(
  build-essential
  ninja-build
  pkg-config
  python3
  libgl1-mesa-dev
  libglx-mesa-dev
  libopengl-dev
  libx11-dev
  libxrandr-dev
  libxinerama-dev
  libxcursor-dev
  libxi-dev
  libxext-dev
  libxfixes-dev
  libxxf86vm-dev
  libxkbcommon-dev
  libgtkmm-3.0-dev
  mesa-utils
)

if [ "$WANT_TIDY" -eq 1 ]; then
  PACKAGES+=(clang-tidy)
fi
if [ "$WANT_DOCS" -eq 1 ]; then
  PACKAGES+=(latexmk)
fi

package_installed() {
  dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'
}

package_known() {
  apt-cache show "$1" >/dev/null 2>&1
}

MISSING=()
SKIPPED=()
for package in "${PACKAGES[@]}"; do
  if package_installed "$package"; then
    continue
  fi
  if ! package_known "$package"; then
    SKIPPED+=("$package")
    continue
  fi
  MISSING+=("$package")
done

if [ "${#SKIPPED[@]}" -ne 0 ]; then
  echo "Illumo Linux deps: not in this apt archive (skipped): ${SKIPPED[*]}"
fi

if [ "${#MISSING[@]}" -eq 0 ]; then
  echo "Illumo Linux deps: required apt packages are already installed."
  exit 0
fi

echo "Illumo Linux deps: missing: ${MISSING[*]}"

if [ "$DRY_RUN" -eq 1 ]; then
  echo "dry-run: sudo apt-get update && sudo apt-get install --no-install-recommends -y ${MISSING[*]}"
  exit 0
fi

run_apt() {
  "$@" apt-get update -qq
  "$@" apt-get install --no-install-recommends -y "${MISSING[@]}"
}

if [ "$(id -u)" -eq 0 ]; then
  run_apt
  exit 0
fi

if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
  run_apt sudo -n
  exit 0
fi

cat >&2 <<EOF
Illumo Linux deps: cannot install automatically (need root or passwordless sudo).
From a terminal run:

  sudo apt-get update
  sudo apt-get install --no-install-recommends -y ${MISSING[*]}

Then re-run CMake. To skip this helper: -DILLUMO_INSTALL_LINUX_DEPS=OFF
EOF
exit 1
