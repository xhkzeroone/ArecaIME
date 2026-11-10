# Rules and Behavioral Constraints

## Safety & Remote Operations
- **NEVER execute `git push` or remote operations autonomously**: Never run `git push`, `git push --force`, or any remote repository/server modifying/deleting commands unless the user explicitly requests it in a prompt.
- **Strictly Local Execution**: Keep all operations strictly local (`git commit`, `git status`, `cmake`, `ctest`). Remote push/sync is strictly reserved for the user.

## Writing & Documentation Style
- **Get straight to the core point (Đi thẳng vào trọng tâm)**: Write clean, concise, direct text and code comments without redundant fluff.
- **No Parentheses Noise**: Never add unnecessary filler or explanatory parentheticals `()` in documentation, code comments, or text responses.


## Pull Request
- Trước khi commit hoặc chuẩn bị PR, làm theo mục "Quy tắc bắt buộc cho Pull Request" trong `CONTRIBUTING.md` và skill `.claude/skills/areca-pr/SKILL.md`.
