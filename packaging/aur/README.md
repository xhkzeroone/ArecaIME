# Publish `fcitx5-areca` lên AUR

Thư mục này dành cho repository AUR của package ổn định. Không dùng
`packaging/arch/PKGBUILD`: file đó nhận source tarball do GitHub Actions tạo
trước và chỉ phục vụ job CI.

## Chuẩn bị tài khoản

1. Tạo tài khoản tại <https://aur.archlinux.org/register>.
2. Thêm public SSH key trong trang **My Account** của AUR.
3. Kiểm tra kết nối:

   ```bash
   ssh aur@aur.archlinux.org help
   ```

Tên package được dùng là `fcitx5-areca` để không xung đột với package Java cũ
đang dùng tên `areca`.

## Publish lần đầu

Tag GitHub tương ứng phải tồn tại trước khi build. Với bản hiện tại:

```bash
git ls-remote --tags https://github.com/xhkzeroone/ArecaIME.git v8.0.2
```

Clone repository AUR mới, chép đúng hai file package rồi test:

```bash
git -c init.defaultBranch=master clone \
  ssh://aur@aur.archlinux.org/fcitx5-areca.git aur-fcitx5-areca
cd aur-fcitx5-areca
cp /path/to/ArecaIME/packaging/aur/PKGBUILD .
cp /path/to/ArecaIME/packaging/aur/.SRCINFO .
makepkg --syncdeps --cleanbuild --check
namcap PKGBUILD fcitx5-areca-*.pkg.tar.zst
git add PKGBUILD .SRCINFO
git commit -m "Initial import: fcitx5-areca 8.0.2"
git push origin master
```

AUR chỉ nhận metadata/source recipe, không nhận file `.pkg.tar.zst` vừa build.

## Cập nhật phiên bản

Sau khi tạo tag GitHub mới:

1. Sửa `pkgver`, đặt lại `pkgrel=1`.
2. Lấy gitlink Bamboo của tag và cập nhật `_bamboo_commit`:

   ```bash
   git ls-tree vX.Y.Z bamboo/bamboo-core
   ```

3. Sinh lại `.SRCINFO`, build thử và push repository AUR:

   ```bash
   makepkg --printsrcinfo > .SRCINFO
   makepkg --syncdeps --cleanbuild --check
   git add PKGBUILD .SRCINFO
   git commit -m "Update to X.Y.Z"
   git push
   ```

Không được quên `.SRCINFO`; AUR dùng file này để hiển thị version và dependency.
