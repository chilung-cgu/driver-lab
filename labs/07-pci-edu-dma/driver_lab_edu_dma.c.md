# `driver_lab_edu_dma.c` 詳解

## 結論

`labs/07-pci-edu-dma/driver_lab_edu_dma.c` 是 PCI EDU 路線的第三段。Lab05 讓 driver 能碰到 EDU BAR0 register，Lab06 讓 EDU 能用 interrupt 通知 driver，Lab07 則讓 EDU 自己搬資料，並用 interrupt 告訴 driver DMA 已完成。

這份 driver 的主線是：

```text
PCI core match QEMU EDU 1234:11e8
  -> probe()
  -> enable PCI device / map BAR0
  -> set 28-bit DMA mask
  -> allocate coherent DMA buffer
  -> request IRQ
  -> run QEMU EDU full-status ACK regression
  -> RAM -> EDU internal buffer
  -> wait DMA completion IRQ
  -> EDU internal buffer -> RAM
  -> wait DMA completion IRQ
  -> memcmp(tx, rx)
  -> optional one-page streaming TX -> EDU -> coherent-RX probe
```

這一關不是在學 CPU 自己 `memcpy()`。真正重點是：

```text
CPU 用 kernel virtual address 操作 buffer
device 用 dma_addr_t 操作同一塊或指定的 DMA address
driver 把 DMA address 寫進 device register
device 完成後用 IRQ 通知 driver
driver 最後驗資料是否真的搬對
```

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- 本檔 source、[`README.md`](README.md)、[`test.sh.md`](test.sh.md)、[`Makefile.md`](Makefile.md)。
- 05-07 導讀：[`../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md`](../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md)、[`../../docs/guides/linux-guest-05-to-07-walkthrough.md`](../../docs/guides/linux-guest-05-to-07-walkthrough.md)。
- Linux kernel documentation：Dynamic DMA mapping Guide、generic DMA API、PCI MSI/IRQ 相關文件。
- QEMU 官方 `EDU device` spec。

這裡解釋 Lab07 的 coherent DMA buffer、單一 IRQ vector、QEMU EDU DMA register 與 round-trip self-test，以及預設關閉的單頁 streaming-TX probe。它不展開 scatter-gather、長壽命 streaming ownership、IOMMU/PASID/SVA、DMA engine framework、cache maintenance 細節或真實 device descriptor ring。

## 先理解這份檔案在 repo 的位置

Lab05-07 是一條完整的 PCI device bring-up 主線：

```text
05-pci-edu-mmio
  driver 能 bind device，能 map BAR0，能讀寫 register

06-pci-edu-irq
  device 能 raise interrupt，handler 能 ack，probe 能等 completion

07-pci-edu-dma
  driver 能配置 DMA buffer，device 能搬資料，IRQ 到了後 driver 能驗結果
```

相關檔案：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_edu_dma.c`](driver_lab_edu_dma.c) | Lab07 PCI EDU DMA driver 本體 |
| [`test.sh.md`](test.sh.md) | Linux guest smoke test |
| [`test-swiotlb.sh.md`](test-swiotlb.sh.md) | 預設關閉的 forced-SWIOTLB streaming regression |
| [`Makefile.md`](Makefile.md) | external module kbuild 入口 |
| [`../05-pci-edu-mmio/driver_lab_edu_mmio.c.md`](../05-pci-edu-mmio/driver_lab_edu_mmio.c.md) | PCI/MMIO 前置 |
| [`../06-pci-edu-irq/driver_lab_edu_irq.c.md`](../06-pci-edu-irq/driver_lab_edu_irq.c.md) | IRQ/completion 前置 |
| [`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md) | guest 看不到 EDU 時的環境排查 |

## 這份檔案要解決什麼問題？

前兩關都還是 CPU 主動碰 register，或 device 通知 driver。Lab07 的新問題是：

```text
要怎麼把一段 memory 安全交給 device 搬？
device 搬完後，driver 怎麼知道？
driver 怎麼驗證搬回來的資料沒錯？
```

這裡有三種位置要分清楚：

| 名稱 | 在 source 裡 | 誰使用 | 意義 |
|---|---|---|---|
| CPU pointer | `dl->dma_buf` / `tx_buf` / `rx_buf`，以及 opt-in 的 `stream_tx_buf` | kernel CPU code | CPU 用來填 pattern、清 RX、在 ownership 回到 CPU 後做 `memcmp()`。 |
| DMA address | `dl->dma_handle` / `tx_dma` / `rx_dma`，以及 opt-in 的 `stream_tx_dma` | EDU device | driver 寫進 EDU DMA register，device 用它存取 guest RAM。 |
| EDU internal buffer | `DL_EDU_DEVICE_RAM_OFFSET` = `0x40000` | EDU device | QEMU EDU 裝置內部 4096-byte buffer 的 offset。 |

最容易犯的錯是把 `dma_addr_t` 當成 CPU pointer。Linux DMA 文件明確說，CPU 不能直接 reference `dma_addr_t`，因為 DMA address space 和 CPU virtual address space 可能透過 IOMMU/host bridge 轉換。

## 它怎麼被 build / load / 呼叫？

Build：

```sh
cd labs/07-pci-edu-dma
make
```

產物：

```text
driver_lab_edu_dma.ko
```

Load：

```sh
sudo insmod ./driver_lab_edu_dma.ko
```

呼叫流程：

```text
insmod
  -> module_pci_driver() generated init
  -> pci_register_driver()
  -> PCI core 找到 guest 裡的 1234:11e8
  -> call dl_edu_dma_probe()
  -> probe() 建立 PCI/MMIO/IRQ/DMA path
  -> probe() 執行 round-trip self-test
```

Unload：

```sh
sudo rmmod driver_lab_edu_dma
```

呼叫流程：

```text
rmmod
  -> pci_unregister_driver()
  -> PCI core call dl_edu_dma_remove()
  -> quiesce/free IRQ and vector
  -> free coherent mapping only after DMA quiescence is proven
  -> pci_iounmap()
  -> pci_disable_device()
  -> pci_release_region()
```

## 讀 source 的主線

第一次請照這個順序讀：

1. DMA/IRQ register constants：先知道 EDU 要哪些 source/destination/count/command register。
2. `struct dl_edu_dma_dev`：看 PCI/MMIO/IRQ/DMA resource 如何放在同一個 per-device state。
3. `dl_edu_dma_handler()`：DMA completion IRQ 到了如何 acknowledge 與 `complete()`。
4. `dl_edu_dma_wait_for_irq()` 與 `dl_edu_dma_wait_for_cmd_clear()`：看 driver 如何等「IRQ 到」和「device command bit 清掉」。
5. `dl_edu_dma_program_addrs()`：看 DMA address 如何寫進 EDU register。
6. `dl_edu_dma_run_once()`：看一次 DMA transaction 如何被設定、等待並檢查它自己的 status/count。
7. `dl_edu_dma_run_ack_regression()`：看 QEMU-specific unknown/all-bits status 如何在 DMA 前排空。
8. `dl_edu_dma_probe()`：看完整 resource acquisition、round-trip 與 error path。
9. `dl_edu_dma_remove()`：看正常 unload cleanup 順序。

## 一、EDU DMA register 與 command bit

原始碼：

```c
#define DL_EDU_IRQ_STATUS_REG 0x24
#define DL_EDU_IRQ_RAISE_REG 0x60
#define DL_EDU_IRQ_ACK_REG 0x64
#define DL_EDU_DMA_SRC_REG 0x80
#define DL_EDU_DMA_DST_REG 0x88
#define DL_EDU_DMA_COUNT_REG 0x90
#define DL_EDU_DMA_CMD_REG 0x98

#define DL_EDU_DEVICE_RAM_OFFSET 0x00040000ULL
#define DL_EDU_DMA_IRQ_MASK 0x00000100U
#define DL_EDU_UNKNOWN_IRQ_MASK 0x80000000U
#define DL_EDU_ALL_IRQ_MASK (~0U)
#define DL_EDU_DMA_CMD_START 0x01U
#define DL_EDU_DMA_CMD_FROM_DEVICE 0x02U
#define DL_EDU_DMA_CMD_IRQ 0x04U
```

對照 QEMU EDU spec：

| Register / bit | 意義 |
|---|---|
| `0x80` source address | DMA source。 |
| `0x88` destination address | DMA destination。 |
| `0x90` transfer count | 搬幾個 bytes。 |
| `0x98 bit 0` start | 開始 DMA transfer。 |
| `0x98 bit 1` direction | `0`: RAM -> EDU；`1`: EDU -> RAM。 |
| `0x98 bit 2` IRQ | transfer 完成後 raise `0x100` interrupt。 |
| `0x40000` | EDU 內部 4096-byte buffer offset。 |

DMA 前也會用到 IRQ status/raise/ACK register：`0x24` 是 per-device status
word，`0x60` 由 QEMU-only regression 寫入 `0x80000000` 與 `0xffffffff`，
`0x64` 則接收完整非零 status word 作為 ACK。這兩個 injected value 不是
可攜硬體協定的宣稱；它們是用來驗證 QEMU EDU model 可任意 raise 的行為。

Lab07 的 round-trip 是：

```text
tx_dma -> EDU 0x40000
  command = START | IRQ

EDU 0x40000 -> rx_dma
  command = START | FROM_DEVICE | IRQ
```

## 二、private state：PCI、IRQ、DMA 都放在同一個 lifecycle

原始碼：

```c
struct dl_edu_dma_dev {
	struct pci_dev *pdev;
	void __iomem *bar0;
	int irq_vector;
	unsigned long irq_flags;
	struct completion irq_done;
	u32 last_irq_status;
	u32 irq_count;
	void *dma_buf;
	dma_addr_t dma_handle;
	u8 *tx_buf;
	u8 *rx_buf;
	void *stream_tx_buf;
	dma_addr_t stream_tx_dma;
	bool stream_tx_mapped;
};
```

欄位分組：

| 分組 | 欄位 | 角色 |
|---|---|---|
| PCI/MMIO | `pdev`, `bar0` | PCI core device 與 BAR0 MMIO base。 |
| IRQ | `irq_vector`, `irq_flags`, `irq_done`, `last_irq_status`, `irq_count` | DMA completion interrupt path。 |
| Coherent DMA | `dma_buf`, `dma_handle`, `tx_buf`, `rx_buf` | baseline coherent allocation 與 tx/rx 切片。 |
| Optional streaming DMA | `stream_tx_buf`, `stream_tx_dma`, `stream_tx_mapped` | `streaming_probe=1` 時的一頁 `DMA_TO_DEVICE` mapping 與 ownership state。 |

`dma_buf` 和 `dma_handle` 是一組，必須一起保存，之後 `dma_free_coherent()` 要用同一組值釋放。若 opt-in streaming mapping 成功，CPU page、`dma_addr_t` 與 mapped flag 也必須一起保存：timeout/reset failure 前不能先 `dma_unmap_single()` 或 `free_page()`，否則 device 仍可能使用該 address。

## 三、DMA completion IRQ handler：非零 status 都先完整 ACK

原始碼的核心語意：

```c
status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
if (!status)
	return IRQ_NONE;

iowrite32(status, dl->bar0 + DL_EDU_IRQ_ACK_REG);
(void)ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);

known = status & DL_EDU_KNOWN_IRQ_MASK;
if (status & ~DL_EDU_KNOWN_IRQ_MASK)
	dev_warn_ratelimited(...unknown bits...);
else if (known & ~DL_EDU_DMA_IRQ_MASK)
	dev_warn_ratelimited(...unexpected known bits...);
if (!(known & DL_EDU_DMA_IRQ_MASK))
	return IRQ_HANDLED;

dl->last_irq_status = status;
dl->irq_count++;
complete(&dl->irq_done);
return IRQ_HANDLED;
```

Lab06 的 normal completion bit 是 `0x2`；Lab07 的 DMA completion bit 是
`0x100`。兩者同樣以自己的 per-device status register 判斷 ownership：
**只有 status 為 0 才回 `IRQ_NONE`**。QEMU EDU 的 arbitrary raise value
表示 unknown nonzero status 仍是 EDU 的 pending source；若只 ACK known
mask，它可能讓 legacy INTx 持續 asserted。

完整 ACK 後，known mask 只負責語意：factorial `0x1`、Lab06 self-test
`0x2` 與 unknown-only status 都記 diagnostic 並回 `IRQ_HANDLED`，但不
complete DMA waiter。只有 status 包含 `0x100` 才保存完整 status、遞增
`irq_count`、`complete()`。因此 `0xffffffff` 會受控完成一次（它含
`0x100`），而 `0x80000000` 不會。

## 四、等待 DMA：IRQ 和 command bit 是兩個觀測點

Lab07 有兩個等待 helper。

第一個等 IRQ：

```c
static int dl_edu_dma_wait_for_irq(struct dl_edu_dma_dev *dl, const char *phase)
{
	unsigned long timeout_jiffies;

	timeout_jiffies = msecs_to_jiffies(DL_EDU_DMA_WAIT_TIMEOUT_MS);
	if (!wait_for_completion_timeout(&dl->irq_done, timeout_jiffies)) {
		dev_err(&dl->pdev->dev, "%s timed out after %u ms\n",
			phase, DL_EDU_DMA_WAIT_TIMEOUT_MS);
		return -ETIMEDOUT;
	}

	return 0;
}
```

第二個等 command bit 清掉：

```c
static int dl_edu_dma_wait_for_cmd_clear(struct dl_edu_dma_dev *dl,
					 const char *phase)
{
	unsigned long deadline;

	deadline = jiffies + msecs_to_jiffies(DL_EDU_DMA_WAIT_TIMEOUT_MS);
	while (time_before(jiffies, deadline)) {
		u32 cmd;

		cmd = ioread32(dl->bar0 + DL_EDU_DMA_CMD_REG);
		if (!(cmd & DL_EDU_DMA_CMD_START))
			return 0;

		usleep_range(1000, 2000);
	}

	dev_err(&dl->pdev->dev, "%s command bit did not clear in time\n", phase);
	return -ETIMEDOUT;
}
```

為什麼要兩個？

| 觀測點 | 代表 |
|---|---|
| IRQ completion | handler 收到 EDU DMA completion interrupt。 |
| command bit cleared | EDU DMA command register 的 start bit 已回到 idle。 |

只等 IRQ 可能不知道 device command 狀態是否也回到 idle；只 poll command bit 又練不到 Lab06 建立的 IRQ/completion path。Lab07 兩者都做，讓 self-test 更有教學價值。

## 五、`dl_edu_dma_program_addrs()`：寫的是 DMA address

原始碼：

```c
static void dl_edu_dma_program_addrs(struct dl_edu_dma_dev *dl,
				     dma_addr_t src, dma_addr_t dst)
{
	iowrite32(lower_32_bits(src), dl->bar0 + DL_EDU_DMA_SRC_REG);
	iowrite32(lower_32_bits(dst), dl->bar0 + DL_EDU_DMA_DST_REG);
}
```

這裡的 `src` / `dst` 不是 CPU pointer，而是 device 要用的 DMA address 或 EDU internal buffer offset。

Lab07 先設定 28-bit DMA mask：

```c
dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
```

所以 `lower_32_bits()` 在這個 lab 裡符合目前設計。QEMU EDU 預設只支援 28-bit DMA address；這也是為什麼 source 註解說先用 coherent DMA 配低位址，並以 32-bit MMIO access 寫入 source/destination。

## 六、`dl_edu_dma_run_once()`：一次 DMA transaction

原始碼：

```c
static int dl_edu_dma_run_once(struct dl_edu_dma_dev *dl, dma_addr_t src,
			       dma_addr_t dst, u32 cmd, const char *phase)
{
	u32 expected_irq_count;
	int ret;

	if (!dl->bus_master_enabled)
		return -EIO;
	expected_irq_count = dl->irq_count + 1U;
	reinit_completion(&dl->irq_done);
	dl_edu_dma_program_addrs(dl, src, dst);
	iowrite32(DL_EDU_DMA_BUFFER_BYTES, dl->bar0 + DL_EDU_DMA_COUNT_REG);
	iowrite32(cmd, dl->bar0 + DL_EDU_DMA_CMD_REG);

	ret = dl_edu_dma_wait_for_irq(dl, phase);
	if (ret)
		return ret;

	ret = dl_edu_dma_wait_for_cmd_clear(dl, phase);
	if (ret)
		return ret;
	if (dl->last_irq_status != DL_EDU_DMA_IRQ_MASK ||
	    dl->irq_count != expected_irq_count)
		return -EIO;

	dev_info(&dl->pdev->dev, "%s finished\n", phase);
	return 0;
}
```

白話說，這個 helper 是把一次 DMA 工作單交給 EDU：

```text
清空 completion
確認 BME 已經在 handler/state 都 ready 後啟用
記住本次預期的相對 irq_count
寫 source address
寫 destination address
寫 count
寫 command 啟動 DMA
等 completion IRQ
等 command start bit 清掉
要求 last IRQ status 恰為 0x100，且 count 只增加一次
印出 phase finished
```

`phase` 只是 log 名稱，例如：

```text
ram-to-edu transfer
edu-to-ram transfer
```

timeout 時 log 可以直接指出是哪一段壞。

這個 status/count assertion 是 normal DMA 的證據層，不能被前面的
`0xffffffff` regression completion 滿足；regression 在進入第一段 DMA 前
已明確 `reinit_completion()` 排空該 completion。

## 七、測試 pattern：最後要能 `memcmp()`

原始碼：

```c
static void dl_edu_dma_fill_pattern(u8 *buf)
{
	size_t i;

	for (i = 0; i < DL_EDU_DMA_BUFFER_BYTES; ++i)
		buf[i] = (u8)(i ^ 0x5a);
}
```

baseline 在 `tx_buf` 填固定 pattern，並把 `rx_buf` 清成 0。這樣 round-trip 後：

```c
memcmp(dl->tx_buf, dl->rx_buf, DL_EDU_DMA_BUFFER_BYTES)
```

才有意義。

如果 `rx_buf` 一開始沒有清，某些錯誤可能被舊資料掩蓋。這裡清零是為了讓「資料真的被搬回來」更容易驗證。

## 八、probe 前半：PCI/MMIO 和 DMA mask

原始碼：

```c
ret = pci_enable_device(pdev);
...
pci_clear_master(pdev);
pci_intx(pdev, 0);
...
ret = pci_request_region(pdev, DL_EDU_BAR_INDEX, KBUILD_MODNAME);
...
dl->bar0 = pci_iomap(pdev, DL_EDU_BAR_INDEX, 0);
...
ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
```

前面是 Lab05/Lab06 的基礎：

| 步驟 | 目的 |
|---|---|
| `pci_enable_device()` | 啟用 PCI device。 |
| `pci_clear_master()` / `pci_intx(pdev, 0)` | bring-up 初期先禁止 bus-master traffic 與 legacy INTx，直到 buffers/IRQ state 都 ready。 |
| `pci_request_region()` | 宣告 BAR0 resource ownership。 |
| `pci_iomap()` | map BAR0 MMIO register window。 |

此時 BME 仍是關的。driver 先驗證 BAR/identity、停掉 factorial producer、
確認 inherited DMA command idle、清掉殘留非零 status，才設定 DMA mask 與
配置 coherent buffer。這避免尚未有 buffer/handler 時就讓 device 主動 DMA。

接著是 Lab07 的第一個重點：

```c
dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
```

QEMU EDU 官方文件說預設 DMA mask 是 28 bits，也就是 256 MiB。driver 必須告訴 DMA API 這顆 device 的可定址範圍。Linux DMA 文件也明確說：如果 `dma_set_mask*()` 失敗，不能繼續對這個 device 做 DMA，否則行為未定。

## 九、`dma_alloc_coherent()`：一個 allocation，兩種位址

原始碼：

```c
dl->dma_buf = dma_alloc_coherent(&pdev->dev,
				 DL_EDU_DMA_BUFFER_BYTES * 2,
				 &dl->dma_handle, GFP_KERNEL);
if (!dl->dma_buf) {
	dev_err(&pdev->dev, "dma_alloc_coherent failed\n");
	ret = -ENOMEM;
	goto err_iounmap;
}

dl->tx_buf = dl->dma_buf;
dl->rx_buf = (u8 *)dl->dma_buf + DL_EDU_DMA_BUFFER_BYTES;
tx_dma = dl->dma_handle;
rx_dma = dl->dma_handle + DL_EDU_DMA_BUFFER_BYTES;
```

這段是 Lab07 最重要的概念。

`dma_alloc_coherent()` 回傳兩個值：

| 值 | source 裡 | 誰用 | 用途 |
|---|---|---|---|
| CPU virtual address | `dl->dma_buf` | CPU/kernel code | 填 pattern、清 rx、`memcmp()`。 |
| DMA address | `dl->dma_handle` | EDU device | 寫進 DMA source/destination register。 |

接著 driver 把 512 bytes coherent buffer 切成兩半：

```text
dma_buf
  +0                     tx_buf: 256 bytes
  +256                   rx_buf: 256 bytes
```

DMA address 也對應切成兩半：

```text
dma_handle
  +0                     tx_dma
  +256                   rx_dma
```

請注意：

```text
tx_buf  是 CPU pointer
tx_dma  是 device DMA address

rx_buf  是 CPU pointer
rx_dma  是 device DMA address
```

它們描述同一塊 coherent allocation 的不同視角，但不能混用。

## 十、IRQ setup：沿用 Lab06

原始碼：

```c
ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
...
dl->irq_vector = pci_irq_vector(pdev, 0);
dl->irq_flags = (pdev->msi_enabled || pdev->msix_enabled) ? 0 : IRQF_SHARED;
...
ret = request_irq(dl->irq_vector, dl_edu_dma_handler, dl->irq_flags,
		  KBUILD_MODNAME, dl);
```

Lab07 的 DMA completion 需要 IRQ，所以它沿用 Lab06 的 IRQ setup。

這裡仍然只要一條 IRQ vector。`request_irq()` 的最後一個參數傳 `dl`，handler 收到後可以找回 BAR0、completion、IRQ count 等 state。

`request_irq()` 成功後，legacy mode 先解除 INTx mask；接著不分 IRQ mode 都
以 `pci_set_master()` 啟用 BME，並記錄 `bus_master_enabled = true`。這是
DMA 和 MSI 都可能需要的 device-originated memory-transaction 權限，故被
刻意放在 vector、handler、completion 與 coherent buffer 全部 ready 的最後
時點。

下一步不是立刻 DMA，而是 `dl_edu_dma_run_ack_regression()`：它先送
unknown-only `0x80000000`，要求 status 清零但不 complete/count；再送
all-bits `0xffffffff`，要求完整 ACK、一次 completion、`last_irq_status`
恰為 `0xffffffff`，並在第一個 DMA command 前 `reinit_completion()`。
這是 QEMU EDU 專用 regression，不是要求真實裝置接受任意 status bit。

## 十一、兩段 DMA round-trip

第一段：RAM -> EDU。

原始碼：

```c
ret = dl_edu_dma_run_once(dl, tx_dma, DL_EDU_DEVICE_RAM_OFFSET,
			  DL_EDU_DMA_CMD_START | DL_EDU_DMA_CMD_IRQ,
			  "ram-to-edu transfer");
```

解讀：

```text
source      = tx_dma                 (guest RAM coherent buffer)
destination = 0x40000                (EDU internal buffer)
command     = START | IRQ            (direction bit 0: RAM -> EDU)
```

第二段：EDU -> RAM。

原始碼：

```c
ret = dl_edu_dma_run_once(dl, DL_EDU_DEVICE_RAM_OFFSET, rx_dma,
			  DL_EDU_DMA_CMD_START |
			  DL_EDU_DMA_CMD_FROM_DEVICE |
			  DL_EDU_DMA_CMD_IRQ,
			  "edu-to-ram transfer");
```

解讀：

```text
source      = 0x40000                (EDU internal buffer)
destination = rx_dma                 (guest RAM coherent buffer)
command     = START | FROM_DEVICE | IRQ
```

`DL_EDU_DMA_CMD_FROM_DEVICE` 的命名要從 QEMU EDU 的觀點理解：direction bit 1 代表從 EDU 裝置內部 buffer 搬回 RAM。

## 十二、`memcmp()`：驗證不是只有 IRQ 成功

原始碼：

```c
if (memcmp(dl->tx_buf, dl->rx_buf, DL_EDU_DMA_BUFFER_BYTES) != 0) {
	dev_err(&pdev->dev, "round-trip compare failed\n");
	ret = -EIO;
	goto err_quiesce;
}

dev_info(&pdev->dev,
	 "round-trip compare passed, irq_count=%u last_status=0x%08x\n",
	 dl->irq_count, dl->last_irq_status);
```

前面兩段 DMA 都完成後，最後才比對資料。

這個檢查回答：

```text
不是只有 request_irq 成功
不是只有 handler 進來
不是只有 command bit 清掉
而是 tx 的內容真的經過 EDU 內部 buffer 又回到 rx
```

每一次 normal DMA 都自行 assert「status 恰為 `0x100`、count 從該 command
前增加一次」。在這個固定 QEMU regression 加兩段 DMA 的正常路徑上，最後
log 的 count 會包含 all-bits 的受控 completion 與兩次 DMA completion；文件
不把它誤寫成只會有兩次。

## 十三、`streaming_probe=1`：有界的 SWIOTLB streaming TX 驗證

正常 module load 不會跑這條路。專用 `test-swiotlb.sh` 才會以 module parameter 啟用：

```text
streaming TX CPU page
→ dma_map_single(... PAGE_SIZE, DMA_TO_DEVICE)
→ EDU local RAM
→ dma_unmap_single(... PAGE_SIZE, DMA_TO_DEVICE)
→ EDU local RAM → coherent RX
→ compare first 256 bytes
```

這個形狀刻意小：whole-page mapping 避免 partial cache-line 的教學雜訊，EDU 只讀第一個 256 bytes，coherent RX 仍沿用既有的 device-to-CPU completion/order path。它不是把 coherent allocation 誤稱為 streaming，也不是完整 SG/descriptor-ring 設計。

mapping 成功後，CPU 不再讀寫 `stream_tx_buf`，直到第一段 transfer 完成且 `dma_unmap_single()` 把 ownership 歸還。`dma_need_sync()` 的 log 只是固定環境的交叉檢查；它不保證 SWIOTLB bounce，也不替代 map/unmap 的 ownership contract。專用 test 同時要求 Linux 的 `swiotlb_bounced` tracepoint 顯示 4096-byte `FORCE` event，才能把「forced SWIOTLB」列為已觀察。

如果 streaming command timeout，`stream_tx_mapped` 仍保持 true。既有 quiesce path 先關 BME、確認 command idle 或 reset，只有確定安全後 `dl_edu_dma_free_streaming()` 才 unmap/free。若 reset 也不能證明 quiesce，source 和 coherent allocation 一樣故意保留 streaming page，避免 DMA UAF。

## 十四、error path：先 quiesce DMA/IRQ，再拆 PCI resource

原始碼：

```c
err_quiesce:
	dl_edu_dma_quiesce(dl);
err_free_vectors:
	dl_edu_dma_free_irq_vectors(dl);
err_free_dma:
	dl_edu_dma_free_buffer(dl);
err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_release_region:
	pci_disable_device(pdev);
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	return ret;
err_disable_device:
	pci_disable_device(pdev);
	return ret;
```

成功取得 resource 的順序：

```text
pci_enable_device()
pci_request_region()
pci_iomap()
dma_alloc_coherent()
pci_alloc_irq_vectors()
request_irq()
```

失敗釋放順序：

```text
dl_edu_dma_quiesce()
dl_edu_dma_free_irq_vectors()
dl_edu_dma_free_streaming()
dl_edu_dma_free_buffer()
pci_iounmap()
pci_disable_device()
pci_release_region()
```

這個順序先停止 producer、清完整 status、關 BME、確認 DMA idle 或 reset，
再同步/free IRQ。只有 quiescence 已證明時才釋放 coherent mapping；否則 source
刻意保留 mapping，避免 device 尚可能持有 DMA address 時造成 use-after-free。
最後的 PCI resource 順序是固定的 `pci_iounmap()` →
`pci_disable_device()` → `pci_release_region()`。

## 十五、remove：正常 unload 的 cleanup

原始碼：

```c
static void dl_edu_dma_remove(struct pci_dev *pdev)
{
	struct dl_edu_dma_dev *dl = pci_get_drvdata(pdev);

	dl_edu_dma_quiesce(dl);
	dl_edu_dma_free_irq_vectors(dl);
	dl_edu_dma_free_streaming(dl);
	dl_edu_dma_free_buffer(dl);
	if (dl && dl->bar0)
		pci_iounmap(pdev, dl->bar0);
	pci_disable_device(pdev);
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pr_info("device removed for %s\n", pci_name(pdev));
}
```

正常 `rmmod` 時，PCI core 呼叫 `remove()`。清理順序和 error path 一致：

```text
quiesce source / BME / in-flight DMA / IRQ
IRQ vectors
streaming mapping/page only when safe
coherent DMA buffer only when safe
MMIO mapping
PCI device enable state
BAR region
```

`dl` 不需要手動 `kfree()`，因為它是 `devm_kzalloc()` 配的。

## 十六、PCI ID table、driver struct、module macro

原始碼：

```c
static const struct pci_device_id dl_edu_dma_ids[] = {
	{ PCI_DEVICE(DL_EDU_VENDOR_ID, DL_EDU_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, dl_edu_dma_ids);

static struct pci_driver dl_edu_dma_driver = {
	.name = KBUILD_MODNAME,
	.id_table = dl_edu_dma_ids,
	.probe = dl_edu_dma_probe,
	.remove = dl_edu_dma_remove,
};

module_pci_driver(dl_edu_dma_driver);
```

和 Lab05/06 一樣，PCI core 用 `id_table` match QEMU EDU `1234:11e8`。match 後呼叫 `probe()`；module unload 或 device unbind 時呼叫 `remove()`。

## source、test、觀測點對照

| 操作 | driver path | 觀測點 |
|---|---|---|
| `lspci -Dnn | grep 1234:11e8` | driver 尚未參與 | guest PCI bus 是否有 EDU |
| `insmod ./driver_lab_edu_dma.ko` | generated init -> `pci_register_driver()` | module 是否載入 |
| DMA mask | `dma_set_mask_and_coherent()` | `dma mask configured to 28 bits` |
| coherent allocation | `dma_alloc_coherent()` | `coherent buffer allocated: cpu=... dma=...` |
| IRQ setup | `pci_alloc_irq_vectors()` + `request_irq()` | `/proc/interrupts` 有 driver 名稱 |
| controlled ACK regression | `dl_edu_dma_run_ack_regression()` | `0x80000000` 無 completion；`0xffffffff` 完整 ACK/一次 completion 的 dmesg evidence |
| RAM -> EDU | `dl_edu_dma_run_once(... tx_dma, 0x40000, START | IRQ)` | `ram-to-edu transfer finished` |
| EDU -> RAM | `dl_edu_dma_run_once(... 0x40000, rx_dma, START | FROM_DEVICE | IRQ)` | `edu-to-ram transfer finished` |
| data verify | `memcmp(tx_buf, rx_buf, 256)` | `round-trip compare passed` |
| forced-SWIOTLB only | `streaming_probe=1` + `dl_edu_dma_run_streaming_probe()` | `swiotlb_bounced ... size=4096 FORCE` plus streaming-to-EDU-to-coherent-RX compare |
| unload | `dl_edu_dma_remove()` | PCI driver sysfs directory 消失 |

## 常見卡點

- `probe()` 沒進來：先確認 guest 內 `lspci -Dnn | grep 1234:11e8` 有輸出。
- `dma_set_mask_and_coherent()` 失敗：不能繼續 DMA；先確認 EDU 預設 28-bit mask 與 guest DMA addressing 是否相容。
- `dma_alloc_coherent()` 失敗：先看 DMA mask 是否成功、guest memory 是否足夠、錯誤 log 是否明確。
- DMA timeout：先分辨是 `wait_for_completion_timeout()` timeout，還是 command start bit 沒清掉。
- `unknown-status regression completed or changed IRQ state`：`0x80000000` 不應含 DMA bit；檢查 handler 是否把 nonzero unknown status 完整 ACK、但沒有 `complete()` / 改 count。
- `all-status regression recorded status=... count=...`：`0xffffffff` 必須完整 ACK，並因含 `0x100` 受控完成一次；確認狀態不是被 `~0U` 當成 absent device。
- `recorded IRQ status=... count=...`：某個 normal DMA 的 `last_irq_status` 不是 `0x100`，或 count 沒從該 command 前恰增 1；不可用先前 regression completion 掩蓋。
- `round-trip compare failed`：優先核對 source/destination、direction bit、count、EDU internal buffer offset。
- forced-SWIOTLB probe 沒有 `FORCE` trace：不要用 `dma_need_sync` 猜；先確認獨立 guest 的精確 `swiotlb=force` cmdline、無 IOMMU group、tracefs event 與 4096-byte streaming map。
- 不要把 `dma_handle` 當 CPU pointer；CPU 要用 `dma_buf` / `tx_buf` / `rx_buf`。
- 不要把 `0x40000` 當 guest RAM address；它是 EDU device 內部 buffer offset。
- `make clean` 不會卸載 module，也不會釋放 DMA buffer。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| `dma_alloc_coherent()` 回傳哪兩種位址？ | CPU virtual address 和 device 要用的 `dma_addr_t`。 |
| CPU 可以直接 dereference `dma_addr_t` 嗎？ | 不可以；CPU 用 `void *`/`u8 *`，device 用 DMA address。 |
| Lab07 為什麼用 `DMA_BIT_MASK(28)`？ | QEMU EDU 預設只支援 28-bit DMA address。 |
| `DL_EDU_DEVICE_RAM_OFFSET` 是什麼？ | EDU device 內部 4096-byte buffer 的 offset `0x40000`。 |
| RAM -> EDU 的 command 是什麼？ | `START | IRQ`，direction bit 為 0。 |
| EDU -> RAM 的 command 是什麼？ | `START | FROM_DEVICE | IRQ`。 |
| handler 什麼時候回 `IRQ_NONE`？ | 只有 EDU status register 為 `0`；任何 nonzero status 先完整 ACK，再由 known mask 決定是否完成 DMA waiter。 |
| 為什麼先跑 `0x80000000` / `0xffffffff` regression？ | 驗證 QEMU EDU 的 arbitrary raise value 會完整 ACK，且 unknown-only 不 complete、all-bits completion 在 normal DMA 前被排空。 |
| 為什麼要等 IRQ 又要等 command bit clear？ | IRQ 證明 completion event 到了；command bit clear 證明 device command 狀態回到 idle。 |
| `memcmp()` 驗證什麼？ | 驗證 tx pattern 經 RAM -> EDU -> RAM round-trip 後和 rx buffer 一致。 |
| 為什麼 coherent test 不等於 SWIOTLB evidence？ | coherent allocation 與 streaming map 是不同 DMA API path；forced-SWIOTLB test 必須額外取得 streaming payload compare 與 `swiotlb_bounced ... FORCE` trace。 |
| cleanup 為什麼先 `free_irq()`？ | 避免 IRQ handler 在 DMA/MMIO resource 釋放後仍被呼叫。 |

## 查證來源

- Linux kernel documentation `Dynamic DMA mapping Guide`：CPU/DMA address 差異、`dma_set_mask_and_coherent()`、coherent DMA mapping、`dma_alloc_coherent()` 兩個回傳值與 `dma_free_coherent()`。<https://docs.kernel.org/core-api/dma-api-howto.html>
- Linux kernel documentation `Dynamic DMA mapping using the generic device`：`dma_addr_t` 不可由 CPU 直接 reference、`dma_alloc_coherent()` / `dma_free_coherent()` API 語意。<https://docs.kernel.org/core-api/dma-api.html>
- QEMU documentation `EDU device`：預設 28-bit `dma_mask`、DMA source/destination/count/command register、direction bit、completion IRQ bit、`0x40000` 內部 buffer。<https://www.qemu.org/docs/master/specs/edu.html>
- Linux source `swiotlb_bounced` trace event：device、DMA address、size 與 `FORCE` 欄位的定義。<https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/trace/events/swiotlb.h?h=v6.8>
