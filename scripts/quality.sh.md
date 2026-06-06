# `quality.sh` 詳解

## 結論

這支腳本是 repo 的最小品質閘門。它會做四類檢查：

1. 所有 `.sh` 的 POSIX shell 語法檢查。
2. 如果有 `shellcheck`，就跑 shell 靜態檢查。
3. 用內嵌 Python 檢查 Markdown 相對連結與 anchor。
4. 如果找得到 kernel `checkpatch.pl`，就檢查 `.c` / `.h` 風格。

常用方式：

```sh
./scripts/quality.sh .
```

## 不確定處 / 查證範圍

這份講義只解釋本 repo 的品質檢查流程。它不把 `shellcheck` 或 Linux kernel `checkpatch.pl` 的所有規則展開；那些工具的規則會隨版本變動，實際結果以本機工具輸出為準。

## 先理解這份檔案在 repo 的位置

路徑：

```text
scripts/quality.sh
```

它不是某個 lab 的功能測試，而是跨 repo 的「提交前基本檢查」。lab 行為仍由各 lab `test.sh` 驗證。

## 這份檔案要解決什麼問題

這個 repo 同時有 kernel C、userspace C、shell scripts、Markdown docs。如果每一類都手動檢查，容易漏掉：

- shell script 語法錯誤。
- Markdown 相對連結指錯位置。
- Markdown anchor 不存在。
- kernel C style 問題。
- 產物目錄裡的 `.mod.c` 被誤拿去 checkpatch。

`quality.sh` 把這些檢查集中成一個可重跑入口。

## 它怎麼被執行

原始碼片段：

```sh
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TARGET_DIR=${1:-$ROOT_DIR}
KERNEL_TREE=${KERNEL_TREE:-}
```

### 這段在做什麼

- `ROOT_DIR`：算出 repo root。
- `TARGET_DIR`：預設檢查整個 repo，也可以傳入特定目錄。
- `KERNEL_TREE`：可手動指定 kernel source/build tree，用來找 `scripts/checkpatch.pl`。

例如只檢查 `scripts/`：

```sh
./scripts/quality.sh scripts
```

## 讀 source 的主線

主線很直：

```text
run_shell_syntax_checks
run_shellcheck
run_markdown_link_checks
run_checkpatch
```

也就是先檢查 shell，再檢查 Markdown，最後視環境檢查 C/H。

## 一、shell 語法檢查

原始碼片段：

```sh
run_shell_syntax_checks() {
    find "$TARGET_DIR" -type f -name '*.sh' | while IFS= read -r file; do
        printf 'sh -n %s\n' "$file"
        sh -n "$file"
    done
}
```

### 這段在做什麼

它找出目標目錄底下所有 `.sh`，逐一執行：

```sh
sh -n file.sh
```

`sh -n` 只做語法解析，不執行腳本內容。

### 為什麼這很適合本 repo

很多 lab test script 會執行 `insmod`、`rmmod`、`mount`、QEMU 等操作，不適合在品質檢查階段全部真的跑一次。`sh -n` 可以先抓出括號、quote、`if/fi`、function syntax 這類低階錯誤，而且不會改動系統狀態。

## 二、可選的 `shellcheck`

原始碼片段：

```sh
run_shellcheck() {
    if ! command -v shellcheck >/dev/null 2>&1; then
        warn "shellcheck 不存在，略過 shell 靜態檢查。"
        return 0
    fi

    find "$TARGET_DIR" -type f -name '*.sh' | while IFS= read -r file; do
        printf 'shellcheck %s\n' "$file"
        shellcheck "$file"
    done
}
```

### 這段在做什麼

如果系統有 `shellcheck`，就對所有 `.sh` 跑靜態分析；沒有就印 warning 並繼續。

### 為什麼缺 `shellcheck` 不讓品質檢查失敗

這個 repo 需要能在乾淨 Linux VM、雲端 server、macOS host 上工作。`shellcheck` 很有用，但不是最小必要條件。腳本保留「有就跑、沒有就跳過」的設計，讓基本檢查不被額外工具 availability 卡死。

## 三、Markdown 連結檢查

原始碼片段：

```sh
run_markdown_link_checks() {
    if ! command -v python3 >/dev/null 2>&1; then
        warn "python3 不存在，略過 Markdown 連結檢查。"
        return 0
    fi

    python3 - "$TARGET_DIR" "$ROOT_DIR" <<'PY'
...
PY
}
```

### 這段在做什麼

它用內嵌 Python 掃描 Markdown link：

```text
[文字] + (相對路徑.md#anchor)
```

並檢查：

- local link 不能是絕對路徑。
- link target 必須存在。
- link 不可以逃出 repo root。
- 如果連到 Markdown anchor，該 anchor 要真的存在。

### 為什麼 companion docs 很需要這段

companion docs 的核心體驗是「讀 source 時能沿著相對連結跳到旁邊講義」。如果 link 指錯，學習流程會立刻斷掉。

這支檢查可以抓出：

- `../../` 多一層或少一層。
- README section 改名後 anchor 失效。
- 不小心寫入本機絕對路徑。

## 四、anchor 產生規則

Python 裡的核心片段：

```python
def slugify(text: str) -> str:
    text = text.strip().lower().replace('`', '')
    cleaned = []
    for ch in text:
        if ch.isalnum() or ch in {' ', '-', '_'}:
            cleaned.append(ch)
    slug = ''.join(cleaned).replace(' ', '-')
    while '--' in slug:
        slug = slug.replace('--', '-')
    return slug.strip('-')
```

### 這段在做什麼

它把 Markdown heading 轉成簡化 anchor。這不是完整模擬所有 GitHub/MkDocs anchor 規則，而是本 repo 目前需要的最小檢查。

### 限制

如果未來文件大量使用複雜標點、HTML anchor、或非標準 renderer 規則，這段可能需要擴充。現在它足夠檢查 repo 內常見的中文標題、英文 code identifier 與簡單符號。

## 五、尋找 `checkpatch.pl`

原始碼片段：

```sh
locate_checkpatch() {
    if [ -n "$KERNEL_TREE" ] && [ -x "$KERNEL_TREE/scripts/checkpatch.pl" ]; then
        printf '%s\n' "$KERNEL_TREE/scripts/checkpatch.pl"
        return 0
    fi

    if [ "$(uname -s)" = "Linux" ] && [ -x "/lib/modules/$(uname -r)/build/scripts/checkpatch.pl" ]; then
        printf '%s\n' "/lib/modules/$(uname -r)/build/scripts/checkpatch.pl"
        return 0
    fi

    return 1
}
```

### 這段在做什麼

它用兩種方式找 Linux kernel 的 `checkpatch.pl`：

1. 使用者用 `KERNEL_TREE` 明確指定。
2. 在 Linux 上從 `/lib/modules/$(uname -r)/build/scripts/checkpatch.pl` 找。

### 為什麼 macOS 通常會跳過

macOS 沒有 running Linux kernel 的 `/lib/modules/$(uname -r)/build`。除非你手動指定 `KERNEL_TREE`，否則 checkpatch 會跳過。

這符合 repo 的工作模式：macOS 可做文件與語法檢查，Linux host/guest 做 kernel build/load 與 checkpatch。

## 六、執行 checkpatch

原始碼片段：

```sh
find "$TARGET_DIR" -type f \( -name '*.c' -o -name '*.h' \) \
    ! -name '*.mod.c' \
    ! -name '.module-common.c' \
    ! -path '*/.tmp_versions/*' | while IFS= read -r file; do
    printf 'checkpatch %s\n' "$file"
    perl "$checkpatch" --no-tree -f "$file"
done
```

### 這段在做什麼

它找出 `.c` / `.h`，排除 kbuild 產生物與共同產物，再用 kernel `checkpatch.pl` 檢查。

### 為什麼要排除 `.mod.c` 和 `.tmp_versions`

外部 kernel module build 後會產生中間檔。這些不是教學 source，不應該被當成要維護的 source style。否則你會把時間花在修 build artifact，而不是修 repo code。

## 這份檔案和其他檔案的對照

| 檔案 | 關係 |
|---|---|
| [`install-git-hooks.sh`](install-git-hooks.sh) | 設定 repo hook 後，commit 前會跑最小保護機制。 |
| [`check-kernel-env.sh`](check-kernel-env.sh) | 檢查 Linux kernel build tree；影響 checkpatch 與 module build 可用性。 |
| [`../docs/reference/companion-docs-index.md`](../docs/reference/companion-docs-index.md) | Markdown link check 會驗這類索引連結是否正確。 |

## 常見卡點

### `WARN: shellcheck 不存在`

不是失敗。代表這輪只跑 `sh -n`，沒有 shellcheck 靜態分析。

### `WARN: 找不到 checkpatch.pl`

不是失敗。代表目前環境找不到 kernel checkpatch。若你在 Linux guest，先確認 kernel headers/build tree 是否存在；也可以用 `KERNEL_TREE=/path/to/linux` 指定。

### Markdown anchor 檢查失敗

先打開錯誤訊息指出的來源檔與目標檔。常見原因是標題被改名，或 link 裡的 `#anchor` 沒跟著更新。

## 讀完後你應該能回答

1. `quality.sh .` 和 lab `test.sh` 的差別是什麼？
2. 為什麼 `sh -n` 適合當 shell script 的第一層檢查？
3. `shellcheck` 和 `checkpatch.pl` 為什麼都是可選檢查？
4. Markdown link check 為什麼禁止 repo 內文件使用絕對 local path？
5. 為什麼 checkpatch 要排除 `.mod.c` 和 `.tmp_versions`？
