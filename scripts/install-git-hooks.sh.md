# `install-git-hooks.sh` 詳解

## 結論

這支腳本把 repo 內建的 `.githooks/` 設成目前 clone 的 Git hooks 目錄。它不修改 source code，但會改你的 local Git config：

```sh
git config core.hooksPath .githooks
```

常用方式：

```sh
./scripts/install-git-hooks.sh
```

## 不確定處 / 查證範圍

這份講義只解釋本 repo 的 hook 安裝方式。不同 Git 版本對 hook 執行時機可能有細節差異；實際 hook 內容以 `.githooks/` 目錄中的檔案為準。

## 先理解這份檔案在 repo 的位置

路徑：

```text
scripts/install-git-hooks.sh
```

它的對象是「這個 clone 的 Git 設定」，不是遠端 repo，也不是所有 clone。你在另一台機器或另一個 worktree，仍要各自設定。

## 這份檔案要解決什麼問題

repo 希望每次 commit 前至少有基本保護：

- `pre-commit`：執行最小品質檢查。
- `commit-msg`：檢查 Conventional Commits 與中文主旨規則。

如果每個人都手動 copy hook 到 `.git/hooks/`，容易不一致。`core.hooksPath` 可以讓 repo 明確使用 `.githooks/`。

## 讀 source 的主線

原始碼片段：

```sh
REPO_ROOT=$(git rev-parse --show-toplevel)
HOOKS_PATH="$REPO_ROOT/.githooks"

if [ ! -d "$HOOKS_PATH" ]; then
    printf 'ERROR: missing hooks directory: %s\n' "$HOOKS_PATH" >&2
    exit 1
fi

chmod +x "$HOOKS_PATH"/pre-commit "$HOOKS_PATH"/commit-msg
git config core.hooksPath "$HOOKS_PATH"

printf 'Configured core.hooksPath=%s\n' "$HOOKS_PATH"
```

## 一、找 repo root

```sh
REPO_ROOT=$(git rev-parse --show-toplevel)
```

這行讓你不必站在 repo root 執行腳本。只要目前目錄在 repo 裡面，Git 就能找出最上層目錄。

如果你在非 Git repo 裡執行，這行會失敗，腳本也會因 `set -e` 停止。

## 二、確認 `.githooks` 存在

```sh
HOOKS_PATH="$REPO_ROOT/.githooks"

if [ ! -d "$HOOKS_PATH" ]; then
    printf 'ERROR: missing hooks directory: %s\n' "$HOOKS_PATH" >&2
    exit 1
fi
```

這段避免把 Git config 指到不存在的 hooks 目錄。若 `.githooks/` 不存在，後續 commit 可能以為有保護，實際上沒有。

## 三、確保 hook 可執行

```sh
chmod +x "$HOOKS_PATH"/pre-commit "$HOOKS_PATH"/commit-msg
```

Git hooks 必須是可執行檔。這行確保 repo 內建的兩個 hook 有 execute bit。

## 四、設定 `core.hooksPath`

```sh
git config core.hooksPath "$HOOKS_PATH"
```

這會寫入目前 repo 的 local Git config。效果是 Git 之後從 `.githooks/` 讀 hooks，而不是預設的 `.git/hooks/`。

## 這份檔案和其他檔案的對照

| 檔案 | 關係 |
|---|---|
| [`quality.sh`](quality.sh) | pre-commit hook 會執行的品質檢查入口。 |
| [`../.githooks/pre-commit`](../.githooks/pre-commit) | commit 前的最小品質保護。 |
| [`../.githooks/commit-msg`](../.githooks/commit-msg) | commit message 格式與中文主旨檢查。 |

## 常見卡點

### 這會不會改到 GitHub？

不會。`git config core.hooksPath` 預設改的是目前 clone 的 local config。

### 換 branch 後還需要重跑嗎？

通常不用，因為 local Git config 跟 branch 無關。但如果你換到另一個 clone、另一個 worktree，或 `.githooks/` 被移除，就要重新確認。

### commit 被 hook 擋下來怎麼辦？

先看 hook 輸出的錯誤。這個 repo 的規則通常是品質檢查失敗，或 commit message 不符合 `type(scope): 中文主旨`。

## 讀完後你應該能回答

1. 這支腳本會修改 source code 嗎？
2. `git rev-parse --show-toplevel` 解決什麼問題？
3. 為什麼要用 `.githooks/` 而不是手動 copy 到 `.git/hooks/`？
4. `core.hooksPath` 是 local 設定還是遠端設定？
