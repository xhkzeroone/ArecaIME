#!/usr/bin/env bash
# Ghép release note cho một tag: danh sách PR do GitHub tự sinh, phần viết tay
# trong docs/release-notes/<tag>.md (nếu có), rồi đoạn cài đặt cố định.
# Dùng: release-notes.sh <tag> <owner/repo>  (cần GH_TOKEN cho gh api)
set -euo pipefail

tag="$1"
repo="$2"
root="$(cd "$(dirname "$0")/../.." && pwd)"
handwritten="$root/docs/release-notes/$tag.md"

# API tự tìm tag trước đó để lấy PR và link Full Changelog. Lỗi API thì vẫn
# ra release, chỉ thiếu phần này.
if generated="$(gh api "repos/$repo/releases/generate-notes" \
  -f tag_name="$tag" --jq .body 2>/dev/null)" && [ -n "$generated" ]; then
  printf '%s\n\n' "$generated"
fi

if [ -f "$handwritten" ]; then
  cat "$handwritten"
  printf '\n'
else
  printf '## Areca %s\n\n' "$tag"
fi

cat <<FOOTER
### Cài đặt hoặc cập nhật
\`\`\`
curl -fsSL https://raw.githubusercontent.com/$repo/main/install.sh | bash
\`\`\`

Installer sẽ tự nhận diện hệ điều hành, chọn gói phù hợp từ release mới nhất và hướng dẫn cấp quyền uinput.

Xin cảm ơn anh/chị/em đã báo lỗi, thử nghiệm trên nhiều môi trường và đóng góp ý tưởng giúp Areca ngày càng ổn định hơn.
FOOTER
