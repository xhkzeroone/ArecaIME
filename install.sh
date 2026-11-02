#!/usr/bin/env bash
set -euo pipefail

readonly ARECA_REPO="xhkzeroone/ArecaIME"
ARECA_TMP_DIR=""
ARECA_AUR_PACKAGE=""
ARECA_REBOOT_REQUIRED=0

log() { printf '[areca] %s\n' "$*"; }
die() { printf '[areca] Lỗi: %s\n' "$*" >&2; exit 1; }
cleanup() { [[ -z "$ARECA_TMP_DIR" || ! -d "$ARECA_TMP_DIR" ]] || rm -rf -- "$ARECA_TMP_DIR"; }
trap cleanup EXIT

ask_yes_no() {
  local prompt="$1" answer
  [[ -r /dev/tty ]] || return 1
  while true; do
    printf '%s [y/N] ' "$prompt" > /dev/tty
    IFS= read -r answer < /dev/tty || return 1
    case "${answer,,}" in
      y|yes|c|co|có) return 0 ;;
      ''|n|no|k|khong|không) return 1 ;;
      *) printf 'Vui lòng nhập y hoặc n.\n' > /dev/tty ;;
    esac
  done
}

run_root() {
  if [[ $EUID -eq 0 ]]; then "$@"; else
    command -v sudo >/dev/null 2>&1 || die "Cần cài sudo để cài gói hệ thống."
    sudo "$@"
  fi
}

restart_fcitx() {
  local target_user="${SUDO_USER:-${USER:-}}" target_uid
  if ! command -v fcitx5 >/dev/null 2>&1; then
    log "Không tìm thấy fcitx5 để khởi động lại. Hãy đăng xuất rồi đăng nhập lại."
    return
  fi

  log "Đang khởi động lại Fcitx5..."
  if [[ -z "$target_user" || "$target_user" == root || "$(id -un)" == "$target_user" ]]; then
    fcitx5 -rd >/dev/null 2>&1 || log "Không thể tự khởi động lại Fcitx5; hãy đăng xuất rồi đăng nhập lại."
    return
  fi

  target_uid="$(id -u "$target_user")"
  sudo -H -u "$target_user" env \
    XDG_RUNTIME_DIR="/run/user/$target_uid" \
    DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$target_uid/bus" \
    fcitx5 -rd >/dev/null 2>&1 \
    || log "Không thể tự khởi động lại Fcitx5; hãy đăng xuất rồi đăng nhập lại."
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Không tìm thấy '$1'. Hãy cài $1 rồi chạy lại."
}

update_existing_arch_package() {
  local package version
  [[ "$ARECA_PACKAGE_TYPE" == pacman ]] || return 1
  require_command pacman

  for package in fcitx5-areca-bin fcitx5-areca-git; do
    if version="$(pacman -Q "$package" 2>/dev/null | awk '{print $2}')"; then
      log "Đã phát hiện $package ${version:-không rõ phiên bản}."
      if ! ask_yes_no "Bạn có muốn gỡ $package và thay bằng bản GitHub release không?"; then
        log "Đã giữ nguyên phiên bản hiện tại."
        exit 0
      fi

      ARECA_AUR_PACKAGE="$package"
      return 1
    fi
  done
  return 1
}

stop_fcitx_for_profile() {
  local fcitx_pids pid attempt
  fcitx_pids="$(pgrep -u "$(id -u)" -x fcitx5 || true)"
  [[ -n "$fcitx_pids" ]] || return 0

  while IFS= read -r pid; do
    kill "$pid" 2>/dev/null || true
  done <<< "$fcitx_pids"

  while IFS= read -r pid; do
    for ((attempt = 0; attempt < 50; attempt++)); do
      kill -0 "$pid" 2>/dev/null || break
      sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
      log "Không dừng được Fcitx5; giữ nguyên profile để tránh mất cấu hình."
      return 1
    fi
  done <<< "$fcitx_pids"
}

configure_fcitx_profile_for_user() {
  local config_home="${XDG_CONFIG_HOME:-$HOME/.config}"
  local data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
  local config_dir="$config_home/fcitx5" profile temp_file max_item descriptor candidate

  profile="$config_dir/profile"
  descriptor=""
  for candidate in \
    "$data_home/fcitx5/inputmethod/areca.conf" \
    /usr/local/share/fcitx5/inputmethod/areca.conf \
    /usr/share/fcitx5/inputmethod/areca.conf; do
    if [[ -r "$candidate" ]]; then
      descriptor="$candidate"
      break
    fi
  done
  if [[ -z "$descriptor" ]]; then
    log "Không tìm thấy areca.conf trong Fcitx5; chưa sửa profile. Hãy kiểm tra package đã cài đủ file."
    return 1
  fi

  if [[ -f "$profile" ]] && awk '
    /^\[Groups\/0\/Items\/[0-9]+\]$/ { item=1; next }
    /^\[/ { item=0 }
    item && /^Name=areca$/ { found=1 }
    END { exit !found }
  ' "$profile" && awk '
    /^\[Groups\/0\]$/ { group=1; next }
    /^\[/ { group=0 }
    group && /^DefaultIM=areca$/ { found=1 }
    END { exit !found }
  ' "$profile"; then
    log "Areca đã có trong profile Fcitx5 và đang được chọn mặc định."
    return 0
  fi

  stop_fcitx_for_profile || return 1
  mkdir -p "$config_dir"
  if [[ ! -f "$profile" ]]; then
    cat > "$profile" <<'EOF'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=areca

[Groups/0/Items/0]
Name=keyboard-us
Layout=

[Groups/0/Items/1]
Name=areca
Layout=

[GroupOrder]
0=Default
EOF
    printf '[areca] Đã tạo profile Fcitx5 và chọn Areca làm bộ gõ mặc định.\n'
    return
  fi

  if ! awk '
    /^\[Groups\/0\/Items\/[0-9]+\]$/ { item=1; next }
    /^\[/ { item=0 }
    item && /^Name=areca$/ { found=1 }
    END { exit !found }
  ' "$profile"; then
    max_item="$(awk '
      /^\[Groups\/0\/Items\/[0-9]+\]$/ {
        item_index=$0; sub(/^.*\//, "", item_index); sub(/\].*$/, "", item_index)
        if (item_index > max) max=item_index
      }
      END { print max }
    ' "$profile")"
    max_item=$(( ${max_item:-0} + 1 ))
    printf '\n[Groups/0/Items/%s]\nName=areca\nLayout=\n' "$max_item" >> "$profile"
  fi

  temp_file="$(mktemp "$config_dir/profile.XXXXXX")"
  awk '
    /^\[/ {
      if (in_default && !default_written) print "DefaultIM=areca"
      in_default=($0 == "[Groups/0]")
      if (in_default) has_default=1
      print
      next
    }
    in_default && /^DefaultIM=/ {
      if (!default_written) print "DefaultIM=areca"
      default_written=1
      next
    }
    { print }
    END {
      if (in_default && !default_written) print "DefaultIM=areca"
      if (!has_default) {
        print ""
        print "[Groups/0]"
        print "Name=Default"
        print "Default Layout=us"
        print "DefaultIM=areca"
      }
    }
  ' "$profile" > "$temp_file"
  chmod --reference="$profile" "$temp_file"
  mv -f -- "$temp_file" "$profile"
  printf '[areca] Đã thêm Areca vào profile Fcitx5 và chọn làm bộ gõ mặc định.\n'
}

configure_fcitx_profile() {
  local target_user="${SUDO_USER:-${USER:-}}" config_home="${XDG_CONFIG_HOME:-}"
  if [[ $EUID -eq 0 ]]; then
    if [[ -z "$target_user" || "$target_user" == root ]]; then
      log "Không xác định được tài khoản desktop; bỏ qua tự cấu hình Fcitx5."
      return
    fi
    sudo -H -u "$target_user" env XDG_CONFIG_HOME="$config_home" \
      bash -c "$(declare -f log stop_fcitx_for_profile configure_fcitx_profile_for_user); configure_fcitx_profile_for_user"
  else
    configure_fcitx_profile_for_user
  fi
}

select_platform() {
  [[ "$(uname -s)" == Linux ]] || die "Installer này chỉ hỗ trợ Linux."
  case "$(uname -m)" in x86_64|amd64) ;; *) die "Release chỉ hỗ trợ x86_64, không hỗ trợ $(uname -m)." ;; esac
  [[ -r /etc/os-release ]] || die "Không đọc được /etc/os-release."
  # shellcheck disable=SC1091
  . /etc/os-release
  ARECA_OS_NAME="${PRETTY_NAME:-${ID:-Linux}}"
  case "${ID:-}" in
    ubuntu)
      ARECA_PACKAGE_TYPE=deb
      case "${VERSION_ID:-}" in
        24.04) ARECA_ASSET_MARKER=_ubuntu24_amd64.deb ;;
        25.10) ARECA_ASSET_MARKER=_ubuntu25_amd64.deb ;;
        26.04) ARECA_ASSET_MARKER=_ubuntu26_amd64.deb ;;
        *) die "Ubuntu ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;;
      esac ;;
    linuxmint)
      ARECA_PACKAGE_TYPE=deb
      case "${VERSION_ID:-}" in 22|22.*) ARECA_ASSET_MARKER=_mint22_amd64.deb ;; *) die "Linux Mint ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;; esac ;;
    pop)
      ARECA_PACKAGE_TYPE=deb
      case "${VERSION_ID:-}" in 24.04) ARECA_ASSET_MARKER=_popos24_amd64.deb ;; *) die "Pop!_OS ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;; esac ;;
    zorin)
      ARECA_PACKAGE_TYPE=deb
      case "${VERSION_ID:-}" in 17|17.*) ARECA_ASSET_MARKER=_zorin17_amd64.deb ;; *) die "Zorin OS ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;; esac ;;
    arch) ARECA_PACKAGE_TYPE=pacman; ARECA_ASSET_MARKER=-x86_64.pkg.tar.zst ;;
    cachyos|manjaro|endeavouros|garuda) ARECA_PACKAGE_TYPE=pacman; ARECA_ASSET_MARKER="-x86_64.${ID}.pkg.tar.zst" ;;
    fedora)
      ARECA_PACKAGE_TYPE=rpm
      case "${VERSION_ID:-}" in 42|43|44) ARECA_ASSET_MARKER=".fedora${VERSION_ID}.x86_64.rpm" ;; *) die "Fedora ${VERSION_ID:-không rõ} chưa có trong danh sách release." ;; esac ;;
    nobara|bazzite) ARECA_PACKAGE_TYPE=rpm; ARECA_ASSET_MARKER=".${ID}.x86_64.rpm" ;;
    opensuse-tumbleweed) ARECA_PACKAGE_TYPE=rpm; ARECA_ASSET_MARKER=.opensuse-tumbleweed.x86_64.rpm ;;
    *) die "${ARECA_OS_NAME} chưa có trong danh sách release được hỗ trợ." ;;
  esac
}

find_release_asset() {
  local json tag url candidate
  log "Đang kiểm tra release mới nhất..."
  json="$(curl -fsSL --retry 3 -H 'Accept: application/vnd.github+json' \
    "https://api.github.com/repos/${ARECA_REPO}/releases/latest")" \
    || die "Không thể lấy thông tin release mới nhất. Hãy kiểm tra kết nối mạng."
  tag="$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' <<< "$json" | head -n1)"
  [[ -n "$tag" ]] || die "Release mới nhất không có tag hợp lệ."
  url=""
  while IFS= read -r candidate; do
    if [[ "$candidate" == *"$ARECA_ASSET_MARKER" ]]; then url="$candidate"; break; fi
  done < <(sed -n 's/.*"browser_download_url"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' <<< "$json")
  [[ -n "$url" ]] || die "Release $tag không có gói dành cho ${ARECA_OS_NAME}."
  ARECA_RELEASE_TAG="$tag"; ARECA_ASSET_URL="$url"; ARECA_ASSET_NAME="${url##*/}"
}

download_release_asset() {
  ARECA_TMP_DIR="$(mktemp -d)"
  ARECA_PACKAGE_PATH="$ARECA_TMP_DIR/$ARECA_ASSET_NAME"
  log "Tìm thấy $ARECA_RELEASE_TAG: $ARECA_ASSET_NAME"
  curl -fL --retry 3 --progress-bar -o "$ARECA_PACKAGE_PATH" "$ARECA_ASSET_URL" || die "Tải gói cài đặt thất bại."
}

install_package() {
  local package_path="$ARECA_PACKAGE_PATH"
  log "Đang cài đặt (hệ thống có thể hỏi mật khẩu)..."
  case "$ARECA_PACKAGE_TYPE" in
    deb) require_command apt-get; run_root apt-get install -y "$package_path" ;;
    pacman)
      require_command pacman
      if [[ -n "$ARECA_AUR_PACKAGE" ]]; then
        log "Đang gỡ $ARECA_AUR_PACKAGE để thay bằng gói GitHub release..."
        run_root pacman -R --noconfirm "$ARECA_AUR_PACKAGE"
      fi
      run_root pacman -U --needed --noconfirm "$package_path" ;;
    rpm)
      if command -v dnf >/dev/null 2>&1; then run_root dnf install -y "$package_path"
      elif command -v zypper >/dev/null 2>&1; then run_root zypper --non-interactive install "$package_path"
      else die "Không tìm thấy dnf hoặc zypper để cài gói RPM."; fi ;;
  esac
}

uinput_is_configured() {
  local target_user="${SUDO_USER:-${USER:-}}"
  [[ -n "$target_user" && "$target_user" != root ]] || return 1
  if id -nG "$target_user" | tr ' ' '\n' | grep -Fxq input; then
    return 0
  fi
  if [[ "$target_user" == "$(id -un)" ]]; then
    [[ -w /dev/uinput ]]
  elif command -v runuser >/dev/null 2>&1; then
    runuser -u "$target_user" -- test -w /dev/uinput
  else
    return 1
  fi
}

prepare_uinput() {
  local target_user="${SUDO_USER:-${USER:-}}"
  if uinput_is_configured; then
    log "Uinput đã được cấu hình cho '$target_user'; không cần reboot."
    return
  fi

  [[ -n "$target_user" && "$target_user" != root ]] \
    || die "Không xác định được user desktop để cấu hình uinput. Hãy chạy installer từ tài khoản desktop."
  ask_yes_no "Uinput chưa được cấu hình cho '$target_user'. Cấu hình quyền và tự reboot sau khi cài?" \
    || die "Đã hủy cài đặt vì uinput chưa được cấu hình."

  getent group input >/dev/null 2>&1 || run_root groupadd --system input
  run_root usermod -aG input "$target_user"
  run_root modprobe uinput || true
  if command -v udevadm >/dev/null 2>&1; then
    run_root udevadm control --reload-rules
    run_root udevadm trigger --subsystem-match=misc --action=add || true
  fi
  ARECA_REBOOT_REQUIRED=1
  log "Đã cấp quyền uinput cho '$target_user'; máy sẽ tự reboot sau khi cài xong."
}

main() {
  require_command curl
  select_platform
  log "Hệ điều hành: $ARECA_OS_NAME"
  find_release_asset
  if ! update_existing_arch_package; then
    download_release_asset
    prepare_uinput
    install_package
  fi
  configure_fcitx_profile
  log "Cài đặt Areca hoàn tất."
  log "Areca đã được chọn làm bộ gõ mặc định trong profile Fcitx5."
  if [[ "$ARECA_REBOOT_REQUIRED" == 1 ]]; then
    log "Đang reboot để áp dụng quyền uinput..."
    run_root systemctl reboot
  else
    restart_fcitx
  fi
}

main "$@"
