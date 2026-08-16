# `test.sh` 詳解

## 結論

`labs/05-pci-edu-mmio/test.sh` 是 Lab05 的 Linux guest smoke test。它驗證的不是普通 kernel module load/unload，而是「guest 內真的有 QEMU EDU，PCI core 真的讓 driver bind，BAR0 MMIO liveness check 真的通過」。

測試主線：

```text
confirm Linux
confirm lspci exists
confirm lspci sees 1234:11e8
confirm /sys/bus/pci/devices has vendor/device 0x1234:0x11e8
build driver_lab_edu_mmio.ko
write a unique /dev/kmsg marker
insmod
confirm driver bound to 1234:11e8
rmmod
confirm PCI driver sysfs directory removed
extract marker-scoped dmesg for probe/BAR/liveness/remove
make clean
```

這支 script 必須在 Linux guest 或能看見 QEMU EDU 的 Linux 環境裡跑。macOS host 不能直接跑這支測試。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab05 driver：[`driver_lab_edu_mmio.c.md`](driver_lab_edu_mmio.c.md)。
- repo helper：[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- QEMU bring-up 文件：[`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md)。
- Linux/QEMU 官方文件：PCI driver flow、QEMU EDU device。

這裡不假設你的 host/guest image 一定已經準備好；如果看不到 `1234:11e8`，這支 test 會直接停，因為那是環境前置條件失敗。

## 一、Linux guard

原始碼：

```sh
if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機或 Linux guest 上執行。\n' >&2
    exit 1
fi
```

Lab05 需要：

- Linux kernel module build tree。
- `insmod` / `rmmod`。
- PCI sysfs。
- QEMU EDU device。

所以不能在 macOS 直接跑。

## 二、路徑、module name、dmesg log

原始碼：

```sh
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
MODULE_NAME=driver_lab_edu_mmio
DMESG_LOG=$(mktemp)
DMESG_ALL=$(mktemp)
DMESG_MARKER="${MODULE_NAME}: lab05-test marker pid=$$ epoch=$(date +%s)"
SUDO=
```

| 變數 | 用途 |
|---|---|
| `SCRIPT_DIR` | Lab05 目錄。 |
| `ROOT_DIR` | repo 根目錄，用來 source helper。 |
| `MODULE_NAME` | `driver_lab_edu_mmio`，給 `lsmod` / `rmmod` / sysfs driver path 使用。 |
| `DMESG_LOG` | 保存 marker 後的本輪 kernel log，供成功/diagnostic gate 使用。 |
| `DMESG_ALL` | 暫存完整 `dmesg`，用來找 marker；不把整份共享 log 當成本輪證據。 |
| `DMESG_MARKER` | 含 module、PID、epoch 的唯一 marker，讓 script 能界定本次 run。 |

## 三、cleanup 與 trap

原始碼：

```sh
cleanup() {
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$DMESG_LOG" "$DMESG_ALL"
}

trap cleanup EXIT INT TERM
```

如果測試中途失敗，cleanup 會嘗試卸載殘留 module，並刪掉暫存 log。

PCI lab 尤其需要這個保護，因為前一次 bind 狀態殘留可能影響下一輪測試。

## 四、sudo 與 filesystem helper

原始碼：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
FS_SUDO=$SUDO
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

不是 root 時使用 `sudo`。

source helper 後會使用：

- `fs_expect_pci_device_id`
- `fs_expect_pci_driver_bound`
- `fs_expect_absent`

## 五、檢查 `lspci`

原始碼：

```sh
if ! command -v lspci >/dev/null 2>&1; then
    printf 'ERROR: 找不到 lspci。請先安裝 pciutils。\n' >&2
    exit 1
fi
```

`lspci` 來自 `pciutils`。如果 guest 沒裝，先裝工具，不要先改 driver。

Debian/Ubuntu 常見：

```sh
sudo apt install -y pciutils
```

## 六、確認 guest 真的看得到 EDU

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
| `lspci -nn | grep 1234:11e8` | 人類最直覺的 PCI bus 觀測。 |
| `fs_expect_pci_device_id 0x1234 0x11e8` | 從 `/sys/bus/pci/devices/*/{vendor,device}` 找證據。 |

如果這裡失敗：

```text
probe() 一定不會進來
```

第一個要修的是 QEMU/guest bring-up，例如確認 host 啟動參數有 `-device edu`。

## 七、build module 與清掉舊 module

原始碼：

```sh
cd "$SCRIPT_DIR"
make

if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi
```

`make` 會透過 [`Makefile.md`](Makefile.md) 建出：

```text
driver_lab_edu_mmio.ko
```

如果前一次 module 還載著，先卸載，避免 driver bind 狀態混亂。

## 八、寫 marker、insmod、確認 driver bind

原始碼：

```sh
printf '%s\n' "$DMESG_MARKER" | $SUDO tee /dev/kmsg >/dev/null
$SUDO insmod "./${MODULE_NAME}.ko"
fs_expect_pci_driver_bound "$MODULE_NAME" 0x1234 0x11e8
```

script 不執行 `dmesg -C`，因為那會清除共享的 kernel log。它改寫入唯一 marker；稍後 marker 若已從 ring buffer 遺失，測試直接失敗，不能用不可靠的切片聲稱本輪 log 乾淨。

`insmod` 後，driver 註冊到 PCI core，若 `1234:11e8` 還沒有其他 driver 接手，PCI core 會呼叫 `dl_edu_mmio_probe()`。

`fs_expect_pci_driver_bound` 會檢查：

```text
/sys/bus/pci/drivers/driver_lab_edu_mmio
```

底下是否有 vendor/device 符合 `0x1234:0x11e8` 的 bound device。

## 九、擷取 marker-scoped log 與 gate

原始碼：

```sh
$SUDO dmesg >"$DMESG_ALL"
grep -Fq "$DMESG_MARKER" "$DMESG_ALL"
awk -v marker="$DMESG_MARKER" '
    index($0, marker) { capture = 1; next }
    capture { print }
' "$DMESG_ALL" >"$DMESG_LOG"

grep -q "${MODULE_NAME}: probe start" "$DMESG_LOG"
grep -q 'BAR0 mapped' "$DMESG_LOG"
grep -q 'liveness check passed' "$DMESG_LOG"
grep -q 'device removed' "$DMESG_LOG"
```

這三個 grep 對應到 driver 的三個階段：

| dmesg 訊號 | 代表 |
|---|---|
| `probe start` | PCI core 已經呼叫 `dl_edu_mmio_probe()`。 |
| `BAR0 mapped` | `pci_iomap()` 成功，driver 拿到 MMIO base。 |
| `liveness check passed` | `iowrite32()` / `ioread32()` round-trip 符合 QEMU EDU liveness 規格。 |

marker 後沒有 `probe start`，優先查 PCI ID match / bind；若 marker 本身不存在，代表無法隔離本輪訊息，必須先停下來保存環境狀態，不可把整份 dmesg 當作證據。

如果 `BAR0 mapped` 沒出現，優先查 `pci_enable_device()` / `pci_request_region()` / `pci_iomap()`。

如果 `liveness check passed` 沒出現，優先查 register offset 和 expected value。

## 十、rmmod、PCI driver sysfs cleanup、make clean

原始碼：

```sh
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent "/sys/bus/pci/drivers/$MODULE_NAME" "PCI driver sysfs directory"
make clean
```

`rmmod` 會讓 PCI core unregister driver，並對已 bind device 呼叫 remove path：

```text
dl_edu_mmio_remove()
  -> pci_iounmap()
  -> pci_disable_device()
  -> pci_release_region()
```

`fs_expect_absent` 確認 driver sysfs directory 已消失，避免 module unregister 失敗或殘留。

`make clean` 清掉 kbuild artifact；它不會卸載 module，所以順序必須是先 `rmmod`。

## test 和 source 的對照

| test 片段 | 驗證的 source path |
|---|---|
| `lspci -nn | grep 1234:11e8` | 前置環境，driver 尚未參與 |
| `fs_expect_pci_device_id 0x1234 0x11e8` | PCI sysfs 有 EDU device |
| `insmod ./driver_lab_edu_mmio.ko` | `module_pci_driver()` generated init |
| `fs_expect_pci_driver_bound` | PCI core match + bind 成功 |
| `grep probe start` | `dl_edu_mmio_probe()` 進入 |
| `grep BAR0 mapped` | `pci_iomap()` 成功 |
| `grep liveness check passed` | QEMU EDU liveness MMIO test 成功 |
| `rmmod` | `dl_edu_mmio_remove()` |
| `fs_expect_absent /sys/bus/pci/drivers/...` | PCI driver unregister 完成 |

## 常見卡點

- `lspci` 不存在：先安裝 `pciutils`。
- `lspci` 看不到 `1234:11e8`：先修 QEMU 啟動或確認自己在 guest 裡。
- `insmod` 成功但 `probe start` 沒有：查 device 是否被其他 driver bind，或 ID table 是否 match。
- `BAR0 mapped` 沒有：查 `pci_enable_device()`、`pci_request_region()`、BAR index。
- `liveness check passed` 沒有：查 QEMU EDU spec 的 `0x04` liveness 行為。
- marker 寫入或讀取失敗：確認可使用 `sudo` 寫 `/dev/kmsg` 與讀 `dmesg`；不能改用 `dmesg -C` 或整份舊 log 作替代。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 test 為什麼先查 `lspci`？ | 沒有 QEMU EDU device 時，driver `probe()` 不會進來。 |
| `fs_expect_pci_device_id` 查哪裡？ | `/sys/bus/pci/devices/*/vendor` 和 `device`。 |
| `fs_expect_pci_driver_bound` 驗什麼？ | driver sysfs directory 底下有 bound 到 `0x1234:0x11e8` 的 device。 |
| marker-scoped dmesg gate 是什麼？ | `probe start`、`BAR0 mapped`、`liveness check passed`、`device removed`，並拒絕本輪 marker 後的已知 kernel diagnostics。 |
| rmmod 後驗什麼？ | `/sys/bus/pci/drivers/driver_lab_edu_mmio` 已移除。 |

## 查證來源

- Linux kernel documentation `How To Write Linux PCI Drivers`：PCI driver registration、probe/remove、初始化與 teardown。<https://docs.kernel.org/PCI/pci.html>
- QEMU documentation `EDU device`：EDU PCI ID、BAR/MMIO 與 liveness register。<https://www.qemu.org/docs/master/specs/edu.html>
