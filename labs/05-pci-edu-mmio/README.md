# 05 — QEMU EDU PCI/MMIO

## 目標

完成第一個最小PCI host driver閉環：

```text
EDU被guest列舉
→ PCI ID match / bind
→ probe()
→ validate、claim、map BAR0
→ identification與liveness MMIO
→ remove/error path釋放resource
```

這一關不做IRQ與DMA。先證明「裝置存在、driver接手、BAR正確、最小register path可用」。

> [!IMPORTANT]
> Current audit branch已通過static/build gate，但本branch的真正load/MMIO runtime仍需在你的Linux/QEMU guest執行`test.sh`並保存log。

## 先讀

1. [`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)
2. [`../../docs/guides/lab-05-study-order.md`](../../docs/guides/lab-05-study-order.md)
3. [`../../qemu/README.md`](../../qemu/README.md)
4. [`../../docs/guides/qemu-edu-first-pass.md`](../../docs/guides/qemu-edu-first-pass.md)
5. [`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md)
6. ARM host跑x86_64 guest時讀[`../../qemu/arm-host-x86-guest.md`](../../qemu/arm-host-x86-guest.md)

## 先備gate

在guest內：

```sh
uname -m
uname -r
lspci -Dnn | grep 1234:11e8
test -e "/lib/modules/$(uname -r)/build"
```

如果`lspci`看不到EDU，PCI core沒有可match的function，`probe()`不會進來。先修QEMU/guest，不要先改driver。

## Driver與device誰先出現？

兩種順序都可以：

```text
device先列舉，driver後註冊
```

或：

```text
driver先註冊，supported device後出現
```

Driver core在每一側出現時都可嘗試match/bind；只要function尚未被其他driver擁有且match/policy允許，便呼叫`probe()`。

這不表示所有實體PCIe裝置都支援任意hotplug。實體熱插拔還需要slot、controller、firmware與OS支援；QEMU `device_add`也需machine/device model支援。

## Current source流程

主要檔案：[`driver_lab_edu_mmio.c`](driver_lab_edu_mmio.c)

### 1. Match table

```c
static const struct pci_device_id dl_edu_mmio_ids[] = {
    { PCI_DEVICE(0x1234, 0x11e8) },
    { }
};
```

`MODULE_DEVICE_TABLE(pci, ...)`匯出modalias資訊；`module_pci_driver()`包裝register/unregister。`probe()`由driver core呼叫，不是userspace syscall callback。

### 2. Per-device state

```c
struct dl_edu_mmio_dev {
    struct pci_dev *pdev;
    u8 __iomem *bar0;
    resource_size_t bar0_len;
    ...
};
```

`bar0`用byte-addressed `u8 __iomem *`，因此`bar0 + 0x04`明確表示4-byte offset。不要用普通pointer dereference。

### 3. Enable與BAR validation

```text
pci_enable_device
→ pci_resource_flags(BAR0)含IORESOURCE_MEM
→ pci_resource_len(BAR0)至少涵蓋offset 0x04 + u32
```

Validation發生在mapping前，避免把I/O-port或過短resource當成可32-bit存取的MMIO window。

### 4. Ownership與mapping

```text
pci_request_region(BAR0)
→ pci_iomap(BAR0)
```

- `pci_request_region()`：claim resource ownership；
- `pci_iomap()`：建立`__iomem` mapping。

不要直接讀raw BAR register後自行當CPU address。Raw BAR encoding、PCI resource與kernel I/O mapping是三個不同view。

### 5. Identification與liveness

EDU register：

| Offset | 用途 |
|---|---|
| `0x00` | identification |
| `0x04` | liveness：讀回最近寫入值的bitwise inverse |

Current source：

```text
ioread32(ident)
→ iowrite32(pattern, liveness)
→ ioread32(liveness)
→ compare with ~pattern
```

該read-back同時可讓先前PCI posted write推進到same-device read ordering point；它不代表所有一般device command都完成。產品driver仍依datasheet的status、IRQ、completion queue或其他protocol判斷操作完成。

## Error與remove

Lab05尚未啟動IRQ/DMA producer，resource dependency較單純：

```text
pci_iounmap
→ pci_release_region
→ pci_disable_device
```

Probe每個失敗點只撤銷已成功取得的resource。到Lab06/07後，必須先mask/stop/synchronize IRQ與DMA，再釋放它們可能使用的memory/MMIO。

## Filesystem與觀測點

Lab05不建立`/dev/driver_lab_*`。主要觀測：

| 入口 | 用途 |
|---|---|
| `lspci -Dnn -d 1234:11e8` | EDU是否被列舉 |
| `lspci -Dnnk -d 1234:11e8` | 目前bind哪個driver |
| `/sys/bus/pci/devices/<BDF>/` | resource、vendor/device、driver link |
| `/sys/bus/pci/drivers/driver_lab_edu_mmio/` | driver registration/bind |
| `dmesg` | probe、BAR、ident、liveness、remove |

BDF是此次enumeration結果，不要寫死`00:03.0`或`00:04.0`。

## 執行

```sh
cd labs/05-pci-edu-mmio
./test.sh
```

Current test會：

1. 確認Linux guest與`lspci`；
2. 以ID與sysfs確認EDU；
3. 若同名module已載入則拒絕執行，不擅自卸載別人的state；
4. build module；
5. 記錄測試前kernel log行數；
6. `insmod`、確認bind；
7. `rmmod`、確認driver sysfs entry消失；
8. 只擷取本次新增log；
9. gate `probe start`、`BAR0 mapped`、`ident`、`liveness check passed`、`device removed`；
10. 若本次log含BUG/WARNING/KASAN/KCSAN/Oops/UAF則失敗。

Test不執行`dmesg -C`，避免清除整台系統共享的kernel log。如果ring buffer在測試中wrap，test會明確失敗，不能可靠隔離本次訊息。

## 成功訊號

類似：

```text
driver_lab_edu_mmio: probe start for 0000:00:04.0
driver_lab_edu_mmio 0000:00:04.0: BAR0 mapped, len=1048576 bytes
driver_lab_edu_mmio 0000:00:04.0: ident=0x...
driver_lab_edu_mmio 0000:00:04.0: liveness check passed
driver_lab_edu_mmio: device removed for 0000:00:04.0
05-pci-edu-mmio smoke test passed.
```

BDF與ident依環境/spec，不要求逐字相同。

## 失敗時

先讀[`debug-checklist.md`](debug-checklist.md)。順序：

```text
Linux/headers
→ EDU enumeration
→ existing driver ownership
→ module registration/probe return
→ BAR type/length
→ request conflict
→ iomap
→ register offset/width/spec
→ teardown
```

## 第一輪先略過

- TLP header/credit細節；
- MSI/MSI-X；
- DMA/IOMMU；
- AER/DPC；
- power/reset/hot-unplug；
- production device firmware與board-specific quirks。

## Self-check

1. `lspci`看不到EDU時，為什麼`probe()`不可能靠改source被叫到？
2. Raw BAR、PCI resource、`__iomem` mapping差在哪裡？
3. `pci_request_region()`與`pci_iomap()`各做什麼？
4. 為什麼map前要驗BAR type與length？
5. Liveness read-back能證明什麼，不能證明什麼？
6. Driver先載入與device先出現，match/bind行為如何收斂？

<details>
<summary>參考答案</summary>

1. `lspci`依PCI enumeration；沒有`pci_dev`就沒有match target，driver core不會呼叫probe。應先讓QEMU/guest列舉EDU。
2. Raw BAR是config register encoding；PCI resource是core解析/轉換後管理的range；`__iomem` mapping是driver交給I/O accessor的kernel I/O virtual view。
3. Request region取得resource ownership；iomap建立mapping。兩者不可互相取代。
4. 防止用錯I/O-vs-memory resource或越界存取未涵蓋的register。
5. 證明EDU最小BAR0 write/read與inverse語意；read可作posted-write completion point。它不驗IRQ/DMA，也不證明一般device operation已執行完成。
6. Driver core在driver註冊和device出現時都嘗試match；只要match且unbound/policy允許就bind並呼叫probe。實體hotplug能力是另一問題。

</details>

## 官方來源

- Linux PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
- PCI support library: <https://docs.kernel.org/driver-api/pci/pci.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
