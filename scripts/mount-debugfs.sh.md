# `mount-debugfs.sh` 詳解

## 結論

這支腳本負責在 Linux 上把 debugfs 掛到 `/sys/kernel/debug`。它主要服務 Lab01 debugfs logging，也會幫助後續需要看 kernel debug surface 的情境。

常用方式：

```sh
./scripts/mount-debugfs.sh
```

如果 debugfs 已經掛載，腳本會直接印出 `debugfs is already mounted.` 並結束。

## 不確定處 / 查證範圍

這份講義只解釋本 repo 的掛載 helper。它不討論 distro-specific 的永久掛載設定、systemd mount unit 或 kernel config 差異。若你的 kernel 沒啟用 debugfs，這支腳本無法單靠 mount 解決。

## 先理解這份檔案在 repo 的位置

路徑：

```text
scripts/mount-debugfs.sh
```

它通常接在 [`check-kernel-env.sh`](check-kernel-env.sh) 的 warning 後面使用：

```text
WARN: debugfs 尚未掛載，可執行 ./scripts/mount-debugfs.sh
```

相關學習入口：

- [`../labs/01-debugfs-logging/README.md`](../labs/01-debugfs-logging/README.md)
- [`../docs/onboarding/kernel-filesystem-surfaces.md`](../docs/onboarding/kernel-filesystem-surfaces.md)

## 這份檔案要解決什麼問題

Lab01 的 driver 會建立 debugfs entries。如果 `/sys/kernel/debug` 沒有掛載 debugfs，你可能會遇到：

- module 載入成功，但看不到 `/sys/kernel/debug/driver_lab_debugfs/...`。
- 誤以為 `debugfs_create_dir()` 或 `debugfs_create_file()` 沒有執行。
- smoke test 的 debugfs path 檢查失敗。

這支腳本把「檢查是否已掛載」與「需要時用 sudo 掛載」包成一個固定入口。

## 讀 source 的主線

主線只有四段：

1. 確認作業系統是 Linux。
2. 從 `/proc/mounts` 判斷 debugfs 是否已掛載。
3. root 直接 `mount`。
4. 非 root 透過 `sudo mount`。

## 一、只允許 Linux

原始碼片段：

```sh
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi
```

### 這段在做什麼

如果不是 Linux，就直接失敗。

### 白話講

debugfs 是 Linux kernel 的 debug filesystem。macOS 沒有 `/proc/mounts` 這套 Linux mount table，也沒有 Linux debugfs，所以這裡不做 fallback。

## 二、避免重複掛載

原始碼片段：

```sh
if grep -qs ' /sys/kernel/debug ' /proc/mounts; then
    printf 'debugfs is already mounted.\n'
    exit 0
fi
```

### 這段在做什麼

它檢查 `/proc/mounts` 是否已有 `/sys/kernel/debug` 這個 mount point。

### 為什麼不是直接 `mount` 就好

重複 mount 會讓輸出與狀態更混亂，也可能因環境而報錯。先檢查再掛載可以讓腳本重跑時保持安全：

- 已掛載：成功結束。
- 未掛載：才嘗試 mount。

## 三、依權限選擇 `mount` 或 `sudo mount`

原始碼片段：

```sh
if [ "$(id -u)" -eq 0 ]; then
    mount -t debugfs none /sys/kernel/debug
else
    sudo mount -t debugfs none /sys/kernel/debug
fi
```

### 這段在做什麼

root 直接執行：

```sh
mount -t debugfs none /sys/kernel/debug
```

非 root 則使用：

```sh
sudo mount -t debugfs none /sys/kernel/debug
```

### 參數白話

| 片段 | 意義 |
|---|---|
| `mount` | 掛載 filesystem。 |
| `-t debugfs` | 指定 filesystem type 是 debugfs。 |
| `none` | debugfs 沒有一般 block device backing store，常用 `none`。 |
| `/sys/kernel/debug` | debugfs 的 mount point。 |

## 這份檔案和其他檔案的對照

| 檔案 | 關係 |
|---|---|
| [`check-kernel-env.sh`](check-kernel-env.sh) | 健檢發現 debugfs 未掛載時，提示使用本腳本。 |
| [`fs-surface-checks.sh`](fs-surface-checks.sh) | Lab01 smoke test 會用 helper 檢查 debugfs entry 是否存在。 |
| [`../labs/01-debugfs-logging/driver_lab_debugfs_logging.c`](../labs/01-debugfs-logging/driver_lab_debugfs_logging.c) | 會呼叫 debugfs API 建立 entries。 |

## 常見卡點

### `sudo: command not found`

某些極簡 container 或 VM 可能沒有 `sudo`。如果你不是 root，又沒有 `sudo`，這支腳本無法提升權限。請改用 root shell 或補齊 sudo。

### `permission denied`

代表目前使用者沒有 mount 權限，或 sudo policy 不允許。這不是 driver code 問題。

### 掛載後仍看不到 Lab01 路徑

先確認 module 是否真的載入，再看 `dmesg`。debugfs 掛載只是前提；driver 還是要成功呼叫 debugfs create API。

## 讀完後你應該能回答

1. 為什麼這支腳本在 macOS 不應該有 fallback？
2. 為什麼要先看 `/proc/mounts`？
3. `mount -t debugfs none /sys/kernel/debug` 每個參數代表什麼？
4. debugfs 掛載成功是否代表 Lab01 driver 一定正確？
