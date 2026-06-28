# Lab05 讀懂順序

這份文件是 `05-pci-edu-mmio` 的第一入口。它不取代
[`qemu-edu-first-pass.md`](qemu-edu-first-pass.md)、[`linux-guest-05-to-07-walkthrough.md`](linux-guest-05-to-07-walkthrough.md)
或 Lab05 source companion docs，而是把你開始讀之前要看的文件、host/guest 邊界、source trace 順序與驗收標準整理成一條可以照著走的路線。

## 先確認你可以進 `05`

開始前先確認五件事：

- 你已完成 `00-04` 的第一輪，能回答各 lab README 的「完成後你應該能回答」。
- 你知道 `04` 的重點是 shared state、lifetime、cleanup，而不只是「加一把 mutex」。
- 你知道 `probe/remove` 是裝置生命週期入口，不是 shell 直接呼叫的 C function。
- 你能接受 `05-07` 的實際驗證位置是 `Linux guest`，不是 `macOS` host。
- 你能在 guest 裡用 `lspci -nn | grep 1234:11e8` 看見 QEMU EDU device。

如果最後一項不成立，先不要讀 Lab05 driver source。先回去補：

1. [`../../qemu/README.md`](../../qemu/README.md)
2. [`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md)
3. [`qemu-edu-first-pass.md`](qemu-edu-first-pass.md)
4. [`linux-guest-05-to-07-walkthrough.md`](linux-guest-05-to-07-walkthrough.md)

## 正確閱讀順序

1. 讀 [`../onboarding/03-to-05-concurrency-pci-bridge.md`](../onboarding/03-to-05-concurrency-pci-bridge.md) 的 `04 -> 05` 段落。先理解入口從 `/dev`、debugfs、ioctl callback，轉成 PCI core 在 match 後呼叫 `probe()`。
2. 讀 [`../concepts/pcie-primer.md`](../concepts/pcie-primer.md)。第一輪只抓 PCI ID、BAR、MMIO、`probe/remove` 的最低心智模型。
3. 讀 [`qemu-edu-first-pass.md`](qemu-edu-first-pass.md)。理解 QEMU EDU 是教學用 PCI device，`05/06/07` 是同一路線的三個階段。
4. 讀本文件，先決定後續每一步看哪裡、哪些先略過。
5. 讀 [`../../qemu/README.md`](../../qemu/README.md) 與 [`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md)。確認 host 負責啟 QEMU，guest 才負責 build/load driver。
6. 在 guest 內確認 `lspci -nn | grep 1234:11e8`。看不到就先修 QEMU/guest，不要先追 driver code。
7. 讀 [`../../labs/05-pci-edu-mmio/README.md`](../../labs/05-pci-edu-mmio/README.md)。確認本關目標、成功訊號與不做 IRQ/DMA。
8. 讀 [`../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c`](../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c) 和 [`../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c.md`](../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c.md)。照下面的 source trace 順序讀，不要從第一行硬掃。
9. 讀 [`../../labs/05-pci-edu-mmio/test.sh`](../../labs/05-pci-edu-mmio/test.sh) 和 [`../../labs/05-pci-edu-mmio/test.sh.md`](../../labs/05-pci-edu-mmio/test.sh.md)。確認 smoke test 驗的是環境、bind、dmesg 成功訊號，不是完整 PCI correctness proof。
10. 讀 [`../../labs/05-pci-edu-mmio/debug-checklist.md`](../../labs/05-pci-edu-mmio/debug-checklist.md)、[`../reference/common-failures.md`](../reference/common-failures.md) 和 [`../reference/debugging-playbook.md`](../reference/debugging-playbook.md)。學會先分辨 QEMU/guest 問題、bind 問題、BAR/MMIO 問題。
11. 最後讀 [`../onboarding/05-to-07-pci-irq-dma-bridge.md`](../onboarding/05-to-07-pci-irq-dma-bridge.md)。這份只用來知道 Lab05 後面會接 IRQ/DMA，不要把 06/07 的內容提前塞進 05。

## Host / Guest 邊界

Lab05 最常見的錯誤不是 C code，而是站錯位置。

| 位置 | 負責什麼 | 第一輪觀測 |
|---|---|---|
| host | 啟動 QEMU、掛入 `-device edu`、準備 guest image | `qemu-system-x86_64 ... -device edu ...` |
| guest | 安裝 build tools/kernel headers、執行 `make`、`insmod`、`rmmod`、`test.sh` | `uname -a`、`lspci -nn | grep 1234:11e8` |

如果你在 macOS host 上，macOS 可以啟 QEMU，但不能直接 build/load Linux kernel module。Lab05 的 `./test.sh` 必須在 Linux guest 或能看見 QEMU EDU 的 Linux 環境裡跑。

## 三輪閱讀法

Lab05 的第一輪邊界很重要：本關實作主線是 `PCI probe + BAR0 MMIO + liveness check`。IRQ、DMA、MSI、AER、reset、power management 都不是 Lab05 smoke test 要你立刻完成的內容。

第一輪只看「driver 怎麼拿到裝置」：

- QEMU EDU 的 PCI ID 是 `1234:11e8`。
- `dl_edu_mmio_ids` match 後，PCI core 才會呼叫 `dl_edu_mmio_probe()`。
- `lspci` 看不到 `1234:11e8` 時，`probe()` 不會進來。

第二輪看「driver 怎麼拿到 BAR0」：

- `pci_enable_device()` 成功後才繼續碰 BAR/MMIO。
- `pci_request_region()` 宣告 BAR0 resource 由本 driver 使用。
- `pci_iomap()` 把 BAR0 map 成 `void __iomem *`，之後用 `ioread32()` / `iowrite32()` 存取。

第三輪看「成功與失敗如何收尾」：

- liveness check 寫入 `0x04` 後，依 QEMU EDU 規格期待讀回 bitwise inverse。
- error path 要依取得 resource 的反方向釋放。
- remove path 要做 `pci_iounmap()`、`pci_release_region()`、`pci_disable_device()`。

## Source trace 順序

讀 [`../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c`](../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c) 時，照這個順序：

1. `DL_EDU_VENDOR_ID` / `DL_EDU_DEVICE_ID`：確認 driver match 哪顆 device。
2. `DL_EDU_BAR_INDEX`、`DL_EDU_IDENT_REG`、`DL_EDU_LIVENESS_REG`：確認 Lab05 只碰 BAR0 裡的兩個 register。
3. `struct dl_edu_mmio_dev`：看每顆 matched device 的 private state。
4. `dl_edu_mmio_ids`：看 PCI ID table 與 terminator。
5. `dl_edu_mmio_driver`：看 `.probe` / `.remove` callback。
6. `dl_edu_mmio_probe()`：看 private state、enable device、request BAR、map BAR。
7. `ioread32()` / `iowrite32()`：看 identification 與 liveness check。
8. `err_iounmap`、`err_release_region`、`err_disable_device`：看 error path 是否反向釋放。
9. `dl_edu_mmio_remove()`：看 module unload 或 unbind 時如何 cleanup。
10. `module_pci_driver()`：理解 source 裡為什麼沒有手寫 `module_init()` / `module_exit()`。

## 第一輪可以先略過

- PCI enumeration、configuration space、BAR assignment 的完整平台細節。
- IRQ、MSI、MSI-X、interrupt acknowledge。
- DMA mask、coherent DMA buffer、IOMMU。
- AER、reset、power management。
- real hardware bring-up 的 clock、firmware、link training 與 board-specific quirk。

先把「guest 真的有 device」、「PCI core 真的呼叫 `probe()`」、「BAR0 真的能 MMIO read/write」講清楚，比先背完整 PCI subsystem 重要。

## 實驗驗收方式

手動示範順序：

```sh
cd labs/05-pci-edu-mmio
lspci -nn | grep 1234:11e8
make
sudo dmesg -C || true
sudo insmod ./driver_lab_edu_mmio.ko
sudo dmesg | tail -n 80
sudo rmmod driver_lab_edu_mmio
make clean
```

自動 smoke test：

```sh
./labs/05-pci-edu-mmio/test.sh
```

觀察重點：

- `lspci -nn | grep 1234:11e8` 要先看得到 EDU device。
- `dmesg` 裡要看到 `probe start`。
- `dmesg` 裡要看到 `BAR0 mapped`。
- `dmesg` 裡要看到 `liveness check passed`。
- `rmmod` 後 `/sys/bus/pci/drivers/driver_lab_edu_mmio` 不應殘留。

## Debug 順序

如果卡住，請照這個順序切，不要一開始就改 source：

1. `uname -a`：確認你在 Linux guest 或 Linux 環境。
2. `command -v lspci`：確認 guest 有 `pciutils`。
3. `lspci -nn | grep 1234:11e8`：確認 QEMU EDU device 存在。
4. `/sys/bus/pci/devices/*/{vendor,device}`：確認 sysfs 也看得到 `0x1234:0x11e8`。
5. `sudo insmod ./driver_lab_edu_mmio.ko`：確認 module load 成功。
6. `sudo dmesg | tail -n 80`：找 `probe start`、`BAR0 mapped`、`liveness check passed`。
7. `/sys/bus/pci/drivers/driver_lab_edu_mmio`：確認 driver 已註冊且 bind 到 EDU。

## 完成後你應該能回答

| 問題 | 標準答案方向 |
|---|---|
| Lab05 為什麼接在 Lab04 後面？ | Lab04 先練 shared state 與 cleanup；Lab05 開始把同樣的 lifetime 思維放到 PCI `probe/remove` 與 resource cleanup。 |
| `probe()` 是誰呼叫的？ | PCI core 在 driver 註冊後，對 match `1234:11e8` 且未被其他 driver 擁有的 device 呼叫。 |
| `lspci` 看不到 `1234:11e8` 時，為什麼不能先怪 driver？ | guest 裡沒有 EDU device 時，PCI core 沒有可 match 的 device，`probe()` 不會進來。 |
| BAR0 在 Lab05 是什麼？ | QEMU EDU 的 MMIO register window，官方規格是 PCI Region 0，I/O memory，1 MB。 |
| `pci_request_region()` 和 `pci_iomap()` 差在哪？ | 前者宣告 BAR0 resource ownership；後者建立 driver 可用的 MMIO mapping。 |
| 為什麼不能直接解參考 `void __iomem *`？ | MMIO 不是一般 RAM，portable driver 要透過 `ioread32()` / `iowrite32()` 這類 accessor。 |
| liveness check 驗什麼？ | 對 `0x04` 寫入 pattern 後讀回 bitwise inverse，確認最小 MMIO write/read path 可用。 |
| cleanup 順序是什麼？ | 依取得 resource 的反方向釋放：`pci_iounmap()`、`pci_release_region()`、`pci_disable_device()`。 |

## 官方查證入口

- [How To Write Linux PCI Drivers](https://docs.kernel.org/PCI/pci.html)
- [PCI Support Library](https://docs.kernel.org/driver-api/pci/pci.html)
- [Bus-Independent Device Accesses](https://docs.kernel.org/driver-api/device-io.html)
- [Building External Modules](https://docs.kernel.org/kbuild/modules.html)
- [QEMU EDU device](https://www.qemu.org/docs/master/specs/edu.html)
