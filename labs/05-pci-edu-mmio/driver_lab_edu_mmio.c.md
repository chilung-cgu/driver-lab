# `driver_lab_edu_mmio.c` 詳解

## 結論

`labs/05-pci-edu-mmio/driver_lab_edu_mmio.c` 是 driver-lab 第一個 PCI driver。前面 Lab00-04 多半是 module 自己建立 debugfs 或 `/dev` 入口；Lab05 開始改成由 Linux PCI core 掃到 QEMU EDU device 後，把 matched device 交給 driver 的 `probe()`。

這份 driver 的主線是：

```text
PCI core 掃到 1234:11e8
  -> match dl_edu_mmio_ids
  -> call dl_edu_mmio_probe()
  -> pci_enable_device()
  -> pci_request_region(BAR0)
  -> pci_iomap(BAR0)
  -> ioread32 ident register
  -> iowrite32/ioread32 liveness register
```

Lab05 不建立 `/dev/driver_lab_*`，也不處理 IRQ/DMA。它只驗證三件事：

1. guest 內真的有 QEMU EDU PCI device。
2. driver 能 bind 並進 `probe()`。
3. BAR0 MMIO register window 可讀寫。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- 本檔 source、[`README.md`](README.md)、[`test.sh.md`](test.sh.md)、[`Makefile.md`](Makefile.md)。
- repo 既有導讀：[`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)、[`../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md`](../../docs/onboarding/START-HERE.md)、[`../../docs/guides/qemu-edu-first-pass.md`](../../docs/guides/qemu-edu-first-pass.md)。
- Linux kernel documentation：PCI driver flow、PCI support library、device I/O accessor、kbuild external modules。
- QEMU 官方 `EDU device` spec。

這裡不展開 PCI enumeration、config space、AER、power management、IOMMU、IRQ 或 DMA。那些會在 06/07 或更後面才值得深入。

## 先理解這份檔案在 repo 的位置

`05-07` 是同一條 PCI EDU 學習路線：

```text
05-pci-edu-mmio
  probe/remove + BAR0 + MMIO read/write

06-pci-edu-irq
  在 05 基礎上加 request_irq + interrupt acknowledge

07-pci-edu-dma
  在 06 基礎上加 coherent DMA buffer + DMA round-trip
```

相關檔案：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_edu_mmio.c`](driver_lab_edu_mmio.c) | Lab05 PCI/MMIO driver 本體 |
| [`test.sh.md`](test.sh.md) | Linux guest smoke test |
| [`Makefile.md`](Makefile.md) | external module kbuild 入口 |
| [`../../qemu/launch-edu-vm.sh`](../../qemu/launch-edu-vm.sh) | host 端啟動 QEMU EDU guest |
| [`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md) | guest 看不到 EDU 時的 bring-up 檢查 |

## 這份檔案要解決什麼問題？

PCI driver 的第一個問題不是「我要建立哪個 `/dev`」，而是：

```text
這顆硬體有沒有出現在 PCI bus？
kernel PCI core 有沒有把它交給我的 driver？
我有沒有安全拿到它的 BAR resource？
```

QEMU EDU 的官方 PCI ID 是：

```text
vendor = 0x1234
device = 0x11e8
```

所以 driver 先宣告：

```c
{ PCI_DEVICE(DL_EDU_VENDOR_ID, DL_EDU_DEVICE_ID) }
```

PCI core match 後才會呼叫 `dl_edu_mmio_probe()`。如果 guest 裡 `lspci -nn | grep 1234:11e8` 沒有輸出，這份 driver 的 `probe()` 根本不會被呼叫。

## 它怎麼被 build / load / 呼叫？

Build：

```sh
cd labs/05-pci-edu-mmio
make
```

產物：

```text
driver_lab_edu_mmio.ko
```

Load：

```sh
sudo insmod ./driver_lab_edu_mmio.ko
```

這時不是 shell 直接呼叫 `probe()`。流程是：

```text
insmod
  -> module_pci_driver() generated init
  -> pci_register_driver()
  -> PCI core 找目前 bus 上可 match 的 device
  -> 對 1234:11e8 呼叫 dl_edu_mmio_probe()
```

Unload：

```sh
sudo rmmod driver_lab_edu_mmio
```

流程：

```text
rmmod
  -> generated exit
  -> pci_unregister_driver()
  -> PCI core 對已 bind device 呼叫 dl_edu_mmio_remove()
```

## 讀 source 的主線

第一次請照這個順序讀：

1. `DL_EDU_VENDOR_ID` / `DL_EDU_DEVICE_ID`：先確認 driver match 哪顆 device。
2. `dl_edu_mmio_ids`：看 PCI ID table。
3. `dl_edu_mmio_driver`：看 `.probe` / `.remove` callback。
4. `dl_edu_mmio_probe()`：看 enable、request BAR、map BAR、讀寫 register。
5. `dl_edu_mmio_remove()`：看 cleanup 是否反向釋放。
6. `module_pci_driver()`：理解 module init/exit 是註冊/反註冊 PCI driver。

## 一、QEMU EDU ID 與 register offset

原始碼：

```c
#define DL_EDU_VENDOR_ID 0x1234
#define DL_EDU_DEVICE_ID 0x11e8

#define DL_EDU_BAR_INDEX 0
#define DL_EDU_IDENT_REG 0x00
#define DL_EDU_LIVENESS_REG 0x04
#define DL_EDU_LIVENESS_PATTERN 0xa5a55a5aU
```

對照 QEMU EDU spec：

| 常數 | 意義 |
|---|---|
| `0x1234:0x11e8` | QEMU EDU PCI vendor/device ID。 |
| `BAR0` | EDU 的 MMIO register window。 |
| `0x00` | identification register。 |
| `0x04` | liveness check register。 |

Lab05 只用 `0x00` 和 `0x04`。IRQ 和 DMA register 留到 06/07。

## 二、private state：每顆 device 一份

原始碼：

```c
struct dl_edu_mmio_dev {
	struct pci_dev *pdev;
	void __iomem *bar0;
	resource_size_t bar0_len;
	u32 ident;
	u32 liveness_pattern;
	u32 liveness_result;
};
```

這份 struct 是 driver 對一顆 matched EDU device 的 private state。

| 欄位 | 意義 |
|---|---|
| `pdev` | PCI core 交給 probe 的 `struct pci_dev`。 |
| `bar0` | `pci_iomap()` 後得到的 MMIO base。 |
| `bar0_len` | BAR0 長度，用於 log / sanity check。 |
| `ident` | 從 `0x00` identification register 讀回的值。 |
| `liveness_pattern` | 寫入 `0x04` 的測試 pattern。 |
| `liveness_result` | 從 `0x04` 讀回的結果。 |

`void __iomem *` 很重要：它不是一般 RAM pointer。對它做一般 `*ptr` 解參考不是正確 MMIO access 模型；要用 `ioread32()` / `iowrite32()`。

## 三、probe 一開始：配置 private state

原始碼：

```c
dl = devm_kzalloc(&pdev->dev, sizeof(*dl), GFP_KERNEL);
if (!dl)
	return -ENOMEM;

dl->pdev = pdev;
pci_set_drvdata(pdev, dl);
```

`devm_kzalloc()` 是 device-managed allocation。它把這份記憶體綁在 `pdev->dev` lifecycle 上；device detach 時會由 devres 機制釋放，所以 remove path 不需要手動 `kfree(dl)`。

`pci_set_drvdata(pdev, dl)` 把 private state 掛到 `pdev` 上。remove path 可以用：

```c
struct dl_edu_mmio_dev *dl = pci_get_drvdata(pdev);
```

把同一份 state 取回。

## 四、`pci_enable_device()`

原始碼：

```c
ret = pci_enable_device(pdev);
if (ret) {
	dev_err(&pdev->dev, "pci_enable_device failed: %d\n", ret);
	return ret;
}
```

這是接手 PCI device 的第一步之一。成功後才繼續 request BAR / map BAR。

第一輪不用展開 PCI config space 細節；先記住：

```text
enable device 失敗
  -> 不要繼續碰 BAR/MMIO
```

## 五、`pci_request_region()`：宣告使用 BAR0

原始碼：

```c
ret = pci_request_region(pdev, DL_EDU_BAR_INDEX, KBUILD_MODNAME);
if (ret) {
	dev_err(&pdev->dev, "pci_request_region BAR%d failed: %d\n",
			DL_EDU_BAR_INDEX, ret);
	goto err_disable_device;
}
```

參數角色：

| 參數 | 角色 |
|---|---|
| `pdev` | PCI core 交給 driver 的那顆 device。 |
| `DL_EDU_BAR_INDEX` | BAR index，Lab05 固定是 0。 |
| `KBUILD_MODNAME` | owner name，方便 resource/debug 顯示。 |

這一步是在宣告：

```text
這段 BAR0 resource 現在由我這個 driver 使用
```

如果失敗，driver 已經成功 enable device，所以要跳到 `err_disable_device`。

## 六、`pci_resource_len()` 與 `pci_iomap()`

原始碼：

```c
dl->bar0_len = pci_resource_len(pdev, DL_EDU_BAR_INDEX);
dl->bar0 = pci_iomap(pdev, DL_EDU_BAR_INDEX, 0);
if (!dl->bar0) {
	dev_err(&pdev->dev, "pci_iomap BAR%d failed\n", DL_EDU_BAR_INDEX);
	ret = -ENOMEM;
	goto err_release_region;
}
```

`pci_resource_len()` 讀 BAR0 長度。QEMU EDU spec 說它的 I/O memory 是 1 MB；driver 這裡把實際長度印出來，讓你能從 `dmesg` 確認。

`pci_iomap()` 把 BAR0 map 成 kernel 可用的 MMIO base：

```text
BAR0 resource
  -> pci_iomap()
  -> void __iomem *bar0
```

第三個參數 `0` 表示 map 整個 BAR。

## 七、讀 identification register

原始碼：

```c
dl->ident = ioread32(dl->bar0 + DL_EDU_IDENT_REG);
dev_info(&pdev->dev, "ident=0x%08x\n", dl->ident);
```

這是第一個 MMIO read。

重點：

- `dl->bar0` 是 MMIO base。
- `DL_EDU_IDENT_REG` 是 offset `0x00`。
- `ioread32()` 是 32-bit MMIO read accessor。

不要把它想成一般 C memory load。這是在透過 mapped BAR 讀 device register。

## 八、liveness check：最小 MMIO write/read round-trip

原始碼：

```c
dl->liveness_pattern = DL_EDU_LIVENESS_PATTERN;
iowrite32(dl->liveness_pattern, dl->bar0 + DL_EDU_LIVENESS_REG);
dl->liveness_result = ioread32(dl->bar0 + DL_EDU_LIVENESS_REG);
expected = ~dl->liveness_pattern;

if (dl->liveness_result != expected) {
	...
	ret = -EIO;
	goto err_iounmap;
}
```

QEMU EDU 的 liveness register 行為是：

```text
write value X to 0x04
read 0x04 returns bitwise inverse of X
```

所以 driver 寫：

```text
0xa5a55a5a
```

期待讀回：

```text
~0xa5a55a5a
```

這不是驗證完整 device 功能，而是最小證明：

```text
BAR0 mapping 可用
MMIO write 有到 device
MMIO read 有從 device 回來
register offset/endianness 至少符合這個測試
```

## 九、error path：反向釋放

原始碼：

```c
err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_release_region:
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
err_disable_device:
	pci_disable_device(pdev);
	return ret;
```

讀 error label 時問：

```text
目前成功拿到哪些 resource？
從最後拿到的開始釋放。
```

對照：

| 成功拿到 | 失敗後釋放 |
|---|---|
| `pci_enable_device()` | `pci_disable_device()` |
| `pci_request_region()` | `pci_release_region()` |
| `pci_iomap()` | `pci_iounmap()` |

如果 liveness check 失敗，已經成功 map BAR0，所以要先 `pci_iounmap()`。

## 十、remove：module unload / device removal path

原始碼：

```c
static void dl_edu_mmio_remove(struct pci_dev *pdev)
{
	struct dl_edu_mmio_dev *dl = pci_get_drvdata(pdev);

	if (dl && dl->bar0)
		pci_iounmap(pdev, dl->bar0);

	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pci_disable_device(pdev);
	pr_info("device removed for %s\n", pci_name(pdev));
}
```

remove 會在：

- module unload 時由 PCI core 呼叫。
- device 被移除 / unbind 時由 PCI core 呼叫。

這裡不需要 `kfree(dl)`，因為 `dl` 是 `devm_kzalloc()` 配的。

## 十一、PCI ID table 與 driver struct

原始碼：

```c
static const struct pci_device_id dl_edu_mmio_ids[] = {
	{ PCI_DEVICE(DL_EDU_VENDOR_ID, DL_EDU_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, dl_edu_mmio_ids);

static struct pci_driver dl_edu_mmio_driver = {
	.name = KBUILD_MODNAME,
	.id_table = dl_edu_mmio_ids,
	.probe = dl_edu_mmio_probe,
	.remove = dl_edu_mmio_remove,
};
```

`dl_edu_mmio_ids` 是 match 條件。最後的 `{ }` 是 terminator。

`MODULE_DEVICE_TABLE(pci, ...)` 會把 device ID 資訊匯出到 module metadata，讓 userspace/module loading 工具知道這個 module 支援哪些 PCI IDs。

`pci_driver` 則告訴 PCI core：

| 欄位 | 本 lab 的值 |
|---|---|
| `.name` | `driver_lab_edu_mmio` |
| `.id_table` | `dl_edu_mmio_ids` |
| `.probe` | `dl_edu_mmio_probe` |
| `.remove` | `dl_edu_mmio_remove` |

## 十二、`module_pci_driver()`

原始碼：

```c
module_pci_driver(dl_edu_mmio_driver);
```

這個 macro 會產生 module init/exit，幫你做：

```text
module init:
  pci_register_driver(&dl_edu_mmio_driver)

module exit:
  pci_unregister_driver(&dl_edu_mmio_driver)
```

所以你不會在 source 裡看到手寫 `module_init()` / `module_exit()`。

## source、test、觀測點對照

| 操作 | driver path | 觀測點 |
|---|---|---|
| `lspci -nn | grep 1234:11e8` | driver 尚未參與 | guest PCI bus 是否有 EDU |
| `insmod ./driver_lab_edu_mmio.ko` | generated init -> `pci_register_driver()` | module 載入是否成功 |
| PCI ID match | `dl_edu_mmio_probe()` | `dmesg` 的 `probe start` |
| BAR0 map | `pci_request_region()` + `pci_iomap()` | `BAR0 mapped, len=...` |
| liveness test | `iowrite32()` + `ioread32()` | `liveness check passed` |
| `rmmod driver_lab_edu_mmio` | generated exit -> `dl_edu_mmio_remove()` | driver sysfs entry 消失 |

## 常見卡點

- `probe()` 不會因為你打開 source 就自己跑；必須 guest 內有 matched PCI device。
- `lspci` 看不到 `1234:11e8` 時，先修 QEMU/guest，不要先改 driver。
- BAR0 是 MMIO register window，不是一般 RAM。
- `void __iomem *` 要用 `ioread32()` / `iowrite32()` access。
- `pci_request_region()` 和 `pci_iomap()` 是兩件事：前者宣告 resource ownership，後者建立 CPU 可用 mapping。
- liveness register 的 expected value 是 bitwise inverse，不是原值。
- `devm_kzalloc()` 配的 private state 不需要在 remove 手動 `kfree()`。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| QEMU EDU 的 PCI ID 是什麼？ | `1234:11e8`。 |
| `probe()` 什麼時候會被呼叫？ | PCI core 掃到 device 且 match `dl_edu_mmio_ids`，driver 註冊時或 device 出現時呼叫。 |
| BAR0 在 Lab05 是什麼？ | QEMU EDU 的 MMIO register window。 |
| `pci_iomap()` 回傳什麼？ | 可供 driver 用 `ioread32()` / `iowrite32()` 存取的 `void __iomem *` MMIO base。 |
| liveness check 驗什麼？ | 寫入 `0x04` 後讀回 bitwise inverse，確認最小 MMIO read/write path 可用。 |
| cleanup 順序是什麼？ | `pci_iounmap()`、`pci_release_region()`、`pci_disable_device()`。 |
| 為什麼 remove 不手動 `kfree(dl)`？ | `dl` 由 `devm_kzalloc()` 配置，綁在 device lifecycle 上。 |

## 查證來源

- Linux kernel documentation `How To Write Linux PCI Drivers`：PCI driver structure、ID table、probe/remove、常見初始化與 teardown 步驟。<https://docs.kernel.org/PCI/pci.html>
- Linux kernel documentation `PCI Support Library`：PCI helper API reference。<https://docs.kernel.org/driver-api/pci/pci.html>
- Linux kernel documentation `Bus-Independent Device Accesses`：MMIO accessor 與 device I/O 背景。<https://docs.kernel.org/driver-api/device-io.html>
- QEMU documentation `EDU device`：PCI ID、1 MB I/O memory、liveness register、IRQ/DMA register map。<https://www.qemu.org/docs/master/specs/edu.html>
