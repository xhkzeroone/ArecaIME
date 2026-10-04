## Mô tả thay đổi
Mô tả tóm tắt nội dung thay đổi và mục đích của Pull Request này.

## Issue liên quan
Đóng hoặc liên kết tới issue nếu có:
- Fixes #
- Closes #

## Loại thay đổi
Đánh dấu chọn loại thay đổi phù hợp:
- [ ] Bug fix - Sửa lỗi hiện có
- [ ] New feature - Thêm tính năng mới
- [ ] Refactor - Tối ưu hoặc cấu trúc lại mã nguồn
- [ ] Performance - Cải thiện hiệu năng xử lý phím
- [ ] Documentation - Cập nhật tài liệu
- [ ] Build/CI - Cấu hình CMake hoặc GitHub Actions

## Các bước kiểm thử đã thực hiện
Mô tả cách bạn đã kiểm tra thay đổi này:
1. Biên dịch thành công với CMake: `cmake --build build -j$(nproc)`
2. Toàn bộ unit tests đều vượt qua: `ctest --test-dir build --output-on-failure`
3. Kiểm tra thực tế trên ứng dụng: ghi rõ tên app đã test như Chrome, LibreOffice, VS Code...

## Checklist trước khi gửi
- [ ] Mã nguồn tuân thủ coding style hiện tại của dự án
- [ ] Đã chạy và vượt qua toàn bộ unit test
- [ ] Đã kiểm tra tính năng gõ thực tế trên môi trường Wayland
- [ ] Không làm phát sinh cảnh báo biên dịch mới
