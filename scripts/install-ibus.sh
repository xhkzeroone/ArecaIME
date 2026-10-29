#!/usr/bin/env bash
set -euo pipefail

ARECA_ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ARECA_BUILD_DIR="${BUILD_DIR:-$ARECA_ROOT_DIR/build-ibus-release}"
ARECA_PREFIX="${PREFIX:-/usr}"
ARECA_INSTALL_DEPS=1
ARECA_TARGET_USER="${SUDO_USER:-${USER:-}}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      ARECA_PREFIX="${2:?missing value for --prefix}"
      shift 2
      ;;
    --build-dir)
      ARECA_BUILD_DIR="${2:?missing value for --build-dir}"
      shift 2
      ;;
    --skip-deps)
      ARECA_INSTALL_DEPS=0
      shift
      ;;
    -h|--help)
      cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --prefix PATH       Installation prefix (default: /usr)
  --build-dir PATH    CMake build directory
  --skip-deps         Do not install build dependencies
EOF
      exit 0
      ;;
    *)
      echo "[ibus-areca] Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

install_dependencies() {
  # shellcheck disable=SC1091
  . /etc/os-release
  if command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed --noconfirm base-devel cmake ninja go pkgconf ibus
  elif command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y build-essential cmake ninja-build golang-go \
      pkg-config ibus libibus-1.0-dev
  elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y gcc-c++ cmake ninja-build golang pkgconf-pkg-config \
      ibus ibus-devel
  elif command -v zypper >/dev/null 2>&1; then
    sudo zypper --non-interactive install --no-recommends gcc-c++ cmake ninja \
      go pkg-config ibus ibus-devel
  else
    echo "[ibus-areca] Unsupported package manager on ${PRETTY_NAME:-this system}" >&2
    return 1
  fi
}

switch_to_ibus() {
  echo "[ibus-areca] Stopping Fcitx5..."
  if command -v fcitx5-remote >/dev/null 2>&1; then
    fcitx5-remote -e >/dev/null 2>&1 || true
  fi
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user stop fcitx5.service org.fcitx.Fcitx5.service \
      >/dev/null 2>&1 || true
  fi
  pkill -x fcitx5 >/dev/null 2>&1 || true

  echo "[ibus-areca] Starting IBus..."
  if command -v ibus >/dev/null 2>&1; then
    ibus restart >/dev/null 2>&1 || true
  fi
  if ! pgrep -x ibus-daemon >/dev/null 2>&1; then
    ibus-daemon --daemonize --replace --xim
  fi
}

if [[ "$ARECA_INSTALL_DEPS" == 1 ]]; then
  install_dependencies
fi

if [[ -d "$ARECA_ROOT_DIR/.git" ]]; then
  git -C "$ARECA_ROOT_DIR" submodule update --init
fi

cmake -S "$ARECA_ROOT_DIR" -B "$ARECA_BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$ARECA_PREFIX" \
  -DBUILD_TESTING=OFF \
  -DARECA_BUILD_FCITX=OFF \
  -DARECA_BUILD_SETTINGS=OFF \
  -DARECA_BUILD_IBUS=ON
cmake --build "$ARECA_BUILD_DIR" --parallel

if [[ ! -x "$ARECA_BUILD_DIR/ibus-areca" ]]; then
  echo "[ibus-areca] libibus was not detected; ibus-areca was not built" >&2
  exit 1
fi
"$ARECA_BUILD_DIR/ibus-areca" --self-test

sudo cmake --install "$ARECA_BUILD_DIR"
sudo modprobe uinput || true
if [[ -n "$ARECA_TARGET_USER" ]] && command -v usermod >/dev/null 2>&1; then
  sudo usermod -aG input "$ARECA_TARGET_USER" || true
fi
sudo udevadm control --reload-rules || true
sudo udevadm trigger || true

switch_to_ibus

cat <<EOF
[ibus-areca] Done.

Next:
  - Log out and log in if the input group was added for the first time.
  - Fcitx5 was stopped and IBus was started for the current session.
  - Open the desktop Keyboard/Input Sources settings.
  - Add Vietnamese → Areca.
EOF
