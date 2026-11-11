---
name: areca-task-memory
description: Cách làm việc tiết kiệm token trong repo ArecaIME - ghi nhớ tiến độ task vào file ghi chú, bản đồ dự án để khỏi dò lại, quy tắc đọc file, đọc log và chạy lệnh gọn. Dùng khi bắt đầu hoặc tiếp tục một task nhiều bước, khi phiên làm việc dài, trước khi context bị tóm tắt, hoặc khi người dùng dán log rất dài.
---

# Ghi nhớ task và tiết kiệm token

Mục tiêu: không đọc lại thứ đã biết, không dò lại thứ đã có bản đồ, và để phiên sau tiếp tục được ngay từ file ghi chú thay vì đọc lại cả lịch sử.

## 1. File ghi chú task

Mỗi task nhiều bước có một file `.claude/task-notes/<slug>.md` (đã nằm trong `.gitignore`, không commit). `<slug>` ngắn, không dấu, ví dụ `surrounding-stale-snapshot`.

**Khi bắt đầu hoặc tiếp tục task:** liệt kê `.claude/task-notes/`, đọc file liên quan **trước** khi đọc code. Có ghi chú thì tin nó và chỉ kiểm tra lại những gì có thể đã đổi (commit mới, file đã sửa).

**Cập nhật ở các mốc:** sau khi người dùng chốt quyết định, sau mỗi commit, trước khi dừng hay khi phiên đã dài. Không cập nhật sau từng lệnh nhỏ.

Mẫu (giữ dưới khoảng 60 dòng, ghi đè chứ không nối dài):

```markdown
# <Tên task>
Cập nhật: <YYYY-MM-DD HH:MM>

## Mục tiêu
<1-3 dòng, theo lời người dùng>

## Quyết định đã chốt
- <quyết định> — <lý do / người dùng chọn gì> (không mở lại trừ khi người dùng hỏi)

## Đã bỏ / không làm
- <hướng đã thử và bị bỏ> — <vì sao>, commit <sha> nếu có

## Trạng thái
- Commit: <sha> <tiêu đề> (đã push / chưa push)
- File đã sửa: <đường dẫn ngắn>
- Đã kiểm: <build, ctest x/y, thử thực tế gì>

## Việc tiếp theo
1. <bước cụ thể>

## Bẫy đã gặp
- <lỗi, nguyên nhân, cách tránh>
```

Phần **Đã bỏ / không làm** quan trọng nhất: nó ngăn đề xuất lại đúng thứ người dùng đã gạt.

## 2. Ba nơi lưu, đừng lẫn

| Nơi | Lưu gì | Ví dụ |
|---|---|---|
| `.claude/task-notes/*.md` | Tiến độ và quyết định của **một** task | "Firefox chỉ bỏ qua kiểm tra trễ, không bỏ probe" |
| Auto memory (`~/.claude/projects/.../memory/`) | Sở thích, cách làm việc của người dùng dùng cho **mọi** task | "Không thêm dòng Co-Authored-By", "Người dùng tự push" |
| Repo (`docs/`, skill, comment code) | Sự thật về code mà người khác cũng cần | Kiến trúc, chính sách backend, bất biến scheduler |

Không chép nội dung code hay git log vào ghi chú; ghi đường dẫn và sha là đủ.

## 3. Bản đồ dự án (khỏi phải dò)

| Cần tìm | Ở đâu |
|---|---|
| Kiến trúc, pipeline, chính sách backend | `docs/ARCHITECTURE.md` (đọc theo mục, đừng đọc cả file) |
| Quy tắc code, nơi đặt hàm mới | skill `areca-code` |
| Quy tắc PR | skill `areca-pr`, `CONTRIBUTING.md` |
| Kiến thức port sang IBus | skill `areca-ibus-port` |
| Nhận phím, chọn mode | `src/areca_engine.cpp` (`keyEvent`, `activate`, `reset`) |
| Chọn backend, verdict, probe | `src/areca_engine_backend.cpp` (`selectRewriteBackend`, `evaluateReliability`) |
| Config, `applyConfig`, `timing` | `src/areca_engine_config.cpp`, khai báo option ở `src/areca_config.h` |
| Menu tray fcitx | `src/areca_engine_actions.cpp` |
| Rewrite / Preedit | `src/rewrite_mode.*`, `src/preedit_mode.*` |
| Scheduler, cửa sổ chặn reset | `src/input_scheduler.*` |
| So khớp snapshot, text đã biết | `src/surrounding_probe_match.h` |
| Danh sách app đặc biệt | `src/program_compatibility.cpp` |
| AT-SPI | `src/window_focus_tracker.cpp`, `src/atspi/` |
| App cài đặt GTK | `gtk-settings/areca_settings.py` |
| Version | `CMakeLists.txt`, `packaging/{arch,aur}/PKGBUILD`, `packaging/aur/.SRCINFO`, ví dụ trong `docs/AUR_PUBLISHING_GUIDE.md` và `packaging/aur/README.md` |

## 4. Quy tắc đọc

- **Tìm trước, đọc sau:** `grep -n` tên hàm hoặc chuỗi log, rồi đọc đúng khoảng bằng `sed -n 'a,bp'` hoặc `Read` với `offset`/`limit`.
- **Không đọc cả các file lớn:** `docs/ARCHITECTURE.html`, `website/*.html` (hơn 4000 dòng mỗi file, chỉ là bản dựng từ markdown), `bamboo/` (submodule), `build*/`.
- **Đã đọc hoặc vừa sửa thì không đọc lại để kiểm tra:** Edit thất bại sẽ tự báo lỗi.
- **Diff:** xem `git diff --stat` trước, chỉ mở diff đầy đủ của file cần xem.
- **Câu hỏi rộng** ("chỗ nào dùng X", "có code nào làm Y"): giao cho subagent Explore và chỉ nhận kết luận, không kéo hàng chục file vào context.
- **Dò nhiều thứ độc lập:** gộp vào một lệnh hoặc gọi song song.

## 5. Log và output lệnh

- **Log fcitx5 người dùng dán:** chỉ lấy các dòng quyết định, đừng diễn giải từng dòng. Các mẫu cần tìm:
  `rewrite select backend|reliability first-probe|stale during probe|probe succeeded|surrounding restore|reset rejected|app reset|bamboo result`.
- **Gợi ý cho người dùng lệnh lọc sẵn** thay vì xin cả log:
  `fcitx5 -rd 2>&1 | grep -E "<mẫu ở trên>"`.
- **Build và test:** chỉ in phần cần, ví dụ
  `cmake --build build -j"$(nproc)" 2>&1 | grep -E "error|warning:"` và `ctest --test-dir build 2>&1 | grep -E "tests passed|failed"`.
  Chỉ chạy `--output-on-failure` khi có test hỏng.
- **Lệnh dài:** chạy nền, không poll.

## 6. Quy trình lặp lại trong repo này (khỏi phải tìm lại)

- Main checkout bị hook chặn sửa trực tiếp. Sửa trong worktree, commit, rồi `git -C <main> cherry-pick <sha>`. Worktree lệch thì `git reset --hard main`.
- Không tự push. Người dùng tự tag và push. Commit không có dòng attribution.
- Trước commit: `clang-format -i` các file đã sửa, `git clang-format --diff main` phải sạch, build không warning, `ctest` qua hết.
- Sửa `docs/ARCHITECTURE.md` thì cập nhật luôn `docs/ARCHITECTURE.html`. Script dựng HTML nằm trong scratchpad và có thể đã mất; khi đó sửa tay đúng đoạn HTML tương ứng.
- Scratchpad bị dọn khi qua ngày: đừng để thứ cần giữ ở đó.

## 7. Khi phiên dài hoặc sắp bị tóm tắt

1. Cập nhật file ghi chú task theo mẫu ở mục 1.
2. Thông tin về người dùng mới học được (sở thích, điều họ không muốn) thì lưu vào auto memory, không để trong ghi chú task.
3. Câu trả lời cuối nêu: commit nào, đã push chưa, còn gì chưa kiểm.
