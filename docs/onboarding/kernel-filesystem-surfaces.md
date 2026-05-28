# Kernel filesystem 入口導讀

這份文件只解決一個問題：

> driver code 呼叫某些 kernel API 後，為什麼 Linux 裡會突然出現 `/dev/...`、`/sys/...`、`/proc/...` 或 `/sys/kernel/debug/...` 這些路徑？

如果你正在讀 `02-char-device`，這份尤其重要。第一輪請先把「路徑用途」分清楚，不要急著追完整 kobject、udev 或 VFS 內部。

## 先分清楚四種入口

| 路徑 | 第一輪理解 | 本 repo 主要在哪裡出現 |
|---|---|---|
| `/dev/...` | userspace 對 device 做 `open/read/write/ioctl/poll/mmap` 的操作入口 | `02-04` |
| `/sys/...` | kernel device model、bus、class、device 的觀測入口 | `02-07` |
| `/proc/...` | kernel runtime 狀態或控制入口 | dynamic debug、`/proc/devices`、`/proc/interrupts` |
| `/sys/kernel/debug/...` | debugfs，給 developer debug 與觀測，不是穩定產品 ABI | `01` |

白話說：

- `/dev/driver_lab_char0` 是你真正 `read()` / `write()` 的櫃台窗口。
- `/sys/class/driver_lab_char/...` 是 kernel device model 的身分資料，不是主要資料通道。
- `/proc/devices`、`/proc/interrupts` 是查 kernel 目前狀態的報表。
- `/sys/kernel/debug/...` 是 debug 用觀測窗，適合教學和 bring-up，不適合當產品 ABI。

## `02-char-device` 的路徑是怎麼出現的

在 `labs/02-char-device/driver_lab_char.c` 裡，init path 可以拆成兩條線：一條讓 VFS 知道 callback，一條讓使用者看得到 device。

```text
alloc_chrdev_region()
    取得 major/minor
          ↓
cdev_init() + cdev_add()
    讓 VFS 能用 major/minor 找到 file_operations
          ↓
class_create("driver_lab_char")
    建立 class，通常可在 /sys/class/driver_lab_char 看到
          ↓
device_create(..., devt, ..., "driver_lab_char0")
    建立 device object 與 sysfs class entry
          ↓
devtmpfs / udev
    通常讓 /dev/driver_lab_char0 出現
```

第一輪請精準記住：

| API / 機制 | 它造成什麼效果 |
|---|---|
| `alloc_chrdev_region(&dl_char_devt, 0, 1, "driver_lab_char")` | 分配一組 major/minor；可用 `/proc/devices` 輔助確認名字。 |
| `cdev_init(&dl_char_cdev, &dl_char_fops)` | 把 char device object 接到 callback table。 |
| `cdev_add(&dl_char_cdev, dl_char_devt, 1)` | 讓 kernel 知道這個 major/minor 對應到這個 cdev。 |
| `class_create("driver_lab_char")` | 建立 device class；通常會看到 `/sys/class/driver_lab_char`。 |
| `device_create(..., dl_char_devt, ..., "driver_lab_char0")` | 建立 device object；通常會看到 `/sys/class/driver_lab_char/driver_lab_char0`。 |
| devtmpfs / udev | 依 kernel device 資訊建立或調整 `/dev/driver_lab_char0`。 |

`/sys/class/driver_lab_char/driver_lab_char0` 在很多系統上會是 symlink，指向 `/sys/devices/virtual/driver_lab_char/driver_lab_char0`。這是正常的：`/sys/class` 是依功能分類的視角，`/sys/devices` 是 device tree 的視角。

## 不要把 `/sys` 和 `/dev` 混在一起

| 問題 | 簡短答案 |
|---|---|
| 我應該對哪個路徑做 `read/write`？ | 對 `/dev/driver_lab_char0`。 |
| `/sys/class/...` 是拿來做什麼？ | 看 kernel device model 裡有沒有註冊出 class/device。 |
| `/sys/class/.../dev` 裡的數字是什麼？ | 通常是 `major:minor`，可和 `/dev` node 對照。 |
| `/dev/driver_lab_char0` 一定是 udev 建的嗎？ | 不一定。現代 Linux 常由 devtmpfs 先建立，udev 再調整權限、owner 或 symlink。 |
| `cdev_add()` 會直接建立 `/dev` 檔案嗎？ | 不會。它讓 VFS 能用 major/minor 找到 driver callback。 |

所以你讀 `02` 時，可以這樣驗：

```sh
ls -l /dev/driver_lab_char0
ls -l /sys/class/driver_lab_char/driver_lab_char0
cat /sys/class/driver_lab_char/driver_lab_char0/dev
grep driver_lab_char /proc/devices
```

這些檢查也已放進對應 lab 的 `test.sh`。也就是說，smoke test 不只驗 driver 行為，也會順便驗證教學中說過的 filesystem surface 是否真的出現。

如果 `/dev/driver_lab_char0` 沒出現，第一個還是先看：

```sh
sudo dmesg | tail -n 50
```

接著再查 `/sys/class/driver_lab_char/` 是否存在。若 sysfs device 有出現但 `/dev` node 沒出現，才往 devtmpfs / udev 層追。

## 各 lab 會看到哪些 filesystem 入口

| Lab | 你會看到的路徑 | 第一輪用途 |
|---|---|---|
| `00` | 沒有新 `/dev` 或 debugfs 路徑 | 只練 `.ko` load/unload 與 `dmesg`。 |
| `01` | `/sys/kernel/debug/driver_lab_debugfs/*`、`/proc/dynamic_debug/control` | debugfs 狀態觀測與 dynamic debug 控制。 |
| `02` | `/dev/driver_lab_char0`、`/sys/class/driver_lab_char/driver_lab_char0`、`/proc/devices` | char device 的 userspace 入口、device model 觀測、major/minor 對照。 |
| `03` | `/dev/driver_lab_ctl0`、`/sys/class/driver_lab_ctl/driver_lab_ctl0` | 同一個 device node 擴成 `read/write/ioctl/poll/mmap`。 |
| `04` | `/dev/driver_lab_race0`、`/sys/class/driver_lab_race/driver_lab_race0` | 用 CLI 對同一個 driver state 施壓。 |
| `05` | `/sys/bus/pci/devices/...`、`/sys/bus/pci/drivers/driver_lab_edu_mmio`、`lspci` | PCI device 與 driver bind 狀態。 |
| `06` | PCI sysfs 路徑、`/proc/interrupts` | IRQ vector 與 handler 是否被觸發。 |
| `07` | PCI sysfs 路徑、`/proc/interrupts` | DMA 完成 IRQ 與 PCI resource 觀測；DMA buffer 本身不是普通檔案。 |
| `08` | `tests/driver_lab_char_cli` build artifact | userspace runtime，不會新增 kernel filesystem 入口。 |
| `09` | 依附 `03` 的 `/dev/driver_lab_ctl0` 與 script log | stress 腳本反覆打同一個 driver 入口。 |

## PCI labs 的 `/sys` 第一輪怎麼看

`05-07` 不是 `device_create()` 類型的教學 device node。它們是 PCI driver，所以第一個觀測點先是 PCI bus：

```sh
lspci -nn | grep 1234:11e8
ls /sys/bus/pci/devices
ls /sys/bus/pci/drivers
```

第一輪只要知道：

- QEMU EDU 必須先出現在 PCI bus，`probe()` 才可能進來。
- PCI driver bind 後，sysfs 會反映 device 與 driver 的關係。
- `06/07` 的 IRQ 可以用 `dmesg` 和 `/proc/interrupts` 輔助觀察。

## Cleanup 時哪些路徑會消失

| init / probe 建立 | exit / remove 釋放 | 路徑效果 |
|---|---|---|
| `debugfs_create_dir()` / `debugfs_create_file()` | `debugfs_remove()` | `/sys/kernel/debug/driver_lab_debugfs` 消失。 |
| `device_create()` | `device_destroy()` | sysfs device entry 與 `/dev` node 會被移除或失效。 |
| `class_create()` | `class_destroy()` | `/sys/class/<class>` 這類 class 入口消失。 |
| `cdev_add()` | `cdev_del()` | VFS 不再把該 cdev 當成 live char device。 |
| `pci_register_driver()` / `module_pci_driver()` | driver unregister / `remove()` | PCI driver bind 關係被拆掉。 |

第一輪記法：

> driver 建立的路徑不是永久檔案。module unload 或 PCI remove 時，對應 entry 應該跟著被拆掉。

## 官方參考

- [Linux sysfs documentation](https://docs.kernel.org/filesystems/sysfs.html)
- [Linux device driver infrastructure](https://docs.kernel.org/driver-api/infrastructure.html)
- [Linux DebugFS](https://docs.kernel.org/filesystems/debugfs.html)
- [Linux allocated devices](https://docs.kernel.org/admin-guide/devices.html)
