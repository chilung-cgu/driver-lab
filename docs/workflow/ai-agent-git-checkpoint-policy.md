# AI Agent + Git Checkpoint 規則範本

## 先講結論

可以，而且應該分成 3 層一起做，不要只靠一句「記得幫我 commit」：

1. `AGENTS.md`：定義 agent 的行為規則
2. `Codex hooks`：在回合過程中提醒或攔截
3. `Git hooks`：在 commit 當下做最後把關

單靠其中一層都不夠穩。

## 建議的 checkpoint 規則

對「從 0 建一個新 project」這種任務，我建議把 commit 節奏固定成：

1. 建好 repo 與目錄骨架，且有第一版 README
2. 第一個可執行或可驗證的最小閉環完成
3. 每一個獨立功能完成，且測試可跑
4. 文件或測試大幅補強後
5. 大重構前先 commit 一次，大重構後再 commit 一次

不要等到全部做完才一次 commit。

## 最實用的 `AGENTS.md` 規則

把這段放進新專案的 `AGENTS.md`，效果會比口頭提醒穩定很多：

```md
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
- Commit message 使用 Conventional Commits，且 `type(scope):` 後面的主旨預設使用繁體中文（台灣用語）。
```

## 為什麼還要搭配 Codex hooks

`AGENTS.md` 只能提供規則，不能保證 agent 在長任務中每次都主動停下來檢查。

Codex 官方文件目前支援：

- 在 `.codex/config.toml` 開啟 `codex_hooks`
- 用 `hooks.json` 或 inline `[hooks]`
- 事件包含 `PreToolUse`、`PostToolUse`、`SessionStart`、`Stop`

所以比較合理的做法是：

- `PostToolUse`：在大量 `apply_patch` 或 shell 編輯後，檢查 dirty worktree 是否過大
- `Stop`：在本回合結束前，提醒 agent 檢查是否該做 checkpoint commit

## 最小 `.codex/config.toml`

```toml
[features]
codex_hooks = true
```

## 最小 `.codex/hooks.json` 概念

```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "/usr/bin/python3 \"$(git rev-parse --show-toplevel)/.codex/hooks/git_checkpoint_guard.py\"",
            "timeout": 10,
            "statusMessage": "Checking git checkpoint state"
          }
        ]
      }
    ]
  }
}
```

這種 hook 最適合做「提醒」或「要求 agent 再檢查一次」，不要讓它直接幫你自動 commit。自動 commit 太容易把不該送進去的東西一起送掉。

## Git hook 應該做什麼

Git 官方文件支援把 hook 放在 `$GIT_DIR/hooks`，也支援用 `core.hooksPath` 指定統一目錄。

對新專案我建議至少做兩個：

### `pre-commit`

用途：

- 跑 formatting / lint / smoke test
- 阻止明顯不該進版控的東西
- 擋掉 `.DS_Store`、log、暫存檔、過大的 binary

### `commit-msg`

用途：

- 強制 commit message 符合 Conventional Commits
- 強制主旨包含繁體中文內容
- 避免出現 `update`、`fix stuff` 這種沒資訊量的訊息

## 這個 repo 已附的最小範本

`driver-lab` 目前已經附上：

- [`.githooks/pre-commit`](../../.githooks/pre-commit)
- [`.githooks/commit-msg`](../../.githooks/commit-msg)
- [`../../scripts/install-git-hooks.sh`](../../scripts/install-git-hooks.sh)

啟用方式：

```sh
./scripts/install-git-hooks.sh
```

這組範本目前做的事很保守：

- `pre-commit`：擋 `.DS_Store`，並執行 `scripts/quality.sh`
- `commit-msg`：要求 Conventional Commits，且主旨預設使用繁體中文

它的目的不是「全面自動化」，而是把最容易忘記的基本紀律先釘住。

## 你可以直接採用的工作流

### Agent 規則

- `AGENTS.md` 要求 checkpoint commit
- 完成一個功能就停下來檢查 `git status`

### 執行層

- `.codex/config.toml` 開啟 hooks
- `Stop` hook 提醒 dirty worktree 是否過大

### 最後把關

- `pre-commit` 跑最小檢查
- `commit-msg` 管 commit 訊息格式

## 我建議的 dirty worktree 門檻

這不是 Git 官方規定，是實務上對 agent 很有用的經驗值：

- 修改檔案超過 `8` 個
- 未提交 diff 超過約 `300` 行
- 已經跨了 `docs + code + tests + scripts` 多種變更

出現以上任一條件時，就應該停下來問：

- 這些改動是不是一個完整里程碑？
- 能不能先整理成一個可 review 的 commit？

## 什麼不要自動化

- 不要讓 hook 自動 `git add` 或自動 `git commit`
- 不要讓 agent 每改一個字就 commit
- 不要把彼此無關的變更硬湊在同一個 commit

你要的是「穩定 checkpoint」，不是「commit 噪音」。
