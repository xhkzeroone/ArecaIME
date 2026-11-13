---
name: areca-release
description: Quy trình bắt buộc khi ra bản ArecaIME (fcitx5-areca) - tăng version, viết release note, tạo tag và push. Dùng mỗi khi được bảo tăng version, bump, tạo tag, ra release, viết release note hoặc sửa job release trong CI.
---

# Ra bản ArecaIME

Làm đủ các bước theo thứ tự. Thiếu bước nào thì không được tạo tag.

## 1. Tăng version
Đổi số version (ví dụ `8.3.13` → `8.3.14`) ở **tất cả** các chỗ sau, rồi `grep` số cũ để chắc không sót:
- `CMakeLists.txt` (`project(... VERSION x.y.z ...)`)
- `gtk-settings/areca_settings.py` (`APP_VERSION = "vx.y.z"`)
- `packaging/arch/PKGBUILD`, `packaging/aur/PKGBUILD` (`pkgver`, giữ `pkgrel=1`)
- `packaging/aur/.SRCINFO` (`pkgver` và `#tag=vx.y.z` trong `source`)
- Ví dụ lệnh trong `docs/AUR_PUBLISHING_GUIDE.md` và `packaging/aur/README.md`

## 2. Viết release note (bắt buộc)
Tạo `docs/release-notes/vx.y.z.md`, commit **cùng commit tăng version**. Job release trong
`.github/workflows/package-linux.yml` gọi `.github/scripts/release-notes.sh` để ghép:
danh sách PR GitHub tự sinh ("What's Changed", "Full Changelog") + file này + đoạn cài đặt cố định.

Vì vậy file **chỉ chứa phần viết tay**. Không chép danh sách PR, link compare, lệnh cài đặt
hay lời cảm ơn cuối, vì script đã thêm các phần đó.

Mẫu:
```markdown
## Areca vx.y.z

### Tính năng mới
- **Tên ngắn của thay đổi.** Một hai câu cho người dùng: thấy gì khác, bật/tắt ở đâu.

### Sửa lỗi
- **Triệu chứng người dùng gặp.** Nguyên nhân bằng lời thường và giờ Areca làm gì. Có người báo/gửi PR thì cảm ơn `@tên` kèm PR #số.
```
- Viết tiếng Việt cho người dùng cuối, không nêu tên hàm, tên class hay hash commit.
- Chỉ giữ các mục có nội dung (`Tính năng mới`, `Thay đổi`, `Sửa lỗi`, `Phát hành`...).
- Nội dung lấy từ `git log v<bản trước>..HEAD`. Không bịa thay đổi chưa có trong log.

## 3. Commit, tag, push
- Commit: `Bump package version to x.y.z` (không có dòng attribution).
- Tag **annotated**: `git tag -a vx.y.z -m "vx.y.z"`.
- Chỉ push khi người dùng bảo trong lần yêu cầu này: `git push origin main` rồi `git push origin vx.y.z`.
  Không tạo hoặc sửa GitHub Release bằng tay, CI sẽ tạo khi tag được push.
- Tag đã có trên remote (`git ls-remote --tags origin vx.y.z`) thì lấy số tiếp theo, không ghi đè tag.

## 4. Báo lại
Báo hash commit, tên tag, kết quả push, và nói rõ chưa kiểm tra CI/trang release nếu chưa xem.
