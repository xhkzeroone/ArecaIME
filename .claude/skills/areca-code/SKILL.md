---
name: areca-code
description: Quy tắc code, kiến trúc và pattern của ArecaIME. Dùng trước khi viết, sửa, refactor hoặc review code C++ trong src/, tests/, code Go trong bamboo-bridge/, hoặc khi thêm backend rewrite, option cấu hình, mode nhập liệu, log hay test.
---

# Quy tắc code ArecaIME

ArecaIME là addon fcitx5 viết bằng C++20. Engine ghép dấu là Bamboo viết bằng Go, gọi qua cgo. Mọi code chạy trên event loop của fcitx5 trừ những thành phần được ghi rõ là có thread riêng. Quy tắc gửi PR nằm ở skill `areca-pr` và `CONTRIBUTING.md`.

## Bản đồ kiến trúc

| Thành phần | File | Vai trò |
|---|---|---|
| Engine fcitx | `src/areca_engine*` | Nhận `keyEvent`, chọn mode, chọn backend, nạp config, nối mọi thành phần bằng provider |
| Mode | `src/mode_handler.h`, `rewrite_mode.*`, `preedit_mode.*`, `redirect_mode.*` | Mỗi mode implement `InputModeHandler` và giữ state riêng |
| Hàng đợi phím | `src/input_scheduler.*`, `key_queue.h` | FIFO single-flight: một phím xử lý xong mới tới phím sau, có post-commit barrier |
| Bamboo | `src/bamboo_engine_adapter.*`, `bamboo-bridge/main.go` | `VietnameseEngine` trả `BambooResult` gồm `deleteCount` và `commitText` |
| Rewrite backend | `src/rewrite_backend.h` và `*_backend.*` | Xoá chữ cũ rồi commit chữ mới theo `RewritePlan` |
| Thiết bị phát phím | `uinput_device.*`, `xtest_device.*`, `native_device.*` | Interface có hàm virtual để test thay bằng fake |
| Lag thích nghi | `adaptive_wait.h`, `adaptive_wait_monitor.*` | Tăng thời gian chờ khi timer bị trễ |
| Nhận diện app | `window_focus_tracker.*`, `atspi/`, `input_type_detector.*`, `program_compatibility.*` | AT-SPI chạy thread riêng, phần còn lại là hàm thuần |
| Surrounding text | `surrounding_text_*`, `surrounding_probe_match.h`, `surrounding_reliability.h` | Cache, probe, restore từ đã commit |
| Cấu hình | `src/areca_config.h` | `FCITX_CONFIGURATION`, có thêm UI ở `gtk-settings/` và `settings/` |

`ArecaEngine` được chia theo nhóm hàm, cùng một class. Thêm hàm mới vào đúng file:

- `areca_engine.cpp`: constructor, `activate`/`keyEvent`/`deactivate`/`reset`, chọn mode, warmup thiết bị, addon factory
- `areca_engine_backend.cpp`: chọn rewrite backend, đánh giá độ tin cậy, cache verdict, probe surrounding
- `areca_engine_backend_menu.cpp`: menu chọn backend theo app (phím `SelectBackendKey`)
- `areca_engine_config.cpp`: đọc/ghi config, `timing()`, `applyConfig()`
- `areca_engine_actions.cpp`: action trên status area và cập nhật UI

## Luồng xử lý một phím ở chế độ Rewrite
1. `ArecaEngine::keyEvent` lọc ô mật khẩu, terminal, menu chọn backend, phím phát lại từ uinput.
2. `RewriteModeHandler::handleKeyEvent` quyết định forward, reset, restore surrounding hoặc đưa vào `InputScheduler`.
3. `InputScheduler::processNext` gọi Bamboo, nhận `BambooResult`.
4. `deleteCount == 0` thì `commitString` ngay. Ngược lại tạo `RewritePlan` và gọi `RewriteBackend::apply`.
5. Backend trả `Completed`, `Pending` hoặc `Failed`. `Pending` phải gọi `RewriteDone` đúng một lần với đúng `transactionId`.
6. Scheduler chờ post-commit barrier rồi mới lấy phím kế tiếp. Backend `Failed` làm scheduler `stalled` để không phá thứ tự phím.

## Quy tắc bất biến
- Chỉ scheduler và rewrite backend được gọi `commitString` hoặc `deleteSurroundingText` trong chế độ Rewrite.
- Không reset engine khi `shouldRejectReset()` trả true.
- Không chặn event loop. D-Bus đồng bộ, đọc file, `sleep`, chờ tiến trình, tính toán nặng phải chạy trên thread riêng. Thread chỉ đưa kết quả về qua `std::atomic` hoặc mutex giữ rất ngắn, như `WindowFocusTracker`.
- Không sửa state của engine hay input context từ thread khác. Dữ liệu từ thread hoặc tiến trình ngoài được đưa vào event loop qua `addIOEvent` hoặc chỉ đặt cờ để keyEvent xử lý, như `MouseClickTracker`.
- Hành vi mặc định không đổi khi thêm option mới.

## Pattern bắt buộc

### Phụ thuộc qua provider
Class không đọc config hay gọi `ArecaEngine` trực tiếp. Truyền `std::function` qua constructor và đặt tên alias rõ nghĩa:
```cpp
using DebugProvider = std::function<bool()>;
using ContextBoolProvider = std::function<bool(fcitx::InputContext &)>;
```
Provider được gọi lại mỗi lần dùng để bật tắt config có hiệu lực ngay, không cache giá trị.

### State theo input context
State của từng ô nhập là `struct XxxInputState final : public fcitx::InputContextProperty`, lấy bằng `inputContext.propertyFor(&factory)`. Không dùng map toàn cục theo con trỏ input context.

### Giữ tham chiếu input context
Callback bất đồng bộ giữ `inputContext.watch()` và kiểm tra `.get()` trước khi dùng. Không giữ `InputContext *` hay reference qua timer.

### Rewrite backend mới
- Kế thừa `RewriteBackend`, implement `name()` và `apply()`.
- `apply()` trả `Failed` ngay nếu đang có transaction hoặc `transactionId == 0`.
- Lưu `transactionId_`, `onDone_`, `inputContext_` trước khi gây tác dụng phụ vì frontend có thể gọi lại đồng bộ.
- Mọi timer lưu trong `std::unique_ptr<fcitx::EventSourceTime>`, gọi `setOneShot()`, trong callback `std::move` timer ra biến cục bộ trước khi chạy tiếp.
- Commit sau thời gian chờ dùng `EventLoopPostTask` để chạy ở pha post. Không dùng timer accuracy 0 để giả lập.
- Có `clearPending()` đặt lại toàn bộ state, gọi trong destructor và khi hoàn tất.
- Mất input context thì hoàn tất không commit và báo `Succeeded`, không treo scheduler.
- Thời gian chờ lấy từ `RewritePlan`, chọn theo frontend bằng hàm `resolve...WaitMs`. Không hard-code số ms.
- Nếu dùng `AdaptiveWait` thì gọi `beginTransaction`, `completeTransaction` hoặc `cancelTransaction` cân xứng.

### Config
- Khai báo trong `FCITX_CONFIGURATION` ở `src/areca_config.h`. Số dùng `fcitx::Option<int, fcitx::IntConstrain>` có giới hạn. Option nội bộ dùng `HiddenOption`.
- Nhãn hiển thị tiếng Việt bọc trong `N_()`.
- Key ini dạng PascalCase, tên biến camelCase cùng nghĩa.
- Option thời gian có hậu tố `Ms`. Option theo frontend có tiền tố `wayland`, `xim`, `fcitx4`, `dbus`, `ibus`.
- Option người dùng thấy được phải thêm ở `gtk-settings/areca_settings.py` và `settings/ui_views.cpp`.

### Log
- Chỉ log khi `debugProvider_()` hoặc `debugEnabled()` trả true. Lỗi thật dùng `FCITX_ERROR()` không cần điều kiện.
- Mở đầu `areca: `, theo sau là tên thành phần rồi các cặp `key=value`, ví dụ `areca: uinput-backspace start tx=12 backspaces=2`.
- Có `tx=` khi log thuộc một transaction rewrite.

### Bamboo bridge
- Hàm Go export tên `ArecaBamboo...`, nhận handle `uint64_t`. Chuỗi trả về cấp bằng `C.CString` và phía C++ phải `free`.
- Gọi cgo đồng bộ, nên giữ hàm Go ngắn và không làm việc nặng trên mỗi phím.
- C++ chỉ dùng Bamboo qua `VietnameseEngine` để test thay được bằng engine giả.

## Phong cách
- Format theo `.clang-format` ở gốc repo, chạy `git clang-format` cho dòng đã sửa.
- Mọi code nằm trong `namespace areca`. Hàm phụ trợ chỉ dùng trong một file đặt trong namespace ẩn danh.
- Header dùng `#pragma once`. Thứ tự include: thư viện chuẩn, fcitx, rồi header của dự án, mỗi nhóm cách một dòng trống.
- Đặt tên: class PascalCase, hàm và biến camelCase, member có hậu tố `_`, hằng số `kName` hoặc `static constexpr` PascalCase trong class.
- Class không định kế thừa đánh dấu `final`. Override luôn ghi `override`.
- Class giữ tài nguyên xoá copy constructor và copy assignment.
- Dùng `std::string_view` và `starts_with` để so tên frontend hay chương trình.
- Đơn vị thời gian ghi trong tên biến: `Ms`, `Usec`. Thời điểm lấy bằng `fcitx::now(CLOCK_MONOTONIC)`.
- Bắt `std::exception` quanh lời gọi Bamboo, lỗi thì reset engine và để phím đi tiếp thay vì crash fcitx5.

## Comment
- Tiếng Việt, ngắn, nói vì sao chứ không kể lại code làm gì.
- Ghi lại quirk của ứng dụng hay frontend cụ thể đã gây ra code đó.
- Không ngoặc đơn giải thích thừa. Không nêu tên dự án hay người mà kỹ thuật được tham khảo.
- Đánh dấu chỗ tạm thời bằng `TODO:` kèm điều kiện để gỡ.

## Test
- Mỗi test là một file `tests/xxx_test.cpp` có `main()` dùng `assert`, build với `-Wall -Wextra -Wpedantic -UNDEBUG`, đăng ký trong `CMakeLists.txt` bằng `add_executable`, `target_include_directories` trỏ tới `src`, và `add_test`.
- Logic thuần nên tách thành hàm hoặc header không phụ thuộc fcitx để test trực tiếp, như `adaptive_wait.h`, `surrounding_text_restore.*`, `surrounding_probe_match.h`.
- Backend test dùng `fcitx::EventLoop` thật, `InputContext` giả override `commitStringImpl`, `deleteSurroundingTextImpl`, `forwardKeyImpl`, và thiết bị giả kế thừa `UinputDevice` hoặc `XTestBackspaceDevice`.
- Cần đọc state riêng thì dùng `friend struct XxxTestAccess`, không mở public chỉ để test.
- Test hành vi thời gian so sánh thứ tự và khoảng tối thiểu, không so bằng chính xác số micro giây.
