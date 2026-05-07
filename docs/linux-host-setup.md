# Linux Host 建置與風險檢查

## 適用對象

- 你在 `macOS` 上編輯
- 你有一台可 `sudo` 且可重開機的 `Linux host`
- 你要在 Linux 上 build / load / 測試 kernel module

## 先決條件

### 必要工具

- `gcc` 或 `clang`
- `make`
- `git`
- 對應目前 kernel 的 headers / build tree

最少要確認這個目錄存在：

```sh
ls -ld /lib/modules/"$(uname -r)"/build
```

## 第一次上機先跑

先不要急著執行。

先看：

- [`beginner-glossary.md`](beginner-glossary.md)
- [`check-kernel-env-explained.md`](check-kernel-env-explained.md)

再執行：

```sh
./scripts/check-kernel-env.sh
```

這個腳本會幫你檢查：

- 是否真的是 Linux
- `/lib/modules/$(uname -r)/build` 是否存在
- `make` / `gcc` / `git` 是否存在
- `debugfs` 是否已掛載
- Secure Boot 是否可能影響 unsigned module

如果你跑完後看到輸出卻看不懂，直接對照：

- [`check-kernel-env-explained.md`](check-kernel-env-explained.md)

## debugfs

許多 lab 會用到 `debugfs`。

檢查：

```sh
grep '/sys/kernel/debug' /proc/mounts
```

若尚未掛載：

```sh
./scripts/mount-debugfs.sh
```

## Secure Boot / Module Signature

如果系統啟用了強制模組簽章，unsigned module 可能無法載入。

常見檢查方式：

```sh
mokutil --sb-state
```

如果沒有 `mokutil`，至少先觀察：

- `dmesg`
- 發生載入失敗時的錯誤訊息

## 第 10 週之後額外建議安裝

為了做 kernel build、KUnit、QEMU 與較完整測試，通常還會需要：

- `bc`
- `bison`
- `flex`
- `pkg-config`
- `libelf-dev`
- `libssl-dev`
- `dwarves`
- `qemu-system-x86`

實際套件名稱依 distro 而異，請使用你的發行版套件管理器查詢。

## 建議目錄

```text
~/driver-lab/
~/linux-mainline/
~/linux-build/
```

## 常用操作速查

### build 外掛模組

```sh
make -C /lib/modules/"$(uname -r)"/build M="$PWD"
```

### 載入 / 卸載模組

```sh
sudo insmod ./your_module.ko
sudo rmmod your_module
```

### 看 kernel log

```sh
sudo dmesg | tail -n 50
journalctl -k -n 50
```

## 不建議的做法

- 不要在 macOS 直接嘗試 build Linux kernel module
- 不要把 Docker container 當主要 driver lab
- 不要在未確認 module signature 限制前，花大量時間 debug `insmod` 失敗
