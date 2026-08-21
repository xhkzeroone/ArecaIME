# Areca IME

Areca là bộ gõ tiếng Việt cho Linux dưới dạng addon Fcitx5, viết bằng C++20 và
chạy chủ yếu trên Wayland. Areca dùng trực tiếp `bamboo-core` để xử lý tiếng
Việt, nhưng tự quản lý thời điểm xử lý phím và cách sửa nội dung đã hiển thị.

Điểm khác biệt chính của Areca là mọi **text key do addon xử lý** đều đi qua
hàng đợi FIFO. Khi pipeline rảnh, key đầu được pump ngay trong callback;
scheduler vẫn tuần tự hoá toàn bộ pipeline và không cho phím sau chen vào một
rewrite đang phát Backspace hoặc barrier sau commit.

> Areca hiện là dự án thử nghiệm. Backend fallback phát Backspace qua
> `InputContext::forwardKey()`; hãy bật debug khi thử trên frontend mới.

## Lời cảm ơn

Areca sử dụng [Bamboo Engine / bamboo-core](https://github.com/BambooEngine/bamboo-core),
thư viện xử lý tiếng Việt do **Lương Thành Lâm** phát triển và phát hành theo
giấy phép MIT. Phần chuyển đổi Telex và các quy tắc tiếng Việt của Areca đến từ
chính `bamboo-core`; Areca chỉ bổ sung lớp tích hợp Fcitx5, scheduler và các
backend rewrite.

Xin cảm ơn tác giả Bamboo cùng những người được dự án Bamboo ghi công:

- Trung Ngo, tác giả `bogo.js`.
- Trần Kỳ Nam, tác giả `GoTiengViet`.

Mã nguồn và license nguyên bản được giữ trong submodule
[`bamboo/bamboo-core`](bamboo/bamboo-core).

## Ý tưởng thiết kế

Ứng dụng Wayland không phải lúc nào cũng cung cấp `SurroundingText` chính xác.
Trong khi đó, gõ tiếng Việt kiểu Telex thường cần sửa lại những ký tự đã xuất
hiện, ví dụ `a` + `w` biến thành `ă`. Areca giải quyết bài toán này bằng ba lớp:

1. `bamboo-core` quyết định chuỗi tiếng Việt mới.
2. Scheduler quyết định **khi nào** được xử lý phím tiếp theo.
3. Rewrite backend quyết định **cách** xoá chuỗi cũ và chèn chuỗi mới.

```text
Fcitx5 keyEvent
       │ filter text key
       ▼
 KeyQueue (FIFO)
       │ pump ngay nếu pipeline đang rảnh
       │ giữ lại nếu commit/rewrite đang pending
       ▼
 InputScheduler ──► BambooEngineAdapter ──► BambooResult
                                             │
                         deleteCount == 0 ────┤──► unchanged: commitString
                                                  └──► transformed: commitString
                                             │
                         deleteCount > 0  ────┘
                                             │
                         ReliabilityChecker  │
                                  ┌──────────┴──────────┐
                                  ▼                     ▼
                       SurroundingTextBackend   ForwardBackspaceBackend
                       delete + commit ngay     forward N Backspace
                                                → wait → commit
```

Các invariant quan trọng:

- Queue giữ đúng thứ tự phím đầu vào.
- Chỉ một phím được Bamboo xử lý tại một thời điểm.
- Key đầu được xử lý ngay; key sau chỉ được pump khi commit/rewrite trước đã
  hoàn tất và qua `PostCommitDelayMs`.
- Chỉ có một rewrite bất đồng bộ pending.
- Không commit text mới trước khi phát đủ Backspace và hết settling wait.
- Phím mới vẫn được nhận vào queue khi backend đang chạy, nhưng chưa được xử lý.
- Không `sleep()` trên main thread Fcitx5; mọi delay đều dùng event loop.

Chi tiết từng component và state machine nằm trong
[Tài liệu kiến trúc](docs/ARCHITECTURE.md).

## Các đường rewrite

### SurroundingText

Ở rewrite đầu tiên của mỗi input context, Areca so phần từ ngay trước cursor
với text mà Bamboo tin rằng đang hiển thị. Kết quả được cache cho ô nhập đó:

- Khớp: dùng `deleteSurroundingText()` rồi `commitString()`.
- Không khớp, snapshot không hợp lệ hoặc app không hỗ trợ capability: fallback
  sang uinput hoặc forward-Backspace.

Areca tự động cập nhật cache SurroundingText nội bộ (`st.deleteText()` và `st.setText()`) ngay sau mỗi thao tác xóa và commit, giữ cho bộ đệm Fcitx5 luôn đồng bộ tức thì với màn hình.

### Uinput Backspace

Khi `/dev/uinput` khả dụng và ứng dụng cần phát Backspace ở mức phần cứng kernel (như terminal DBus hoặc ứng dụng chưa xác định), Areca tự động dùng `UinputBackspaceBackend` gửi sự kiện `KEY_BACKSPACE` trực tiếp qua thiết bị uinput kernel rồi commit text mới. Script cài đặt tự động tạo file rule `99-uinput-areca.rules` để phân quyền cho nhóm `uinput`.

### Forward Backspace

Khi SurroundingText không đáng tin cậy và uinput không khả dụng, addon dùng
`InputContext::forwardKey()` để phát đúng `N` cặp Backspace press/release. Phím
đầu được phát ngay; các phím sau cách nhau `BackspaceDelayMs`. Sau Backspace
cuối, backend chờ `AfterBackspaceWaitMs`, commit text mới, báo hoàn tất đúng một
lần rồi scheduler mới bắt đầu `PostCommitDelayMs`. Riêng input context có
frontend `wayland` tự động dùng `WaylandAfterBackspaceWaitMs`, mặc định 3 ms.

```text
forward Backspace × N
        → AfterBackspaceWaitMs
        → commitString
        → rewrite done
        → PostCommitDelayMs
```

Trong toàn bộ transaction, scheduler vẫn giữ `processing=true`, vì vậy key đến
sau chỉ được append vào FIFO.

Areca lọc program trước: chỉ VS Code và các bản phân nhánh,
IDE/code editor/developer tool, hoặc terminal Linux đã biết (terminal của
distro/desktop environment và terminal bên thứ ba) mới được kiểm tra capability
mask. Trong nhóm này, addon chỉ ép backend forward-Backspace khi mask chính xác
là `0x72`. Mask không được xét cho các ứng dụng ngoài allowlist này; input có
SurroundingText unreliable vốn đã đi qua forward backend theo policy mặc định.
Nếu frontend không cung cấp program name, addon không áp dụng rule này để tránh
ép nhầm và làm hỏng ứng dụng không xác định.

## Bảo vệ state trước reset của ứng dụng

Một số ứng dụng gọi `reset()` nhiều lần trong lúc người dùng vẫn đang gõ. Areca bảo vệ trạng thái gõ bằng cơ chế:

- Trong vòng 50 ms ngay sau khi hoàn tất rewrite (hoặc khi có rewrite/key đang xử lý), Areca tự động từ chối các lệnh reset dội ngược từ trình duyệt web để giữ ổn định con trỏ.
- Khi không còn rewrite pending và ngoài cửa sổ 50 ms, lệnh reset hợp lệ từ ứng dụng sẽ xoá Bamboo state và queue của input context ngay lập tức.
- Verdict SurroundingText được lưu ở cấp addon. Backspace, Ctrl+A và phím di chuyển bảo vệ riêng verdict này trong 1 giây; lifecycle vẫn reset các state nhập khác như bình thường.

Password field luôn bypass Bamboo, SurroundingText và rewrite backend; phím gốc được
forward thẳng để không đưa nội dung nhạy cảm vào state của addon.

## Cài đặt nhanh

Installer hỗ trợ Arch/CachyOS, Debian/Ubuntu và Fedora. Script sẽ cài dependency,
khởi tạo submodule Bamboo, build, chạy test và cài addon:

```bash
./scripts/install.sh
```

Nếu dependency đã có sẵn:

```bash
./scripts/install.sh --skip-deps
```

Một số lựa chọn khác:

```bash
./scripts/install.sh --user
./scripts/install.sh --build-dir build-make
./scripts/install.sh --skip-tests
./scripts/install.sh --no-restart
```

Sau khi cài, mở `fcitx5-configtool` và thêm **Areca (Bamboo)** vào danh sách
input method.

Trên Plasma Wayland, KWin có thể là process trực tiếp khởi chạy Fcitx bằng một
`WAYLAND_SOCKET` riêng; khi đó `fcitx5 -rd` từ terminal
có thể không thay được instance đang giữ addon cũ. Xem cách xác nhận và xử lý
trong [Hướng dẫn debug](docs/DEBUGGING.md).

## Build package trên GitHub

Workflow [Build Linux packages](.github/workflows/package-linux.yml) chỉ build
ba dòng distro đang được hỗ trợ cho release:

- Ubuntu 24.04 và Ubuntu mới nhất: hai gói `.deb` độc lập. Ubuntu 24.04 giữ mốc
  tương thích tối thiểu; Ubuntu 26.04 kiểm tra trực tiếp Fcitx5 mới.
- Arch Linux rolling: gói `.pkg.tar.zst` từ
  [`packaging/arch/PKGBUILD`](packaging/arch/PKGBUILD).
- Fedora mới nhất: gói `.rpm` được build bên trong image `fedora:latest`.

Mỗi nhánh đều build addon và chạy toàn bộ CTest trước khi tạo artifact. Bamboo
được checkout và đưa vào source archive theo submodule. Pull request, push vào `main`
và chạy thủ công sẽ tạo artifact của workflow; tag `v*` hoặc tag bắt đầu bằng
số còn tự động tải các gói lên GitHub Release tương ứng.

Bộ metadata dành riêng cho AUR nằm tại [`packaging/aur`](packaging/aur). Xem
[hướng dẫn publish `fcitx5-areca`](packaging/aur/README.md) để tạo repository
AUR, build kiểm tra và cập nhật package theo tag GitHub.

## Build thủ công

Yêu cầu:

- CMake 3.20 trở lên.
- Compiler hỗ trợ C++20.
- Go.
- Fcitx5 Core, Config và Utils development packages.
- Ninja hoặc Make.

```bash
git submodule update --init
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

Nếu thư mục `build` cũ dùng Ninja nhưng máy hiện tại không còn Ninja, dùng một
build directory mới thay vì tái sử dụng cache khác generator:

```bash
cmake -S . -B build-make -G "Unix Makefiles"
cmake --build build-make
```

Build tạo addon `areca.so`.

Bộ cài đặt cài icon Areca dạng scalable và symbolic có màu vào theme `hicolor`
dưới các tên `areca`, `fcitx-areca` và `org.fcitx.Fcitx5.fcitx-areca`, sau đó
cập nhật icon cache nếu `gtk-update-icon-cache` có sẵn.

## Cấu hình

Mở cấu hình Areca trong `fcitx5-configtool`. Các tuỳ chọn thường dùng nằm ngay
trong màn hình chính và được lưu tại `~/.config/fcitx5/conf/areca.conf`:

```ini
PresentationMode=Rewrite
SwitchModeKey=Alt+space
BambooInputMethod=Telex 2
OutputCharset=Unicode
SpellcheckMode="Khôi phục từ ngay trong lúc gõ"
BackspaceRecovery=True
ModernStyle=True
AutoCapitalizeAfterPunctuation=False
EnableMacro=True
CapitalizeMacro=True
Debug=True
```

Chọn **Cấu hình nâng cao** để mở panel timing riêng. Các giá trị trong
panel này được lưu tại `~/.config/fcitx5/conf/areca-advanced.conf`:

```ini
BackspaceDelayMs=1
AfterBackspaceWaitMs=10
WaylandAfterBackspaceWaitMs=3
XimAfterBackspaceWaitMs=10
Fcitx4AfterBackspaceWaitMs=10
DbusAfterBackspaceWaitMs=5
PostCommitDelayMs=20
PreciseTiming=True
ForceUinput=False
```

| Panel | Tuỳ chọn | Ý nghĩa |
| --- | --- | --- |
| Chính | `PresentationMode` | `Rewrite` dùng queue và SurroundingText/forward-Backspace; `Preedit` xử lý đồng bộ bằng Bamboo; `Redirect (EN)` forward nguyên KeyEvent, không gọi Bamboo, queue hay commit text. Rewrite và Preedit có state/engine tách biệt, Redirect không có mutable state. |
| Chính | `SwitchModeKey` | Hotkey quay vòng `Rewrite → Preedit → Redirect (EN) → Rewrite`, mặc định `Alt+Space`; mode mới được lưu global và hiện bằng popup thông tin của Fcitx5. |
| Chính | `BambooInputMethod` | Tên input method được định nghĩa bởi Bamboo, mặc định `Telex 2`. |
| Chính | `OutputCharset` | Bảng mã do Bamboo cung cấp, mặc định `Unicode`; gồm Unicode dựng sẵn/tổ hợp cùng các bảng mã tương thích cũ như TCVN3, VNI Windows, VIQR… |
| Chính | `SpellcheckMode` | Chế độ kiểm tra cấu trúc âm tiết của Bamboo; có ba mức: `"Không kiểm tra (Tắt)"` – tắt hoàn toàn; `"Khôi phục từ sau khi gõ xong"` – tại word boundary, tự khôi phục từ tiếng Việt không hợp lệ về chuỗi phím Latin ban đầu; `"Khôi phục từ ngay trong lúc gõ"` – như mode 2 nhưng khôi phục trong khi gõ từng ký tự. Mặc định `"Khôi phục từ ngay trong lúc gõ"`. |
| Chính | `BackspaceRecovery` | Bật khôi phục lỗi chính tả khi nhấn Backspace trong lúc Bamboo còn composition, ví dụ `nhanhsh` + Backspace có thể khôi phục về `nhánh`. Tính năng này tự tắt với backend `uinput`; mặc định `True`. |
| Chính | `ModernStyle` | `True` đặt dấu kiểu `hoà`, `thuý`; `False` dùng kiểu `hòa`, `thúy`. |
| Chính | `AutoCapitalizeAfterPunctuation` | Tự viết hoa chữ ASCII đầu tiên sau `.`, `!`, `?` và khoảng trắng. Nhiều khoảng trắng vẫn giữ trạng thái chờ; `Enter` không kích hoạt. |
| Chính | `EnableMacro` | Bật thay thế từ viết tắt tại dấu cách hoặc dấu câu. |
| Chính | `CapitalizeMacro` | Tự đổi nội dung macro thành chữ thường/toàn chữ hoa theo cách viết key. |
| Chính | `Debug` | Bật log chi tiết của addon. |
| Nâng cao | `BackspaceDelayMs` | Delay giữa hai cặp Backspace press/release được forward. |
| Nâng cao | `AfterBackspaceWaitMs` | Thời gian chờ sau Backspace cuối cho các frontend khác / chưa xác định, mặc định 10 ms. |
| Nâng cao | `WaylandAfterBackspaceWaitMs` | Thời gian chờ riêng sau Backspace cuối cho frontend Wayland, mặc định 3 ms. |
| Nâng cao | `XimAfterBackspaceWaitMs` | Thời gian chờ riêng sau Backspace cuối cho frontend XIM, mặc định 10 ms. |
| Nâng cao | `Fcitx4AfterBackspaceWaitMs` | Thời gian chờ riêng sau Backspace cuối cho frontend Fcitx4, mặc định 10 ms. |
| Nâng cao | `DbusAfterBackspaceWaitMs` | Thời gian chờ riêng sau Backspace cuối cho frontend DBus, mặc định 5 ms. |
| Nâng cao | `PostCommitDelayMs` | Settling window độc lập sau mọi text commit. |
| Nâng cao | `PreciseTiming` | Dùng accuracy `1µs` cho timer Backspace và post-commit; nếu tắt sẽ dùng timer coalescing mặc định của event loop. |
| Nâng cao | `ForceUinput` | Ép dùng uinput thay cho forward Backspace khi khả dụng, mặc định `False`. |

Lưu ý: đổi giá trị mặc định trong source không ghi đè file cấu hình đã tồn tại.
Khi nâng cấp từ bản cũ, Areca tự đọc timing còn nằm trong `areca.conf`;
nếu `areca-advanced.conf` đã tồn tại thì file mới luôn được ưu tiên. Hãy sửa file
người dùng hoặc dùng giao diện cấu hình Fcitx5 rồi reload addon.
Danh sách `BambooInputMethod` và `OutputCharset` trong giao diện được lấy động
từ `bamboo-core`, không được hard-code trong addon.

## Macro

Mở cấu hình Areca trong `fcitx5-configtool`, chọn **Chỉnh sửa macro** rồi thêm
các cặp `Từ viết tắt → Nội dung thay thế`. Macro được lookup không phân biệt
hoa/thường và chỉ expand khi gặp word boundary, nên key vẫn có thể được gõ như
một phần của từ dài hơn.

Ví dụ với macro `bt → Be There` và `CapitalizeMacro=True`:

```text
bt<Space>  → be there␠
BT<Space>  → BE THERE␠
Bt<Space>  → Be There␠
```

Expansion chạy trước spell-check, được encode theo `OutputCharset`, sau đó đi
qua cùng scheduler và rewrite backend như kết quả Bamboo bình thường. Dữ liệu
được lưu tại `~/.config/fcitx5/conf/areca-macro-table.conf`.

Areca giữ lại composition Bamboo của từ vừa chốt. Vì vậy sau khi Backspace qua
dấu cách hoặc dấu câu, có thể tiếp tục sửa dấu hay xoá từ vừa gõ thay vì Bamboo
coi đó là một từ hoàn toàn mới.

## Redirect (EN)

`Redirect (EN)` là mode global do người dùng tự chọn. Ở mode này press event
được forward nguyên bản như password field; release event không bị filter. Không
có text nào đi qua Bamboo, queue, timer, SurroundingText hoặc rewrite backend.

Areca không tự đổi mode theo ứng dụng.

## Trạng thái và giới hạn hiện tại

- `Rewrite` và `Preedit` là hai presentation mode độc lập. `Redirect` không giữ
  state. Đổi mode sẽ hủy queue và composition cũ trước khi mode mới nhận phím.
- Reliability là heuristic một lần cho mỗi input context, không phải chứng minh
  tuyệt đối rằng mọi snapshot tương lai đều đúng.
- Khả năng chuyển `forwardKey()` tới ứng dụng vẫn phụ thuộc frontend/compositor.
- Native addon `.so` không hot reload. File mới có thể đã cài nhưng process
  Fcitx hiện tại vẫn map inode cũ trong RAM.

## Tài liệu

- [Kiến trúc và luồng hoạt động](docs/ARCHITECTURE.md)
- [Cài đặt, reload và debug](docs/DEBUGGING.md)

## 🌴 Tác giả & Đồng hành

- **Kim Xuân Hồng** ([@kimxuanhong](https://github.com/kimxuanhong) / [@xhkzeroone](https://github.com/xhkzeroone)) — *Tác giả & Khởi tạo dự án*

<a href="https://github.com/xhkzeroone/ArecaIME/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=xhkzeroone/ArecaIME" alt="Areca IME Contributors" />
</a>

Mọi đóng góp báo lỗi hoặc gửi Pull Request đều được hoan nghênh. Xem hướng dẫn tại [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Areca được phát hành theo giấy phép [MIT](LICENSE). `bamboo-core` là một dự án
độc lập và giữ license/copyright riêng tại
[`bamboo/bamboo-core/LICENSE`](bamboo/bamboo-core/LICENSE).
