# AGENTS.md

## 語言規則

- 預設以繁體中文（台灣用語）撰寫自然語言說明、文件、工作報告與 commit message。
- 程式碼、指令、檔名、路徑、API 名稱、kernel helper 名稱可維持原文。

## Git Checkpoints

- 這個專案採用小步提交，不允許把整個專案累積到最後才一次 commit。
- Agent 在以下時機必須主動評估是否該建立 checkpoint commit：
  1. repo 骨架與 README 已可 review
  2. 第一個可執行或可驗證的最小版本完成
  3. 一個獨立功能完成，且對應測試已補齊
  4. 準備做跨檔案重構前
- 若工作區已累積多個彼此無關的變更，必須先整理成邏輯分組再提交。
- 不可使用 `git add .`；必須按功能或目的分批 stage。
- 每次 commit 前必須：
  1. 檢查 `git status --short`
  2. 執行本專案要求的最小測試或品質檢查
  3. 自行 review diff，確認沒有把暫存 debug 垃圾一起提交

## Commit Message 規則

- Commit message 保留 Conventional Commits 前綴：
  - `feat(scope): ...`
  - `fix(scope): ...`
  - `docs(scope): ...`
- `type(scope):` 後面的主旨預設使用繁體中文（台灣用語）。
- 若需要 body，body 也預設使用繁體中文；測試命令、錯誤訊息、檔名與 API 名稱可保留原文。
- 避免只寫 `update`、`fix bug`、`misc changes` 這類低資訊量訊息。

## Hook 規則

- repo 內建的 `commit-msg` hook 會檢查：
  - 是否符合 Conventional Commits
  - 主旨是否包含至少一個中文字符
- repo 內建的 `pre-commit` hook 會執行最小品質檢查。
