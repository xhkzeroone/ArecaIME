---
name: areca-ibus-port
description: Kiến thức để viết lại Areca (fcitx5-areca) thành engine IBus (ibus-areca). Dùng khi thiết kế, viết hoặc review ibus-areca, khi cần biết pipeline gõ của Areca, chính sách chọn backend xoá chữ, cách xử lý surrounding text trễ, các danh sách ứng dụng đặc biệt, hoặc cách đổi một khái niệm fcitx5 sang IBus.
---

# Viết lại Areca cho IBus

Areca là bộ gõ tiếng Việt chạy dạng addon fcitx5, C++20, dùng `bamboo-core` (Go) qua cgo. Skill này gom những gì cần mang sang ibus-areca, những gì thuộc riêng fcitx5, và các bài học đã trả giá. Chi tiết đầy đủ nằm ở `docs/ARCHITECTURE.md`, `docs/ARECA_ARCHITECTURE_SPECIFICATION.md`, `docs/DEBUGGING.md` của repo fcitx5-areca. Quy tắc code chung xem skill `areca-code`.

## Mang sang nguyên vẹn, không phụ thuộc fcitx5

| Phần | File | Ghi chú |
|---|---|---|
| Cầu nối Bamboo | `bamboo-bridge/main.go`, submodule `bamboo/bamboo-core` | Hàm export `ArecaBamboo*` nhận handle `uint64_t`, chuỗi trả về cấp bằng `C.CString`, phía C++ phải `free` |
| Adapter và diff | `src/bamboo_engine_adapter.*`, `src/types.h` | Interface `VietnameseEngine`, kết quả `BambooResult` |
| Tách từ, tự viết hoa | `src/word_segment.*`, `src/sentence_capitalizer.*` | Hàm thuần |
| Khôi phục từ đã commit | `src/surrounding_text_restore.*` | Extractor từ trước con trỏ, tối đa 16 ký tự |
| So khớp snapshot | `src/surrounding_probe_match.h` | `surroundingProbeMatches`, `surroundingSnapshotEndsWith` (NBSP coi như dấu cách), `updateKnownTextBeforeCursor` |
| Logic preedit | `src/preedit_logic.*` | |
| Thời gian chờ thích nghi | `src/adaptive_wait.h` | Logic thuần; phần đo máy bận trong `adaptive_wait_monitor.*` cần viết lại cho GLib |
| Nhận diện ứng dụng | `src/program_compatibility.*`, `src/browser_autocomplete.*` | Danh sách terminal, IDE, office, trình duyệt, họ VS Code, Discord/Signal |
| Thiết bị phát phím | `src/uinput_device.*`, `src/xtest_device.*`, `src/native_device.*` | uinput không phụ thuộc IM framework; Native dùng libei qua RemoteDesktop portal hoặc XTest |
| AT-SPI | `src/window_focus_tracker.*`, `src/atspi/` | Thread riêng, xem bài học bên dưới |
| Click chuột | `src/mouse_click_tracker.*`, `areca-mouse-monitor` | Process con libinput, báo qua pipe |

Bamboo bridge cung cấp: `ArecaBambooCreate(method, modernStyle)`, `Destroy`, `AddMacro`, `ExpandMacro`, `InputMethodNames`, `CharsetNames`, `Encode`, `CanProcess`, `Process(key, spellCheck, *restored)`, `Restore(visibleText)`, `FinalizeWord`, `Backspace`, `RecoverBackspace`, `Reset`.

## Pipeline gõ (giữ nguyên ở IBus)

**Diff.** Sau mỗi phím, adapter lấy chuỗi mới từ Bamboo, **encode theo bảng mã trước**, tìm common prefix UTF-8, rồi trả `currentText`, `newText`, `deleteCount` (số ký tự đuôi cần xoá), `commitText` (đuôi mới cần chèn). Ví dụ Telex `a` rồi `w` cho `deleteCount=1`, `commitText="ă"`. Encode trước diff thì các trường luôn đúng với chuỗi đã nằm trong ứng dụng, kể cả VNI Windows hay VIQR.

**Ba chế độ.**
- **Rewrite** (mặc định): chữ được commit ngay vào ứng dụng; khi cần sửa dấu thì xoá `deleteCount` ký tự rồi commit `commitText`.
- **Preedit**: giữ từ đang gõ trong preedit, commit ở word boundary. Xử lý đồng bộ, không dùng scheduler.
- **Redirect**: forward phím nguyên bản.

Ứng dụng có thể ghi đè chế độ hoặc backend qua `areca-apps.conf`. Phím `SelectBackendKey` (mặc định dấu huyền) mở menu chọn ngay trong ứng dụng.

**Scheduler Rewrite (bất biến quan trọng nhất).**
- FIFO single-flight: một phím xử lý xong mới tới phím sau. Phím đến khi đang bận chỉ được nối vào cuối hàng.
- Backend báo `Pending` thì phải gọi `RewriteDone` đúng một lần, đúng `transactionId`. Backend báo `Failed` làm scheduler dừng hẳn (fail closed) để không phá thứ tự phím.
- Sau mỗi commit có post-commit barrier `PostCommitDelayMs` (20 ms) trước khi lấy phím kế.
- **Chặn reset 50 ms sau commit** (`kResetRejectWindowUsec`), và chặn suốt lúc đang rewrite. Trình duyệt hay gửi reset vài chục ms sau khi chữ trong ô đổi; để lọt reset đó thì mất từ đang gõ và restore chạy giữa từ, gây lặp chữ. Đánh đổi: Backspace hay phím mũi tên thật của người dùng trong 50 ms đó bị coi như phím Areca tự phát.
- Chỉ scheduler và backend được commit hoặc xoá surrounding ở Rewrite.

**Phím đầu từ khi rảnh** (`ForwardFirstCharacter`, trình duyệt): Bamboo xử lý phím đúng một lần; nếu đầu ra không đổi thì để phím gốc đi tiếp cho ứng dụng (editor web cần thấy key event thật để mở chế độ soạn thảo).

## Backend xoá chữ và cách chọn

| Backend | Cách xoá | Xác nhận |
|---|---|---|
| Surrounding V1 | `deleteSurroundingText(-N, N)` một lần | Probe lần đầu |
| Surrounding V2 | `(-1, 1)` N lần | Xác nhận lần xoá cuối |
| uinput Backspace | Backspace × N qua `/dev/uinput` | Đếm Backspace quay về engine (ACK) |
| uinput Shift-select | Shift + Left × N rồi commit đè vùng chọn | Shift up quay về |
| Native Backspace / Shift-select | Như trên qua libei hoặc XTest | Không ACK |
| ForwardKey Backspace | `forwardKey(Backspace)` × N | Không ACK |

Thứ tự chọn khi `deleteCount > 0` (`selectRewriteBackend`, `src/areca_engine_backend.cpp`):
1. Terminal: Backspace, ưu tiên phím phần cứng.
2. Gợi ý autocomplete của trình duyệt hoặc đang có vùng chọn: ô URL dùng Shift-select; ô khác Backspace, thêm 1 Backspace nếu vùng chọn là gợi ý ngay sau từ.
3. Ô URL không có surrounding: Shift-select.
4. `UseBackspaceForUrl` và ô URL: Shift-select, không có thì Backspace.
5. Ghi đè theo ứng dụng.
6. Tự động: surrounding nếu verdict cho phép; trình duyệt + `ShiftSelectFallbackForBrowser` thì Shift-select; còn lại Backspace.

**Verdict surrounding** (theo input context, xoá khi focus, reset hay click trừ khi đang được bảo vệ 1 s sau Backspace, Ctrl+A, di chuyển):
- Snapshot có text, cursor và anchor đều lớn hơn 0 thì được tin và vào probe.
- Ép Backspace: chương trình lập trình, terminal, office (`isProgrammingProgram`, `isTerminalProgram`, `isOfficeProgram`), và họ VS Code với capability mask `0x72`. **Giữ các danh sách này**: chúng chặn những lỗi khác với snapshot trễ, ví dụ LibreOffice báo snapshot đúng nhưng xoá bất đồng bộ.
- **Kiểm tra snapshot trễ chạy trước khi gửi lệnh xoá**: lúc probe, snapshot phải kết thúc bằng từ đang hiện trong ô; không thì probe thất bại ngay, dùng phím, thử lại một lần ở từ kế tiếp.
- Probe gửi lệnh xoá rồi chờ snapshot mới khớp; khi đáng lẽ không còn gì trước con trỏ thì snapshot phải rỗng mới tính là thành công. Chờ 150 ms lần đầu, 80 ms lần thử lại; hết hạn thì chuyển sang backend dự phòng.

## Restore (sửa lại từ đã gõ)

Trạng thái hiện tại, đã chốt sau nhiều vòng thử:
- `RestoreSurroundingText` **mặc định tắt**, chỉ áp dụng cho Rewrite. Preedit tắt hẳn restore từ surrounding (vẫn giữ code).
- Restore là cờ one-shot, bật khi focus, reset hoặc click; theo sự kiện surrounding update chỉ khi Areca **không biết** text trước con trỏ.
- Mỗi mode giữ `knownTextBeforeCursor`: scheduler báo qua `CommitObserver` mỗi lần xoá N ký tự rồi chèn text. Bị xoá khi focus, reset, click, hoặc khi ứng dụng tự xử lý một phím (event không bị filter).
- Trước mỗi lần restore, snapshot phải kết thúc bằng `knownTextBeforeCursor`; lệch thì chỉ bỏ qua lần đó, **không khoá theo ô**.
- Rewrite không restore cho ứng dụng có backend gần nhất là Backspace hay Shift-select.

## Đổi khái niệm fcitx5 sang IBus

API IBus dưới đây là tên trong libibus 1.5. Kiểm tra lại với bản IBus mục tiêu trước khi dựa vào, nhất là các chỗ đánh dấu (?).

| fcitx5 | IBus |
|---|---|
| `InputMethodEngine::keyEvent`, `filterAndAccept()` | Signal/vfunc `process-key-event(keyval, keycode, state)`, trả `TRUE` nghĩa là đã nhận. Phím nhả có `IBUS_RELEASE_MASK` |
| Không filter để phím đi tiếp | Trả `FALSE` |
| `forwardKey` | `ibus_engine_forward_key_event(keyval, keycode, state)` |
| `commitString` | `ibus_engine_commit_text(IBusText*)`; gọi được bất cứ lúc nào, nên commit trễ sau timer vẫn được |
| `deleteSurroundingText(offset, n)` | `ibus_engine_delete_surrounding_text(offset, nchars)` |
| `surroundingText()` và `SurroundingTextUpdated` | `ibus_engine_get_surrounding_text(&text, &cursor, &anchor)` và signal `set-surrounding-text`. Client phải có `IBUS_CAP_SURROUNDING_TEXT`; IBus chỉ bắt đầu gửi snapshot sau khi engine gọi `get_surrounding_text` ít nhất một lần |
| Preedit | `ibus_engine_update_preedit_text_with_mode(text, cursor, visible, IBUS_ENGINE_PREEDIT_COMMIT)` để preedit được commit khi mất focus |
| `CapabilityFlag::Password`, `Url`, `Terminal` | `set-content-type(purpose, hints)` với `IBUS_INPUT_PURPOSE_PASSWORD`, `_URL`, `_TERMINAL` |
| `capabilityFlags()` | `set-capabilities` (`IBUS_CAP_PREEDIT_TEXT`, `IBUS_CAP_SURROUNDING_TEXT`…) |
| `activate` / `deactivate` / `reset` | `enable`, `focus-in` / `focus-out`, `disable` / `reset` |
| `InputContext::program()` | Không có sẵn. `focus-in-id(object_path, client)` (IBus 1.5.27+?) cho chuỗi client kiểu `gtk3-im:<prgname>`; ngoài ra dùng AT-SPI như hiện tại |
| Event loop fcitx (`EventSourceTime`, `EventLoopPostTask`) | GLib main loop: `g_timeout_add`, `g_idle_add`; IBus engine chạy trên main loop của `ibus-daemon` client process |
| `fcitx::SimpleAction` trên StatusArea | `IBusPropList` + `IBusProperty` (`PROP_TYPE_TOGGLE`, `RADIO`, `MENU`) qua `ibus_engine_register_properties` và `property-activate`. GNOME Shell vẽ `TOGGLE` thành công tắc gạt trong menu nguồn nhập |
| Addon config `.conf` + `ReloadAddonConfig` qua D-Bus | Tự đọc file cấu hình (giữ định dạng `areca*.conf` để dùng lại app cài đặt GTK) hoặc GSettings; theo dõi thay đổi bằng `GFileMonitor` |
| Đăng ký addon | File component XML trong `/usr/share/ibus/component/` khai báo engine `areca` |
| Frontend `wayland`/`dbus`/`xim`… với timing riêng | IBus có đường GTK/Qt IM module, XIM, và Wayland qua compositor; cần đo lại các thời gian chờ theo từng đường |

Lưu ý khi port scheduler: IBus đòi `process-key-event` trả về ngay. Như fcitx5, nhận phím (`TRUE`), đưa vào FIFO, rồi xoá và commit bất đồng bộ bằng timer GLib. Đừng chặn main loop chờ ACK.

## Bài học đã trả giá

- **Snapshot trễ là chuyện thường.** Firefox trên đường text-input-v3 (Wayland) và một số app báo surrounding chậm đúng một nhịp. Hàm so khớp lỏng (`ends_with("")` luôn đúng) từng làm app không hề xoá được bị xếp là "xoá được bằng surrounding", gây `goõ`. Firefox qua module GTK (`GTK_IM_MODULE=fcitx`, frontend D-Bus) thì snapshot đồng bộ.
- **Snapshot trễ khác lệnh xoá hỏng.** Một app có thể báo trễ mà vẫn xoá đúng (Firefox), hoặc báo đúng mà xoá sai (LibreOffice). Kiểm tra sau commit kiểu "đoán rồi sửa bằng Backspace" đã thử và bị bỏ vì sửa nhầm thì xoá mất chữ đúng.
- **Đừng khoá tính năng theo input context.** Trình duyệt dùng chung một context cho mọi ô trong cửa sổ; một ô lỗi sẽ kéo cả cửa sổ. Chỉ bỏ qua đúng lần không chắc chắn.
- **Bản sao snapshot cục bộ không phải bằng chứng.** Areca tự sửa snapshot sau mỗi lần xoá hoặc commit mà không phát sự kiện; chỉ snapshot do ứng dụng gửi mới dùng để kết luận.
- **Wayland text-input-v3 không cho tên chương trình.** AT-SPI (`GetConnectionUnixProcessID` → `/proc/<pid>/comm`) là nguồn dự phòng, nhưng `comm` cắt ở 15 ký tự, và app không bật accessibility sẽ khiến tracker giữ tên app trước (từng nhận nhầm `ghostty`).
- **D-Bus trên thread riêng phải dùng kết nối private.** `dbus_connection_open` trả kết nối dùng chung; gọi `dbus_connection_close` trên nó làm libdbus abort cả tiến trình lúc thoát. Dùng `dbus_connection_open_private` / `dbus_bus_get_private` và `set_exit_on_disconnect(FALSE)`.
- **AT-SPI tốn kém.** Gộp sự kiện focus, công bố tên app ngay khi có PID, chỉ dò cây cha khi thiếu PID, timeout 200 ms, poke không chờ trả lời.
- **Máy bận làm hỏng timing.** Adaptive wait cộng thời gian chờ khi load cao (Docker VM, indexer); cân nhắc không cộng theo load average khi port.
- **Chromium báo dấu cách cuối dòng thành NBSP.** So khớp phải coi NBSP như dấu cách.

## Cấu hình người dùng cần giữ

`areca.conf` (khoá ini, mặc định): `BambooInputMethod` (Telex 2), `OutputCharset` (Unicode), `SpellcheckMode` (khôi phục ngay khi gõ), `ModernStyle` (true), `EnableMacro` (true), `CapitalizeMacro` (true), `RevertMacroOnBackspace` (true), `MacroBypassModifier` (Shift), `AutoCapitalizeAfterPunctuation` (false), `BackspaceRecovery` (true), `RestoreSurroundingText` (false), `ShiftSelectFallbackForBrowser` (false), `UseBackspaceForUrl` (true), `DisableVietnameseInTerminal` (false), `Debug` (false). `areca-advanced.conf` giữ timing từng backend và các lựa chọn uinput/Native, mouse tracking, phím đầu. Định nghĩa đầy đủ trong `src/areca_config.h`.

## Kiểm thử

- Mang theo các test logic thuần trong `tests/` (adapter Bamboo, tách từ, so khớp snapshot, restore, scheduler, backend với `InputContext` giả). Thay `InputContext` giả bằng lớp giả của engine IBus.
- Kịch bản tay bắt buộc trên mỗi đường frontend: gõ `gox ngon laf duwojc` (phải ra `gõ ngon là được`), thêm dấu liên tục trong một từ, Ctrl+A rồi Backspace rồi gõ tiếp, click vào giữa từ cũ rồi sửa dấu, thanh địa chỉ và ô có gợi ý của Chrome, Firefox, LibreOffice, terminal.
- Bật log debug và đọc theo `docs/DEBUGGING.md`: `rewrite select backend`, `reliability first-probe`, `surrounding snapshot stale during probe`, `surrounding restore …`, `reset rejected`.
