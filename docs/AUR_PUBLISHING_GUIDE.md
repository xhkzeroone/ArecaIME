# Hướng dẫn phát hành fcitx5-areca lên Arch User Repository

Tài liệu hướng dẫn chi tiết quy trình đưa gói fcitx5-areca lên Arch User Repository và bảo trì các bản cập nhật tiếp theo.

---

## 1. Chuẩn bị tài khoản và khóa SSH

### Bước 1: Đăng ký tài khoản
Truy cập trang đăng ký chính thức:
https://aur.archlinux.org/register

### Bước 2: Cấu hình SSH Key
1. Tạo cặp khóa SSH trên máy nếu chưa có:
   ```bash
   ssh-keygen -t ed25519 -C "email_cua_ban@domain.com"
   ```
2. Sao chép nội dung khóa công khai:
   ```bash
   cat ~/.ssh/id_ed25519.pub
   ```
3. Đăng nhập vào AUR, mở mục **My Account** và dán nội dung khóa vào ô **SSH Public Key**.

### Bước 3: Kiểm tra kết nối
Chạy lệnh kiểm tra quyền truy cập vào máy chủ AUR:
```bash
ssh aur@aur.archlinux.org help
```
Kết quả hiển thị danh sách các lệnh trợ giúp của AUR nghĩa là kết nối thành công.

---

## 2. Chuẩn bị tệp tin gói

Thư mục `packaging/aur` trong repository ArecaIME chứa sẵn toàn bộ cấu hình cần thiết:
- `PKGBUILD`: Kịch bản tải mã nguồn theo Git tag, cập nhật submodule BambooEngine và biên dịch bằng CMake kết hợp Ninja.
- `.SRCINFO`: Tệp siêu dữ liệu định dạng chuẩn để máy chủ AUR đọc thông tin phiên bản, bản quyền và danh sách gói phụ thuộc.

Lưu ý: Tên gói được đặt là `fcitx5-areca` để tránh trùng lặp với gói phần mềm Java mang tên `areca` trên kho phần mềm.

---

## 3. Quy trình phát hành lần đầu

### Bước 1: Xác nhận tag phiên bản trên GitHub
Phiên bản khai báo trong PKGBUILD phải có Git tag tương ứng trên remote repository GitHub trước khi build:
```bash
git ls-remote --tags https://github.com/xhkzeroone/ArecaIME.git v8.0.2
```

### Bước 2: Clone repository trắng từ AUR
```bash
git clone ssh://aur@aur.archlinux.org/fcitx5-areca.git
cd fcitx5-areca
```

### Bước 3: Sao chép cấu hình vào thư mục AUR
```bash
cp /home/xuanhong/Desktop/ArecaIME/packaging/aur/PKGBUILD .
cp /home/xuanhong/Desktop/ArecaIME/packaging/aur/.SRCINFO .
```

### Bước 4: Kiểm tra quá trình build cục bộ
Chạy lệnh sau để build thử và chạy test suite hoàn chỉnh:
```bash
makepkg --syncdeps --cleanbuild --check
```

Kiểm tra tính hợp lệ và tuân thủ tiêu chuẩn Arch Packaging bằng công cụ namcap:
```bash
namcap PKGBUILD fcitx5-areca-*.pkg.tar.zst
```

### Bước 5: Đẩy lên máy chủ AUR
AUR chỉ lưu trữ mã nguồn đóng gói, tuyệt đối không commit tệp nhị phân `.pkg.tar.zst`:
```bash
git add PKGBUILD .SRCINFO
git commit -m "Initial import: fcitx5-areca 8.0.2"
git push origin master
```

Sau khi hoàn tất, gói phần mềm sẽ xuất hiện công khai tại địa chỉ:
https://aur.archlinux.org/packages/fcitx5-areca

Người dùng Arch Linux có thể cài đặt trực tiếp qua yay, paru hoặc công cụ makepkg:
```bash
yay -S fcitx5-areca
```

---

## 4. Quy trình cập nhật phiên bản mới

Mỗi khi phát hành bản cập nhật mới cho ArecaIME:

### Bước 1: Tạo và đẩy tag mới lên GitHub
```bash
git tag -a vX.Y.Z -m "Release vX.Y.Z"
git push origin vX.Y.Z
```

### Bước 2: Xác định mã commit của submodule Bamboo
Nếu có thay đổi submodule Bamboo, lấy mã commit chính xác của submodule tại tag mới:
```bash
git ls-tree vX.Y.Z bamboo/bamboo-core
```

### Bước 3: Cập nhật PKGBUILD
Trong thư mục repository AUR:
1. Sửa `pkgver=X.Y.Z`
2. Đặt lại `pkgrel=1`
3. Cập nhật `_bamboo_commit` tương ứng nếu có thay đổi

### Bước 4: Tạo lại tệp .SRCINFO
Luôn đồng bộ lại `.SRCINFO` từ `PKGBUILD`:
```bash
makepkg --printsrcinfo > .SRCINFO
```

### Bước 5: Kiểm tra và đẩy thay đổi
```bash
makepkg --syncdeps --cleanbuild --check
git add PKGBUILD .SRCINFO
git commit -m "Update to X.Y.Z"
git push origin master
```
