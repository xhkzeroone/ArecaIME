---
name: areca-pr
description: Kiểm tra và chuẩn bị thay đổi theo quy tắc đóng góp của ArecaIME trước khi commit hoặc tạo Pull Request. Dùng khi được yêu cầu tạo PR, chuẩn bị PR, review PR, hoặc kiểm tra một nhánh có đạt quy tắc đóng góp chưa.
---

# Chuẩn bị Pull Request cho ArecaIME

Quy tắc gốc nằm ở mục "Quy tắc bắt buộc cho Pull Request" trong `CONTRIBUTING.md`. Đọc mục đó trước, rồi làm lần lượt các bước dưới đây. Bước nào không đạt thì sửa hoặc báo lại cho người dùng, không bỏ qua.

## 1. Phạm vi
- Xem `git diff main...HEAD --stat` và `git log main..HEAD`.
- Diff chỉ được phục vụ một vấn đề. Thấy thay đổi không liên quan thì đề nghị tách PR.
- Diff không được chứa thư mục build, file IDE, log, đổi version, đổi `packaging/` hay đổi submodule `bamboo/bamboo-core` trừ khi PR là về chính phần đó.

## 2. Định dạng
- Chạy `git clang-format main` để format đúng những dòng đã sửa.
- Kiểm tra lại bằng `git clang-format --diff main`, kết quả phải rỗng.
- Có sửa file Go thì chạy `gofmt -l bamboo-bridge`, kết quả phải rỗng.
- Không chạy clang-format cho file mà PR không sửa.
- CI (`.github/workflows/format.yml`) kiểm tra toàn bộ `src/` và `tests/` bằng clang-format 23.1.4; PR không qua bước này sẽ không được merge.

## 3. Build và test
- Build vào thư mục riêng để không đụng build của người dùng:
  ```bash
  cmake -S . -B build-pr -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
  cmake --build build-pr -j
  ctest --test-dir build-pr --output-on-failure
  ```
- Build lâu thì chạy nền. Không được có cảnh báo compiler mới so với `main`.
- Sửa lỗi phải có test tái hiện lỗi. Tính năng mới phải có test. Test mới phải được đăng ký `add_test` trong `CMakeLists.txt` và xuất hiện trong kết quả `ctest`.

## 4. Rà soát luồng xử lý phím
Đọc diff trong `src/` và kiểm tra từng điểm:
- Không có lệnh chặn trên main thread của fcitx5: `dbus_connection_send_with_reply_and_block`, đọc file, `sleep`, `waitpid` không `WNOHANG`, vòng lặp nặng trong callback phím, timer hoặc IO event.
- Không gọi `commitString` hay `deleteSurroundingText` ngoài `InputScheduler` và các rewrite backend.
- Mọi reset engine đều tôn trọng `shouldRejectReset()`.
- Timer hoặc thời gian chờ mới có comment giải thích lý do và có cấu hình nếu ảnh hưởng tới người dùng.
- Hành vi mặc định của tính năng cũ không đổi, hoặc lý do đổi được ghi trong mô tả PR.

## 5. Cấu hình, log, comment
- Option mới có trong `src/areca_config.h`, mặc định giữ hành vi cũ, và có mặt ở `gtk-settings/areca_settings.py` lẫn `settings/ui_views.cpp` nếu người dùng thấy được.
- Log debug nằm trong điều kiện `debugProvider_()` hoặc `debugEnabled()`, bắt đầu bằng `areca: `, dạng `key=value`, không log dữ liệu nhạy cảm.
- Comment tiếng Việt, ngắn, giải thích lý do, không ngoặc đơn giải thích thừa, không nêu tên dự án hay người được tham khảo.

## 6. Commit
- Tiêu đề tiếng Anh, thể mệnh lệnh, viết hoa chữ đầu, tối đa 72 ký tự, không dấu chấm cuối.
- Thân commit nói vì sao thay đổi khi lý do không hiển nhiên.
- Không thêm `Co-Authored-By` hay `Generated with`.
- Nhánh đã rebase lên `main` mới nhất, commit sửa vặt đã được gộp.

## 7. Mô tả PR
- Điền theo `.github/PULL_REQUEST_TEMPLATE.md`.
- Bảng kiểm thử thực tế phải ghi app, Wayland hay X11, chế độ Rewrite hay Preedit, backend rewrite. Nếu chưa gõ thử thật thì ghi rõ là chưa thử, không tự điền kết quả.
- Chỉ đánh dấu mục checklist đã thực sự kiểm tra.

## Giới hạn
- Không `git push` và không tạo PR trên GitHub nếu người dùng chưa yêu cầu rõ.
- Kết thúc bằng bản tóm tắt: các bước đạt, các bước còn thiếu, và việc người dùng cần tự làm như gõ thử trên app thật.
