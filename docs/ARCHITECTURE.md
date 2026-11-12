# Kiến trúc Areca IME

Tài liệu mô tả pipeline hiện tại của Areca. Mục tiêu thiết kế: giữ đúng thứ tự
phím, không chạy hai rewrite cùng lúc và tách engine tiếng Việt khỏi chi tiết
của Fcitx5, Wayland hay X11.

Xem thêm:
- [Đặc tả kỹ thuật](ARECA_ARCHITECTURE_SPECIFICATION.md): invariant, chính sách
  chọn backend, sequence diagram từng backend và bảng timing.
- [Thời gian chờ thích nghi](ADAPTIVE_DELAYS.md).
- [Hướng dẫn debug](DEBUGGING.md).

## Thành phần

| Thành phần | File | Trách nhiệm |
| --- | --- | --- |
| `ArecaEngine` | `src/areca_engine*` | Nhận event Fcitx5, chọn mode theo ứng dụng, chọn rewrite backend, nạp config, giữ cache verdict backend của context hiện tại. |
| `InputModeHandler` | `src/mode_handler.h` | Interface lifecycle và KeyEvent chung cho các mode. |
| `RewriteModeHandler` | `src/rewrite_mode.*` | Policy phím của Rewrite, restore surrounding, thử phím đầu khi rảnh rồi đưa phím vào `InputScheduler`. |
| `PreeditModeHandler` | `src/preedit_mode.*` | Xử lý Bamboo đồng bộ và hiển thị preedit, không dùng scheduler hay rewrite backend. |
| `RedirectModeHandler` | `src/redirect_mode.*` | Forward phím nguyên bản, không có Bamboo hay state. |
| `RewriteInputState`, `PreeditInputState` | `src/rewrite_mode.h`, `src/preedit_mode.h` | State theo input context: Bamboo engine, tự viết hoa, cờ restore, khoảng bảo vệ reset. |
| `InputScheduler`, `KeyQueue` | `src/input_scheduler.*`, `src/key_queue.h` | FIFO single-flight, transaction rewrite và post-commit barrier. |
| `BambooEngineAdapter` | `src/bamboo_engine_adapter.*`, `bamboo-bridge/main.go` | Gọi `bamboo-core` qua cgo, trả `BambooResult`. |
| `BackendVerdict` | `src/surrounding_reliability.h` | Kết quả probe surrounding text của context hiện tại, số lần probe và retry. |
| Rewrite backend | `src/*_backend.*` | 7 backend implement `RewriteBackend`, xem [bảng backend](#rewrite-backend). |
| `AdaptiveWait`, `AdaptiveWaitMonitor` | `src/adaptive_wait*` | Tăng thời gian chờ khi timer trễ, ACK chậm hoặc máy bận. |
| `EventLoopPostTask` | `src/event_loop_post.h` | Đưa commit sang pha post của event loop. |
| `InputTypeDetector` | `src/input_type_detector.*` | Nhận diện trình duyệt, terminal, thanh địa chỉ Chromium, khả năng forward Backspace. |
| `WindowFocusTracker` | `src/window_focus_tracker.*`, `src/atspi/` | Thread riêng đọc AT-SPI để biết chương trình và ô đang focus. |
| `MouseClickTracker`, `areca-mouse-monitor` | `src/mouse_click_tracker.*` | Process con libinput báo click qua pipe để reset composition. |
| `DynamicIslandAnnouncement` | `src/notification/` | Phát tín hiệu D-Bus khi chọn Areca, xem [tài liệu riêng](input-method-announcement.md). |

## Phân tách cấu hình

| File | Nội dung |
| --- | --- |
| `conf/areca.conf` | Tuỳ chọn thường dùng: kiểu gõ, bảng mã, chính tả, macro, URL, terminal, debug. |
| `conf/areca-advanced.conf` | Timing của từng backend, chọn uinput hay Native, mouse tracking, phím đầu. |
| `conf/areca-apps.conf` | Mode hoặc backend ghi đè theo ứng dụng. |
| `conf/areca-macro-table.conf` | Bảng macro. |

Các khoá timing cũ trong `areca.conf` vẫn được đọc ẩn để migrate. File nâng cao
được load sau và luôn được ưu tiên. Ứng dụng cài đặt ImGui cũ có nút preset
hiệu năng, đặt các thời gian chờ chính về 1, 3 hoặc 5 ms.

`PreciseTiming=True` đặt accuracy của timer backend và post-commit thành `1 µs`.
Khi tắt, accuracy bằng `0` và event loop được phép gộp timer.

## Chọn mode

`PresentationMode` toàn cục mặc định là Rewrite và không còn hiện trong giao
diện. Phím tắt đổi mode toàn cục `SwitchModeKey` đã bị tắt trong code.

Mode thực tế của một input context được quyết định theo ứng dụng:

1. Ứng dụng có mục trong `areca-apps.conf` với mode `Preedit` hoặc `Redirect`
   dùng mode đó.
2. Mode `Surrounding`, `Shift select`, `Backspace`, `Virtual keyboard` dùng Rewrite và ghi đè backend.
3. `Tự động` hoặc không có mục dùng mode toàn cục.

Phím `SelectBackendKey`, mặc định phím dấu huyền, mở menu chọn ngay trên ứng
dụng đang dùng khi không có composition và không có rewrite đang chạy. Menu có 7
lựa chọn: Tự động, Surrounding, Shift select, Backspace, Virtual keyboard,
Preedit, Redirect. Tên
mỗi lựa chọn kèm backend cụ thể sẽ dùng, ví dụ `Backspace (Uinput)`. Lựa chọn
được lưu vào `areca-apps.conf` cùng tên frontend. Ứng dụng không báo tên được
đặt tên tạm `context-<uuid>`.

## Vòng đời phím trong Rewrite

1. Fcitx5 gọi `ArecaEngine::keyEvent()`.
2. Ô mật khẩu đi thẳng vào handler để forward phím gốc.
3. Nếu `DisableVietnameseInTerminal=True` và ứng dụng là terminal, phím được
   forward nguyên bản và state của mode hiện tại bị reset.
4. Phím đang thuộc menu chọn backend được xử lý cho menu.
5. Backspace do backend uinput phát ra và Left, Shift do backend shift-select
   phát ra được trả về backend tương ứng trước mọi xử lý khác.
6. Click chuột đang chờ được áp dụng nếu scheduler cho phép reset.
7. `RewriteModeHandler::handleKeyEvent()`:
   - Phím di chuyển, Tab, Escape, tổ hợp Ctrl, Alt, Super reset Bamboo, arm
     restore rồi forward.
   - Backspace đồng bộ Bamboo hoặc chạy khôi phục chính tả.
   - Phím chữ Bamboo xử lý được thử restore surrounding nếu đang arm.
   - Trình duyệt bật `ForwardFirstCharacter` thử `InputScheduler::handleIdleKey()`.
   - Phím còn lại được `filterAndAccept()` rồi đưa vào `KeyQueue`.
8. Scheduler đang rảnh thì xử lý ngay đúng một phím: gọi Bamboo, nhận
   `BambooResult`.
9. `deleteCount == 0`: `commitString()` ngay. Ngược lại tạo `RewritePlan`, chọn
   backend và gọi `apply()`.
10. Backend báo hoàn tất, scheduler chờ `PostCommitDelayMs` rồi mới lấy phím
    tiếp theo trong queue.

Scheduler không dùng timer trước Bamboo và không hút hết queue trong một lần.
Phím đến trong lúc pipeline bận chỉ được nối vào cuối FIFO. Backend báo `Failed`
làm scheduler dừng hẳn để không phá thứ tự phím.

### Khoảng bảo vệ reset

`shouldRejectReset()` trả true khi đang xử lý phím, đang có rewrite hoặc trong
50 ms sau lần commit cuối. Trong khoảng đó reset từ ứng dụng, click chuột và
phím di chuyển do backend phát ra đều không xoá state Bamboo. Rewrite còn có
thêm 20 ms bảo vệ sau Backspace vì một số frontend X11 gửi reset ngay sau
Backspace đã forward.

`ArecaEngine::reset()` gọi `requestProtectedReset()` của mode hiện tại. Reset chỉ
chạy ngay nếu không nằm trong khoảng bảo vệ, không có timer trì hoãn.

## Phím đầu khi Rewrite đang rảnh

Một số editor web cần nhận sự kiện phím qua frontend để mở chế độ soạn thảo. Khi
`ForwardFirstCharacter=True`, với input context của trình duyệt, chưa chờ
Backspace release và phím không bị tự viết hoa đổi, handler thử
`InputScheduler::handleIdleKey()` trước `filterAndAccept()`.

Scheduler chỉ nhận khi Bamboo đang rỗng, không xử lý phím nào, không có rewrite,
queue rỗng, không bị dừng và đã qua khoảng bảo vệ sau commit. Bamboo vẫn xử lý
phím đúng một lần:

- Đầu ra không đổi, không xoá, không macro: event được để chưa filter để
  frontend tự giao phím gốc. Bamboo giữ state để `a` rồi `s` vẫn thành `á`.
  Scheduler vẫn giữ barrier `PostCommitDelayMs`.
- Đầu ra đổi: event bị accept và kết quả vừa tính được áp dụng ngay, không
  enqueue lại.
- Không đủ điều kiện: trả false, không sửa engine hay event.

`KeyEvent::forward()` của Fcitx là getter. Việc không filter event mới là điều
cho phím đi tiếp.

## BambooResult và diff

Go bridge tạo `bamboo.IEngine` bằng `bamboo.NewEngine(method, bamboo.EstdFlags)`.
`BambooEngineAdapter` giữ chuỗi mà nó tin là đang hiển thị. Sau mỗi phím, adapter
lấy chuỗi mới từ Bamboo, encode theo `OutputCharset`, tìm common prefix UTF-8 và
tạo:

```text
currentText  chuỗi trước phím
newText      chuỗi sau phím
deleteCount  số ký tự ở đuôi cũ cần xoá
commitText   đuôi mới cần chèn
```

Ví dụ Telex: `a` rồi `w` cho `deleteCount = 1`, `commitText = "ă"`.

Vì encode trước khi tính diff, các trường trên luôn mô tả đúng chuỗi đã commit
vào ứng dụng, kể cả với Unicode tổ hợp, VNI Windows hay VIQR.

Ký tự Bamboo không xử lý được là word boundary. Hành vi tại boundary theo
`SpellcheckMode`, mặc định là khôi phục ngay trong lúc gõ:

- **Không kiểm tra**: commit từ nguyên trạng.
- **Khôi phục từ sau khi gõ xong**: gọi `IsValid(true)` trước khi reset. Từ có
  dấu nhưng sai cấu trúc âm tiết được `RestoreLastWord(false)` về chuỗi phím
  Latin ban đầu rồi mới nối boundary.
- **Khôi phục từ ngay trong lúc gõ**: như trên, và mỗi phím trung gian truyền
  `spellCheck=1` vào `ArecaBambooProcess` để Bamboo rollback ngay.

Kiểm tra chính tả dùng luật có sẵn của `bamboo-core`, không dùng từ điển ngoài.

`ModernStyle=True` cho cách đặt dấu `oà`, `uý`; `False` cho `òa`, `úy`. Ánh xạ
ngược với tên flag nội bộ `EstdToneStyle` là có chủ ý để tên option mô tả đúng
chuỗi đầu ra.

## Macro

Bảng macro gồm các cặp `Key`, `Value`. Tại word boundary, bridge lấy từ hiện tại
ở `PunctuationMode`, tra key không phân biệt hoa thường và mở rộng trước khi kiểm
tra chính tả. Khi `CapitalizeMacro=True`:

- Key viết thường cho value viết thường.
- Key toàn chữ hoa cho value toàn chữ hoa.
- Key viết hoa chữ đầu cho value viết hoa chữ đầu mỗi từ.
- Kiểu hoa thường khác giữ nguyên value.

Adapter reset Bamboo sau khi khớp, nối boundary, encode rồi tính diff. Macro vì
vậy đi qua đúng đường rewrite thông thường.

`MacroBypassModifier`, mặc định Shift, cho phép giữ một phím bổ trợ khi nhấn
Space để bỏ qua macro cho đúng lần đó. Cờ `suppressMacro` gắn trên từng phần tử
trong FIFO nên gõ nhanh không làm cờ áp nhầm sang phím sau.

`RevertMacroOnBackspace=True`: Backspace ngay sau khi macro mở rộng hoàn tác về
key viết tắt kèm boundary.

## Tự viết hoa và khôi phục từ vừa gõ

`AutoCapitalizeAfterPunctuation`, mặc định tắt, theo dõi `.`, `!`, `?` rồi khoảng
trắng theo từng input context. Chữ ASCII thường kế tiếp được đổi thành chữ hoa
trước khi vào queue. Phím đã đổi không đi nhánh phím đầu vì phím vật lý không có
Shift. Enter, reset, Backspace, di chuyển con trỏ và shortcut xoá trạng thái chờ.

Sau khi kết thúc một từ, adapter giữ composition của từ đó và đếm các boundary đã
commit phía sau. Backspace lùi qua các boundary này. Khi boundary cuối bị xoá,
composition được phục hồi để phím sau còn sửa được dấu. Composition chỉ bị bỏ
khi bắt đầu từ mới hoặc khi reset thật sự chạy.

`BackspaceRecovery=True` cho phép Backspace trong từ đang gõ dựng lại chuỗi đã
kiểm tra chính tả, ví dụ `nhanhsh` về `nhánh`.

## Rewrite backend

| Backend | Tên trong log | Cách xoá | Xác nhận |
| --- | --- | --- | --- |
| `SurroundingTextBackend` | `surrounding-text` | `deleteSurroundingText(-N, N)` một lần | Probe bằng `SurroundingTextUpdated` ở lần đầu |
| `SurroundingTextV2Backend` | `surrounding-text-v2` | `deleteSurroundingText(-1, 1)` N lần | Xác nhận lần xoá cuối ở mọi rewrite |
| `UinputShiftSelectBackend` | `uinput-shift-select` | Shift giữ + Left × N qua `/dev/uinput`, commit đè vùng chọn | ACK bằng Shift up quay về Fcitx |
| `NativeShiftSelectBackend` | `native-shift-select` | Như trên qua libei | Không ACK |
| `UinputBackspaceBackend` | `uinput-backspace` | Backspace × N qua `/dev/uinput` | ACK bằng số Backspace quay về Fcitx |
| `NativeBackspaceBackend` | `native-backspace` | Backspace × N qua libei | Không ACK |
| `ForwardBackspaceBackend` | `forward-backspace` | `forwardKey(Backspace)` × N | Không ACK |
| `VirtualKeyboardBackend` | `virtual-keyboard` | Backspace × N rồi gõ luôn chữ mới bằng phím ảo, không `commitString` | Thiết bị báo phát xong; phím dội ngược được chuyển thẳng cho ứng dụng |

`VirtualKeyboardBackend` dùng `zwp_virtual_keyboard_v1` của Wayland. Ký tự mới
được gán tạm vào keycode rảnh qua keymap rồi phát như phím thường, nên xoá và chèn
đi chung một kênh và thứ tự do compositor giữ. Chỉ có khi compositor cấp giao thức
này cho kết nối thường (wlroots, Hyprland, sway); KWin và Mutter không cấp. Thiết bị
chạy trên thread riêng và báo về event loop qua `EventDispatcher`. Chỉ dùng khi chọn
**Virtual keyboard** cho ứng dụng.

Native chỉ dùng libei qua phiên RemoteDesktop portal trên Wayland. Không có
libei hoặc portal từ chối thì Native không khả dụng và bộ chọn dùng uinput.

Mọi backend chờ một khoảng trước commit, khoảng này cộng thêm phần của
`AdaptiveWait`. Commit luôn được đưa sang pha post của event loop bằng
`EventLoopPostTask`. Timer giữa các bước xoá chạy callback trực tiếp. Chi tiết
từng backend nằm trong [đặc tả](ARECA_ARCHITECTURE_SPECIFICATION.md#5-sequence-diagram-các-backend).

Backend được chọn qua ba bộ chọn có thứ tự ưu tiên:

- **Surrounding**: V2 nếu `UseSurroundingV2InsteadOfSurroundingV1=True`, ngược lại V1.
- **Shift select**: Native nếu `UseXTestInsteadOfUinput=True` và khả dụng, rồi
  uinput, rồi Native.
- **Backspace**: khi cần phím phần cứng hoặc `UseUinputInsteadOfForwardKey=True`,
  thử Native nếu được ưu tiên, rồi uinput, rồi Native; cuối cùng là ForwardKey.

## Chọn backend

Khi `deleteCount > 0`, `selectRewriteBackend()` quyết định theo thứ tự:

1. **Terminal** dùng bộ chọn Backspace có ưu tiên phím phần cứng. Terminal được
   nhận diện theo tên chương trình, AT-SPI, hoặc frontend DBus không có tên.
2. **Autocomplete trình duyệt hoặc đang có vùng chọn**: ô URL dùng Shift select.
   Ô khác dùng Backspace, thêm 1 Backspace nếu vùng chọn nằm ngay sau từ đang gõ
   hoặc là gợi ý autocomplete.
3. **Ô URL trong trình duyệt không có surrounding text** dùng Shift select.
   Ô URL được nhận diện bằng `CapabilityFlag::Url` hoặc AT-SPI với thanh địa chỉ
   Chromium.
4. **`UseBackspaceForUrl=True` và ô là URL**: Shift select, không có thì Backspace.
5. **Cấu hình theo ứng dụng**:
   - `Surrounding`: dùng bộ chọn Surrounding nếu verdict cho phép, không thì
     Shift select khi bật fallback trình duyệt, cuối cùng là Backspace.
   - `Shift select`: Shift select trừ terminal, không có thì Backspace.
   - `Backspace`: bộ chọn Backspace.
   - `Virtual keyboard`: `VirtualKeyboardBackend` nếu compositor hỗ trợ, không thì
     bộ chọn Backspace.
6. **Tự động**:
   - Verdict cho phép surrounding: bộ chọn Surrounding, kèm probe nếu chưa xác nhận.
   - Trình duyệt và `ShiftSelectFallbackForBrowser=True`: Shift select.
   - Còn lại: bộ chọn Backspace. Cần phím phần cứng khi là terminal hoặc
     `canForwardBackspaces()` trả false, ví dụ ứng dụng SDL hay frontend DBus,
     Fcitx4 thiếu capability phù hợp.

Backend tự động được chọn gần nhất cho mỗi ứng dụng được nhớ để hiển thị trong
menu chọn backend.

## Verdict surrounding text

Verdict được đánh giá ở rewrite đầu tiên cần xoá trong một input context:

- Snapshot có text, cursor và anchor đều lớn hơn 0 thì được đánh dấu tin cậy và
  chờ probe.
- Chương trình lập trình, terminal, office luôn bị buộc dùng Backspace.
- Họ VS Code với capability mask `0x72` cũng bị buộc dùng Backspace.
- Snapshot gợi ý autocomplete không được dùng để probe.
- Trong lúc chờ probe, snapshot không chứa từ đang gõ ngay trước con trỏ nghĩa
  là frontend gửi surrounding trễ. Probe bị tính là thất bại và restore surrounding
  bị tắt cho context đó.

Probe gửi lệnh xoá rồi chờ `SurroundingTextUpdated` khớp ngữ nghĩa theo
`surroundingProbeMatches`: con trỏ trùng anchor, text trước con trỏ đúng như kỳ
vọng sau khi xoá, không còn text cũ, và không nhận nhầm trạng thái trung gian.
Lần probe đầu chờ 150 ms, lần retry chờ 80 ms. Hết hạn thì verdict thành không tin
cậy, transaction chuyển sang backend fallback, và chỉ retry một lần ở từ kế tiếp.

Verdict được cache theo UUID của context đang dùng. Lifecycle như activate,
deactivate, reset, click chuột xoá verdict để đánh giá lại, trừ khi verdict đang
được bảo vệ: Backspace, Ctrl+A, phím di chuyển hoặc chọn text bảo vệ verdict
trong 1 giây.

## Surrounding cache

Backend cập nhật object `surroundingText()` mà Fcitx đang giữ ngay sau khi xoá
hoặc commit, để thao tác kế tiếp không đọc snapshot cũ. Frontend vẫn là nguồn dữ
liệu chính và snapshot mới từ ứng dụng sẽ thay cache này.

## Sửa từ đã commit

`RestoreSurroundingText=True`, mặc định tắt, cho phép đặt con trỏ sau một từ đã
gõ xong rồi tiếp tục sửa dấu cho từ đó. Chỉ áp dụng cho Rewrite: Preedit tắt hẳn
restore từ surrounding vì nó phải xoá từ cũ khỏi ứng dụng theo snapshot, snapshot
sai là xoá nhầm chữ.

**Khi nào được arm.** Cờ restore là one-shot theo input context, được bật khi:

- Focus vào ô nhập.
- Reset context, kể cả reset từ ứng dụng hay click chuột.
- Nhận `SurroundingTextUpdated`, tức mỗi khi ứng dụng báo text hoặc vị trí con
  trỏ thay đổi. Rewrite chỉ arm theo sự kiện này khi không biết chắc text trước
  con trỏ (xem dưới), vì update sau chính lần commit của Areca không có nghĩa là
  con trỏ đã đổi chỗ.

**Text đã biết trước con trỏ.** Mỗi mode giữ `knownTextBeforeCursor`: phần text
Areca chắc chắn vừa đưa vào ứng dụng. Scheduler báo qua `CommitObserver` mỗi lần
xoá N ký tự rồi chèn text, kể cả phím đầu từ được forward nguyên vẹn. Phần này bị
xoá khi focus, reset, click hoặc khi ứng dụng tự xử lý một phím (event không bị
filter), và giới hạn 256 byte.

**Khi nào chạy.** Ở phím chữ đầu tiên mà Bamboo xử lý được sau khi arm. Phím
không xử lý được như Space tắt cờ. Cờ luôn bị tắt trước khi kiểm tra điều kiện:

- Bảng mã Unicode, không phải ô mật khẩu, ứng dụng có `SurroundingText`.
- Rewrite: không phải ô có `CapabilityFlag::Url`, Bamboo rỗng, không có rewrite,
  queue rỗng, ngoài khoảng bảo vệ reset.
- Snapshot kết thúc bằng `knownTextBeforeCursor` (NBSP tính như dấu cách). Thiếu
  thì frontend đang gửi trễ: bỏ qua đúng lần restore đó, lần sau vẫn thử lại.
- Rewrite: backend rewrite gần nhất của app là surrounding (hoặc chưa từng chọn
  backend cho app). App phải xoá bằng phím (Backspace, Shift select) thì snapshot
  của nó cũng không đáng tin nên không restore.

**Cách khôi phục.** Extractor quét lùi từ con trỏ, dừng ở ký tự không phải chữ
Latin hoặc chữ tiếng Việt dựng sẵn, tối đa 16 ký tự. Chuỗi UTF-8 lỗi, dấu tổ hợp
và từ dài hơn giới hạn bị từ chối. Bridge tách chữ gốc và hiệu ứng dấu, thử một
tập phím hữu hạn trên engine tạm, chỉ nạp vào engine thật khi output khớp đúng từ
trên màn hình. Không khớp thì bỏ dần ký tự đầu để xử lý trường hợp con trỏ nằm
giữa từ như `tiế|n`.

Rewrite giữ nguyên text trên màn hình, phím kế tiếp tạo diff đi qua scheduler như
bình thường.

## Preedit mode

Preedit không dùng `InputScheduler`, rewrite backend hay verdict. Mỗi input
context có `PreeditInputState` và engine Bamboo riêng, xử lý phím đồng bộ.

- Hiển thị bằng preedit của client nếu ứng dụng có `CapabilityFlag::Preedit`,
  ngược lại dùng preedit phía Fcitx.
- Backspace trong composition gọi `processBackspace()` khi bật `BackspaceRecovery`.
- Space và dấu câu chạy macro, kiểm tra chính tả rồi commit ngay.
- Backspace lùi qua boundary được forward nguyên bản. Khi chạm tới từ, handler xoá
  `từ + boundary cuối` bằng `deleteSurroundingText()` rồi đưa từ lên preedit, chỉ
  khi ứng dụng hỗ trợ surrounding và snapshot khớp chính xác.
- Enter, Tab, Delete, phím di chuyển, Escape và shortcut commit composition trước
  khi forward.
- Click chuột chỉ chuẩn bị restore khi composition đang trống.

Hai mode xử lý tiếng Việt chỉ dùng chung cấu hình và lớp adapter, không dùng
chung engine instance hay state.

## Redirect mode

`RedirectModeHandler` forward phím nhấn bằng `KeyEvent::forward()` và để nguyên
phím nhả cho Fcitx. Mode này không có state để reset.

## Theo dõi click chuột

`MouseClickTracker` quản lý process con `areca-mouse-monitor`. Helper dùng
libinput và udev trên `XDG_SEAT`, mặc định `seat0`, chạy cùng quyền user với
Fcitx.

```text
libinput/udev → areca-mouse-monitor → pipe stdout (R/C)
             → MouseClickTracker trên event loop → cờ click chờ xử lý
             → ArecaEngine::keyEvent → kiểm tra khoảng bảo vệ → reset handler
```

- `R`: libinput đã khởi tạo, chưa đảm bảo đọc được thiết bị.
- `C`: có nút chuột được nhấn hoặc tap touchpad. Di chuyển, cuộn, nhả nút không
  gửi gì.
- Pipe nonblocking, nhiều click gộp thành một cờ. stderr chỉ dùng cho log.
- Callback pipe chỉ đặt cờ. Trước phím nhấn kế tiếp, addon đọc thêm pipe. Nếu
  đang trong khoảng bảo vệ, cờ được giữ cho phím sau. Nếu không thì reset handler
  của mode hiện tại và xoá verdict theo lifecycle.
- Activation khởi động lại helper đã mất kết nối và bỏ click cũ. Huỷ tracker đóng
  pipe, kill và `waitpid()` helper.

`EnableMouseTracking=False` dừng hẳn helper. Helper cài vào
`CMAKE_INSTALL_LIBEXECDIR`. Rule `70-areca-pointer.rules` cấp `uaccess` cho chuột
và touchpad của phiên local đang active. Thiếu quyền chỉ làm mất tính năng reset
theo click. Xem [debug mouse tracker](DEBUGGING.md#mouse-tracker).

## Nhận diện ứng dụng

- Tên chương trình lấy từ `InputContext::program()`. Khi trống hoặc là proxy của
  GNOME Shell, Areca dùng tên từ `WindowFocusTracker` và cache vào state.
- `WindowFocusTracker` chạy thread riêng, kết nối bus accessibility, theo dõi sự
  kiện focus và lưu chương trình, class, cờ terminal. Thread chính chỉ đọc qua
  atomic và mutex ngắn.
- `InputTypeDetector` dùng các thông tin trên cùng danh sách trong
  `program_compatibility.cpp` để nhận diện trình duyệt, Chromium, terminal, thanh
  địa chỉ Omnibox và ứng dụng SDL.
