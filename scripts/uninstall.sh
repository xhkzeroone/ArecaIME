#!/usr/bin/env bash
set -euo pipefail

ARECA_ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ARECA_BUILD_DIR="${BUILD_DIR:-$ARECA_ROOT_DIR/build}"
ARECA_PREFIX="${PREFIX:-/usr}"
ARECA_TARGET_USER="${SUDO_USER:-${USER:-}}"
ARECA_PURGE_DATA=1
ARECA_RESTART_FCITX=1
ARECA_USER_ONLY=0

areca_target_home() {
  if [[ -n "$ARECA_TARGET_USER" ]] && command -v getent >/dev/null 2>&1; then
    getent passwd "$ARECA_TARGET_USER" | cut -d: -f6
    return
  fi
  printf '%s\n' "${HOME:?HOME is required}"
}

ARECA_TARGET_HOME="$(areca_target_home)"

run_as_target_user() {
  if [[ -z "$ARECA_TARGET_USER" || "$(id -un)" == "$ARECA_TARGET_USER" ]]; then
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

run_as_root() {
  if [[ $EUID -eq 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    echo "[areca] Cần quyền sudo để xóa file hệ thống" >&2
    return 1
  fi
}

remove_path() {
  local target="$1"
  if [[ -z "$target" || "$target" == "/" ]]; then
    return
  fi
  if [[ -e "$target" || -L "$target" ]]; then
    echo "[areca] Removing $target"
    if [[ -w "$target" && -w "$(dirname "$target")" ]]; then
      rm -rf -- "$target"
    else
      run_as_root rm -rf -- "$target"
    fi
  fi
}

remove_manifest_files() {
  local manifest="$1"
  if [[ -f "$manifest" ]]; then
    echo "[areca] Checking manifest $manifest"
    while IFS= read -r line || [[ -n "$line" ]]; do
      if [[ -n "$line" && "$line" =~ areca ]]; then
        remove_path "$line"
      fi
    done < "$manifest"
  fi
}

remove_installed_files_in_prefix() {
  local prefix="$1"
  if [[ -z "$prefix" || ! -d "$prefix" ]]; then
    return
  fi

  remove_path "$prefix/bin/areca-settings"
  remove_path "$prefix/libexec/areca-mouse-monitor"
  remove_path "$prefix/libexec/areca-uinput-server"
  remove_path "$prefix/lib/areca-mouse-monitor"

  remove_path "$prefix/lib/fcitx5/areca.so"
  remove_path "$prefix/lib64/fcitx5/areca.so"
  for arch_lib in "$prefix"/lib/*/fcitx5/areca.so; do
    remove_path "$arch_lib"
  done

  remove_path "$prefix/share/applications/areca-settings.desktop"
  remove_path "$prefix/share/applications/io.github.areca.Settings.desktop"
  remove_path "$prefix/share/areca/gtk-settings"
  remove_path "$prefix/share/areca/icons/hicolor/scalable/apps/areca-settings.svg"
  remove_path "$prefix/bin/areca-settings"

  remove_path "$prefix/share/fcitx5/addon/areca.conf"
  remove_path "$prefix/share/fcitx5/inputmethod/areca.conf"

  remove_path "$prefix/share/pixmaps/areca-settings.svg"
  remove_path "$prefix/share/icons/hicolor/scalable/apps/fcitx-areca.svg"
  remove_path "$prefix/share/icons/hicolor/scalable/apps/areca.svg"
  remove_path "$prefix/share/icons/hicolor/scalable/apps/org.fcitx.Fcitx5.fcitx-areca.svg"
  remove_path "$prefix/share/icons/hicolor/scalable/apps/areca-settings.svg"
  remove_path "$prefix/share/icons/hicolor/symbolic/apps/fcitx-areca-symbolic.svg"
  remove_path "$prefix/share/icons/hicolor/symbolic/apps/areca-symbolic.svg"
  remove_path "$prefix/share/icons/hicolor/symbolic/apps/org.fcitx.Fcitx5.fcitx-areca-symbolic.svg"
  remove_path "$prefix/share/icons/char-white/apps/16/areca.svg"
  remove_path "$prefix/share/icons/char-white/apps/16/areca_72x72.svg"
  remove_path "$prefix/share/icons/char-white/apps/symbolic/areca-symbolic.svg"
  remove_path "$prefix/share/icons/char-white/apps/symbolic/areca_72x72-symbolic.svg"

  remove_path "$prefix/lib/udev/rules.d/70-areca-pointer.rules"
  remove_path "$prefix/lib/udev/rules.d/99-uinput-areca.rules"
  remove_path "$prefix/lib/udev/rules.d/99-areca-uinput.rules"

  remove_path "$prefix/lib/systemd/user/areca-uinput-server.service"
  remove_path "$prefix/lib/modules-load.d/areca-uinput.conf"
}

stop_systemd_service() {
  local service_name="areca-uinput-server.service"
  if command -v systemctl >/dev/null 2>&1; then
    echo "[areca] Stopping legacy systemd user service"
    run_as_target_user systemctl --user stop "$service_name" >/dev/null 2>&1 || true
    run_as_target_user systemctl --user disable "$service_name" >/dev/null 2>&1 || true
  fi
  remove_path "$ARECA_TARGET_HOME/.config/systemd/user/$service_name"
  remove_path "$ARECA_TARGET_HOME/.config/systemd/user/default.target.wants/$service_name"
  remove_path "/tmp/areca-uinput.sock"
}

reload_udev_rules() {
  if command -v udevadm >/dev/null 2>&1; then
    echo "[areca] Reloading udev rules"
    run_as_root udevadm control --reload-rules >/dev/null 2>&1 || true
    run_as_root udevadm trigger >/dev/null 2>&1 || true
  fi
}

update_caches() {
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    for icon_dir in "/usr/share/icons/hicolor" "$ARECA_TARGET_HOME/.local/share/icons/hicolor"; do
      if [[ -d "$icon_dir" ]]; then
        echo "[areca] Updating icon cache in $icon_dir"
        if [[ -w "$icon_dir" ]]; then
          gtk-update-icon-cache -f -t "$icon_dir" >/dev/null 2>&1 || true
        else
          run_as_root gtk-update-icon-cache -f -t "$icon_dir" >/dev/null 2>&1 || true
        fi
      fi
    done
  fi

  if command -v update-desktop-database >/dev/null 2>&1; then
    for app_dir in "/usr/share/applications" "$ARECA_TARGET_HOME/.local/share/applications"; do
      if [[ -d "$app_dir" ]]; then
        echo "[areca] Updating desktop database in $app_dir"
        if [[ -w "$app_dir" ]]; then
          update-desktop-database "$app_dir" >/dev/null 2>&1 || true
        else
          run_as_root update-desktop-database "$app_dir" >/dev/null 2>&1 || true
        fi
      fi
    done
  fi

  if command -v kbuildsycoca6 >/dev/null 2>&1; then
    echo "[areca] Refreshing KDE application cache"
    run_as_target_user kbuildsycoca6 --noincremental >/dev/null 2>&1 || true
  elif command -v kbuildsycoca5 >/dev/null 2>&1; then
    echo "[areca] Refreshing KDE application cache"
    run_as_target_user kbuildsycoca5 --noincremental >/dev/null 2>&1 || true
  fi
}

remove_user_data() {
  echo "[areca] Removing Areca user configuration and data"
  local config_dirs=()
  if [[ -n "${XDG_CONFIG_HOME:-}" ]]; then
    config_dirs+=("$XDG_CONFIG_HOME/fcitx5")
  fi
  config_dirs+=("$ARECA_TARGET_HOME/.config/fcitx5")

  for dir in "${config_dirs[@]}"; do
    remove_path "$dir/conf/areca.conf"
    remove_path "$dir/conf/areca-advanced.conf"
    remove_path "$dir/conf/areca-apps.conf"
    remove_path "$dir/conf/areca-macro-table.conf"
    remove_path "$dir/conf/areca-settings.conf"
    remove_path "$dir/areca-libei-restore-token"
  done
}

remove_distro_package() {
  if [[ "$ARECA_USER_ONLY" == 1 ]]; then
    return
  fi

  if command -v pacman >/dev/null 2>&1; then
    local arch_pkgs=()
    mapfile -t arch_pkgs < <(pacman -Qqs '^fcitx5-areca' 2>/dev/null || true)
    local prov
    prov="$(pacman -Qq fcitx5-areca 2>/dev/null || true)"
    if [[ -n "$prov" ]]; then
      arch_pkgs+=("$prov")
    fi

    local -A seen_arch=()
    for pkg in "${arch_pkgs[@]}"; do
      if [[ -n "$pkg" && -z "${seen_arch[$pkg]:-}" ]]; then
        seen_arch["$pkg"]=1
        echo "[areca] Phát hiện package $pkg đã cài đặt"
        if command -v paru >/dev/null 2>&1 && [[ "$(id -u)" != 0 ]]; then
          echo "[areca] Gỡ $pkg bằng paru"
          paru -R --noconfirm "$pkg" || run_as_root pacman -R --noconfirm "$pkg" || true
        elif command -v yay >/dev/null 2>&1 && [[ "$(id -u)" != 0 ]]; then
          echo "[areca] Gỡ $pkg bằng yay"
          yay -R --noconfirm "$pkg" || run_as_root pacman -R --noconfirm "$pkg" || true
        else
          echo "[areca] Gỡ $pkg bằng pacman"
          run_as_root pacman -R --noconfirm "$pkg" || true
        fi
      fi
    done
  fi

  if command -v dpkg >/dev/null 2>&1; then
    local deb_pkgs=()
    mapfile -t deb_pkgs < <(dpkg -l 'fcitx5-areca*' 2>/dev/null | awk '/^ii/ {print $2}' | cut -d: -f1 || true)
    local -A seen_deb=()
    for pkg in "${deb_pkgs[@]}"; do
      if [[ -n "$pkg" && -z "${seen_deb[$pkg]:-}" ]]; then
        seen_deb["$pkg"]=1
        echo "[areca] Phát hiện package $pkg đã cài đặt"
        if command -v apt-get >/dev/null 2>&1; then
          echo "[areca] Gỡ $pkg bằng apt-get"
          run_as_root apt-get remove --purge -y "$pkg" || true
        else
          echo "[areca] Gỡ $pkg bằng dpkg"
          run_as_root dpkg -P "$pkg" || true
        fi
      fi
    done
  fi

  if command -v rpm >/dev/null 2>&1; then
    local rpm_pkgs=()
    mapfile -t rpm_pkgs < <(rpm -qa --qf "%{NAME}\n" 'fcitx5-areca*' 2>/dev/null || true)
    local -A seen_rpm=()
    for pkg in "${rpm_pkgs[@]}"; do
      if [[ -n "$pkg" && -z "${seen_rpm[$pkg]:-}" ]]; then
        seen_rpm["$pkg"]=1
        echo "[areca] Phát hiện package $pkg đã cài đặt"
        if command -v dnf >/dev/null 2>&1; then
          echo "[areca] Gỡ $pkg bằng dnf"
          run_as_root dnf remove -y "$pkg" || true
        elif command -v zypper >/dev/null 2>&1; then
          echo "[areca] Gỡ $pkg bằng zypper"
          run_as_root zypper --non-interactive remove "$pkg" || true
        else
          echo "[areca] Gỡ $pkg bằng rpm"
          run_as_root rpm -e "$pkg" || true
        fi
      fi
    done
  fi
}

restart_fcitx() {
  if [[ "$ARECA_RESTART_FCITX" == 1 ]] && command -v fcitx5 >/dev/null 2>&1; then
    echo "[areca] Restarting Fcitx5"
    run_as_target_user fcitx5 -rd >/dev/null 2>&1 || true
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      ARECA_USER_ONLY=1
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
    --keep-data)
      ARECA_PURGE_DATA=0
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
  --user                 Only remove user installation in $ARECA_TARGET_HOME/.local
  --prefix PATH          Custom installation prefix to clean
  --build-dir PATH       CMake build directory to check install manifest
  --keep-data            Keep user configuration files in ~/.config/fcitx5
  --no-restart           Do not restart Fcitx5 after uninstall
EOF
      exit 0
      ;;
    *)
      echo "[areca] Unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

echo "[areca] Starting ArecaIME uninstallation"

stop_systemd_service

remove_distro_package

remove_manifest_files "$ARECA_BUILD_DIR/install_manifest.txt"
remove_manifest_files "$ARECA_ROOT_DIR/build/install_manifest.txt"
remove_manifest_files "$ARECA_ROOT_DIR/build-make/install_manifest.txt"

if [[ "$ARECA_USER_ONLY" == 1 ]]; then
  remove_installed_files_in_prefix "$ARECA_TARGET_HOME/.local"
else
  remove_installed_files_in_prefix "$ARECA_PREFIX"
  if [[ "$ARECA_PREFIX" != "/usr" ]]; then
    remove_installed_files_in_prefix "/usr"
  fi
  remove_installed_files_in_prefix "/usr/local"
  remove_installed_files_in_prefix "$ARECA_TARGET_HOME/.local"

  remove_path "/etc/udev/rules.d/70-areca-pointer.rules"
  remove_path "/etc/udev/rules.d/99-uinput-areca.rules"
  remove_path "/etc/udev/rules.d/99-areca-uinput.rules"

  reload_udev_rules
fi

if [[ "$ARECA_PURGE_DATA" == 1 ]]; then
  remove_user_data
fi

update_caches
restart_fcitx

echo "[areca] Uninstallation complete. Fcitx5 and other input methods remain untouched."
