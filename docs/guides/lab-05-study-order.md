# Lab05 讀懂順序

> 這份文件是 `05-pci-edu-mmio` 的閱讀入口。它把環境、driver bind、BAR validation、MMIO與驗收排成一條路線；current source與QEMU/Linux官方文件仍是最終依據。

## 進入 Lab05 前

先確認：

- Lab00～04第一輪已完成；
- 你能說明`probe/remove`由driver core在match/bind時呼叫，不是shell直接call C function；
- 你知道resource lifetime不只是「反序free」，還要先停止並同步仍可能使用resource的執行者；
- Lab05～07的module build/load與測試位置是能看見EDU的Linux guest；
- guest內執行`lspci -Dnn | grep 1234:11e8`可看到EDU。

看不到EDU時，先讀：

1. [`../../qemu/README.md`](../../qemu/README.md)
2. [`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md)
3. ARM host補讀 [`../../qemu/arm-host-x86-guest.md`](../../qemu/arm-host-x86-guest.md)
4. [`qemu-edu-first-pass.md`](qemu-edu-first-pass.md)
5. [`linux-guest-05-to-07-walkthrough.md`](linux-guest-05-to-07-walkthrough.md)

## 建議閱讀順序

1. [`../onboarding/START-HERE.md`](../onboarding/START-HERE.md) 的`04 → 05`。
2. [`../concepts/pcie-primer.md`](../concepts/pcie-primer.md)：先建立PCI function、BAR resource、`__iomem`與posted-write completion的正確區分。
3. [`qemu-edu-first-pass.md`](qemu-edu-first-pass.md)。
4. [`../../qemu/README.md`](../../qemu/README.md)與bring-up checklist。
5. 在guest確認EDU與matching kernel headers。
6. [`../../labs/05-pci-edu-mmio/README.md`](../../labs/05-pci-edu-mmio/README.md)。
7. [`../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c`](../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c)：照下方source trace。
8. [`../../labs/05-pci-edu-mmio/test.sh`](../../labs/05-pci-edu-mmio/test.sh)：看它實際驗什麼。
9. [`../../labs/05-pci-edu-mmio/debug-checklist.md`](../../labs/05-pci-edu-mmio/debug-checklist.md)、[`../reference/debugging.md`](../reference/debugging.md)。
10. 最後讀 [`../onboarding/START-HERE.md`](../onboarding/START-HERE.md)，只先看後續地圖。

Companion `.c.md/.sh.md` 若與current source或accuracy audit不同，以current source為準。

## Host / guest邊界

| 位置 | 負責什麼 | 第一輪觀測 |
|---|---|---|
| host | 啟QEMU、選guest architecture/accelerator、掛`-device edu`、準備disk/network | QEMU process與launch arguments |
| guest | `lspci`、kernel headers、build `.ko`、`insmod/rmmod`、執行`test.sh` | `uname -m/-r`、`lspci -Dnn`、`dmesg`、sysfs |

macOS可以當QEMU host，但不能直接載入Linux kernel module。ARM host跑x86_64 guest通常走TCG，不是KVM/HVF硬體加速；速度慢不代表driver錯。

## 三輪閱讀法

### 第一輪：device如何遇到driver

QEMU EDU的PCI ID是`1234:11e8`。Driver的`dl_edu_mmio_ids`宣告match條件，`module_pci_driver()`註冊`struct pci_driver`。

兩種順序都成立：

```text
device先被列舉 → driver後註冊 → match/bind → probe
```

或：

```text
driver先註冊 → supported device後出現 → match/bind → probe
```

但實體PCIe hotplug是否可用取決於slot/controller/firmware/platform；不要把「driver與device順序無關」誤寫成「所有PCIe裝置都可任意熱插拔」。

### 第二輪：取得BAR0前先驗證

Current source流程：

```text
pci_enable_device()
→ 確認BAR0有IORESOURCE_MEM
→ 確認length涵蓋最高使用offset 0x04 + 4 bytes
→ pci_request_region()
→ pci_iomap()
→ ioread32/iowrite32
```

這裡有三個不同概念：

- `pci_resource_flags/len()`：查PCI core解析後的resource；
- `pci_request_region()`：宣告resource ownership，防止另一driver同時使用；
- `pci_iomap()`：建立`__iomem` mapping，供I/O accessor使用。

不要從configuration space手讀raw BAR後自行當成CPU address。

### 第三輪：liveness與cleanup的證據邊界

EDU liveness register在offset `0x04`：寫入pattern後讀回bitwise inverse。這驗證：

- driver已bind；
- BAR0 mapping可讀寫；
- 該EDU register的最小round-trip符合spec。

Read-back同時可讓先前posted MMIO write到達相應ordering point，但不能推廣成「任何device命令都已執行完成」。真實硬體仍依status、IRQ或completion protocol判斷。

Lab05尚未啟動IRQ/DMA producer，因此error/remove主要是：

```text
pci_iounmap
→ pci_disable_device
→ pci_release_region
```

到Lab06/07後，必須先quiesce非同步IRQ/DMA，才可沿resource dependency拆除。

## Source trace

依序看：

1. `DL_EDU_VENDOR_ID` / `DL_EDU_DEVICE_ID`。
2. `DL_EDU_IDENT_REG`、`DL_EDU_LIVENESS_REG`與`DL_EDU_MMIO_MIN_LEN`。
3. `struct dl_edu_mmio_dev`：byte-addressed `u8 __iomem *bar0`。
4. `dl_edu_mmio_ids`與`dl_edu_mmio_driver`。
5. `dl_edu_mmio_probe()`：enable → type/length validation → request → map。
6. identification read。
7. liveness write/read/compare。
8. `err_iounmap`／`err_disable_and_release`／`err_disable_device`，並分辨哪些 branch 尚未取得 BAR reservation。
9. `dl_edu_mmio_remove()`。
10. `module_pci_driver()`。

## 實驗

```sh
cd labs/05-pci-edu-mmio
./test.sh
```

手動第一輪：

```sh
lspci -Dnn | grep 1234:11e8
make
sudo insmod ./driver_lab_edu_mmio.ko
sudo dmesg | tail -n 100
sudo rmmod driver_lab_edu_mmio
make clean
```

不要在共享開發機械式執行`dmesg -C`；current smoke test以載入前後的log位置隔離本次訊息。

## Debug順序

1. `uname -m; uname -r`。
2. `test -e /lib/modules/$(uname -r)/build`。
3. `command -v lspci`。
4. `lspci -Dnn | grep 1234:11e8`。
5. 查`/sys/bus/pci/devices/*/{vendor,device}`。
6. 查該function是否已由別的driver bind。
7. `insmod`後看`probe start`或明確error。
8. 若probe進來，再查BAR flags/length、request conflict、mapping與liveness result。

## Smoke test能證明什麼

Current test檢查：

- Linux與`pciutils`；
- guest可列舉EDU；
- external module可build/load；
- driver sysfs bind成立；
- `probe start`、`BAR0 mapped`、`liveness check passed`；
- unload後driver sysfs entry消失。

它不能證明：

- 真實PHY/link品質；
- 所有BAR offsets都安全；
- relaxed/WC ordering；
- hot-unplug；
- IRQ/DMA teardown；
- production reset/error recovery。

## Self-check

1. `lspci`看不到EDU時，為什麼不該先改`probe()`？
2. `pci_request_region()`與`pci_iomap()`各自解決什麼？
3. Current source為什麼在map前檢查BAR type與minimum length？
4. EDU liveness read-back證明什麼、不證明什麼？
5. Driver先載入與device先出現，最後如何收斂到`probe()`？

<details>
<summary>參考答案</summary>

1. `lspci`依PCI enumeration/config access，不依你的driver；看不到表示還沒有可match的`pci_dev`，問題早於bind/probe。
2. Request region取得resource ownership；iomap建立可交給I/O accessor的`__iomem` mapping。前者不是mapping，後者也不取代ownership。
3. 防止把I/O-port或過短resource當成至少可32-bit存取offset `0x04`的MMIO window，避免越界或錯誤access type。
4. 它驗EDU BAR0最小write/read path與spec-defined inverse；read可作posted-write completion point，但不等於一般device command已完成，也不驗IRQ/DMA。
5. Driver core在driver註冊與device出現兩個事件後都會嘗試match/bind；只要function未被其他driver擁有且match/policy允許，便呼叫probe。實體hotplug能力另由平台決定。

</details>

## 第一輪可以略過

- TLP欄位與credit細節；
- MSI/MSI-X；
- DMA/IOMMU；
- AER/DPC/reset/power management；
- SR-IOV與multi-function；
- real-card firmware/clock/board quirks。

## 官方查證入口

- PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
- PCI support library: <https://docs.kernel.org/driver-api/pci/pci.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- External modules: <https://docs.kernel.org/kbuild/modules.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
