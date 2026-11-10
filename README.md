# Areca IME

[![GitHub stars](https://img.shields.io/github/stars/xhkzeroone/ArecaIME?style=flat&logo=github)](https://github.com/xhkzeroone/ArecaIME/stargazers)
[![GitHub release](https://img.shields.io/github/v/release/xhkzeroone/ArecaIME)](https://github.com/xhkzeroone/ArecaIME/releases/latest)
[![Commit activity](https://img.shields.io/github/commit-activity/m/xhkzeroone/ArecaIME)](https://github.com/xhkzeroone/ArecaIME/commits/main)
[![Last commit](https://img.shields.io/github/last-commit/xhkzeroone/ArecaIME)](https://github.com/xhkzeroone/ArecaIME/commits/main)
[![Build Linux packages](https://github.com/xhkzeroone/ArecaIME/actions/workflows/package-linux.yml/badge.svg)](https://github.com/xhkzeroone/ArecaIME/actions/workflows/package-linux.yml)

Areca là bộ gõ tiếng Việt cho Linux, chạy như một addon của Fcitx5 và được tối
ưu cho Wayland. Lõi ghép dấu là
[Bamboo Engine](https://github.com/BambooEngine/bamboo-core). Areca tự chọn cách
sửa chữ phù hợp với từng ứng dụng để gõ trực tiếp, không gạch chân, kể cả trên
trình duyệt, Electron, terminal và ứng dụng XWayland.

> Areca đang trong giai đoạn thử nghiệm. Khi gặp lỗi, hãy
> [mở issue](https://github.com/xhkzeroone/ArecaIME/issues) kèm bản phân phối,
> desktop, ứng dụng đang dùng và log debug.

## Mục lục

- [Tính năng](#tính-năng)
- [Cài đặt nhanh](#cài-đặt-nhanh)
- [Cài đặt chi tiết](#cài-đặt-chi-tiết)
- [Cấu hình](#cấu-hình)
- [uinput và quyền truy cập](#uinput-và-quyền-truy-cập)
- [Xử lý sự cố](#xử-lý-sự-cố)
- [Build từ mã nguồn](#build-từ-mã-nguồn)
- [Đóng góp](#đóng-góp)
- [Tài liệu](#tài-liệu)
- [Ghi nhận](#ghi-nhận)
- [Giấy phép](#giấy-phép)

## Tính năng

**Kiểu gõ và bảng mã**
- Telex, Telex 2, Telex W, VNI, VIQR, Microsoft layout, VNI bàn phím tiếng Pháp
  và các kiểu kết hợp Telex + VNI, Telex + VNI + VIQR.
- Xuất Unicode dựng sẵn, Unicode tổ hợp hoặc bảng mã cũ như TCVN3 (ABC), VNI
  Windows, VISCII, VPS, Windows 1258.
- Đặt dấu kiểu mới như `oà`, `uý` hoặc kiểu cũ như `òa`, `úy`.

**Chế độ hiển thị**
- **Rewrite trực tiếp**: chữ hiện thẳng trong ô nhập, không preedit. Đây là chế
  độ mặc định.
- **Preedit**: từ đang gõ được gạch chân cho tới khi kết thúc từ.
- **Redirect (EN)**: chuyển tiếp phím nguyên bản.

**Tự chọn backend sửa chữ theo ứng dụng**
- Surrounding text khi ứng dụng hỗ trợ xoá qua giao thức input method.
- Backspace hoặc Shift+Left phát qua uinput, Libei/XTest hoặc ForwardKey khi
  ứng dụng không hỗ trợ surrounding text.
- Xử lý riêng ô URL, thanh địa chỉ và gợi ý tự hoàn thành của trình duyệt.
- Tự nhận ra ứng dụng đang focus qua AT-SPI. Có thể ghi đè chế độ và backend
  cho từng ứng dụng, hoặc bấm phím tắt để chọn ngay trên ứng dụng đang dùng.
- Tự tăng thời gian chờ khi máy bận để tránh mất hoặc lặp chữ.

**Gõ thông minh**
- Kiểm tra chính tả: khôi phục từ gốc ngay trong lúc gõ hoặc sau khi gõ xong.
- Khôi phục lỗi chính tả khi nhấn Backspace.
- Sửa lại từ đã gõ xong khi đưa con trỏ về sau từ đó.
- Tự viết hoa sau dấu kết câu.
- Macro gõ tắt, tự đổi hoa thường theo từ viết tắt, hoàn tác bằng Backspace và
  giữ một phím bổ trợ khi nhấn Space để tạm bỏ qua macro.
- Tự reset sau khi click chuột sang vị trí khác.
- Tuỳ chọn tắt tiếng Việt trong terminal.

**Cài đặt**
- Ứng dụng cài đặt GTK riêng `areca-settings`, biểu tượng khay hệ thống, và vẫn
  dùng được `fcitx5-configtool`.

## Cài đặt nhanh

```bash
curl -fsSL https://raw.githubusercontent.com/xhkzeroone/ArecaIME/main/install.sh | bash
```

Sau đó:

1. Thiết lập biến môi trường cho desktop đang dùng theo mục
   [Thiết lập desktop](#3-thiết-lập-desktop).
2. Đăng xuất rồi đăng nhập lại.
3. Thêm **Areca (Bamboo)** vào Fcitx5 theo mục
   [Thêm Areca vào Fcitx5](#4-thêm-areca-vào-fcitx5).

## Cài đặt chi tiết

### 1. Cài Fcitx5

Areca cần Fcitx5, công cụ cấu hình và module GTK/Qt:

```bash
# Ubuntu / Debian
sudo apt install fcitx5 fcitx5-config-qt fcitx5-frontend-all

# Arch Linux / CachyOS
sudo pacman -S fcitx5-im

# Fedora
sudo dnf install fcitx5 fcitx5-autostart fcitx5-configtool fcitx5-gtk fcitx5-qt
```

Bản phân phối khác: cài tối thiểu `fcitx5`, công cụ cấu hình Fcitx5 và module
GTK/Qt từ kho phần mềm.

### 2. Cài Areca

**Installer**, cần `curl` và `sudo`. Mọi thao tác cấp quyền và khởi động lại đều
hỏi xác nhận trước:

```bash
curl -fsSL https://raw.githubusercontent.com/xhkzeroone/ArecaIME/main/install.sh | bash
```

**Arch Linux qua AUR:**

```bash
# Bản binary dựng sẵn
paru -S fcitx5-areca-bin

# Bản mới nhất từ mã nguồn Git
paru -S fcitx5-areca-git
```

Có thể thay `paru` bằng `yay`.

Bản phân phối chưa có gói dựng sẵn thì xem mục
[Build từ mã nguồn](#build-từ-mã-nguồn).

### 3. Thiết lập desktop

Kiểm tra phiên đang dùng bằng:

```bash
echo "$XDG_CURRENT_DESKTOP / $XDG_SESSION_TYPE"
```

Hầu hết desktop dùng chung bộ biến môi trường sau. Bộ đầy đủ này giúp ứng dụng
Wayland native, XWayland, GTK, Qt và SDL đều nhận bộ gõ:

```ini
XMODIFIERS=@im=fcitx
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
SDL_IM_MODULE=fcitx
```

Chỉ làm theo **một** mục phù hợp:

<details>
<summary><b>KDE Plasma Wayland</b></summary>

1. Mở **System Settings → Keyboard → Virtual Keyboard** và chọn **Fcitx 5**.
   KWin tự khởi động Fcitx5, không cần autostart thêm.
2. Lưu bộ biến môi trường ở trên vào `~/.config/environment.d/fcitx.conf`.

Fcitx5 khuyên phiên Wayland thuần chỉ cần `XMODIFIERS`, nhưng ứng dụng
XWayland, GTK hoặc Qt cũ có thể không nhận bộ gõ nếu thiếu module. Nếu riêng một
ứng dụng Wayland native bị nhấp nháy hoặc cửa sổ gợi ý sai vị trí, chỉ bỏ biến
cho ứng dụng đó:

```bash
env -u GTK_IM_MODULE ten-ung-dung
env -u QT_IM_MODULE ten-ung-dung
```

</details>

<details>
<summary><b>GNOME Wayland</b></summary>

1. Lưu bộ biến môi trường vào `~/.config/environment.d/fcitx.conf`.
2. Cho Fcitx5 tự chạy khi đăng nhập:

   ```bash
   mkdir -p ~/.config/autostart
   cp /usr/share/applications/org.fcitx.Fcitx5.desktop ~/.config/autostart/
   ```

</details>

<details>
<summary><b>Hyprland</b></summary>

Thêm vào `~/.config/hypr/hyprland.conf`:

```ini
env = XMODIFIERS,@im=fcitx
env = GTK_IM_MODULE,fcitx
env = QT_IM_MODULE,fcitx
env = SDL_IM_MODULE,fcitx
exec-once = fcitx5 -d
```

Dùng UWSM hoặc đã bật XDG autostart thì bỏ dòng `exec-once` để tránh chạy hai
tiến trình Fcitx5.

</details>

<details>
<summary><b>Niri</b></summary>

Thêm vào `~/.config/niri/config.kdl`:

```kdl
environment {
    XMODIFIERS "@im=fcitx"
    GTK_IM_MODULE "fcitx"
    QT_IM_MODULE "fcitx"
    SDL_IM_MODULE "fcitx"
}

spawn-at-startup "fcitx5" "-d"
```

Phiên Niri chạy qua systemd đã hỗ trợ XDG autostart. Nếu Fcitx5 đã tự chạy từ
`/etc/xdg/autostart` hoặc `~/.config/autostart` thì bỏ `spawn-at-startup`.

</details>

<details>
<summary><b>Sway và Wayland WM khác</b></summary>

1. Lưu bộ biến môi trường vào `~/.config/environment.d/fcitx.conf`.
2. Thêm `exec fcitx5 -d` vào cấu hình Sway, hoặc lệnh khởi động tương đương của
   compositor.

</details>

<details>
<summary><b>Phiên X11</b></summary>

Thêm vào `~/.xprofile` hoặc file khởi động phiên X11 tương đương:

```bash
export XMODIFIERS=@im=fcitx
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export SDL_IM_MODULE=fcitx
fcitx5 -d
```

Desktop đã tự khởi động Fcitx5 thì bỏ dòng cuối.

</details>

Sau khi đổi biến môi trường, đăng xuất rồi đăng nhập lại. Khởi động lại máy là
cách chắc chắn nhất để mọi ứng dụng nhận cấu hình mới.

### 4. Thêm Areca vào Fcitx5

1. Mở `fcitx5-configtool`.
2. Bỏ chọn **Only Show Current Language** nếu chưa thấy bộ gõ.
3. Tìm **Areca (Bamboo)** và thêm vào danh sách bên phải.
4. Chọn Areca từ biểu tượng Fcitx5 hoặc bằng phím tắt chuyển bộ gõ.

## Cấu hình

- Mở **Cài đặt Areca** từ menu ứng dụng hoặc chạy `areca-settings`.
- Hoặc vào `fcitx5-configtool`, chọn Areca rồi bấm nút cấu hình.

Các nhóm cấu hình chính:

| Nhóm | Nội dung |
|---|---|
| Chung | Kiểu gõ, bảng mã, chế độ hiển thị, kiểm tra chính tả, kiểu đặt dấu, tự viết hoa |
| Macro | Bật tắt macro, bảng từ viết tắt, hoàn tác bằng Backspace, phím bỏ qua macro |
| Theo ứng dụng | Ghi đè chế độ và backend cho từng ứng dụng hoặc frontend |
| Nâng cao | Thời gian chờ của từng backend, chọn uinput hay Libei/XTest, log debug |

Cấu hình được lưu ở `~/.config/fcitx5/conf/areca*.conf`.

## uinput và quyền truy cập

Backend Backspace và Shift+Left dùng `/dev/uinput`. Installer và gói phân phối
thường đã thiết lập sẵn. Nếu Areca báo không khởi tạo được uinput:

```bash
# Nạp module ngay
sudo modprobe uinput

# Tự nạp mỗi lần khởi động
echo uinput | sudo tee /etc/modules-load.d/areca-uinput.conf

# Áp dụng udev rule Areca đã cài
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Nếu vẫn chưa có quyền ghi, thêm tài khoản vào nhóm `input` rồi đăng xuất và đăng
nhập lại:

```bash
sudo usermod -aG input "$USER"
```

Kiểm tra:

```bash
lsmod | grep '^uinput'
ls -l /dev/uinput
test -w /dev/uinput && echo "uinput: OK" || echo "uinput: chưa có quyền ghi"
```

Cuối cùng khởi động lại Fcitx5 để Areca thử mở uinput lại:

```bash
fcitx5 -rd
```

Nhóm `input` cho quyền truy cập thiết bị đầu vào ở mức thấp. Không muốn cấp quyền
này thì bỏ qua bước `usermod`, Areca sẽ tự dùng backend dự phòng.

## Xử lý sự cố

### Không gõ được

```bash
pgrep -a fcitx5
fcitx5-diagnose
printenv | grep -E '^(XMODIFIERS|GTK_IM_MODULE|QT_IM_MODULE|SDL_IM_MODULE)='
```

- Chỉ được có một tiến trình Fcitx5.
- Không chạy IBus và Fcitx5 cùng lúc cho một ứng dụng.
- Biến môi trường trống nghĩa là chưa đăng nhập lại sau khi cấu hình.

Xem thêm tài liệu chính thức về
[thiết lập Fcitx5](https://fcitx-im.org/wiki/Setup_Fcitx_5/en) và
[Fcitx5 trên Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland/en).

### Mất chữ, lặp chữ hoặc dấu sai trên một ứng dụng

1. Đặt con trỏ vào ứng dụng đó và bấm phím tắt chọn backend để thử backend khác.
2. Nếu một backend chạy ổn, lưu lại trong mục cấu hình theo ứng dụng.
3. Nếu không backend nào ổn, gửi issue kèm log debug.

### Lấy log debug

1. Bật **Bật log debug Areca** trong cấu hình nâng cao, hoặc đặt `Debug=True`
   trong `~/.config/fcitx5/conf/areca.conf`.
2. Chạy Fcitx5 ngay trong terminal để thấy log:

   ```bash
   fcitx5 -rd
   ```

3. Nếu Fcitx5 ghi vào user journal:

   ```bash
   # Theo dõi trực tiếp
   journalctl --user -f | grep --line-buffered -i areca

   # Toàn bộ log từ lần khởi động hiện tại
   journalctl --user -b --no-pager | grep -i areca

   # Chỉ các dòng liên quan uinput và backend
   journalctl --user -b --no-pager | grep -Ei 'areca.*(uinput|backend|failed|timeout)'
   ```

Cách đọc log của từng backend nằm trong [tài liệu debug](docs/DEBUGGING.md).

## Build từ mã nguồn

Cần bộ biên dịch C++20, CMake, Extra CMake Modules, Ninja, Go và gói phát triển
của Fcitx5, SDL3, libinput, libudev, DBus, Fontconfig, X11, Xtst.

```bash
git clone --recurse-submodules https://github.com/xhkzeroone/ArecaIME.git
cd ArecaIME
```

Dùng script có sẵn để build, cài và khởi động lại Fcitx5:

```bash
./scripts/install.sh
```

Hoặc làm từng bước:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
ctest --test-dir build --output-on-failure

sudo cmake --install build
sudo udevadm control --reload-rules
sudo udevadm trigger
fcitx5 -rd
```

Backend uinput chưa chạy thì xem mục
[uinput và quyền truy cập](#uinput-và-quyền-truy-cập).

## Đóng góp

Mọi báo lỗi, ý tưởng và Pull Request đều được hoan nghênh. Trước khi gửi PR,
hãy đọc [CONTRIBUTING.md](CONTRIBUTING.md), đặc biệt mục quy tắc bắt buộc:

- Mỗi PR chỉ giải quyết một vấn đề.
- Format bằng `git clang-format main`.
- Build sạch, `ctest` qua toàn bộ, có test cho thay đổi.
- Không chặn main thread của Fcitx5.
- Gõ thử trên ứng dụng thật và ghi kết quả vào PR.

## Tài liệu

- [Website giới thiệu Areca](https://xhkzeroone.github.io/ArecaIME/)
- [Kiến trúc trực quan](https://xhkzeroone.github.io/ArecaIME/architecture.html)
- [Tài liệu kiến trúc](docs/ARCHITECTURE.md)
- [Đặc tả kỹ thuật](docs/ARECA_ARCHITECTURE_SPECIFICATION.md)
- [Thời gian chờ thích nghi](docs/ADAPTIVE_DELAYS.md)
- [Hướng dẫn debug](docs/DEBUGGING.md)

## Star History

[![Biểu đồ Star History](https://api.star-history.com/svg?repos=xhkzeroone/ArecaIME&type=Date)](https://star-history.com/#xhkzeroone/ArecaIME&Date)

## Ghi nhận

Xin cảm ơn những người đã đóng góp trực tiếp cho Areca:

[![Contributors](https://contrib.rocks/image?repo=xhkzeroone/ArecaIME)](https://github.com/xhkzeroone/ArecaIME/graphs/contributors)

Xin chân thành cảm ơn các anh, chị, em trong cộng đồng đã dành thời gian trải
nghiệm Areca, báo lỗi, gửi log và chia sẻ những ý tưởng quý báu. Mỗi phản hồi
đều là một đóng góp thiết thực, giúp Areca ngày càng ổn định và hoạt động tốt
hơn trên nhiều ứng dụng, môi trường desktop và bản phân phối Linux.

Areca được xây dựng trên nền tảng
[Bamboo Engine / bamboo-core](https://github.com/BambooEngine/bamboo-core),
thư viện xử lý tiếng Việt do **Lương Thành Lâm** phát triển và chia sẻ với cộng
đồng dưới giấy phép MIT. Dự án xin gửi lời cảm ơn chân thành đến anh vì đã tạo
ra một nền tảng vững chắc, giúp Areca có thể tập trung hoàn thiện trải nghiệm
gõ tiếng Việt trên Fcitx5 và Wayland.

Areca cũng trân trọng ghi nhận những công trình đặt nền móng cho Bamboo Engine:
**Trung Ngo**, tác giả `bogo.js`, và **Trần Kỳ Nam**, tác giả `GoTiengViet`.
Những đóng góp của các tác giả đã giúp hệ sinh thái bộ gõ tiếng Việt nguồn mở
ngày càng phong phú và tạo điều kiện để các dự án như Areca tiếp tục phát triển.

[Mã nguồn Bamboo Core](https://github.com/BambooEngine/bamboo-core) cùng
[thông tin giấy phép MIT nguyên bản](https://github.com/BambooEngine/bamboo-core?tab=MIT-1-ov-file)
được giữ nguyên trong submodule `bamboo/bamboo-core` để thể hiện đầy đủ nguồn
gốc và sự ghi nhận đối với các tác giả.

Xin cảm ơn các dự án bộ gõ tiếng Việt cho Fcitx5 đã cung cấp nhiều ý tưởng và
nguồn tham khảo hữu ích trong quá trình phát triển Areca:

- [VMK — Vietnamese Micro Key](https://github.com/thanhpy2009/VMK)
- [Fcitx5 Lotus](https://github.com/LotusInputMethod/fcitx5-lotus)
- [SKey / fcitx5-skey](https://github.com/collyn/skey)

## Giấy phép

- Areca được phát hành theo giấy phép [MIT](LICENSE).
- Thư viện [Bamboo Core](https://github.com/BambooEngine/bamboo-core) được phát
  hành theo [giấy phép MIT](https://github.com/BambooEngine/bamboo-core?tab=MIT-1-ov-file).
