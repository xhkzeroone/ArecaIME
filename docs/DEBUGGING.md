# Cài đặt, reload và debug

## Kiểm tra build

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

`build/areca.so` là addon vừa build. Với prefix `/usr`, bản được Fcitx load là
`/usr/lib/fcitx5/areca.so` trên hệ thống hiện tại.

## Xác nhận binary đã cài

```bash
ls -li --time-style=long-iso build/areca.so /usr/lib/fcitx5/areca.so
```

Timestamp/size giống nhau chỉ chứng minh file đã copy. Native addon đang chạy
vẫn có thể giữ inode cũ trong RAM.

```bash
fcitx_pid="$(pgrep -n -x fcitx5)"
grep 'areca\.so' "/proc/$fcitx_pid/maps"
```

Nếu output có `(deleted)`, installer đã thay file trên đĩa nhưng Fcitx hiện tại
vẫn chạy code cũ. Cần restart đúng process Fcitx.

## Plasma Wayland

Trên Plasma, KWin có thể trực tiếp spawn Fcitx:

```text
kwin_wayland → fcitx5
```

Process này nhận một `WAYLAND_SOCKET` riêng từ KWin. Vì vậy `fcitx5 -rd` chạy
từ terminal có thể không thay được instance đó dù command không báo lỗi rõ
ràng. Đăng xuất/đăng nhập luôn tạo process mới và chắc chắn nạp addon mới.

Kiểm tra quan hệ process:

```bash
ps -eo pid,ppid,lstart,comm,args | grep -E 'kwin_wayland|fcitx5'
```

Không cần logout nếu process Fcitx thực sự restart và PID đổi. Sau restart, chạy
lại lệnh `/proc/<pid>/maps` để xác nhận không còn `(deleted)`.

## Log addon

Bật `Debug=True` trong `~/.config/fcitx5/conf/areca.conf` hoặc mục **Bật log
debug Areca** trong giao diện. Cách chắc chắn nhất để thấy log là chạy Fcitx
ngay trong terminal:

```bash
fcitx5 -rd 2>&1 | tee ~/areca.log
```

`scripts/install.sh` khởi động lại Fcitx với output bị bỏ đi, nên sau khi cài
bằng script cần tự chạy lại lệnh trên. Nếu phiên desktop thu stderr của Fcitx
vào user journal:

```bash
journalctl --user -f | grep --line-buffered areca
```

Những dòng hữu ích, theo thứ tự xuất hiện trong một lần rewrite:

```text
areca: queue push kind=text seq=
areca: scheduler process seq=
areca: bamboo result current= new= delete= commit=
areca: reliability first-probe text= cursor= anchor= reliable=
areca: reliability cached known= reliable= force_forward=
areca: auto selected surrounding backend=
areca: app-override selected ... backend=
areca: terminal selected backend=
areca: url selected shift-select backend=
areca: url without surrounding selected shift-select backend=
areca: autocomplete selected shift-select backend=
areca: browser autocomplete or address bar strategy=
areca: rewrite select backend= tx= bamboo_delete= plan_backspaces=
areca: surrounding-text start tx=
areca: surrounding probe succeeded via SurroundingTextUpdated event
areca: surrounding delete unconfirmed after
areca: fallback from unconfirmed surrounding delete to backend=
areca: uinput-backspace start tx=
areca: virtual-keyboard-backspace start tx= backspaces= bursts= commit=
areca: virtual-keyboard-backspace done tx= ok= echo_pending=
areca: virtual-keyboard-backspace unavailable, fallback backend=
areca: virtual-keyboard-device unavailable reason=
areca: native unavailable, fallback backend=
areca: native-device portal remote-desktop version= devices=
areca: native-device unsupported reason=
areca: uinput-backspace press tx= seen= expected=
areca: uinput-backspace ack timeout tx=
areca: uinput-shift-select start tx= select_left= emit_left=
areca: uinput-shift-select shift-up ack tx=
areca: uinput-shift-select shift-up ack timeout tx=
areca: uinput-select 2-step commit tx=
areca: forward-backspace start tx=
areca: rewrite done tx=
areca: post-commit barrier delay_us=
```

Dòng liên quan reset và thời gian chờ thích nghi:

```text
areca: reset rejected
areca: app reset rejected (active rewrite or 50ms post-commit window)
areca: event loop lag detected lateness_us= adaptive_extra_ms=
areca: system thermal or load lag detected temp_mc=
areca: ... adaptive commit wait tx= base_ms= extra_ms= effective_ms=
areca: ... lag before commit tx= additional_wait_ms=
areca: ... adaptive wait decayed tx=
```

## Phân biệt lỗi backend

Khi log có:

```text
rewrite select backend=surrounding-text
```

Areca đang dùng `deleteSurroundingText()`. Nếu xoá sai, xem dòng
`reliability first-probe` và `surrounding probe ...`. Dòng
`surrounding snapshot stale during probe` nghĩa là ứng dụng gửi surrounding trễ
lúc thăm dò; Areca chuyển sang Backspace hoặc Shift select và thử lại ở từ kế
tiếp. Dòng `surrounding restore rejected reason=stale-snapshot` nghĩa là một lần
restore bị bỏ qua vì snapshot chưa có text Areca vừa commit. Dòng
`surrounding restore disabled reason=non-surrounding-backend` nghĩa là app
đang xoá chữ bằng Backspace hoặc Shift select nên Rewrite không restore.

Khi log có:

```text
rewrite select backend=forward-backspace
```

Kiểm tra theo thứ tự:

1. `forward-backspace start` có đúng `backspaces` không.
2. Số dòng `forward-backspace sent` có đủ không.
3. `forward-backspace complete` có xuất hiện sau `after_wait_ms` không.
4. Sau complete có `rewrite done` và post-commit barrier không.

Khi log có `rewrite select backend=uinput-shift-select`, kiểm tra:

1. `select_left=N` phải đi cùng `emit_left=N`.
2. Mọi Left và Shift quay về Fcitx đều được forward tới ứng dụng.
3. Sau `shift-up sent` phải có `shift-up ack`. Backend chờ 5 ms trên Wayland
   hoặc 10 ms trên frontend khác, cộng adaptive, rồi commit hai bước: ký tự đầu,
   chờ 5 ms, phần còn lại.
4. `shift-up ack timeout` nghĩa là Shift up không quay về trong 150 ms. Backend
   vẫn commit nhưng tăng thời gian chờ adaptive.

`native-shift-select` và `native-backspace` phát phím qua libei và
không có ACK, nên log chỉ có dòng start, các bước phát phím và commit.

## Chọn backend theo ứng dụng

Nhấn phím chọn backend, mặc định phím dấu huyền, khi đang ở ô nhập và chưa gõ dở
từ nào. Menu hiện tên ứng dụng và backend cụ thể của từng lựa chọn. Lựa chọn được
lưu trong `~/.config/fcitx5/conf/areca-apps.conf`. Log tương ứng:

```text
areca: app-override selected surrounding backend= program=
areca: app-override selected shift-select backend= program=
areca: app-override selected backspace backend= program=
```

Muốn quay về tự động, chọn lại **Tự động** trong menu hoặc xoá mục của ứng dụng
trong file trên rồi reload cấu hình.

Khi log có `rewrite select backend=uinput-backspace`, kiểm tra tương tự:

1. `backspaces=N` phải đi cùng `emit_backspaces=N`.
2. Mọi press/release quay lại đều có `action=forward`; `seen` chỉ là bộ đếm ACK
   tương đối, không dùng để lọc event.
3. Khi `seen=N`, settling wait có thể bắt đầu trong lúc timer phát phím riêng
   vẫn tiếp tục gửi đủ N Backspace. Backend không commit trước khi phát xong.
4. `ack timeout` nghĩa là bộ đếm chưa đạt N trong timeout cấu hình (mặc định
   150 ms). Sau đó backend vẫn chạy settling wait theo frontend và
   `AdaptiveWait` trước khi commit.

## Cấu hình cũ che default mới

Timing hiện được lưu trong
`~/.config/fcitx5/conf/areca-advanced.conf`. Khoá đang ở mặc định được ghi dạng
comment, còn khoá đã đổi sẽ giữ giá trị cũ khi source đổi mặc định. Ví
dụ source nâng `WaylandAfterBackspaceWaitMs` từ 3 lên 10 ms không sửa dòng
`WaylandAfterBackspaceWaitMs=3` đã có. Kiểm tra trực tiếp:

```bash
grep -E '^(BackspaceDelayMs|AfterBackspaceWaitMs|WaylandAfterBackspaceWaitMs|PostCommitDelayMs|PreciseTiming)=' \
  ~/.config/fcitx5/conf/areca-advanced.conf
```

Bản nâng cấp vẫn đọc các giá trị timing cũ trong `areca.conf` khi file
nâng cao chưa có. Sau khi mở và lưu panel **Cấu hình nâng cao**, hãy kiểm tra
file mới ở trên.

Sau khi sửa config, reload/restart Fcitx rồi kiểm tra log timing thực tế.

## Phím đầu đi tiếp khi engine rảnh

Bật `Debug=True` trong cấu hình Areca và tìm:

```text
areca: idle key route=forward reason=unchanged-output
areca: idle key route=commit reason=transformed-output
```

`forward` nghĩa là addon giữ event chưa filter/accept và không commit thêm;
`commit` nghĩa là Bamboo biến đổi đầu ra nên addon phải chặn phím gốc. Khi đã có
composition, queue, rewrite, barrier, chờ Backspace release hoặc tự viết hoa đổi
ký tự, phím đi theo luồng cũ nên không nhất thiết xuất hiện hai dòng trên.

Test tự động `idle-key-forward` kiểm tra phím đầu không bị commit lặp, Bamboo
vẫn giữ trạng thái, output biến đổi được commit và rewrite/queue chặn đường tắt.
Trên web thực tế, thử click vào editor chưa mở chế độ nhập, gõ phím đầu rồi
`a` + `s`; kiểm tra editor mở, không mất/lặp ký tự và ghép dấu đúng. Unit test
không chứng minh frontend/browser sẽ giao sự kiện phím theo cùng một cách.

## Bật/tắt và xác nhận cấu hình

`EnableMouseTracking` và `ForwardFirstCharacter` nằm trong **Cấu hình nâng cao**
(`conf/areca-advanced.conf`), mặc định `True`. Mouse tracking áp dụng cho mọi
ứng dụng. Hai khóa cũ trong `conf/areca.conf` không còn được đọc. Tắt mouse
tracking phải làm process `areca-mouse-monitor` biến mất; bật lại phải tạo helper
mới. Tắt chuyển tiếp phím đầu phải không còn log
`idle key route=...` và text key đi qua queue. Đổi qua giao diện có hiệu lực ngay;
sửa file bằng tay cần reload cấu hình.

Khi bật debug, mỗi lần áp dụng cấu hình có dòng:

```text
areca: input options mouse_tracking=... forward_first_character=...
```

Thử bật/tắt riêng từng tùy chọn, cả khi đang có click pending. Khi bật lại mouse
tracking, click từ trước lúc tắt không được làm reset lần gõ mới.

## Mouse tracker

Với prefix `/usr` và libexec mặc định, helper nằm ở
`/usr/libexec/areca-mouse-monitor`; nếu đổi prefix/libexec, kiểm tra giá trị trong
`build/CMakeCache.txt`. Cần cài cả addon và helper rồi restart Fcitx để nạp bản mới.

```bash
pgrep -af areca-mouse-monitor
ls -l /usr/libexec/areca-mouse-monitor
```

Helper là process con của Fcitx, không có systemd service riêng. Log đi vào
stderr kế thừa từ Fcitx; dùng journal nếu phiên desktop thu stderr vào journal,
hoặc xem nơi launcher Fcitx ghi log. Với `Debug=True`, luồng thường thấy là:

```text
areca: mouse helper started pid=
areca: mouse helper ready pid=
areca: mouse click received; reset pending
areca: mouse reset deferred (rewrite protection)
areca: applying mouse reset mode=
areca: discard pending mouse click on activation
areca: mouse helper cleanup pid=
```

Dòng `deferred` chỉ có khi một phím đến trong khoảng bảo vệ; pending click vẫn
được giữ. `ready` chỉ xác nhận libinput khởi tạo, không xác nhận quyền đọc thiết
bị. WARN về spawn/pipe/read/disconnect luôn hiện kể cả khi tắt debug. Helper
báo `access denied` hoặc `cannot initialize libinput seat` trên stderr.

Nếu spawn thất bại, kiểm tra đường dẫn helper và quyền thực thi. Nếu thiếu quyền
thiết bị, kiểm tra rule `70-areca-pointer.rules` ở thư mục udev hệ thống, ACL của
thiết bị event tương ứng và phiên local đang active. Rule phải chạy trước
`73-seat-late.rules` để logind áp dụng `uaccess`. Sau khi cài rule:

```bash
sudo udevadm control --reload-rules
```

Kết nối lại chuột/touchpad hoặc đăng xuất/đăng nhập để áp dụng; chỉ reload không
cập nhật ACL cho thiết bị đã tồn tại. Helper thoát sẽ được thử chạy lại vào lần
Areca activation tiếp theo. Không có quyền đọc thiết bị chỉ làm mất tính năng
reset theo click, không ngăn luồng nhập liệu thông thường.

Test `mouse-click-tracker` dùng helper giả, kiểm tra protocol, gộp click, giữ cờ
cho reset bị hoãn, start/stop nhiều lần và thu hồi process con. Cần thử riêng
click thật, tap touchpad và mất kết nối helper trên phiên desktop có libinput.
