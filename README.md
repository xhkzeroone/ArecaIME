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

Để bật backend `uinput`, thêm tài khoản hiện tại vào nhóm `input`, sau đó đăng
xuất và đăng nhập lại:

```bash
sudo usermod -aG input "$USER"
```

Nhóm `input` có quyền đọc/ghi thiết bị đầu vào ở mức thấp. Có thể bỏ qua bước
này nếu không muốn cấp quyền; Areca sẽ dùng backend dự phòng.

### Thiết lập Fcitx5

1. Mở `fcitx5-configtool`.
2. Bỏ chọn **Only Show Current Language** nếu chưa thấy bộ gõ.
3. Tìm **Areca (Bamboo)** và thêm vào danh sách bên phải.
4. Chọn Areca từ biểu tượng Fcitx5 hoặc dùng phím tắt chuyển bộ gõ.

Nếu addon chưa xuất hiện, hãy đăng xuất rồi đăng nhập lại. Trên Wayland, cũng
nên chắc chắn phiên desktop đang dùng Fcitx5 làm input method.

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

Mã nguồn cùng thông tin giấy phép nguyên bản của Bamboo được giữ nguyên trong
submodule [`bamboo/bamboo-core`](bamboo/bamboo-core) để thể hiện đầy đủ nguồn
gốc và sự ghi nhận đối với các tác giả.

Xin cảm ơn các dự án bộ gõ tiếng Việt cho Fcitx5 đã cung cấp nhiều ý tưởng và
nguồn tham khảo hữu ích trong quá trình phát triển Areca:

- [VMK — Vietnamese Micro Key](https://github.com/thanhpy2009/VMK)
- [Fcitx5 Lotus](https://github.com/LotusInputMethod/fcitx5-lotus)
- [SKey / fcitx5-skey](https://github.com/collyn/skey)
