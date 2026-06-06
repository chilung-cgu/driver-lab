# `test.sh` 詳解

## 結論

`labs/06-pci-edu-irq/test.sh` 是 Lab06 的 Linux guest smoke test。它驗證的不是「module 可以載入」這麼簡單，而是：

```text
guest 內有 QEMU EDU
driver 能 bind 到 1234:11e8
driver 能 request IRQ
/proc/interrupts 看得到 driver 名稱
EDU self-test interrupt 真的進 handler
handler acknowledge 後 probe 回報 irq self-test passed
module unload 後 PCI driver sysfs entry 消失
```

這支 script 必須在看得到 QEMU EDU device 的 Linux guest 或等價 Linux 環境執行。macOS 不能直接跑。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab06 driver：[`driver_lab_edu_irq.c.md`](driver_lab_edu_irq.c.md)。
- Lab06 build：[`Makefile.md`](Makefile.md)。
- 共用 helper：[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- QEMU EDU 與 Linux IRQ 官方文件。

這裡不假設你的 `s2` shell 或 Linux VM 一定看得到 EDU。若 `lspci -nn | grep 1234:11e8` 沒輸出，test 會在前置檢查階段停止，這是正確行為。

## 測試主線

整支 script 的流程是：

```text
confirm Linux
confirm lspci exists
confirm lspci sees 1234:11e8
confirm PCI sysfs has vendor/device 0x1234:0x11e8
make driver_lab_edu_irq.ko
remove stale module if needed
clear dmesg
insmod
confirm driver bound
confirm /proc/interrupts lists driver_lab_edu_irq
grep dmesg for request_irq ok
grep dmesg for irq status=
grep dmesg for irq self-test passed
rmmod
confirm PCI driver sysfs directory removed
make clean
```

## 一、Linux guard

原始碼：

```sh
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機或 Linux guest 上執行。\n' >&2
    exit 1
fi
```

Lab06 需要：

- Linux kernel module build tree。
- `insmod` / `rmmod`。
- PCI sysfs。
- `/proc/interrupts`。
- QEMU EDU device。

所以不能在 macOS 直接跑。

## 二、路徑、module name、dmesg log

原始碼：

```sh
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_edu_irq
DMESG_LOG=$(mktemp)
SUDO=
```

| 變數 | 用途 |
|---|---|
| `SCRIPT_DIR` | Lab06 目錄，後面 `cd` 進去 build module。 |
| `ROOT_DIR` | repo 根目錄，用來 source 共用 filesystem helper。 |
| `MODULE_NAME` | `driver_lab_edu_irq`，用於 `lsmod`、`rmmod`、sysfs、grep。 |
| `DMESG_LOG` | 保存本次 `dmesg`，讓後面 grep 成功訊號。 |
| `SUDO` | 不是 root 時設為 `sudo`。 |

## 三、cleanup 與 trap

原始碼：

```sh
cleanup() {
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$DMESG_LOG"
}

trap cleanup EXIT INT TERM
```

Lab06 比 Lab05 更需要 cleanup，因為測試中途失敗時可能留下：

- 已載入的 module。
- 已註冊的 IRQ handler。
- 已 bind 的 PCI driver。

`trap cleanup EXIT INT TERM` 確保 script 正常結束、Ctrl-C 或收到 termination signal 時，都會嘗試卸載殘留 module 並刪掉暫存 log。

## 四、sudo 與共用 helper

原始碼：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

不是 root 時，module load/unload 與 `dmesg -C` 需要 sudo。

source helper 後，Lab06 使用：

| helper | 驗證什麼 |
|---|---|
| `fs_expect_pci_device_id` | `/sys/bus/pci/devices` 裡有 `0x1234:0x11e8`。 |
| `fs_expect_pci_driver_bound` | driver 已 bind 到 EDU device。 |
| `fs_expect_proc_interrupt` | `/proc/interrupts` 裡看得到 driver 名稱。 |
| `fs_expect_absent` | unload 後 PCI driver sysfs directory 消失。 |

## 五、確認 `lspci` 存在

原始碼：

```sh
if ! command -v lspci >/dev/null 2>&1; then
    printf 'ERROR: 找不到 lspci。請先安裝 pciutils。\n' >&2
    exit 1
fi
```

`lspci` 來自 `pciutils`。如果缺這個工具，先補 guest 環境，不要先改 driver。

Debian/Ubuntu 常見：

```sh
sudo apt install -y pciutils
```

## 六、確認 guest 看得到 QEMU EDU

原始碼：

```sh
if ! lspci -nn | grep -q '1234:11e8'; then
    printf 'ERROR: guest 內看不到 QEMU edu (1234:11e8)。\n' >&2
    exit 1
fi
fs_expect_pci_device_id 0x1234 0x11e8
```

這裡有兩層檢查：

| 檢查 | 意義 |
|---|---|
| `lspci -nn | grep 1234:11e8` | 從 PCI bus 工具看 EDU 是否存在。 |
| `fs_expect_pci_device_id 0x1234 0x11e8` | 從 PCI sysfs vendor/device 檔案看 EDU 是否存在。 |

如果這裡失敗，`probe()` 一定不會進來，`request_irq()` 也不會發生。第一個要修的是 QEMU/guest bring-up，例如啟動 QEMU 時是否有 `-device edu`。

## 七、build module 與清掉舊 module

原始碼：

```sh
cd "$SCRIPT_DIR"
make

if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi
```

`make` 透過 [`Makefile.md`](Makefile.md) 建出：

```text
driver_lab_edu_irq.ko
```

如果上一輪測試留下同名 module，先卸載。這對 IRQ lab 很重要，因為殘留的 handler/vector 狀態會干擾新一輪觀測。

## 八、清 dmesg、insmod、確認 bind

原始碼：

```sh
$SUDO dmesg -C || true
$SUDO insmod "./${MODULE_NAME}.ko"
fs_expect_pci_driver_bound "$MODULE_NAME" 0x1234 0x11e8
```

`dmesg -C` 是為了讓本次 grep 只看這一輪測試 log。某些環境可能不允許清 dmesg，所以用 `|| true` 不讓它阻止測試。

`insmod` 後：

```text
module init
  -> pci_register_driver()
  -> PCI core match 1234:11e8
  -> call dl_edu_irq_probe()
```

`fs_expect_pci_driver_bound` 檢查 `/sys/bus/pci/drivers/driver_lab_edu_irq` 底下是否有 matched device。

## 九、確認 `/proc/interrupts`

原始碼：

```sh
fs_expect_proc_interrupt "$MODULE_NAME"
```

這個 helper 會 grep `/proc/interrupts` 裡是否有 `driver_lab_edu_irq`。

這是輔助觀測點，代表 `request_irq()` 後 kernel 的 interrupt 報表有列出這個 driver 名稱。但它不是唯一成功標準，因為這一關真正要驗證的是：

```text
IRQ self-test 有進 handler
handler 有 acknowledge
probe 有等到 completion
```

所以後面還要 grep dmesg。

## 十、grep dmesg 成功訊號

原始碼：

```sh
$SUDO dmesg | tee "$DMESG_LOG"

grep -q 'request_irq ok' "$DMESG_LOG"
grep -q 'irq status=' "$DMESG_LOG"
grep -q 'irq self-test passed' "$DMESG_LOG"
```

這三個訊號對應三個階段：

| dmesg 訊號 | 代表 |
|---|---|
| `request_irq ok` | `request_irq()` 成功，handler 已註冊。 |
| `irq status=` | handler 真的被呼叫，讀到 EDU interrupt status，並寫 acknowledge。 |
| `irq self-test passed` | probe 等到 completion，且確認 status bit 已清掉。 |

如果只看到 `request_irq ok`，沒有 `irq status=`，代表 handler 沒進來。優先查 IRQ delivery、`pci_set_master()`、EDU raise register。

如果看到 `irq status=`，但沒有 `irq self-test passed`，代表 handler 有進來，但 self-test 後續檢查可能失敗，例如 status bit 沒清掉。

## 十一、rmmod、sysfs cleanup、make clean

原始碼：

```sh
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent "/sys/bus/pci/drivers/$MODULE_NAME" "PCI driver sysfs directory"
make clean
```

`rmmod` 會讓 PCI core 呼叫 remove path：

```text
dl_edu_irq_remove()
  -> free_irq()
  -> pci_free_irq_vectors()
  -> pci_iounmap()
  -> pci_release_region()
  -> pci_disable_device()
```

`fs_expect_absent` 確認 driver sysfs directory 已消失。這可以抓到 module unregister 或 cleanup 殘留問題。

`make clean` 只清 build artifact，不負責 unload module，所以順序必須是先 `rmmod` 再 `make clean`。

## test 和 source 的對照

| test 片段 | 對應 source / 行為 |
|---|---|
| `lspci -nn | grep 1234:11e8` | 前置環境，driver 尚未參與。 |
| `fs_expect_pci_device_id` | PCI sysfs 有 EDU device。 |
| `insmod ./driver_lab_edu_irq.ko` | `module_pci_driver()` generated init。 |
| `fs_expect_pci_driver_bound` | PCI core match 並 bind 成功。 |
| `fs_expect_proc_interrupt` | `request_irq()` 後 `/proc/interrupts` 有 driver 名稱。 |
| `grep 'request_irq ok'` | `request_irq()` 成功。 |
| `grep 'irq status='` | `dl_edu_irq_handler()` 有處理 self-test interrupt。 |
| `grep 'irq self-test passed'` | `wait_for_completion_timeout()` 等到 handler 且 ACK 後 status 清掉。 |
| `fs_expect_absent` | `rmmod` 後 PCI driver sysfs directory 消失。 |

## 常見卡點

- `guest 內看不到 QEMU edu`：先修 QEMU 啟動參數或 guest 環境，不是 driver source 問題。
- `make` 失敗：先確認 `/lib/modules/$(uname -r)/build` 存在。
- `insmod` 失敗：看 `sudo dmesg | tail -n 50`，不要只看 shell 的一行錯誤。
- `/proc/interrupts` 沒有 `driver_lab_edu_irq`：優先看 `request_irq()` 是否成功。
- 沒有 `irq status=`：handler 沒進來，查 IRQ delivery。
- 沒有 `irq self-test passed`：handler 可能沒 complete，或 acknowledge 後 status bit 沒清掉。
- test 中途 Ctrl-C 後下一輪怪怪的：先 `lsmod | grep driver_lab_edu_irq`，確認 cleanup 有卸載。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 test 必須在哪裡跑？ | 看得到 QEMU EDU `1234:11e8` 的 Linux guest 或等價 Linux 環境。 |
| `lspci` 檢查失敗時該先改 driver 嗎？ | 不該；先修 QEMU/guest bring-up。 |
| `/proc/interrupts` 檢查代表什麼？ | 輔助確認 IRQ handler registration 後 kernel 報表有 driver 名稱。 |
| 三個主要 dmesg gate 是什麼？ | `request_irq ok`、`irq status=`、`irq self-test passed`。 |
| 為什麼 `make clean` 放在 `rmmod` 後？ | `make clean` 只清 build artifact，不會卸載 module 或釋放 IRQ。 |
| cleanup 為什麼要嘗試 `rmmod`？ | 避免失敗中途留下 loaded module、IRQ handler、PCI bind 狀態。 |

## 查證來源

- Linux kernel documentation `The MSI Driver Guide HOWTO`：PCI IRQ vector allocation、`pci_irq_vector()` 與 `pci_free_irq_vectors()`。<https://docs.kernel.org/PCI/msi-howto.html>
- Linux kernel documentation `Linux generic IRQ handling`：IRQ handler registration 與 `dev_id` cookie。<https://docs.kernel.org/core-api/genericirq.html>
- QEMU documentation `EDU device`：interrupt raise/status/acknowledge register 行為。<https://www.qemu.org/docs/master/specs/edu.html>
