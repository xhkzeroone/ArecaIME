# Kiến trúc Areca IME

Tài liệu này mô tả pipeline hiện tại của Areca. Mục tiêu thiết kế là giữ đúng
thứ tự input, tránh rewrite song song và cô lập engine tiếng Việt khỏi chi tiết
Wayland và Fcitx5.

> **Xem thêm:**
> - [Giao diện HTML & Sequence Diagram tương tác](../website/architecture.html)
> - [Đặc tả kỹ thuật & Sequence Diagram (Markdown)](ARECA_ARCHITECTURE_SPECIFICATION.md)

## Thành phần

| Thành phần | Trách nhiệm |
| --- | --- |
| `ArecaEngine` | Nhận event từ Fcitx5, dispatch sang mode đang chọn và giữ cache verdict backend cho context hiện tại. |
| `InputModeHandler` | Interface lifecycle/KeyEvent không chứa state; engine gọi handler đang active qua interface này. |
| `RewriteInputState` | Bamboo, auto-capitalization và reset timer riêng của Rewrite. |
| `PreeditInputState` | Bamboo, composition, auto-capitalization và reset timer riêng của Preedit. |
| `RewriteModeHandler` | Policy KeyEvent/reset của Rewrite; thử phím đầu khi rảnh trước khi enqueue vào `InputScheduler`. |
| `PreeditModeHandler` | Xử lý Bamboo đồng bộ và quản lý UI preedit; không gọi scheduler hay rewrite backend. |
| `RedirectModeHandler` | Forward KeyEvent nguyên bản như password field; không có Bamboo, queue, timer hay mutable state. |
| `KeyQueue` | FIFO chứa key gốc, Unicode codepoint, UTF-8, sequence và reference tới input context. |
| `MouseClickTracker` | Quản lý helper chuột và cờ click trên event loop Fcitx. |
| `areca-mouse-monitor` | Process con libinput/udev gửi thông báo click qua pipe riêng. |
| `InputScheduler` | FIFO single-flight, backend selection, transaction barrier và timer riêng sau commit. |
| `BambooEngineAdapter` | Bridge C++/Go gọi trực tiếp `bamboo-core` và biến chuỗi kết quả thành `BambooResult`. |
| `ReliabilityChecker` | Probe SurroundingText lần đầu và cache verdict theo input context. |
| `RewriteBackend` | Interface chung cho thao tác apply một `RewritePlan`. |
| `SurroundingTextBackend` | Gọi `deleteSurroundingText()` và `commitString()`. |
| `ForwardBackspaceBackend` | Phát tuần tự Backspace press/release bằng `forwardKey()`, chờ settling delay thích ứng theo timer drift rồi commit text và hoàn tất transaction. |
| `UinputBackspaceBackend` | Gửi phím `KEY_BACKSPACE` qua `/dev/uinput` và dùng settling delay thích ứng theo timer drift cho terminal DBus và ứng dụng không xác định. Terminal nhúng trong VS Code dùng `ForwardBackspaceBackend`. |
| `UinputShiftSelectBackend` | Gửi `Shift down`, `Left` × N, `Shift up` qua `/dev/uinput` để bôi đen, sau đó commit từng ký tự mới cho các ứng dụng trình duyệt web. |

## Phân tách cấu hình

Cấu hình chính ở `conf/areca.conf` chỉ chứa các tuỳ chọn dùng thường xuyên.
Timing nằm trong sub-config **Cấu hình nâng cao** tại
`conf/areca-advanced.conf`, dùng cùng cơ chế panel con với trình sửa macro.
Các field timing cũ trong `areca.conf` được giữ ẩn để migrate cấu hình;
file nâng cao, nếu có, luôn được load sau và được ưu tiên.
`PreciseTiming=True` đặt accuracy của timer Backspace và post-commit thành
`1µs`; khi tắt, accuracy bằng `0` và event-loop backend được phép coalesce timer.

## Vòng đời text key trong Rewrite

1. Fcitx5 gọi `ArecaEngine::keyEvent()`.
2. Release event bình thường không bị giữ lại. Modifier-only key được bỏ qua.
3. Password input được forward thẳng và state cũ của context bị xoá.
4. Special key đi theo policy riêng; text key thử nhánh phím đầu khi engine rảnh.
5. Nếu nhánh đó không xử lý, text key được `filterAndAccept()` rồi đẩy vào `KeyQueue`.
6. Nếu pipeline đang rảnh, scheduler pop ngay đúng một key và gọi Bamboo.
7. Scheduler apply `BambooResult`; trong lúc apply/commit/rewrite, key mới chỉ
   được nối vào cuối FIFO.
8. Forward backend phát đúng số Backspace trong plan rồi chờ
   `AfterBackspaceWaitMs` trước khi commit; frontend `wayland` dùng riêng
   `WaylandAfterBackspaceWaitMs`, mặc định 3 ms.
9. Sau khi apply hoàn tất và qua `PostCommitDelayMs`, scheduler mới pump đúng
   một key tiếp theo.

Scheduler không dùng timer trước Bamboo và không dùng vòng lặp hút hết queue.
Key đầu được xử lý inline trong callback Fcitx; queue chỉ giữ các key đến trong
lúc pipeline đang bận. `PostCommitDelayMs` bắt đầu sau khi backend đã commit và
báo hoàn tất.

### Bật/tắt tính năng nhập liệu

Hai tùy chọn nhập liệu trong **Cấu hình nâng cao**
(`conf/areca-advanced.conf`) mặc định bật:

| Khóa | Nhãn giao diện | Khi tắt |
| --- | --- | --- |
| `EnableMouseTracking` | Tự reset bộ gõ sau khi click chuột trong mọi ứng dụng | Hủy tracker, watcher, pipe và process helper; xóa click đang chờ. |
| `ForwardFirstCharacter` | Chuyển tiếp phím đầu để tương thích trình duyệt | Bỏ qua `handleIdleKey()`, text key dùng luồng accept/enqueue cũ. |

Thay đổi qua giao diện cấu hình có hiệu lực ngay. Nếu sửa file bằng tay, cần
reload cấu hình Fcitx. Bật lại mouse tracking tạo helper mới, không giữ click cũ.
Tắt cả hai bằng:

```ini
EnableMouseTracking=False
ForwardFirstCharacter=False
```

Mouse tracking dùng process riêng; tắt sẽ dừng hẳn process đó. Click reset
composition của input context hiện tại trong mọi ứng dụng. Chuyển tiếp phím đầu
chỉ áp dụng cho trình duyệt và không tạo thread/process riêng.

## Phím đầu khi Rewrite đang rảnh

Một số editor web cần nhận sự kiện phím qua frontend để mở chế độ soạn thảo.
Khi `ForwardFirstCharacter=True`, trước `filterAndAccept()`,
`RewriteModeHandler` chỉ thử `InputScheduler::handleIdleKey()` cho input context
trình duyệt, khi không chờ Backspace release và phím không bị đổi bởi tự viết
hoa. Scheduler chỉ nhận nhánh này khi Bamboo không giữ text, không processing,
không rewrite pending, queue rỗng, không stalled và đã qua khoảng bảo vệ reset
sau commit (hiện là 20 ms).

Bamboo vẫn xử lý phím đúng một lần. Nếu `deleteCount == 0`, `commitText` bằng
UTF-8 đầu vào và không mở rộng macro, addon để event chưa filter/accept cho
frontend xử lý. Trạng thái Bamboo được giữ để phím sau vẫn ghép dấu, ví dụ
`a` rồi `s` thành `á`. Nhánh này không gọi `commitString()`, không phát cặp
`forwardKey()` giả và không tự cập nhật surrounding cache. Scheduler vẫn giữ
barrier `PostCommitDelayMs` trước khi xử lý phím kế tiếp để frontend kịp nhận
phím vừa chuyển tiếp.

`KeyEvent::forward()` trong API Fcitx đang dùng là getter; việc để event chưa
filter/accept mới là điều cho phép phím đi tiếp. Cách frontend giao event đến
trang web vẫn cần kiểm tra trên từng môi trường thực tế.

Nếu Bamboo đổi đầu ra, scheduler accept event và áp dụng ngay kết quả đã tính,
không enqueue lại để tránh xử lý cùng phím hai lần. Nếu chưa đủ điều kiện,
`handleIdleKey()` trả false mà không sửa engine/event; caller dùng FIFO như cũ.
Phím bị tự viết hoa cũng dùng luồng commit để giữ ký tự đã biến đổi.

## Theo dõi click và reset composition

`WindowFocusTracker` là thread đọc AT-SPI trong Fcitx. `MouseClickTracker`
quản lý process con riêng `areca-mouse-monitor`; helper dùng libinput/udev trên
`XDG_SEAT` (mặc định `seat0`) và cùng quyền user với Fcitx. Không cần socket
server hoặc thread nhận chuột trong addon.

```text
libinput/udev → areca-mouse-monitor → stdout pipe riêng (R/C)
             → MouseClickTracker trên event loop Fcitx → pending click
             → ArecaEngine::keyEvent → kiểm tra scheduler → reset handler
```

- `R` báo context libinput đã khởi tạo, không đảm bảo có thiết bị đọc được.
- `C` báo nút chuột được nhấn, không phân biệt nút trái/phải/giữa. Helper bật
  tap-to-click trong context libinput riêng; di chuyển, cuộn và nhả nút không
  gửi reset. udev hỗ trợ thiết bị được cắm thêm sau khi helper chạy.
- Pipe nonblocking tránh chặn event loop/dispatch; nhiều click gộp thành một
  cờ reset. stderr dành cho log, không lẫn vào protocol stdout.
- Callback pipe chỉ đánh dấu. Trước phím nhấn tiếp theo, addon đọc thêm pipe để
  lấy click đã đến nhưng callback chưa chạy. Nếu scheduler đang bảo vệ rewrite,
  cờ vẫn được giữ cho phím sau; nếu được phép thì reset handler đang active,
  xử lý cache verdict theo lifecycle và xóa cờ. Redirect không có composition
  để reset. Đây không phải reset tức thời ngay khi click.
- Activation thử khởi động lại helper đã mất kết nối và bỏ click cũ đã nhận.
  Khi pipe đóng, watcher bị tắt; pending click đã nhận vẫn được giữ. Hủy tracker
  sẽ đóng pipe, kết thúc và thu hồi process con bằng `waitpid()`. Helper cũng
  theo dõi pipe để thoát khi Fcitx mất kết nối, kể cả không có click mới.

Helper được cài vào `CMAKE_INSTALL_LIBEXECDIR`; addon nhúng đường dẫn tuyệt đối
ứng với `CMAKE_INSTALL_PREFIX`. Build cần libinput và libudev. Rule
`70-areca-pointer.rules` cấp `uaccess` cho chuột/touchpad của phiên local đang
active, trước `73-seat-late.rules`. Không có ACL/quyền đọc thiết bị thì tính năng
click không hoạt động; nhập liệu Fcitx bình thường vẫn hoạt động. Xem
[Debug mouse tracker](DEBUGGING.md#mouse-tracker) để kiểm tra cài đặt và log.

## BambooResult và diff

Go bridge tạo một `bamboo.IEngine` trực tiếp bằng:

```go
bamboo.NewEngine(method, bamboo.EstdFlags)
```

`BambooEngineAdapter` giữ chuỗi mà nó tin rằng đang hiển thị. Sau mỗi key,
Bamboo trả chuỗi đã xử lý mới; adapter tìm common prefix UTF-8 và tạo:

```text
currentText  chuỗi trước key
newText      chuỗi Bamboo mới
deleteCount  số Unicode character ở suffix cũ cần xoá
commitText   suffix mới cần chèn
```

Ví dụ Telex:

```text
currentText = "a"
key         = "w"
newText     = "ă"
deleteCount = 1
commitText  = "ă"
```

Ký tự Bamboo không xử lý được được xem như word boundary. Hành vi tại boundary
phụ thuộc vào `SpellcheckMode`:

- `"Không kiểm tra (Tắt)"`: không kiểm tra, commit từ nguyên trạng.
- `"Khôi phục từ sau khi gõ xong"`: gọi `IsValid(true)` trước khi reset. Từ có
  ký tự tiếng Việt nhưng cấu trúc âm tiết không hợp lệ được
  `RestoreLastWord(false)` về chuỗi phím Latin ban đầu; adapter tạo delta
  rewrite cho phần restore rồi nối boundary. Từ hợp lệ chỉ commit boundary
  như bình thường.
- `"Khôi phục từ ngay trong lúc gõ"`: như mức trên và ngoài ra khi xử
  lý từng ký tự trung gian, bridge truyền `spellCheck=1` vào
  `ArecaBambooProcess` để Bamboo kiểm tra và rollback ngay trong lúc gõ.

Tính năng này dùng luật có sẵn của `bamboo-core`, chưa dùng dictionary ngoài.

Khi tạo engine, bridge ánh xạ `ModernStyle=True` sang cách đặt dấu `oà/uý` và
`ModernStyle=False` sang `òa/úy`. Việc ánh xạ ngược với tên flag nội bộ
`EstdToneStyle` là có chủ ý để tên option mô tả trực tiếp chuỗi đầu ra.

Bridge cũng xuất danh sách input method và charset trực tiếp từ `bamboo-core`
cho giao diện cấu hình. Adapter giữ Bamboo state ở Unicode nội bộ, nhưng encode
chuỗi mới bằng `OutputCharset` trước khi tính common prefix. Vì vậy
`currentText`, `newText`, `deleteCount` và `commitText` luôn mô tả đúng chuỗi
thực tế đã commit vào ứng dụng, kể cả `Unicode tổ hợp`, VNI Windows hoặc VIQR.

## Macro expansion

Macro table là sub-config riêng gồm các cặp `Key/Value`. Khi xử lý một word
boundary, bridge lấy word hiện tại ở `PunctuationMode`, lookup key không phân
biệt hoa/thường và expand trước spell-check. Nếu `CapitalizeMacro` bật, key viết
thường tạo replacement viết thường, key toàn chữ hoa tạo replacement toàn chữ
hoa, còn kiểu mixed-case giữ nguyên value cấu hình. Adapter reset Bamboo sau
match, nối boundary, encode theo charset rồi tính delta rewrite. Vì thế macro
không có đường commit đặc biệt và vẫn tuân thủ mọi queue/pending invariant.

Nếu `AutoCapitalizeAfterPunctuation` bật, state theo từng input context theo dõi
`.`, `!`, `?` rồi khoảng trắng. Chữ ASCII thường kế tiếp được đổi thành keysym
hoa trước khi enqueue và trước khi Bamboo xử lý. Phím đã đổi hoa vẫn đi qua
scheduler qua FIFO và bỏ qua nhánh phím đầu đi tiếp, vì replay phím
vật lý không có Shift có thể vẫn tạo chữ thường. `Enter`, reset, Backspace, di
chuyển con trỏ và shortcut sẽ xoá trạng thái chờ để tránh viết hoa nhầm.
Sau khi finalize một từ, adapter giữ composition Bamboo của từ đó và đếm các
dấu cách/dấu câu đã commit phía sau. Backspace đi ngược qua các boundary này;
khi boundary cuối bị xoá, composition vừa finalize được phục hồi để lần gõ kế
tiếp có thể sửa dấu hoặc Backspace tiếp tục xoá từ. Composition chỉ bị bỏ khi
người dùng bắt đầu một từ mới hoặc khi protected reset thực sự chạy.

## Backend selection

Với kết quả đi qua `applyResult()` (phím đã được accept), khi `deleteCount == 0`:

- Nếu `commitText` giống text của phím gốc, scheduler dùng `commitString()`.
  Phím gốc đã bị accept trước khi vào queue nên không replay bất đồng bộ bằng
  `forwardKey()`; GNOME
  IBus Wayland có thể không chuyển loại forwarded key này tới text-input client.
- Nếu Bamboo đã biến đổi output dù không cần xoá, scheduler commit
  `commitText`. Trường hợp điển hình là dấu của `Unicode tổ hợp`.
- Cả hai nhánh đều qua barrier sau commit; kết quả từ `handleIdleKey()` đã biến
  đổi được áp dụng trực tiếp, không cần enqueue lại.

Khi `deleteCount > 0`:

1. `ReliabilityChecker` đánh giá input context.
2. Checker lọc program trước. Chỉ khi program thuộc họ VS Code, là IDE/code
   editor/developer tool, hoặc là terminal Linux đã biết thì checker mới đọc và
   so sánh capability mask. Nếu mask chính xác là `0x72`, checker cache
   `forceForwardBackspace` rồi chọn `ForwardBackspaceBackend`. Các ứng dụng
   ngoài allowlist không được xét rule theo mask; verdict unreliable vốn đã
   chọn forward backend theo policy mặc định.
   Program name rỗng cũng nằm ngoài allowlist và không được fallback theo
   frontend, vì không đủ dữ liệu để ép an toàn.
3. Nếu `UseUinputShiftSelectForBrowser` bật, ứng dụng là trình duyệt web và `/dev/uinput` khả dụng: chọn `UinputShiftSelectBackend`. Tuy nhiên nếu phát hiện đang có bôi đen sẵn (`cursor != anchor`) hoặc có `browserAutocomplete`, hệ thống hủy chọn uinput-shift-select và fallback về `ForwardBackspaceBackend` (+1 phím Backspace phụ) để đảm bảo an toàn.
4. Verdict reliable còn lại chọn `SurroundingTextBackend`.
5. Verdict unreliable chọn `ForwardBackspaceBackend`.

## Sơ đồ Sequence chi tiết các Backend và Selection Logic

### 1. Sơ đồ quyết định lựa chọn Backend (`selectRewriteBackend`)

```mermaid
sequenceDiagram
    autonumber
    participant S as InputScheduler
    participant E as ArecaEngine
    participant R as ReliabilityChecker
    participant IC as InputContext

    S->>E: selectRewriteBackend(inputContext, result)
    E->>R: evaluate(inputContext, shownText, verdict)
    R-->>E: ReliabilityDecision
    E->>IC: surroundingText() & check cursor/anchor
    
    alt VS Code embedded terminal
        E-->>S: Return ForwardBackspaceBackend
    else browserAutocomplete OR (surrounding.isValid & cursor != anchor)
        E-->>S: Return ForwardBackspaceBackend (+1 extra backspace)
    else decision.useSurrounding & UseUinputShiftSelectForBrowser & isBrowser & uinputAvailable
        E-->>S: Return UinputShiftSelectBackend
    else decision.useSurrounding
        E-->>S: Return SurroundingTextBackend
    else DBus terminal/unknown & uinputAvailable
        E-->>S: Return UinputBackspaceBackend
    else ForceUinput & uinputAvailable
        E-->>S: Return UinputBackspaceBackend
    else Fallback
        E-->>S: Return ForwardBackspaceBackend
    end
```

### 2. Sơ đồ luồng `SurroundingTextBackend`

```mermaid
sequenceDiagram
    autonumber
    participant S as InputScheduler
    participant B as SurroundingTextBackend
    participant IC as InputContext
    participant EL as EventLoop

    S->>B: apply(inputContext, plan, onDone)
    alt backspaceCount > 0
        B->>IC: deleteSurroundingText(-count, count)
        B->>B: updateSurroundingCacheAfterDelete(...)
        B->>EL: addTimeEvent(SurroundingWaitMs = 3ms)
        Note over B,EL: Chờ event-loop settling delay
        EL-->>B: Timer callback
        B->>IC: commitString(commitText)
        B->>B: updateSurroundingCacheAfterCommit(...)
        B->>S: onDone(transactionId)
    else backspaceCount == 0
        B->>IC: commitString(commitText)
        B->>S: onDone(transactionId) (Immediate Completed)
    end
```

### 3. Sơ đồ luồng `UinputShiftSelectBackend`

```mermaid
sequenceDiagram
    autonumber
    participant S as InputScheduler
    participant B as UinputShiftSelectBackend
    participant DEV as UinputDevice (/dev/uinput)
    participant EL as EventLoop
    participant IC as InputContext

    S->>B: apply(inputContext, plan, onDone)
    B->>DEV: sendKeyEvent(KEY_LEFTSHIFT, 1) (Shift DOWN)
    B->>EL: addTimeEvent(ShiftSelectDelayMs = 1ms)
    
    loop N = backspaceCount lần
        EL-->>B: Timer callback
        B->>DEV: sendKeyEvent(KEY_LEFT, 1) -> (KEY_LEFT, 0)
        B->>EL: addTimeEvent(ShiftSelectDelayMs = 1ms)
    end
    
    EL-->>B: Timer callback (Bôi đen hoàn tất)
    B->>DEV: sendKeyEvent(KEY_LEFTSHIFT, 0) (Shift UP)
    B->>EL: addTimeEvent(AfterSelectWaitMs)
    
    EL-->>B: Timer callback (Settling wait hoàn tất)
    
    loop Lần lượt từng ký tự UTF-8 trong commitText
        B->>IC: commitString(utf8_char)
        B->>EL: addTimeEvent(1ms)
        EL-->>B: Timer callback
    end
    
    B->>B: finishTransaction()
    B->>S: onDone(transactionId)
```

### 4. Sơ đồ luồng `UinputBackspaceBackend`

```mermaid
sequenceDiagram
    autonumber
    participant S as InputScheduler
    participant B as UinputBackspaceBackend
    participant DEV as UinputDevice (/dev/uinput)
    participant EL as EventLoop
    participant IC as InputContext

    S->>B: apply(inputContext, plan, onDone)
    
    loop N = backspaceCount lần
        B->>DEV: sendKeyEvent(KEY_BACKSPACE, 1) -> (KEY_BACKSPACE, 0)
        B->>EL: addTimeEvent(BackspaceDelayMs)
        EL-->>B: Timer callback
    end
    
    B->>EL: addTimeEvent(AfterBackspaceWaitMs)
    EL-->>B: Timer callback (Settling wait hoàn tất)
    B->>IC: commitString(commitText)
    B->>B: clearPending()
    B->>S: onDone(transactionId)
```

### 5. Sơ đồ luồng `ForwardBackspaceBackend`

```mermaid
sequenceDiagram
    autonumber
    participant S as InputScheduler
    participant B as ForwardBackspaceBackend
    participant IC as InputContext
    participant EL as EventLoop

    S->>B: apply(inputContext, plan, onDone)
    
    loop N = backspaceCount lần
        B->>IC: forwardKey(Backspace press/release)
        B->>EL: addTimeEvent(BackspaceDelayMs)
        EL-->>B: Timer callback
    end
    
    B->>EL: addTimeEvent(AfterBackspaceWaitMs)
    EL-->>B: Timer callback (Settling wait hoàn tất)
    B->>IC: commitString(commitText)
    B->>B: clearPending()
    B->>S: onDone(transactionId)
```

## Preedit mode

`PresentationMode=Preedit` không dùng `InputScheduler`, `RewriteBackend`,
`ReliabilityChecker` hay forward-Backspace backend. Nó có riêng:

- `PreeditInputState` cho từng input context;
- một `BambooEngineAdapter` riêng;
- xử lý `KeyEvent` đồng bộ như frontend Preedit của Bamboo;
- composition, sentence-capitalization và delayed-reset riêng.

Text key được xử lý tuần tự rồi hiển thị bằng client preedit nếu app khai báo
`CapabilityFlag::Preedit`; nếu không thì dùng server-side preedit của Fcitx.
Khi composition còn tồn tại, Backspace gọi `processBackspace()` nếu
`BackspaceRecovery=True`, cho phép Bamboo dựng lại chuỗi spell-check như
`nhanhsh` thành `nhánh`; khi option tắt nó chỉ xóa một ký tự. Space/dấu câu chạy
macro và spell-check qua Bamboo rồi được commit ngay. Adapter vẫn giữ finalized
snapshot của từ và số boundary liên tiếp. Backspace qua boundary trung gian được
forward nguyên bản; khi chạm tới từ, handler xóa `từ + boundary cuối` bằng
`deleteSurroundingText()` rồi đưa từ lên preedit, nhưng chỉ khi app hỗ trợ
`SurroundingText` và snapshot khớp chính xác. Nếu không, handler reset finalized
state và forward Backspace gốc. Phím text mới làm adapter bỏ finalized snapshot
cũ và bắt đầu composition mới.
Enter, Tab, Delete và phím di chuyển commit composition trước khi được forward.
Escape và shortcut cũng commit composition trước, sau đó Fcitx forward nguyên
`KeyEvent` gốc.

Hai mode xử lý tiếng Việt chỉ dùng chung cấu hình bất biến và lớp adapter; chúng
không dùng chung engine instance hoặc mutable input state. `RedirectModeHandler`
không có engine/state. `ArecaEngine` chỉ dispatch theo mode global, và reset
cả Rewrite lẫn Preedit khi người dùng đổi mode.

Hotkey `SwitchModeKey` (mặc định `Alt+Space`) được bắt trước bước dispatch vì nó
thay đổi chính handler đích. Areca hủy state/queue của hai handler có state,
quay vòng `Rewrite → Preedit → Redirect → Rewrite`, lưu mode global và gọi popup
thông tin Fcitx5. Hotkey không đổi mode giữa transaction rewrite đang pending;
password context tiếp tục nhận phím gốc như bình thường.

## Redirect mode

`Redirect` chỉ được bật bằng `PresentationMode` global; Areca không tự đổi mode
theo input context. `RedirectModeHandler` không xử lý nội dung: press event gọi
`KeyEvent::forward()`, release event được để nguyên cho Fcitx chuyển tiếp.

## Reliability lifetime

Probe chỉ diễn ra khi rewrite đầu tiên cần delete:

- App phải quảng bá capability `SurroundingText`.
- Snapshot phải valid.
- Từ ngay trước cursor phải có suffix không rỗng khớp với `currentText`.

Kết quả được cache ở cấp `ArecaEngine`, cùng UUID của context hiện tại. Bình
thường lifecycle sẽ xoá verdict để rewrite sau đánh giá lại. Backspace, Ctrl+A
và phím di chuyển/chọn text bảo vệ riêng verdict trong 1 giây; lifecycle và reset
state nhập vẫn chạy bình thường. UUID thay đổi luôn làm cache được tạo lại cho
context mới.

Snapshot browser autocomplete không được dùng làm first probe; checker giữ
`known=false` để lần rewrite bình thường sau mới quyết định reliability.

## Forward-Backspace single-flight state machine

```text
Idle
 │ apply(plan)
 ▼
Forward Backspace đầu tiên
 │ còn Backspace
 ├──► timer(BackspaceDelayMs) ──► forward Backspace kế tiếp
 │ hết Backspace
 └──► timer(AfterBackspaceWaitMs)
                │
 ▼
Commit text → clear pending → post-commit timer → process next key
```

Trong `Pending`, `processing_` vẫn là true nên scheduler không lấy key tiếp.
Key mới chỉ được append vào queue. Backend giữ đúng một transaction và gọi
completion callback đúng một lần sau commit.

## Surrounding cache

Các backend/helper cập nhật object `surroundingText()` mà Fcitx đang cache ngay
sau delete hoặc commit để những thao tác kế tiếp không đọc snapshot cũ. Frontend
vẫn là nguồn dữ liệu chính và snapshot mới của ứng dụng sẽ thay thế cache này.

## Khôi phục Bamboo state từ surrounding text

`InputContextSurroundingTextUpdated` arm một lần thử cho cả `RewriteInputState`
và `PreeditInputState`. Ở phím text hợp lệ kế tiếp, handler đang hoạt động chỉ
đọc từ đứng ngay trước con trỏ khi `RestoreSurroundingText=True`, charset là
`Unicode` và không có selection/password. `RewriteModeHandler` còn yêu cầu
scheduler không có transaction đang chạy.

Extractor lấy tối đa 16 chữ cái Latin/tiếng Việt Unicode dựng sẵn. Chuỗi UTF-8
lỗi, dấu tổ hợp và từ bị cắt bởi giới hạn đều bị từ chối. Khi option tắt,
handler bỏ qua toàn bộ bước đọc và phục hồi.

Bridge không đoán rồi sửa trực tiếp engine đang hoạt động. Nó tách chữ gốc và
các hiệu ứng dấu, thử một tập phím hữu hạn theo input method hiện tại trên
engine tạm, và yêu cầu output khớp chính xác từ trên màn hình. Chỉ khi xác minh
thành công, cùng chuỗi phím mới được nạp vào engine thật. Adapter đồng bộ
`renderedText_`, nên delta do phím kế tiếp tạo ra đi qua scheduler và backend
rewrite hiện có; không có đường xóa/commit riêng cho tính năng này.

`PreeditModeHandler` không thể giữ nguyên bản text đã commit vì composition mới
sẽ hiển thị trùng ngay sau nó. Sau khi bridge xác minh state, handler gọi
`deleteSurroundingText(-N, N)`, cập nhật surrounding cache rồi đặt từ cũ vào
`state.composing`. Phím hiện tại tiếp tục xử lý trên state đó và toàn bộ từ được
hiển thị dưới dạng preedit. Nếu Bamboo ném lỗi sau khi xóa, handler commit bản
từ cũ trở lại trước khi forward phím để tránh mất dữ liệu.

## Reset barrier

`ArecaEngine::reset()` chỉ arm timer của mode đang hoạt động, không reset ngay.
Reset thật xảy ra sau `ResetDelayMs` nếu không có input mới. Rewrite pending
làm timer Rewrite được arm lại. Preedit không có processing queue; phím
mới chỉ huỷ delayed-reset của chính nó. Hai timer và hai composition không tham
chiếu lẫn nhau.

Special key mà user chủ động gõ vẫn có policy tức thời:

- Cursor, Tab, Escape và Ctrl/Alt/Super/Meta combination: reset Bamboo rồi
  forward.
- Return/KP Enter: reset Bamboo rồi forward nguyên event.
- Backspace: gọi `RemoveLastChar(true)` để đồng bộ Bamboo rồi forward.
- Delete: forward mà không thay Bamboo history.

## Hướng mở rộng

### 1. Thêm Mode SurroundingOnly
Policy `SurroundingOnly` sau này có thể được thêm như một rewrite mode thứ ba,
với state và scheduler riêng hoặc backend selector luôn chọn
`SurroundingTextBackend`. Nó không cần thay đổi `PreeditModeHandler`.

### 2. SurroundingTextV2Backend: Xóa từng ký tự thay vì xóa toàn bộ

#### Hiện trạng của v1
`SurroundingTextBackend` v1 thực hiện xóa `N` ký tự trong một câu lệnh duy nhất:
```cpp
inputContext.deleteSurroundingText(-static_cast<int>(plan.backspaceCount), plan.backspaceCount);
```

#### Hạn chế của cơ chế xóa gộp
- **Lỗi tính offset của ứng dụng**: Một số trình duyệt như Firefox Gecko trên Wayland hoặc các bộ soạn thảo Web như Monaco Editor, CodeMirror xử lý câu lệnh xóa gộp `-N` ký tự không ổn định khi phía sau con trỏ đang có các ký tự đặc biệt như dấu ngoặc `}}` hoặc thẻ HTML.
- **Rủi ro lệch vị trí xóa**: Việc xóa gộp một lượt có thể dẫn tới hiện tượng trình duyệt xóa thiếu ký tự hoặc tính nhầm độ dài ký tự UTF-8 multibyte.

#### Cơ chế của SurroundingTextV2Backend
`SurroundingTextV2Backend` sử dụng cơ chế vòng lặp xóa từng ký tự qua timer event loop:
1. Mỗi nhịp timer `SurroundingDeleteDelayMs`, phát lệnh xóa 1 ký tự trước con trỏ: `deleteSurroundingText(-1, 1)`.
2. Lặp lại cho tới khi xóa đủ số ký tự cần thiết `backspaceCount`.
3. Chờ hết thời gian settling delay `AfterSurroundingDeleteWaitMs`, sau đó mới thực hiện `commitString(commitText)`.
4. Có thể kích hoạt thông qua tùy chọn `UseSurroundingV2ForBrowser` khi ứng dụng là trình duyệt.

#### Sơ đồ Sequence của SurroundingTextV2Backend
```mermaid
sequenceDiagram
    autonumber
    participant S as InputScheduler
    participant B as SurroundingTextV2Backend
    participant IC as InputContext
    participant EL as EventLoop

    S->>B: apply(inputContext, plan, onDone)
    
    alt backspaceCount > 0
        loop N = backspaceCount lần
            B->>IC: deleteSurroundingText(-1, 1)
            B->>B: updateSurroundingCacheAfterDelete(-1, 1)
            B->>EL: addTimeEvent(SurroundingDeleteDelayMs = 1ms)
            EL-->>B: Timer callback
        end
        
        B->>EL: addTimeEvent(AfterSurroundingDeleteWaitMs = 3ms)
        EL-->>B: Timer callback
        B->>IC: commitString(commitText)
        B->>B: updateSurroundingCacheAfterCommit(...)
        B->>S: onDone(transactionId)
    else backspaceCount == 0
        B->>IC: commitString(commitText)
        B->>S: onDone(transactionId)
    end
```
