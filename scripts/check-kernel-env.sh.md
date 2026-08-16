# `check-kernel-env.sh` 詳解

## 結論

這支腳本是 driver lab 的「Linux host/guest 起手健檢」。它不驗證任何 lab driver 的正確性，而是先確認你目前的 Linux 環境有沒有足夠條件建置與載入 out-of-tree kernel module。

你可以把它放在每次開始新 Linux 環境時的第一步：

```sh
./scripts/check-kernel-env.sh
```

如果這支腳本在 macOS 跑，它會直接失敗。這是刻意設計，因為 macOS 可以編輯 repo、啟動 QEMU，但不能直接 build/load Linux kernel module。

## 不確定處 / 查證範圍

這份講義只根據本 repo 的腳本、既有 onboarding 文件與 Linux 常見 module build layout 解釋。它不嘗試判斷你的 distro 套件名稱、Secure Boot policy 或雲端 VM kernel headers 安裝方式；那些會依 Ubuntu、Oracle Linux、Fedora、Debian 或 cloud image 而不同。

## 先理解這份檔案在 repo 的位置

路徑：

```text
scripts/check-kernel-env.sh
```

它被放在 repo-level `scripts/`，代表它不是某一章 lab 的專屬測試，而是所有需要 Linux kernel build tree 的章節都會用到的前置工具。

相關文件：

- [`../docs/onboarding/linux-environment.md`](../docs/onboarding/linux-environment.md)：用範例輸出教你怎麼讀結果。
- [`mount-debugfs.sh`](mount-debugfs.sh)：當這支腳本提醒 debugfs 尚未掛載時，下一步通常會用它。
- [`quality.sh`](quality.sh)：repo 的語法、Markdown link 與 checkpatch 檢查入口。

## 這份檔案要解決什麼問題

初學 kernel driver 時，很多錯誤其實不是 driver code 寫錯，而是環境沒有準備好。例如：

- 不在 Linux 上執行。
- `/lib/modules/$(uname -r)/build` 不存在。
- 沒有 `make` 或 compiler。
- debugfs 沒有掛載，所以 Lab01 看不到 `/sys/kernel/debug/...`。
- Secure Boot / module signing 擋住未簽章 `.ko`。
- kernel 已經 tainted，後續 debug 要多一層背景判讀。

這支腳本的價值是把這些「起手障礙」集中成一個可重跑的檢查。

## 它怎麼被執行

這是 POSIX shell script：

```sh
#!/bin/sh
set -eu
```

`set -eu` 的意思是：

- `-e`：未處理的 command failure 會讓腳本停止。
- `-u`：使用未設定變數會直接報錯。

這對健檢腳本很重要，因為它不應該在關鍵檢查失敗後繼續印出看似成功的結果。

## 讀 source 的主線

主線可以拆成六段：

1. 定義輸出 helper：`info`、`warn`、`fail`。
2. 確認現在是 Linux。
3. 算出目前 kernel 對應的 build tree。
4. 檢查基本工具：`make`、`gcc`、`git`。
5. 檢查 debugfs 與 Secure Boot 線索。
6. 讀 kernel taint 狀態。

## 一、輸出 helper：成功、警告、失敗分流

原始碼片段：

```sh
info() {
    printf '%s\n' "$*"
}

warn() {
    printf 'WARN: %s\n' "$*" >&2
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}
```

### 這段在做什麼

`info` 印到 stdout，`warn` 和 `fail` 印到 stderr。`fail` 會用 `exit 1` 結束腳本。

### 白話講

這支腳本把結果分成三種：

- `info`：這是正常資訊。
- `WARN`：這可能會影響後續 lab，但不一定是立即阻斷。
- `ERROR`：這個條件不滿足，繼續做沒有意義。

這比所有訊息都用 `echo` 更清楚，因為你可以一眼分辨「必須修」和「之後注意」。

## 二、只允許在 Linux 執行

原始碼片段：

```sh
if [ "$(uname -s)" != "Linux" ]; then
    fail "這個腳本必須在 Linux 主機上執行。"
fi
```

### 這段在做什麼

它用 `uname -s` 判斷作業系統。如果不是 `Linux`，就直接停止。

### 白話講

driver lab 的 source 可以在 macOS 編輯，甚至 QEMU 可以在 macOS host 上啟動 Linux guest；但 `.ko` 的 build/load/test 必須在 Linux kernel 環境中發生。

所以這裡不是「macOS 不支援 repo」，而是「這個健檢項目只對 Linux 有意義」。

## 三、找目前 kernel 的 build tree

原始碼片段：

```sh
KDIR="/lib/modules/$(uname -r)/build"

info "Kernel version (uname -r): $(uname -r)"
info "Kernel build tree (KDIR): $KDIR"

[ -d "$KDIR" ] || fail "找不到 kernel build tree: $KDIR"
```

### 這段在做什麼

它把 `KDIR` 設成目前正在跑的 kernel 對應 build tree：

```text
/lib/modules/<目前 kernel 版本>/build
```

例如：

```text
/lib/modules/6.17.0-1010-oracle/build
```

### 為什麼重要

本 repo 的 lab Makefile 會走 Linux kbuild 的外部 module 模式，典型形狀是：

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD" modules
```

這裡的重點是「對著目前正在跑的 kernel build」，不是隨便找一份 headers。kernel module 與 running kernel 不匹配時，可能 build 失敗，也可能載入時因 vermagic 或 symbol mismatch 被拒絕。

### 常見誤解

| 誤解 | 正確理解 |
|---|---|
| 有 `gcc` 就能直接 `gcc driver.c` 編出 `.ko` | kernel module 要透過 kbuild，不能當一般 userspace C 程式編。 |
| 任意 kernel headers 都可以 | 應該對應目前 `uname -r` 的 kernel。 |
| macOS 上有 cross compiler 就能直接完成 lab | 編輯與部分 build 準備可以在 macOS，但 load/test Linux module 必須在 Linux kernel 上。 |

## 四、檢查基本工具

原始碼片段：

```sh
for tool in make gcc git; do
    if command -v "$tool" >/dev/null 2>&1; then
        info "OK: found $tool at $(command -v "$tool")"
    else
        warn "missing tool: $tool"
    fi
done
```

### 這段在做什麼

它用 `command -v` 檢查 `make`、`gcc`、`git` 是否在 `PATH` 中。

### 為什麼缺工具只是 WARN

腳本把缺工具印成 `WARN`，不是 `ERROR`。原因是：

- `make` / `gcc` 對 build module 很重要，但某些環境可能使用不同 compiler wrapper。
- `git` 對版本控制重要，但不是 kernel module build 的硬性條件。

不過以這個 repo 的學習流程來說，如果看到這些 warning，最好先補齊工具再繼續。

## 五、檢查 debugfs

原始碼片段：

```sh
if grep -qs ' /sys/kernel/debug ' /proc/mounts; then
    info "OK: debugfs is mounted"
else
    warn "debugfs 尚未掛載，可執行 ./scripts/mount-debugfs.sh"
fi
```

### 這段在做什麼

它從 `/proc/mounts` 檢查 `/sys/kernel/debug` 是否已經是 mount point。

### 對應到哪個 lab

Lab01 會建立 debugfs entries。若 debugfs 沒掛載，module 可能已經載入，但你在 `/sys/kernel/debug/...` 會看不到預期路徑。

這類問題很容易讓初學者誤判成 driver 沒有執行。其實可能只是 filesystem surface 還沒準備好。

### 下一步

如果看到 warning，通常下一步是：

```sh
./scripts/mount-debugfs.sh
```

## 六、檢查 Secure Boot 線索

原始碼片段：

```sh
if command -v mokutil >/dev/null 2>&1; then
    info "Secure Boot state:"
    mokutil --sb-state || true
else
    warn "mokutil 不存在，若 module 載入失敗，請額外檢查 Secure Boot / module signature 設定。"
fi
```

### 這段在做什麼

如果系統有 `mokutil`，腳本會印出 Secure Boot 狀態。若 `mokutil --sb-state` 自己失敗，後面加了 `|| true`，避免整支健檢腳本因此中止。

### 白話講

Secure Boot 不是每台 lab 機器都會遇到，但一旦遇到，症狀通常是 `.ko` build 完了卻載不進 kernel。這段不是要教完整 Secure Boot，而是提醒你：如果 `insmod` 或 `modprobe` 被拒絕，module signing 是要檢查的方向。

### 為什麼 `|| true` 合理

這支腳本的主要任務是環境概覽。`mokutil` 無法讀狀態時，不代表 kernel build tree 不存在，也不代表 `make` 不可用，所以它不應該阻斷整個檢查。

## 七、讀 kernel taint

原始碼片段：

```sh
if [ -r /proc/sys/kernel/tainted ]; then
    taint_value=$(cat /proc/sys/kernel/tainted)
    info "Current taint value: $taint_value"
    if [ "$taint_value" = "0" ]; then
        info "Taint summary: kernel is currently clean/untainted"
    else
        warn "kernel taint is non-zero; see docs/onboarding/linux-environment.md before debugging strange failures."
    fi
fi
```

### 這段在做什麼

如果 `/proc/sys/kernel/tainted` 可讀，就讀出目前 kernel taint 數值。

### 白話講

`0` 可以先理解成「目前 kernel 沒有被標記成 tainted」。非 `0` 代表 kernel 曾遇到某些會影響 debug 信任度的事件，例如載入特定外部模組或發生某些 kernel warning/oops 類狀況。

這不是說非 `0` 就不能學 driver，而是提醒你：如果後面遇到奇怪行為，要記得 kernel 目前不是乾淨背景。

## 這份檔案和其他檔案的對照

| 檔案 | 關係 |
|---|---|
| [`mount-debugfs.sh`](mount-debugfs.sh) | `check-kernel-env.sh` 發現 debugfs 未掛載時的修復工具。 |
| [`quality.sh`](quality.sh) | repo 的品質檢查入口；也會依 Linux kernel tree 決定是否跑 checkpatch。 |
| [`../docs/onboarding/linux-environment.md`](../docs/onboarding/linux-environment.md) | 用範例輸出說明每一行健檢結果。 |
| [`../labs/00-hello-module/README.md`](../labs/00-hello-module/README.md) | 通過環境健檢後，第一個真正開始 build/load module 的 lab。 |

## 常見卡點

### `找不到 kernel build tree`

代表 `/lib/modules/$(uname -r)/build` 不存在。通常要安裝目前 kernel 對應的 headers/build package。套件名稱依 distro 而異，不能從 repo source 直接推斷。

### `debugfs 尚未掛載`

先跑：

```sh
./scripts/mount-debugfs.sh
```

如果沒有權限，它會透過 `sudo mount`。

### `mokutil 不存在`

這不一定是錯。有些 VM 或 server image 沒有安裝 `mokutil`，也可能不支援 Secure Boot。只有當 module load 被拒絕時，才需要把 Secure Boot / module signature 當成重點追查。

## 讀完後你應該能回答

1. 為什麼這支腳本在 macOS 上會直接失敗？
2. `KDIR=/lib/modules/$(uname -r)/build` 對 out-of-tree module build 有什麼意義？
3. debugfs 沒掛載時，Lab01 可能出現什麼誤判？
4. 為什麼 `mokutil --sb-state` 後面可以接 `|| true`？
5. kernel taint 非 `0` 時，對後續 debug 代表什麼？
