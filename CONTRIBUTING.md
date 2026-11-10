# Đóng góp cho Areca IME 🌴

Areca IME là bộ gõ tiếng Việt C++20 cho Fcitx5 trên Linux Wayland, sử dụng hàng đợi FIFO bất đồng bộ, tối ưu độ trễ microsecond và tính ổn định cao.

Mọi đóng góp báo lỗi, đề xuất ý tưởng, tối ưu thuật toán hoặc gửi Pull Request đều được hoan nghênh.

---

## 👥 Tác giả & Đồng hành

### Tác giả & Phát triển chính
- **Kim Xuân Hồng** ([@kimxuanhong](https://github.com/kimxuanhong) / [@xhkzeroone](https://github.com/xhkzeroone))

### Cộng đồng đóng góp

<a href="https://github.com/xhkzeroone/ArecaIME/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=xhkzeroone/ArecaIME" alt="Areca IME Contributors" />
</a>

---

## 🛠️ Hướng dẫn phát triển

### 1. Môi trường cần thiết
- Trình biên dịch C++20: GCC 11+ hoặc Clang 13+
- CMake >= 3.20 & Ninja
- Go 1.18+
- Thư viện Fcitx5 development

### 2. Biên dịch & Kiểm thử
```bash
git clone https://github.com/xhkzeroone/ArecaIME.git
cd ArecaIME
git submodule update --init --recursive

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### 3. Quy trình gửi Pull Request
1. Fork repository và tạo nhánh mới từ `main`:
   ```bash
   git checkout -b feature/ten-tinh-nang
   ```
2. Làm theo toàn bộ [quy tắc bắt buộc](#-quy-tắc-bắt-buộc-cho-pull-request) bên dưới.
3. Rebase với `main` và gửi Pull Request theo mẫu có sẵn.

---

## ✅ Quy tắc bắt buộc cho Pull Request

PR vi phạm một quy tắc dưới đây sẽ được yêu cầu sửa trước khi review.

Bot Claude tự review PR mở từ nhánh trong repo, dựa trên các quy tắc này và skill `areca-pr`, `areca-code`. Với PR từ fork, maintainer comment `@claude review` để gọi bot. Nhận xét của bot chỉ để tham khảo, người review vẫn quyết định cuối cùng.

### Phạm vi
- Mỗi PR chỉ giải quyết một vấn đề. Không gộp refactor, đổi format hay sửa lỗi không liên quan vào cùng PR.
- Không tự tăng version, không sửa `packaging/` hay file release trừ khi PR là về đóng gói.
- Không commit thư mục build, file IDE, log hay file tạm.
- Không đổi submodule `bamboo/bamboo-core` nếu PR không cần đổi engine Bamboo.

### Định dạng mã nguồn
- Mã C++ phải theo `.clang-format` ở gốc repo. Trước khi commit chạy:
  ```bash
  git clang-format main
  ```
- Không format lại những file mà PR không sửa.
- CI chạy `clang-format` 23 trên `src/` và `tests/`. PR sai định dạng sẽ bị báo lỗi.
- Có thể bật hook tự format phần thay đổi đã stage mỗi lần commit:
  ```bash
  git config core.hooksPath .githooks
  ```
- Mã Go trong `bamboo-bridge/` phải qua `gofmt`.

### Build và kiểm thử
- Build sạch, không phát sinh cảnh báo biên dịch mới.
- `ctest --test-dir build --output-on-failure` phải qua toàn bộ.
- Sửa lỗi phải kèm test tái hiện lỗi đó. Tính năng mới phải kèm test cho hành vi mới. Test đặt trong `tests/` và đăng ký bằng `add_test` trong `CMakeLists.txt`.
- Thay đổi đường xử lý phím phải được gõ thử trên ứng dụng thật. Ghi rõ trong PR: tên app, Wayland hay X11, chế độ Rewrite hay Preedit, backend rewrite đang dùng.

### Luồng xử lý phím
- Không chặn main thread của fcitx5. Cấm gọi D-Bus đồng bộ, đọc file, `sleep`, chờ mutex lâu hay tính toán nặng trong callback phím, timer hoặc event loop. Việc chậm phải chạy trên thread riêng như `WindowFocusTracker` và chỉ trao kết quả qua atomic hoặc mutex ngắn.
- Mọi thao tác xoá chữ và commit chữ phải đi qua `InputScheduler` và rewrite backend. Không gọi `commitString` hay `deleteSurroundingText` ngoài luồng này vì sẽ phá thứ tự phím.
- Tôn trọng `shouldRejectReset()`: không reset engine khi rewrite đang chạy hoặc trong khoảng bảo vệ sau commit.
- Hạn chế thêm timer hoặc thời gian chờ mới. Mỗi timer phải có lý do trong comment và có cấu hình nếu là thời gian chờ ảnh hưởng tới người dùng.
- Không đổi hành vi mặc định của tính năng có sẵn nếu không nêu rõ lý do trong PR.

### Cấu hình
- Option mới khai báo trong `src/areca_config.h`, mặc định giữ nguyên hành vi cũ.
- Option người dùng thấy được phải có mặt đủ ở `gtk-settings/areca_settings.py` và `settings/ui_views.cpp`.

### Log
- Log debug chỉ in khi bật Debug, kiểm tra bằng `debugProvider_()` hoặc `debugEnabled()`.
- Mọi dòng log bắt đầu bằng `areca: ` và dùng dạng `key=value` để dễ grep.
- Không log nội dung ô mật khẩu hay dữ liệu nhạy cảm.

### Comment và tài liệu
- Comment viết tiếng Việt, ngắn, giải thích lý do thay vì mô tả lại code.
- Không chèn ngoặc đơn giải thích thừa.
- Không ghi tên dự án hay người mà kỹ thuật được tham khảo.

### Commit
- Tiêu đề commit bằng tiếng Anh, thể mệnh lệnh, viết hoa chữ đầu, không quá 72 ký tự, không dấu chấm cuối. Ví dụ: `Detect lagging surrounding snapshots and guard restore`.
- Thân commit giải thích vì sao thay đổi và ảnh hưởng tới người dùng nếu không hiển nhiên.
- Không thêm dòng `Co-Authored-By` hay `Generated with` của công cụ AI.
- Rebase lên `main` mới nhất, gộp các commit sửa vặt trước khi gửi.

---

## 🐛 Báo lỗi & Đề xuất tính năng

1. Kiểm tra danh sách [Issues đã có](https://github.com/xhkzeroone/ArecaIME/issues) trước khi tạo mới.
2. Cung cấp thông tin Distro Linux, Desktop Environment và ứng dụng phát sinh lỗi.
3. Đính kèm log debug từ lệnh `journalctl --user -f | grep areca` nếu có.

---

## 📜 Giấy phép
Dự án phát hành dưới giấy phép mã nguồn mở [MIT License](LICENSE).
