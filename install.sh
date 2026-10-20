#!/usr/bin/env bash
set -euo pipefail

readonly ARECA_REPO="xhkzeroone/ArecaIME"
ARECA_TMP_DIR=""

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

require_command() {
  command -v "$1" >/dev/null 2>&1 || die "Không tìm thấy '$1'. Hãy cài $1 rồi chạy lại."
}

update_existing_arch_package() {
  local package version helper
  [[ "$ARECA_PACKAGE_TYPE" == pacman ]] || return 1
  require_command pacman

  for package in fcitx5-areca-bin fcitx5-areca-git; do
    if version="$(pacman -Q "$package" 2>/dev/null | awk '{print $2}')"; then
      log "Đã phát hiện $package ${version:-không rõ phiên bản}."
      if ! ask_yes_no "Bạn có muốn cập nhật $package bằng AUR không?"; then
        log "Đã giữ nguyên phiên bản hiện tại."
        exit 0
      fi

      helper=""
      if command -v paru >/dev/null 2>&1; then
        helper=paru
      elif command -v yay >/dev/null 2>&1; then
        helper=yay
      fi
      [[ -n "$helper" ]] || die "Không tìm thấy paru hoặc yay để cập nhật $package."
      [[ $EUID -ne 0 ]] || die "Không nên chạy AUR helper bằng root. Hãy chạy installer bằng tài khoản thường."

      log "Đang cập nhật $package bằng $helper..."
      "$helper" -S --noconfirm "$package"
      log "Đã cập nhật $package."
      return 0
    fi
  done
  return 1
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

install_package() {
  ARECA_TMP_DIR="$(mktemp -d)"
  local package_path="$ARECA_TMP_DIR/$ARECA_ASSET_NAME"
  log "Tìm thấy $ARECA_RELEASE_TAG: $ARECA_ASSET_NAME"
  curl -fL --retry 3 --progress-bar -o "$package_path" "$ARECA_ASSET_URL" || die "Tải gói cài đặt thất bại."
  log "Đang cài đặt (hệ thống có thể hỏi mật khẩu)..."
  case "$ARECA_PACKAGE_TYPE" in
    deb) require_command apt-get; run_root apt-get install -y "$package_path" ;;
    pacman) require_command pacman; run_root pacman -U --needed --noconfirm "$package_path" ;;
    rpm)
      if command -v dnf >/dev/null 2>&1; then run_root dnf install -y "$package_path"
      elif command -v zypper >/dev/null 2>&1; then run_root zypper --non-interactive install "$package_path"
      else die "Không tìm thấy dnf hoặc zypper để cài gói RPM."; fi ;;
  esac
}

configure_uinput() {
  local target_user="${SUDO_USER:-${USER:-}}"
  if [[ -z "$target_user" || "$target_user" == root ]]; then
    log "Không xác định được tài khoản desktop; bỏ qua cấp quyền uinput."; return
  fi
  if ask_yes_no "Cấp cho '$target_user' quyền dùng uinput để tương thích tốt hơn với trình duyệt?"; then
    getent group input >/dev/null 2>&1 || run_root groupadd --system input
    run_root usermod -aG input "$target_user"
    run_root modprobe uinput || true
    if command -v udevadm >/dev/null 2>&1; then
      run_root udevadm control --reload-rules
      run_root udevadm trigger --subsystem-match=misc --action=add || true
    fi
    log "Đã cấp quyền uinput. Quyền nhóm có hiệu lực sau khi đăng nhập lại."
  else
    log "Đã bỏ qua quyền uinput; Areca vẫn dùng được với backend dự phòng."
  fi
}

main() {
  require_command curl
  select_platform
  log "Hệ điều hành: $ARECA_OS_NAME"
  if ! update_existing_arch_package; then
    find_release_asset
    install_package
  fi
  configure_uinput
  log "Cài đặt Areca hoàn tất."
  log "Sau khi đăng nhập lại, mở fcitx5-configtool và thêm 'Areca (Bamboo)'."
  if ask_yes_no "Khởi động lại máy ngay bây giờ?"; then
    log "Đang khởi động lại..."; run_root systemctl reboot
  else
    log "Bạn có thể khởi động lại sau để mọi thay đổi có hiệu lực."
  fi
}

main "$@"
