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

Bật:

```ini
Debug=True
```

Theo dõi log:

```bash
journalctl --user -f | grep areca
```

Những dòng hữu ích:

```text
areca: queue push
areca: scheduler process
areca: bamboo result
areca: reliability first-probe
areca: reliability first-probe force_forward=1 reason=program-compatibility-capability-mask-0x72
areca: selected uinput-shift-select backend for browser
areca: uinput-shift-select start tx=
areca: uinput-select split commit (1ms)
areca: browser autocomplete or active selection strategy=
areca: rewrite select backend=
areca: forward-backspace start
areca: forward-backspace sent
areca: forward-backspace complete
areca: rewrite done
areca: protected reset cancelled/executed
```

## Phân biệt lỗi backend

Khi log có:

```text
rewrite select backend=surrounding-text
```

Areca đang dùng `deleteSurroundingText()`. Kiểm tra first-probe word/shown nếu
xoá sai.

Khi log có:

```text
rewrite select backend=forward-backspace
```

Kiểm tra theo thứ tự:

1. `forward-backspace start` có đúng `backspaces` không.
2. Số dòng `forward-backspace sent` có đủ không.
3. `forward-backspace complete` có xuất hiện sau `after_wait_ms` không.
4. Sau complete có `rewrite done` và post-commit barrier không.

## Cấu hình cũ che default mới

Timing hiện được lưu trong
`~/.config/fcitx5/conf/areca-advanced.conf`. Ví dụ source đổi `ResetDelayMs`
mặc định thành 250 không tự sửa dòng `ResetDelayMs=120` đã tồn tại. Kiểm tra
trực tiếp:

```bash
grep -E '^(BackspaceDelayMs|AfterBackspaceWaitMs|WaylandAfterBackspaceWaitMs|PostCommitDelayMs|PreciseTiming|ResetDelayMs)=' \
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
(`conf/areca-advanced.conf`), mặc định `False`. Hai khóa cũ trong
`conf/areca.conf` không còn được đọc; muốn bật lại, đặt trong cấu hình nâng cao. Tắt mouse tracking phải làm process `areca-mouse-monitor` biến
mất; bật lại phải tạo helper mới. Tắt chuyển tiếp phím đầu phải không còn log
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
