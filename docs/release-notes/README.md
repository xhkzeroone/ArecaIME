# Release note viết tay

Trước khi tạo tag `vX.Y.Z`, viết `docs/release-notes/vX.Y.Z.md` (bắt đầu bằng
`## Areca vX.Y.Z`) và commit cùng lần tăng version. Khi tag được push, job
release ghép danh sách PR do GitHub sinh, nội dung file này và đoạn hướng dẫn
cài đặt (`.github/scripts/release-notes.sh`). Không có file thì release chỉ có
danh sách PR và đoạn cài đặt.

Agent làm việc trong repo phải theo skill `.claude/skills/areca-release/SKILL.md`.
