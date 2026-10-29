# Areca IME

[![GitHub stars](https://img.shields.io/github/stars/xhkzeroone/ArecaIME?style=flat&logo=github)](https://github.com/xhkzeroone/ArecaIME/stargazers)
[![GitHub release](https://img.shields.io/github/v/release/xhkzeroone/ArecaIME)](https://github.com/xhkzeroone/ArecaIME/releases/latest)
[![Commit activity](https://img.shields.io/github/commit-activity/m/xhkzeroone/ArecaIME)](https://github.com/xhkzeroone/ArecaIME/commits/main)
[![Last commit](https://img.shields.io/github/last-commit/xhkzeroone/ArecaIME)](https://github.com/xhkzeroone/ArecaIME/commits/main)
[![Build Linux packages](https://github.com/xhkzeroone/ArecaIME/actions/workflows/package-linux.yml/badge.svg)](https://github.com/xhkzeroone/ArecaIME/actions/workflows/package-linux.yml)

Areca là bộ gõ tiếng Việt cho Linux, chạy dưới dạng addon của Fcitx5 và được
tối ưu cho Wayland. Bộ gõ sử dụng lõi xử lý tiếng Việt của
[Bamboo Engine](https://github.com/BambooEngine/bamboo-core), hỗ trợ Telex và
tự chọn cách sửa từ phù hợp với từng ứng dụng.

> Areca đang trong giai đoạn thử nghiệm. Nếu gặp lỗi, vui lòng gửi báo cáo kèm
> tên bản phân phối, phiên bản hệ điều hành và ứng dụng đang sử dụng.

## Cài đặt

### 1. Cài Fcitx5

Areca là addon của Fcitx5, vì vậy máy phải có Fcitx5 và công cụ cấu hình trước.
Chọn lệnh tương ứng với bản phân phối:

```bash
# Ubuntu / Debian
sudo apt install fcitx5 fcitx5-config-qt fcitx5-frontend-all

# Arch Linux / CachyOS
sudo pacman -S fcitx5-im

# Fedora
sudo dnf install fcitx5 fcitx5-autostart fcitx5-configtool fcitx5-gtk fcitx5-qt
```

Nếu bản phân phối không có đúng tên gói trên, hãy cài tối thiểu `fcitx5`, công
cụ cấu hình Fcitx5 và module GTK/Qt từ kho phần mềm của bản phân phối.

### 2. Cài Areca

```bash
curl -fsSL https://raw.githubusercontent.com/xhkzeroone/ArecaIME/main/install.sh | bash
```

Installer cần `curl` và `sudo`. Mọi thao tác cấp quyền và khởi động lại đều hỏi
xác nhận trước.

### Arch Linux (AUR)

Cài bản binary dựng sẵn:

```bash
paru -S fcitx5-areca-bin
# hoặc
yay -S fcitx5-areca-bin
```

Cài bản mới nhất từ mã nguồn Git:

```bash
paru -S fcitx5-areca-git
# hoặc
yay -S fcitx5-areca-git
```

### 3. Thiết lập desktop và biến môi trường

Chỉ làm theo **một** mục phù hợp với phiên desktop đang dùng. Có thể kiểm tra
bằng `echo "$XDG_CURRENT_DESKTOP / $XDG_SESSION_TYPE"`.

#### KDE Plasma Wayland

1. Mở **System Settings → Keyboard → Virtual Keyboard** và chọn **Fcitx 5**.
   KWin sẽ tự khởi động Fcitx5, không cần thêm lệnh autostart khác.
2. Để dùng ổn định cả ứng dụng Wayland native, XWayland và ứng dụng GTK/Qt
   cũ, tạo `~/.config/environment.d/fcitx.conf` với nội dung:

   ```ini
   XMODIFIERS=@im=fcitx
   GTK_IM_MODULE=fcitx
   QT_IM_MODULE=fcitx
   SDL_IM_MODULE=fcitx
   ```

Đây là cấu hình tương thích được khuyên dùng cho Areca trên phiên Plasma có
lẫn ứng dụng XWayland. Khuyến nghị thuần Wayland của Fcitx5 chỉ cần
`XMODIFIERS`, nhưng một số ứng dụng XWayland/GTK/Qt sẽ không nhận input method
nếu thiếu module tương ứng. Nếu riêng một ứng dụng Wayland native bị nhấp nháy
hoặc sai vị trí cửa sổ gợi ý, hãy chạy thử ứng dụng đó với biến module bị bỏ
trống thay vì gỡ cấu hình cho toàn phiên, ví dụ:

```bash
env -u GTK_IM_MODULE ten-ung-dung
# hoặc
env -u QT_IM_MODULE ten-ung-dung
```

#### GNOME Wayland

Tạo `~/.config/environment.d/fcitx.conf`:

```ini
XMODIFIERS=@im=fcitx
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
SDL_IM_MODULE=fcitx
```

Sau đó bảo đảm Fcitx5 tự chạy cùng phiên đăng nhập:

```bash
mkdir -p ~/.config/autostart
cp /usr/share/applications/org.fcitx.Fcitx5.desktop ~/.config/autostart/
```

Cấu hình đầy đủ này ưu tiên khả năng hoạt động đồng đều giữa ứng dụng Wayland,
XWayland, GTK, Qt và SDL.

#### Hyprland

Thêm vào `~/.config/hypr/hyprland.conf`:

```ini
env = XMODIFIERS,@im=fcitx
env = GTK_IM_MODULE,fcitx
env = QT_IM_MODULE,fcitx
env = SDL_IM_MODULE,fcitx
exec-once = fcitx5 -d
```

Nếu dùng UWSM hoặc đã bật XDG autostart, bỏ dòng `exec-once` để tránh chạy hai
tiến trình Fcitx5.

#### Niri

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
`/etc/xdg/autostart` hoặc `~/.config/autostart`, không thêm
`spawn-at-startup` nữa.

#### Sway và Wayland WM khác

Tạo `~/.config/environment.d/fcitx.conf`:

```ini
XMODIFIERS=@im=fcitx
GTK_IM_MODULE=fcitx
QT_IM_MODULE=fcitx
SDL_IM_MODULE=fcitx
```

Sau đó thêm `exec fcitx5 -d` vào cấu hình Sway, hoặc lệnh khởi động tương đương
của compositor.

#### Phiên X11

Thêm vào `~/.xprofile` (hoặc file khởi động phiên X11 tương đương):

```bash
export XMODIFIERS=@im=fcitx
export GTK_IM_MODULE=fcitx
export QT_IM_MODULE=fcitx
export SDL_IM_MODULE=fcitx
fcitx5 -d
```

Nếu desktop đã tự khởi động Fcitx5 thì bỏ dòng cuối. Sau khi đổi biến môi
trường, hãy đăng xuất rồi đăng nhập lại; khởi động lại máy là cách chắc chắn
nhất để mọi ứng dụng nhận cấu hình mới.

### 4. Thêm Areca vào Fcitx5

1. Mở `fcitx5-configtool`.
2. Bỏ chọn **Only Show Current Language** nếu chưa thấy bộ gõ.
3. Tìm **Areca (Bamboo)** và thêm vào danh sách bên phải.
4. Chọn Areca từ biểu tượng Fcitx5 hoặc dùng phím tắt chuyển bộ gõ.

Kiểm tra nhanh nếu không gõ được:

```bash
pgrep -a fcitx5
fcitx5-diagnose
printenv | grep -E '^(XMODIFIERS|GTK_IM_MODULE|QT_IM_MODULE|SDL_IM_MODULE)='
```

Chỉ nên có một tiến trình Fcitx5. Không chạy đồng thời IBus và Fcitx5 trong
cùng ứng dụng. Xem thêm tài liệu chính thức về
[thiết lập Fcitx5](https://fcitx-im.org/wiki/Setup_Fcitx_5/en) và
[Fcitx5 trên Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland/en).

## Khởi nạp uinput và cấp quyền

Areca dùng `/dev/uinput` cho backend Backspace và Shift+Select. Installer hoặc
gói phân phối thường đã thiết lập phần này. Nếu Areca báo không thể khởi tạo
uinput, trước tiên hãy nạp module:

```bash
sudo modprobe uinput
```

Để module tự được nạp sau mỗi lần khởi động:

```bash
echo uinput | sudo tee /etc/modules-load.d/areca-uinput.conf
```

Nạp lại udev rule đã được Areca cài và áp dụng quyền cho thiết bị:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Nếu tài khoản vẫn không có quyền ghi, thêm tài khoản hiện tại vào nhóm
`input`, sau đó đăng xuất và đăng nhập lại:

```bash
sudo usermod -aG input "$USER"
```

Kiểm tra module, thiết bị và quyền ghi:

```bash
lsmod | grep '^uinput'
ls -l /dev/uinput
test -w /dev/uinput && echo "uinput: OK" || echo "uinput: chưa có quyền ghi"
```

Cuối cùng, khởi động lại Fcitx5 để Areca đóng trạng thái backend cũ và thử mở
uinput lại:

```bash
fcitx5 -rd
```

Nhóm `input` có quyền truy cập thiết bị đầu vào ở mức thấp. Nếu không muốn cấp
quyền này, có thể bỏ qua bước `usermod`; Areca sẽ tự chuyển sang backend dự
phòng khi uinput không khả dụng.

## Cài từ mã nguồn

Phần này dành cho người dùng có bản phân phối hoặc phiên bản chưa có gói dựng
sẵn trong installer. Trước tiên, cài bộ biên dịch C++20, CMake, Extra CMake
Modules, Ninja, Go và các gói phát triển của Fcitx5, SDL3, libinput, libudev,
DBus, Fontconfig, X11 và Xtst bằng trình quản lý gói của hệ điều hành.

Tải mã nguồn cùng Bamboo Engine:

```bash
git clone --recurse-submodules https://github.com/xhkzeroone/ArecaIME.git
cd ArecaIME
```

Developer có thể dùng script build có sẵn:

```bash
./scripts/install.sh
```

Hoặc chạy từng bước thủ công như bên dưới.

Build và chạy test:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Cài vào hệ thống, nạp lại rule thiết bị và khởi động lại Fcitx5:

```bash
sudo cmake --install build
sudo udevadm control --reload-rules
sudo udevadm trigger
fcitx5 -rd
```

Nếu backend `uinput` chưa hoạt động sau khi cài từ mã nguồn, làm theo mục
[Khởi nạp uinput và cấp quyền](#khởi-nạp-uinput-và-cấp-quyền).

## Bản IBus thử nghiệm

`ibus-areca` là engine IBus độc lập dành cho GNOME và các desktop dùng IBus.
Bản này giữ ba backend rewrite: surrounding text, uinput Backspace và uinput
Shift+Select. Phím đầu của mỗi từ luôn được forward trực tiếp để kích hoạt đúng
editor web; cơ chế này được bật cố định và không có cờ tắt. Engine không yêu
cầu Fcitx5 khi build hoặc chạy.

Cài từ mã nguồn:

```bash
./scripts/install-ibus.sh
```

Sau khi cài, đăng xuất rồi đăng nhập lại nếu installer vừa thêm tài khoản vào
nhóm `input`. Mở phần **Keyboard / Input Sources** của desktop và thêm
**Vietnamese → Areca**.

Cấu hình mặc định được cài tại `/usr/share/ibus-areca/config.ini`. Có thể tạo
bản cấu hình riêng tại `~/.config/ibus-areca/config.ini`; engine sẽ ưu tiên
file của người dùng. Khởi động lại IBus sau khi sửa:

```bash
ibus restart
```

Đây là bản thử nghiệm. Cần kiểm tra thêm uinput loopback và surrounding text
trên từng ứng dụng trước khi dùng làm bộ gõ chính.

## Xem log

Để có log chi tiết, bật `Debug=True` trong phần cấu hình nâng cao của Areca,
sau đó khởi động lại Fcitx5:

```bash
fcitx5 -rd
```

Theo dõi log Areca theo thời gian thực:

```bash
journalctl --user -f | grep --line-buffered -i areca
```

Xem toàn bộ log Areca từ lần khởi động hiện tại:

```bash
journalctl --user -b --no-pager | grep -i areca
```

Khi kiểm tra lỗi `uinput`, lọc các dòng liên quan bằng:

```bash
journalctl --user -b --no-pager | grep -Ei 'areca.*(uinput|backend|failed|timeout)'
```

Nếu Fcitx5 không ghi vào user journal, có thể chạy trực tiếp để xem log trong
terminal:

```bash
fcitx5 -rd
```

Hướng dẫn phân tích từng backend nằm trong [tài liệu debug](docs/DEBUGGING.md).

## Website và tài liệu

- [Website giới thiệu Areca](https://xhkzeroone.github.io/ArecaIME/)
- [Kiến trúc trực quan](https://xhkzeroone.github.io/ArecaIME/architecture.html)
- [Tài liệu kiến trúc](docs/ARCHITECTURE.md)
- [Đặc tả kỹ thuật](docs/ARECA_ARCHITECTURE_SPECIFICATION.md)
- [Hướng dẫn debug](docs/DEBUGGING.md)

## Star History

[![Biểu đồ Star History](https://api.star-history.com/svg?repos=xhkzeroone/ArecaIME&type=Date)](https://star-history.com/#xhkzeroone/ArecaIME&Date)

## Contributors

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
- Thư viện [Bamboo Core](https://github.com/BambooEngine/bamboo-core)
  được phát hành theo [giấy phép MIT](https://github.com/BambooEngine/bamboo-core?tab=MIT-1-ov-file).
