#!/usr/bin/env bash
set -euo pipefail

ARECA_ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ARECA_BUILD_DIR="${BUILD_DIR:-$ARECA_ROOT_DIR/build}"
ARECA_PREFIX="${PREFIX:-/usr}"
ARECA_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
ARECA_TARGET_USER="${SUDO_USER:-${USER:-}}"
ARECA_INSTALL_DEPS=1
ARECA_UPDATE_SYSTEM=0
ARECA_RESTART_FCITX=1
ARECA_RUN_TESTS=1

areca_target_home() {
  if [[ -n "$ARECA_TARGET_USER" ]] && command -v getent >/dev/null 2>&1; then
    getent passwd "$ARECA_TARGET_USER" | cut -d: -f6
    return
  fi
  printf '%s\n' "${HOME:?HOME is required}"
}

ARECA_TARGET_HOME="$(areca_target_home)"

is_user_prefix() {
  [[ "$ARECA_PREFIX" == "$ARECA_TARGET_HOME" ||
     "$ARECA_PREFIX" == "$ARECA_TARGET_HOME/"* ]]
}

run_as_target_user() {
  if [[ "$(id -un)" == "$ARECA_TARGET_USER" ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    local target_uid
    target_uid="$(id -u "$ARECA_TARGET_USER")"
    sudo -H -u "$ARECA_TARGET_USER" env \
      XDG_RUNTIME_DIR="/run/user/$target_uid" \
      DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$target_uid/bus" \
      "$@"
  else
    return 1
  fi
}

update_icon_cache() {
  if ! command -v gtk-update-icon-cache >/dev/null 2>&1; then
    return
  fi
  local icon_theme_dir="$ARECA_PREFIX/share/icons/hicolor"
  if [[ ! -d "$icon_theme_dir" ]]; then
    return
  fi
  echo "[areca] Updating hicolor icon cache (optional)"
  if is_user_prefix; then
    gtk-update-icon-cache -f -t "$icon_theme_dir" >/dev/null 2>&1 || true
  else
    sudo gtk-update-icon-cache -f -t "$icon_theme_dir" >/dev/null 2>&1 || true
  fi
}

update_desktop_cache() {
  local applications_dir="$ARECA_PREFIX/share/applications"
  if [[ -d "$applications_dir" ]] && \
     command -v update-desktop-database >/dev/null 2>&1; then
    echo "[areca] Updating desktop application cache (optional)"
    if is_user_prefix; then
      update-desktop-database "$applications_dir" >/dev/null 2>&1 || true
    else
      sudo update-desktop-database "$applications_dir" >/dev/null 2>&1 || true
    fi
  fi

  if command -v kbuildsycoca6 >/dev/null 2>&1; then
    echo "[areca] Refreshing KDE application cache (optional)"
    run_as_target_user kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
  elif command -v kbuildsycoca5 >/dev/null 2>&1; then
    echo "[areca] Refreshing KDE application cache (optional)"
    run_as_target_user kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
  fi
}

install_deps_debian() {
  echo "[areca] Installing build dependencies with apt"
  sudo apt-get update
  local common=(build-essential cmake ninja-build pkg-config extra-cmake-modules
                golang-go libinput-dev libudev-dev libdbus-1-dev libx11-dev
                libxtst-dev libfontconfig-dev)
  local fcitx=(fcitx5 fcitx5-config-qt libfcitx5core-dev
               libfcitx5config-dev libfcitx5utils-dev)
  if ! sudo apt-get install -y "${common[@]}" "${fcitx[@]}"; then
    echo "[areca] Individual Fcitx development packages unavailable; trying fcitx5-dev"
    sudo apt-get install -y "${common[@]}" fcitx5 fcitx5-config-qt fcitx5-dev
  fi

  # SDL3 is not available in the repositories of several supported Ubuntu
  # bases (including Linux Mint 21 and 22). Build it when the distro package is absent.
  if ! sudo apt-get install -y libsdl3-dev; then
    echo "[areca] libsdl3-dev is unavailable; building SDL3 from source"
    sudo apt-get install -y git libxext-dev libxrandr-dev libxcursor-dev \
      libxfixes-dev libxi-dev libxss-dev libxkbcommon-dev libwayland-dev \
      wayland-protocols libdecor-0-dev libdrm-dev libgbm-dev \
      libgl1-mesa-dev libegl1-mesa-dev libibus-1.0-dev libpulse-dev \
      libasound2-dev || true
    local sdl_src sdl_build
    sdl_src="$(mktemp -d)"
    sdl_build="$(mktemp -d)"
    if git clone --depth 1 --branch release-3.2.x \
      https://github.com/libsdl-org/SDL.git "$sdl_src"; then
      local sdl_gen=()
      if command -v ninja >/dev/null 2>&1; then
        sdl_gen=(-G Ninja)
      fi
      cmake -S "$sdl_src" -B "$sdl_build" "${sdl_gen[@]}" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF
      cmake --build "$sdl_build" --parallel
      sudo cmake --install "$sdl_build"
      sudo ldconfig
    else
      echo "[areca] Warning: git clone SDL3 failed; CMake will attempt FetchContent during build" >&2
    fi
    rm -rf -- "$sdl_src" "$sdl_build"
  fi
}

install_deps_arch() {
  local packages=(base-devel cmake ninja pkgconf extra-cmake-modules go
                  libinput systemd-libs dbus libx11 libxtst fcitx5
                  fcitx5-configtool sdl3 fontconfig)
  if [[ "$ARECA_UPDATE_SYSTEM" == 1 ]]; then
    echo "[areca] Updating Arch system and installing build dependencies"
    sudo pacman -Syu --needed --noconfirm "${packages[@]}"
    return
  fi

  # Không dùng pacman -Sy: refresh database rồi cài package mà không nâng toàn
  # hệ thống là partial upgrade và không được Arch hỗ trợ.
  echo "[areca] Installing Arch dependencies without refreshing or upgrading the system"
  if ! sudo pacman -S --needed --noconfirm "${packages[@]}"; then
    cat >&2 <<EOF
[areca] Dependency installation failed without a system update.
[areca] If dependencies are already installed, rerun with --skip-deps.
[areca] To perform a supported full Arch upgrade, rerun with --system-update.
EOF
    return 1
  fi
}

install_deps_fedora() {
  echo "[areca] Installing build dependencies with dnf"
  sudo dnf install -y \
    gcc-c++ cmake ninja-build pkgconf-pkg-config extra-cmake-modules go \
    libinput-devel systemd-devel dbus-devel SDL3-devel fontconfig-devel \
    libX11-devel libXtst-devel fcitx5 fcitx5-devel fcitx5-configtool
}

install_deps_opensuse() {
  echo "[areca] Installing build dependencies with zypper"
  sudo zypper --non-interactive install --no-recommends \
    gcc-c++ cmake ninja pkg-config extra-cmake-modules go \
    libinput-devel systemd-devel fcitx5 fcitx5-devel SDL3-devel \
    fontconfig-devel libX11-devel libXtst-devel dbus-1-devel libudev-devel
}

distro_matches() {
  local needle="$1" token
  for token in ${ID:-} ${ID_LIKE:-}; do
    [[ "$token" == "$needle" ]] && return 0
  done
  return 1
}

install_build_deps() {
  if [[ "$(uname -s)" != "Linux" || ! -r /etc/os-release ]]; then
    echo "[areca] Skipping dependency installation: unsupported host"
    return
  fi

  # shellcheck disable=SC1091
  . /etc/os-release
  if distro_matches nixos; then
    if [[ -z "${IN_NIX_SHELL:-}" ]]; then
      echo "[areca] NixOS dependencies are declared in shell.nix; run inside nix-shell" >&2
      echo "[areca] Example: nix-shell --run './scripts/install.sh --skip-deps --user'" >&2
      return 1
    fi
    echo "[areca] Using build dependencies from nix-shell"
    return
  fi
  if command -v apt-get >/dev/null 2>&1; then
    if distro_matches ubuntu || distro_matches debian || distro_matches linuxmint || distro_matches mint; then
      install_deps_debian
      return
    fi
  fi
  if command -v pacman >/dev/null 2>&1; then
    if distro_matches arch; then install_deps_arch; return; fi
  fi
  if command -v dnf >/dev/null 2>&1; then
    if distro_matches fedora || distro_matches rhel || distro_matches centos; then
      install_deps_fedora
      return
    fi
  fi
  if command -v zypper >/dev/null 2>&1; then
    if distro_matches opensuse || distro_matches suse; then
      install_deps_opensuse
      return
    fi
  fi
  echo "[areca] Skipping dependency installation for ${PRETTY_NAME:-unknown distro}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      ARECA_PREFIX="$ARECA_TARGET_HOME/.local"
      shift
      ;;
    --prefix)
      ARECA_PREFIX="${2:?missing value for --prefix}"
      shift 2
      ;;
    --build-dir)
      ARECA_BUILD_DIR="${2:?missing value for --build-dir}"
      shift 2
      ;;
    --build-type)
      ARECA_BUILD_TYPE="${2:?missing value for --build-type}"
      shift 2
      ;;
    --skip-deps)
      ARECA_INSTALL_DEPS=0
      shift
      ;;
    --no-system-update)
      ARECA_UPDATE_SYSTEM=0
      shift
      ;;
    --system-update)
      ARECA_UPDATE_SYSTEM=1
      shift
      ;;
    --skip-tests)
      ARECA_RUN_TESTS=0
      shift
      ;;
    --no-restart)
      ARECA_RESTART_FCITX=0
      shift
      ;;
    -h|--help)
      cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --user                 Install to $ARECA_TARGET_HOME/.local without sudo
  --prefix PATH          Installation prefix (default: /usr)
  --build-dir PATH       CMake build directory (default: ./build)
  --build-type TYPE      CMake build type (default: RelWithDebInfo)
  --skip-deps            Do not install distro build dependencies
  --no-system-update     Install Arch dependencies without pacman -Sy (default)
  --system-update        Run pacman -Syu before building on Arch
  --skip-tests           Do not run CTest
  --no-restart           Do not attempt to restart Fcitx5

Environment:
  PREFIX=/usr
  BUILD_DIR=./build
  BUILD_TYPE=RelWithDebInfo
EOF
      exit 0
      ;;
    *)
      echo "[areca] Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

case "$ARECA_PREFIX" in
  "~") ARECA_PREFIX="$ARECA_TARGET_HOME" ;;
  "~/"*) ARECA_PREFIX="$ARECA_TARGET_HOME/${ARECA_PREFIX#"~/"}" ;;
esac

echo "[areca] root=$ARECA_ROOT_DIR"
echo "[areca] build=$ARECA_BUILD_DIR prefix=$ARECA_PREFIX type=$ARECA_BUILD_TYPE"

if [[ "$ARECA_INSTALL_DEPS" == 1 ]]; then
  if is_user_prefix; then
    echo "[areca] User install selected; skipping system dependency installation"
  else
    install_build_deps
  fi
fi

if [[ -d "$ARECA_ROOT_DIR/.git" ]]; then
  echo "[areca] Initializing Bamboo submodule"
  git -C "$ARECA_ROOT_DIR" submodule update --init
fi

# Respect an existing CMake generator. A common failure is an old build cache
# configured with Ninja on a machine where ninja is no longer installed. Do
# not delete that cache; select a sibling Makefiles build directory instead.
ARECA_CMAKE_GENERATOR=""
if [[ -f "$ARECA_BUILD_DIR/CMakeCache.txt" ]]; then
  ARECA_CACHED_GENERATOR="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' \
    "$ARECA_BUILD_DIR/CMakeCache.txt" | head -n 1)"
  if [[ "$ARECA_CACHED_GENERATOR" == "Ninja" ]] && \
     ! command -v ninja >/dev/null 2>&1; then
    echo "[areca] Existing build cache requires Ninja, but ninja is unavailable"
    ARECA_BUILD_DIR="${ARECA_BUILD_DIR}-make"
    ARECA_CMAKE_GENERATOR="Unix Makefiles"
    echo "[areca] Using fallback build directory: $ARECA_BUILD_DIR"
  fi
elif command -v ninja >/dev/null 2>&1; then
  ARECA_CMAKE_GENERATOR="Ninja"
elif command -v make >/dev/null 2>&1; then
  ARECA_CMAKE_GENERATOR="Unix Makefiles"
else
  echo "[areca] No build program found; install ninja or make" >&2
  exit 1
fi

ARECA_GENERATOR_ARGS=()
if [[ -n "$ARECA_CMAKE_GENERATOR" ]]; then
  ARECA_GENERATOR_ARGS=(-G "$ARECA_CMAKE_GENERATOR")
fi

if ! command -v c++ >/dev/null 2>&1 && \
   ! command -v g++ >/dev/null 2>&1 && \
   ! command -v clang++ >/dev/null 2>&1; then
  echo "[areca] No C++ compiler found; install g++ or clang++" >&2
  exit 1
fi

export CMAKE_PREFIX_PATH="/usr/local:${CMAKE_PREFIX_PATH:-}"
export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/local/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"

cmake -S "$ARECA_ROOT_DIR" -B "$ARECA_BUILD_DIR" \
  "${ARECA_GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$ARECA_BUILD_TYPE" \
  -DCMAKE_INSTALL_PREFIX="$ARECA_PREFIX" \
  -DARECA_BUILD_SETTINGS=ON

echo "[areca] Building"
cmake --build "$ARECA_BUILD_DIR" -j

if [[ "$ARECA_RUN_TESTS" == 1 ]]; then
  echo "[areca] Running C++ tests"
  ctest --test-dir "$ARECA_BUILD_DIR" --output-on-failure
fi

if is_user_prefix; then
  echo "[areca] Installing to user prefix"
  cmake --install "$ARECA_BUILD_DIR"
else
  echo "[areca] Installing with sudo"
  sudo cmake --install "$ARECA_BUILD_DIR"
  if [[ -n "$ARECA_TARGET_USER" ]] && command -v usermod >/dev/null 2>&1; then
    echo "[areca] Configuring /dev/uinput group permissions for $ARECA_TARGET_USER"
    sudo usermod -aG input "$ARECA_TARGET_USER" || true
    sudo udevadm control --reload-rules >/dev/null 2>&1 && sudo udevadm trigger >/dev/null 2>&1 || true
  fi
fi

update_icon_cache
update_desktop_cache

if [[ "$ARECA_RESTART_FCITX" == 1 ]]; then
  echo "[areca] Restarting Fcitx5 (best effort)"
  run_as_target_user fcitx5 -rd >/dev/null 2>&1 || true
fi

cat <<EOF
[areca] Done.

Next:
  - Open fcitx5-configtool and add "Areca (Bamboo)".
  - If Plasma Wayland keeps the old addon in KWin, log out and log in once.
  - Addon log:  journalctl --user -f | grep areca
EOF
