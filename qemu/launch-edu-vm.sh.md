# `launch-edu-vm.sh` 詳解

## 結論

這支腳本是在 host 上啟動帶有 QEMU EDU PCI device 的 Linux guest。它不是在 host 上 build/load driver；真正的 Lab05-07 driver build、`insmod`、smoke test 仍然發生在 guest 裡。

最小用法：

```sh
QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 ./qemu/launch-edu-vm.sh
```

啟動後，guest 裡應該能用：

```sh
lspci -nn | grep 1234:11e8
```

看到 QEMU EDU device。

## 不確定處 / 查證範圍

這份講義根據本 repo 的 QEMU launch script 與 QEMU EDU lab 流程解釋。它不保證你的 guest image、QEMU build、host acceleration、network firewall 或 SSH server 設定都相同；那些必須依實際 host/guest 環境確認。

## 先理解這份檔案在 repo 的位置

路徑：

```text
qemu/launch-edu-vm.sh
```

相關文件：

- [`README.md`](README.md)：QEMU 目錄總覽。
- [`edu-bringup-checklist.md`](edu-bringup-checklist.md)：host 到 guest 的最小檢查表。
- [`../docs/guides/qemu-edu-first-pass.md`](../docs/guides/qemu-edu-first-pass.md)：第一次做 Lab05-07 的導讀。
- [`../docs/guides/linux-guest-05-to-07-walkthrough.md`](../docs/guides/linux-guest-05-to-07-walkthrough.md)：進 guest 後的完整 runbook。

## 這份檔案要解決什麼問題

Lab05-07 要學 PCI/MMIO/IRQ/DMA，需要一顆可預測的 PCI device。QEMU EDU 正好提供固定 vendor/device ID 與教學用 register interface。

這支腳本負責：

- 找到 `qemu-system-x86_64`。
- 要求使用者明確指定 guest image。
- 依 host OS 選擇 accelerator。
- 加上 `-device edu`。
- 設定 user-mode network 與 SSH port forwarding。
- 用 `-nographic` 讓 VM 在 terminal 裡跑。

## 它怎麼被執行

開頭設定：

```sh
#!/bin/sh
set -eu

QEMU_BIN=${QEMU_BIN:-qemu-system-x86_64}
QEMU_IMAGE=${QEMU_IMAGE:-}
QEMU_ACCEL=${QEMU_ACCEL:-}
QEMU_EXTRA_ARGS=${QEMU_EXTRA_ARGS:-}
SSH_PORT=${SSH_PORT:-2222}
MEMORY_MB=${MEMORY_MB:-2048}
SMP_CPUS=${SMP_CPUS:-2}
HOST_OS=$(uname -s)
```

### 參數表

| 變數 | 預設 | 作用 |
|---|---:|---|
| `QEMU_BIN` | `qemu-system-x86_64` | QEMU executable 名稱或路徑。 |
| `QEMU_IMAGE` | 空 | 必填，guest qcow2 image。 |
| `QEMU_ACCEL` | 空 | 空值時自動選；也可手動指定 `kvm`、`hvf`、`tcg`。 |
| `QEMU_EXTRA_ARGS` | 空 | 額外 QEMU 參數，最後附加。 |
| `SSH_PORT` | `2222` | host 轉到 guest port 22 的 TCP port。 |
| `MEMORY_MB` | `2048` | guest memory size。 |
| `SMP_CPUS` | `2` | guest CPU 數。 |

### 白話講

這支腳本把「固定需要的 QEMU 參數」寫死，把「每台機器不同的選項」留給環境變數覆寫。

## 讀 source 的主線

主線可以拆成五段：

1. 檢查 QEMU 是否支援某個 accelerator。
2. 依 host OS 選預設 accelerator。
3. 強制要求 `QEMU_IMAGE`。
4. 驗證 QEMU binary 與 accelerator。
5. `exec qemu-system-x86_64 ... -device edu ...`。

## 一、檢查 accelerator 是否支援

原始碼片段：

```sh
supports_accel() {
    accel=$1

    if "$QEMU_BIN" -accel help 2>/dev/null | grep -Eq "(^|[[:space:]])${accel}([[:space:]]|$)"; then
        return 0
    fi

    return 1
}
```

### 這段在做什麼

它執行：

```sh
qemu-system-x86_64 -accel help
```

再用 `grep` 檢查輸出中是否列出指定 accelerator。

### 為什麼不用硬猜

不同 host、QEMU build、硬體虛擬化狀態會影響 accelerator 是否可用。用 QEMU 自己的 help output 檢查，比只靠 OS 名稱安全。

## 二、依 host OS 選預設 accelerator

原始碼片段：

```sh
pick_default_accel() {
    case "$HOST_OS" in
        Linux)
            if supports_accel kvm; then
                printf 'kvm\n'
            else
                printf 'tcg\n'
            fi
            ;;
        Darwin)
            if supports_accel hvf; then
                printf 'hvf\n'
            else
                printf 'tcg\n'
            fi
            ;;
        *)
            printf 'tcg\n'
            ;;
    esac
}
```

### 這段在做什麼

它用 `uname -s` 的結果選擇預設 accelerator：

| Host OS | 優先 | fallback |
|---|---|---|
| Linux | `kvm` | `tcg` |
| macOS / Darwin | `hvf` | `tcg` |
| 其他 | `tcg` | 無 |

### 白話講

`kvm` 和 `hvf` 是 host hardware acceleration。`tcg` 是 QEMU 的 software translation fallback，通常比較慢，但相容性高。

對學 driver 來說，速度重要，但第一個目標是 VM 能穩定啟動且 guest 看得到 EDU device。

## 三、強制要求 `QEMU_IMAGE`

原始碼片段：

```sh
if [ -z "$QEMU_IMAGE" ]; then
    printf 'ERROR: 請先設定 QEMU_IMAGE 指向你的 guest image。\n' >&2
    printf '例如：QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 %s\n' "$0" >&2
    exit 1
fi
```

### 這段在做什麼

如果沒有設定 `QEMU_IMAGE`，腳本直接失敗並印出範例。

### 為什麼不給預設 image path

guest image 位置非常依個人環境而定。硬塞預設值很可能導致使用者以為腳本壞了。明確要求 `QEMU_IMAGE` 可以讓錯誤更早、更清楚。

## 四、確認 QEMU binary 與 accelerator

原始碼片段：

```sh
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    printf 'ERROR: 找不到 %s\n' "$QEMU_BIN" >&2
    exit 1
fi

if [ -z "$QEMU_ACCEL" ]; then
    QEMU_ACCEL=$(pick_default_accel)
elif ! supports_accel "$QEMU_ACCEL"; then
    printf 'ERROR: %s 不支援 accel=%s\n' "$QEMU_BIN" "$QEMU_ACCEL" >&2
    exit 1
fi
```

### 這段在做什麼

先確認 QEMU executable 存在。接著：

- 如果使用者沒指定 `QEMU_ACCEL`，就自動選。
- 如果使用者指定了，就確認 QEMU 支援該 accelerator。

### 常見用法

手動指定 software fallback：

```sh
QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 \
QEMU_ACCEL=tcg \
./qemu/launch-edu-vm.sh
```

指定不同 QEMU binary：

```sh
QEMU_BIN=/opt/homebrew/bin/qemu-system-x86_64 \
QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 \
./qemu/launch-edu-vm.sh
```

## 五、真正啟動 QEMU

原始碼片段：

```sh
# shellcheck disable=SC2086
exec "$QEMU_BIN" \
    -accel "$QEMU_ACCEL" \
    -m "$MEMORY_MB" \
    -smp "$SMP_CPUS" \
    -drive "file=$QEMU_IMAGE,if=virtio,format=qcow2" \
    -netdev "user,id=n1,hostfwd=tcp::${SSH_PORT}-:22" \
    -device virtio-net-pci,netdev=n1 \
    -device edu \
    -nographic \
    $QEMU_EXTRA_ARGS
```

### 這段在做什麼

| 參數 | 作用 |
|---|---|
| `-accel "$QEMU_ACCEL"` | 使用選定 accelerator。 |
| `-m "$MEMORY_MB"` | 設定 guest memory。 |
| `-smp "$SMP_CPUS"` | 設定 guest CPU 數。 |
| `-drive "file=...,if=virtio,format=qcow2"` | 掛入 qcow2 guest disk。 |
| `-netdev "user,...hostfwd=tcp::${SSH_PORT}-:22"` | host port 轉到 guest SSH port 22。 |
| `-device virtio-net-pci,netdev=n1` | 加入 virtio network device。 |
| `-device edu` | 加入 QEMU EDU PCI device。 |
| `-nographic` | 使用 terminal console，不開 GUI 視窗。 |
| `$QEMU_EXTRA_ARGS` | 讓使用者追加進階參數。 |

### 為什麼用 `exec`

`exec` 會用 QEMU process 取代 shell script process。好處是：

- signal 行為比較直接。
- script 不需要再等子行程結束。
- terminal 上看到的主要 process 就是 QEMU。

### 為什麼 `QEMU_EXTRA_ARGS` 沒有加引號

這一行前面有：

```sh
# shellcheck disable=SC2086
```

這是刻意允許使用者傳入多個額外 QEMU 參數，例如：

```sh
QEMU_EXTRA_ARGS="-monitor stdio -serial mon:stdio"
```

代價是：`QEMU_EXTRA_ARGS` 不適合放入需要複雜 quoting 的任意字串。它是 convenience escape hatch，不是完整參數 parser。

## Host 與 guest 的責任分界

| 位置 | 做什麼 |
|---|---|
| host | 執行 `launch-edu-vm.sh`，啟動 QEMU，提供 guest image 和 `-device edu`。 |
| guest | 安裝 build tools/kernel headers，build `.ko`，執行 Lab05-07 `test.sh`。 |

這點很重要：如果你在 macOS host 啟動 QEMU，仍然是在 Linux guest 裡 build/load Linux driver，不是在 macOS kernel 裡載入 `.ko`。

## 這份檔案和其他檔案的對照

| 檔案 | 關係 |
|---|---|
| [`edu-bringup-checklist.md`](edu-bringup-checklist.md) | 啟動前後的最小檢查清單。 |
| [`../labs/05-pci-edu-mmio/test.sh`](../labs/05-pci-edu-mmio/test.sh) | guest 內第一個 PCI EDU lab smoke test。 |
| [`../scripts/fs-surface-checks.sh`](../scripts/fs-surface-checks.sh) | guest 內檢查 PCI device / driver bind surface。 |

## 常見卡點

### `ERROR: 請先設定 QEMU_IMAGE`

設定 guest image：

```sh
QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 ./qemu/launch-edu-vm.sh
```

### `找不到 qemu-system-x86_64`

代表 QEMU binary 不在 `PATH`。安裝 QEMU，或用 `QEMU_BIN=/path/to/qemu-system-x86_64` 指定。

### guest 內看不到 `1234:11e8`

先確認腳本真的有跑到 `-device edu`，再在 guest 裡用：

```sh
lspci -nn | grep 1234:11e8
```

若仍看不到，優先檢查 QEMU 版本、device 是否支援、guest 是否完整開機，而不是先改 driver code。

### SSH 連不上 guest

這支腳本只設定 host port forwarding：

```text
host tcp:${SSH_PORT} -> guest tcp:22
```

guest 裡仍然需要 SSH server 正常啟動、防火牆允許、使用者帳號可登入。

## 讀完後你應該能回答

1. 為什麼 `QEMU_IMAGE` 必須由使用者明確設定？
2. `kvm`、`hvf`、`tcg` 在這支腳本裡的角色差異是什麼？
3. `-device edu` 對 Lab05-07 有什麼意義？
4. 為什麼 host 啟動 QEMU 不等於 host 可以 load Linux kernel module？
5. `QEMU_EXTRA_ARGS` 為什麼刻意不加引號？
