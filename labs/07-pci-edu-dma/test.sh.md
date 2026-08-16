# `test.sh` 詳解

## 結論

`labs/07-pci-edu-dma/test.sh` 是 Lab07 的 Linux guest smoke test。它驗證的是完整的 PCI/MMIO/IRQ/DMA round-trip，不只是 module load：

```text
guest 內有 QEMU EDU
driver 能 bind 到 1234:11e8
driver 能設定 28-bit DMA mask
driver 能配置 coherent DMA buffer
driver 能 request IRQ
EDU 能完成 RAM -> EDU -> RAM DMA round-trip
driver 能用 memcmp() 驗證資料一致
module unload 後 PCI driver sysfs entry 消失
```

這支 script 必須在看得到 QEMU EDU device 的 Linux guest 或等價 Linux 環境執行。macOS 不能直接跑。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab07 driver：[`driver_lab_edu_dma.c.md`](driver_lab_edu_dma.c.md)。
- Lab07 build：[`Makefile.md`](Makefile.md)。
- 共用 helper：[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- Linux DMA API 與 QEMU EDU 官方文件。

這裡不假設你的 `s2` shell 或 Linux VM 一定看得到 EDU。若 `lspci -nn | grep 1234:11e8` 沒輸出，test 會在前置檢查階段停止，這是正確行為。

## 測試主線

整支 script 的流程是：

```text
confirm Linux
confirm lspci exists
confirm lspci sees 1234:11e8
confirm PCI sysfs has vendor/device 0x1234:0x11e8
make driver_lab_edu_dma.ko
remove stale module if needed
write a unique dmesg marker
insmod
confirm driver bound
confirm /proc/interrupts lists driver_lab_edu_dma
grep dmesg for dma mask configured
grep dmesg for coherent buffer allocated
grep dmesg for round-trip compare passed
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

Lab07 需要：

- Linux kernel module build tree。
- `insmod` / `rmmod`。
- PCI sysfs。
- `/proc/interrupts`。
- QEMU EDU device。
- DMA API 與可用的 coherent allocation。

所以不能在 macOS 直接跑。

## 二、路徑、module name、dmesg log

原始碼：

```sh
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_edu_dma
DMESG_LOG=$(mktemp)
DMESG_ALL=$(mktemp)
DMESG_MARKER="${MODULE_NAME}: smoke-test marker pid=$$ epoch=$(date +%s)"
SUDO=
loaded_by_test=0
```

| 變數 | 用途 |
|---|---|
| `SCRIPT_DIR` | Lab07 目錄，後面 `cd` 進去 build module。 |
| `ROOT_DIR` | repo 根目錄，用來 source 共用 filesystem helper。 |
| `MODULE_NAME` | `driver_lab_edu_dma`，用於 `lsmod`、`rmmod`、sysfs、grep。 |
| `DMESG_LOG` | 保存本次 `dmesg`，讓後面 grep 成功訊號。 |
| `DMESG_ALL` | 保存單次完整 snapshot，之後由 marker 切出本輪區段。 |
| `DMESG_MARKER` | 含 PID 與 epoch 的唯一 kernel-log 起點，不清全域 log。 |
| `SUDO` | 不是 root 時設為 `sudo`。 |

## 三、cleanup 與 trap

原始碼：

```sh
cleanup() {
    if [ "$loaded_by_test" -eq 1 ] && \
       lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$DMESG_LOG" "$DMESG_ALL"
}

trap cleanup EXIT INT TERM
```

Lab07 中途失敗時可能留下：

- loaded module。
- registered IRQ handler。
- allocated coherent DMA buffer。
- bound PCI driver。

`trap cleanup EXIT INT TERM` 確保 script 結束或被中斷時，會嘗試卸載殘留 module。真正的 DMA buffer/free IRQ cleanup 由 driver 的 `remove()` path 負責。

## 四、sudo 與共用 helper

原始碼：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

不是 root 時，module load/unload、寫 `/dev/kmsg` marker 與讀 `dmesg` 需要 sudo。

source helper 後，Lab07 使用：

| helper | 驗證什麼 |
|---|---|
| `fs_expect_pci_device_id` | `/sys/bus/pci/devices` 裡有 `0x1234:0x11e8`。 |
| `fs_expect_pci_driver_bound` | driver 已 bind 到 EDU device。 |
| `fs_expect_proc_interrupt` | `/proc/interrupts` 裡看得到 driver 名稱。 |
| `fs_expect_absent` | unload 後 PCI driver sysfs directory 消失。 |

## 五、確認 `lspci` 與 EDU 前置條件

原始碼：

```sh
if ! command -v lspci >/dev/null 2>&1; then
    printf 'ERROR: 找不到 lspci。請先安裝 pciutils。\n' >&2
    exit 1
fi

if ! lspci -nn | grep -q '1234:11e8'; then
    printf 'ERROR: guest 內看不到 QEMU edu (1234:11e8)。\n' >&2
    exit 1
fi
fs_expect_pci_device_id 0x1234 0x11e8
```

這裡失敗時，不要先改 DMA code。因為如果 guest 看不到 `1234:11e8`：

```text
PCI core 不會 call probe()
DMA mask 不會設定
coherent buffer 不會配置
IRQ 不會 request
```

第一個要修的是 QEMU/guest bring-up。

## 六、build module 與清掉舊 module

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
driver_lab_edu_dma.ko
```

如果同名 module 已存在，測試直接拒絕執行，避免卸載其他 session
擁有的 IRQ/vector/DMA allocation。

## 七、寫唯一 marker、insmod、確認 bind

原始碼：

```sh
printf '%s\n' "$DMESG_MARKER" | $SUDO tee /dev/kmsg >/dev/null
$SUDO insmod "./${MODULE_NAME}.ko"
loaded_by_test=1
fs_expect_pci_driver_bound "$MODULE_NAME" 0x1234 0x11e8
fs_expect_proc_interrupt "$MODULE_NAME"
```

測試不清全域 kernel log。marker 之後的 snapshot 才是本輪證據；
marker 若因 ring-buffer wrap 消失，測試明確失敗。

`insmod` 後，driver 的 `probe()` 會直接做完整 round-trip self-test。這代表：

```text
如果 insmod 回傳成功
  通常代表 DMA mask、buffer allocation、IRQ、兩段 DMA、memcmp 都通過

如果 insmod 回傳失敗
  要看 dmesg 判斷卡在哪個階段
```

`fs_expect_pci_driver_bound` 確認 `/sys/bus/pci/drivers/driver_lab_edu_dma` 底下有 matched device。

`fs_expect_proc_interrupt` 是輔助觀測，確認 `/proc/interrupts` 裡有 driver 名稱。

## 八、grep dmesg 成功訊號

原始碼：

```sh
$SUDO dmesg >"$DMESG_ALL"
grep -Fq "$DMESG_MARKER" "$DMESG_ALL"
awk -v marker="$DMESG_MARKER" '
    index($0, marker) { capture = 1; next }
    capture { print }
' "$DMESG_ALL" >"$DMESG_LOG"

grep -q 'probe takeover confirmed DMA command idle with BME disabled' "$DMESG_LOG"
grep -q 'dma mask configured' "$DMESG_LOG"
grep -q 'coherent buffer allocated' "$DMESG_LOG"
grep -q 'round-trip compare passed' "$DMESG_LOG"
```

這三個 gate 對應三個層次：

| dmesg 訊號 | 代表 |
|---|---|
| `probe takeover ... idle with BME disabled` | 在 INTx mask 下確認 inherited DMA engine idle。 |
| `dma mask configured` | `dma_set_mask_and_coherent()` 成功。 |
| `coherent buffer allocated` | `dma_alloc_coherent()` 成功，CPU pointer / DMA address 已取得。 |
| `round-trip compare passed` | RAM -> EDU -> RAM 的資料一致。 |

注意：`round-trip compare passed` 比「有 IRQ」更強。它代表資料真的搬回來且和原始 pattern 一致。

如果想看更細的成功訊號，可以手動找：

```text
ram-to-edu transfer finished
edu-to-ram transfer finished
dma irq status=0x00000100 acknowledged
```

## 九、rmmod、sysfs cleanup、make clean

原始碼：

```sh
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent "/sys/bus/pci/drivers/$MODULE_NAME" "PCI driver sysfs directory"
make clean
```

`rmmod` 會讓 PCI core 呼叫 remove path：

```text
dl_edu_dma_remove()
  -> free_irq()
  -> pci_free_irq_vectors()
  -> dma_free_coherent()
  -> pci_iounmap()
  -> pci_release_region()
  -> pci_disable_device()
```

`fs_expect_absent` 確認 driver sysfs directory 已消失，避免 unload 後仍殘留 driver bind 狀態。

`make clean` 只清 build artifact，不負責 unload module 或釋放 DMA buffer，所以順序必須是先 `rmmod`。

## test 和 source 的對照

| test 片段 | 對應 source / 行為 |
|---|---|
| `lspci -nn | grep 1234:11e8` | 前置環境，driver 尚未參與。 |
| `fs_expect_pci_device_id` | PCI sysfs 有 EDU device。 |
| `insmod ./driver_lab_edu_dma.ko` | `module_pci_driver()` generated init，觸發 `probe()` self-test。 |
| `fs_expect_pci_driver_bound` | PCI core match 並 bind 成功。 |
| `fs_expect_proc_interrupt` | `request_irq()` 後 `/proc/interrupts` 有 driver 名稱。 |
| `grep 'dma mask configured'` | `dma_set_mask_and_coherent()` 成功。 |
| `grep 'coherent buffer allocated'` | `dma_alloc_coherent()` 成功。 |
| `grep 'round-trip compare passed'` | 兩段 DMA 完成且 `memcmp()` 成功。 |
| `fs_expect_absent` | `rmmod` 後 PCI driver sysfs directory 消失。 |

## 常見卡點

- `guest 內看不到 QEMU edu`：先修 QEMU 啟動參數或 guest 環境。
- `make` 失敗：先確認 `/lib/modules/$(uname -r)/build` 存在。
- `insmod` 失敗：看 `sudo dmesg | tail -n 120`，DMA driver 的錯誤階段通常會寫在 log。
- 沒有 `dma mask configured`：`probe()` 可能還沒走到 DMA setup，或 `dma_set_mask_and_coherent()` 失敗。
- 沒有 `coherent buffer allocated`：先看 DMA mask 是否成功。
- 沒有 `round-trip compare passed`：往 `ram-to-edu`、`edu-to-ram`、IRQ timeout、command bit clear、`memcmp()` 失敗切。
- test 中途 Ctrl-C 後下一輪怪怪的：先 `lsmod | grep driver_lab_edu_dma`，確認 cleanup 有卸載。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 test 必須在哪裡跑？ | 看得到 QEMU EDU `1234:11e8` 的 Linux guest 或等價 Linux 環境。 |
| `insmod` 成功在 Lab07 通常代表什麼？ | `probe()` 裡的 DMA mask、coherent buffer、IRQ、兩段 DMA 和 `memcmp()` 都成功。 |
| 三個主要 dmesg gate 是什麼？ | `dma mask configured`、`coherent buffer allocated`、`round-trip compare passed`。 |
| `/proc/interrupts` 檢查代表什麼？ | 輔助確認 IRQ handler registration 後 kernel 報表有 driver 名稱。 |
| 為什麼 `round-trip compare passed` 是最強 gate？ | 它證明資料真的從 tx 經 EDU 再回到 rx，且內容一致。 |
| `make clean` 會釋放 DMA buffer 嗎？ | 不會；DMA buffer 由 driver remove/error path 的 `dma_free_coherent()` 釋放。 |

## 查證來源

- Linux kernel documentation `Dynamic DMA mapping Guide`：DMA mask、coherent DMA mapping、`dma_alloc_coherent()` 與 `dma_free_coherent()`。<https://docs.kernel.org/core-api/dma-api-howto.html>
- Linux kernel documentation `Dynamic DMA mapping using the generic device`：`dma_addr_t` 與 coherent allocation API 語意。<https://docs.kernel.org/core-api/dma-api.html>
- QEMU documentation `EDU device`：預設 28-bit `dma_mask`、DMA registers、direction bit、completion IRQ bit 與 `0x40000` internal buffer。<https://www.qemu.org/docs/master/specs/edu.html>
