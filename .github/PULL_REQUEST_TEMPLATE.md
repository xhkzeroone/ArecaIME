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
1. Biên dịch thành công với CMake: `cmake --build build -j$(nproc)`
2. Toàn bộ unit tests đều vượt qua: `ctest --test-dir build --output-on-failure`
3. Kiểm tra thực tế trên ứng dụng:

| Ứng dụng | Wayland/X11 | Chế độ | Backend rewrite | Kết quả |
|---|---|---|---|---|
|  |  |  |  |  |

## Checklist trước khi gửi
Xem đầy đủ tại [quy tắc bắt buộc cho Pull Request](https://github.com/xhkzeroone/ArecaIME/blob/main/CONTRIBUTING.md#-quy-tắc-bắt-buộc-cho-pull-request).
- [ ] PR chỉ giải quyết một vấn đề
- [ ] Đã chạy `git clang-format main`, không format lại file không liên quan
- [ ] Build sạch, không phát sinh cảnh báo biên dịch mới
- [ ] Đã chạy và vượt qua toàn bộ unit test
- [ ] Có test mới cho lỗi được sửa hoặc tính năng được thêm
- [ ] Không chặn main thread của fcitx5, không commit chữ ngoài `InputScheduler`
- [ ] Option mới mặc định giữ hành vi cũ và có trong các giao diện settings
- [ ] Log debug có kiểm tra Debug và bắt đầu bằng `areca: `
- [ ] Đã gõ thử trên ứng dụng thật và ghi vào bảng trên
- [ ] Commit theo quy tắc, không có dòng attribution của công cụ AI
