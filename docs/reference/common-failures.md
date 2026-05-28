# 常見失敗圖鑑

這份文件專門給第一次做 `driver-lab` 的人。

你不需要一開始理解所有 shell script。先知道「哪一類錯誤代表哪一類問題」就夠了。

## `scripts/check-kernel-env.sh` 常見輸出

### `Build tree: /lib/modules/<version>/build`

這不是錯誤。

它表示外掛 kernel module 建置時，會去對接哪一份 kernel build tree。

你可以先把它理解成：

- module build 需要一套跟目前 kernel 對得上的 headers / config / build metadata

如果這個路徑不存在，後面 `make` 幾乎一定會失敗。

### `OK: debugfs is mounted`

這不是功能成功，而是表示：

- 之後 `01-debugfs-logging` 那一關需要的檔案系統介面已經可用

### `Current taint value: 0`

這不是分數，也不是 performance 指標。

它只是表示目前 kernel 狀態還是乾淨的，還沒有被某些狀態標記。

## `make: *** /lib/modules/.../build: No such file or directory`

意思通常是：

- 這台 Linux host 沒裝對應 kernel headers / build tree

優先處理：

1. 確認 `uname -r`
2. 確認該版本 kernel 的 headers 套件是否已安裝
3. 確認 `/lib/modules/$(uname -r)/build` 是否存在

## `insmod: ERROR: could not insert module ...: Operation not permitted`

常見原因：

- 你沒有 root 權限
- Secure Boot / module signature policy 擋住 unsigned module

先檢查：

- 是否用 `sudo`
- [`../onboarding/linux-host-setup.md`](../onboarding/linux-host-setup.md) 裡的 module signing / Secure Boot 說明

## `modprobe: FATAL: Module ... not found`

這通常不是 code 問題。

多半是：

- 你只用 `make` 建出 `.ko`
- 但沒有把 module 安裝到系統 module path

在這個專案前期，通常直接用：

- `sudo insmod ./your_module.ko`

而不是先用 `modprobe`。

## `/sys/kernel/debug` 不存在或看不到預期檔案

常見原因：

- `debugfs` 沒掛載
- module 還沒載入成功
- 你找錯路徑

先檢查：

1. `mount | grep debugfs`
2. `sudo dmesg | tail`
3. lab README 指定的 debugfs 路徑

## `No such file or directory: /dev/driver_lab_char0`

意思通常是：

- char device module 沒有成功建立 device node
- 或 sysfs device 已建立，但 devtmpfs/udev 沒有把 `/dev` node 準備好

先檢查：

1. `insmod` 是否成功
2. `dmesg` 裡有沒有印 major/minor
3. `/sys/class/driver_lab_char/driver_lab_char0` 是否存在
4. `cat /sys/class/driver_lab_char/driver_lab_char0/dev` 是否能看到 `major:minor`
5. devtmpfs / udev 是否有正常建立或調整 `/dev` 節點

如果 `/sys/class/...` 不存在，先追 driver init path；如果 `/sys/class/...` 存在但 `/dev/...` 不存在，才往 devtmpfs / udev 層追。

## `lspci` 看不到 `1234:11e8`

這是 QEMU EDU bring-up 的最前面問題，不是 driver code 問題。

先檢查：

1. QEMU 啟動參數是否真的有 `-device edu`
2. guest 內是否已安裝 `pciutils`
3. 你是不是在正確的 guest 環境裡檢查

## 中斷一直進 handler，停不下來

在 `06-pci-edu-irq` 最常見的原因是：

- 你沒有正確寫 interrupt acknowledge register

先回去看：

- [`../guides/qemu-edu-first-pass.md`](../guides/qemu-edu-first-pass.md)
- QEMU EDU 官方文件對 `0x64` 的描述

## DMA 看起來有跑，但資料不對

先不要直接說「QEMU 有 bug」。

先檢查：

1. source / destination 位址方向
2. transfer count
3. DMA mask
4. buffer 是否超出 EDU 內建 buffer 範圍

## 如果你現在還是看不懂錯誤

不要一次貼整份 log。

先整理四件事：

1. 你在哪一關
2. 你執行了哪一條命令
3. 實際輸出是什麼
4. 你原本預期看到什麼

這樣才容易定位是環境問題、README 不清楚，還是 code 本身有錯。
