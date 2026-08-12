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
  -> RAM -> EDU internal buffer
  -> wait DMA completion IRQ
  -> EDU internal buffer -> RAM
  -> wait DMA completion IRQ
  -> memcmp(tx, rx)
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
- 05-07 導讀：[`../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md`](../../docs/onboarding/START-HERE.md)、[`../../docs/guides/linux-guest-05-to-07-walkthrough.md`](../../docs/guides/linux-guest-05-to-07-walkthrough.md)。
- Linux kernel documentation：Dynamic DMA mapping Guide、generic DMA API、PCI MSI/IRQ 相關文件。
- QEMU 官方 `EDU device` spec。

這裡只解釋 Lab07 實際用到的 coherent DMA buffer、單一 IRQ vector、QEMU EDU DMA register 與 round-trip self-test。不展開 streaming DMA API、scatter-gather、IOMMU/PASID/SVA、DMA engine framework、cache maintenance 細節或真實 device descriptor ring。

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
| CPU pointer | `dl->dma_buf` / `tx_buf` / `rx_buf` | kernel CPU code | CPU 用來填 pattern、做 `memcmp()`。 |
| DMA address | `dl->dma_handle` / `tx_dma` / `rx_dma` | EDU device | driver 寫進 EDU DMA register，device 用它存取 guest RAM。 |
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
  -> free_irq()
  -> pci_free_irq_vectors()
  -> dma_free_coherent()
  -> pci_iounmap()
  -> pci_release_region()
  -> pci_disable_device()
```

## 讀 source 的主線

第一次請照這個順序讀：

1. DMA/IRQ register constants：先知道 EDU 要哪些 source/destination/count/command register。
2. `struct dl_edu_dma_dev`：看 PCI/MMIO/IRQ/DMA resource 如何放在同一個 per-device state。
3. `dl_edu_dma_handler()`：DMA completion IRQ 到了如何 acknowledge 與 `complete()`。
4. `dl_edu_dma_wait_for_irq()` 與 `dl_edu_dma_wait_for_cmd_clear()`：看 driver 如何等「IRQ 到」和「device command bit 清掉」。
5. `dl_edu_dma_program_addrs()`：看 DMA address 如何寫進 EDU register。
6. `dl_edu_dma_run_once()`：看一次 DMA transaction 如何被設定與等待。
7. `dl_edu_dma_probe()`：看完整 resource acquisition、round-trip 與 error path。
8. `dl_edu_dma_remove()`：看正常 unload cleanup 順序。

## 一、EDU DMA register 與 command bit

原始碼：

```c
#define DL_EDU_DMA_SRC_REG 0x80
#define DL_EDU_DMA_DST_REG 0x88
#define DL_EDU_DMA_COUNT_REG 0x90
#define DL_EDU_DMA_CMD_REG 0x98

#define DL_EDU_DEVICE_RAM_OFFSET 0x00040000ULL
#define DL_EDU_DMA_IRQ_MASK 0x00000100U
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
};
```

欄位分組：

| 分組 | 欄位 | 角色 |
|---|---|---|
| PCI/MMIO | `pdev`, `bar0` | PCI core device 與 BAR0 MMIO base。 |
| IRQ | `irq_vector`, `irq_flags`, `irq_done`, `last_irq_status`, `irq_count` | DMA completion interrupt path。 |
| DMA | `dma_buf`, `dma_handle`, `tx_buf`, `rx_buf` | coherent DMA allocation 與 tx/rx 切片。 |

`dma_buf` 和 `dma_handle` 是一組，必須一起保存，之後 `dma_free_coherent()` 要用同一組值釋放。

## 三、DMA completion IRQ handler

原始碼：

```c
static irqreturn_t dl_edu_dma_handler(int irq, void *opaque)
{
	struct dl_edu_dma_dev *dl = opaque;
	u32 pending;
	u32 status;

	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (status == ~0U)
		return IRQ_NONE;
	pending = status & DL_EDU_KNOWN_IRQ_MASK;
	if (!pending)
		return IRQ_NONE;

	iowrite32(pending, dl->bar0 + DL_EDU_IRQ_ACK_REG);
	(void)ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (pending & ~DL_EDU_DMA_IRQ_MASK)
		dev_warn_ratelimited(...);
	if (!(pending & DL_EDU_DMA_IRQ_MASK))
		return IRQ_HANDLED;

	dl->last_irq_status = status;
	dl->irq_count++;
	complete(&dl->irq_done);
	return IRQ_HANDLED;
}
```

這和 Lab06 handler 形狀相同，但 event bit 不同：

```text
Lab06 self-test IRQ: 0x00000002
Lab07 DMA complete IRQ: 0x00000100
```

handler 做四件事：

1. 讀 EDU interrupt status register `0x24`。
2. 遮罩出 factorial `0x1`、Lab06 self-test `0x2` 與 DMA `0x100` 三個已知來源。
3. ACK 所有已知 pending bit 並 read-back，避免 legacy INTx 持續 asserted。
4. 只有 status 含 `DL_EDU_DMA_IRQ_MASK` 時才 `complete()`；其他已知來源一律記
   ratelimited warning，避免 factorial 或 Lab06 self-test IRQ 假冒 DMA completion。

只有完全沒有 EDU 已知 bit 時才回 `IRQ_NONE`；unexpected known source
仍由本 handler ACK 並回 `IRQ_HANDLED`。

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
	int ret;

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

	dev_info(&dl->pdev->dev, "%s finished\n", phase);
	return 0;
}
```

白話說，這個 helper 是把一次 DMA 工作單交給 EDU：

```text
清空 completion
寫 source address
寫 destination address
寫 count
寫 command 啟動 DMA
等 completion IRQ
等 command start bit 清掉
印出 phase finished
```

`phase` 只是 log 名稱，例如：

```text
ram-to-edu transfer
edu-to-ram transfer
```

timeout 時 log 可以直接指出是哪一段壞。

## 七、測試 pattern：最後要能 `memcmp()`

原始碼：

```c
static void dl_edu_dma_fill_pattern(struct dl_edu_dma_dev *dl)
{
	size_t i;

	for (i = 0; i < DL_EDU_DMA_BUFFER_BYTES; ++i)
		dl->tx_buf[i] = (u8)(i ^ 0x5a);

	memset(dl->rx_buf, 0, DL_EDU_DMA_BUFFER_BYTES);
}
```

`tx_buf` 填固定 pattern，`rx_buf` 先清成 0。這樣 round-trip 後：

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
pci_set_master(pdev);
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
| `pci_set_master()` | 允許 device 主動發起 bus transaction，DMA 必須要。 |
| `pci_request_region()` | 宣告 BAR0 resource ownership。 |
| `pci_iomap()` | map BAR0 MMIO register window。 |

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
	goto err_free_irq;
}

dev_info(&pdev->dev, "round-trip compare passed, irq_count=%u\n",
	 dl->irq_count);
```

前面兩段 DMA 都完成後，最後才比對資料。

這個檢查回答：

```text
不是只有 request_irq 成功
不是只有 handler 進來
不是只有 command bit 清掉
而是 tx 的內容真的經過 EDU 內部 buffer 又回到 rx
```

`irq_count` 理想上會反映兩次 DMA completion interrupt：RAM -> EDU 一次，EDU -> RAM 一次。

## 十三、error path：照取得順序反向釋放

原始碼：

```c
err_free_irq:
	free_irq(dl->irq_vector, dl);
err_free_vectors:
	pci_free_irq_vectors(pdev);
err_free_dma:
	dma_free_coherent(&pdev->dev, DL_EDU_DMA_BUFFER_BYTES * 2,
			  dl->dma_buf, dl->dma_handle);
err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_release_region:
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
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
free_irq()
pci_free_irq_vectors()
dma_free_coherent()
pci_iounmap()
pci_release_region()
pci_disable_device()
```

這個順序是保守的。先停 IRQ path，避免 handler 還會使用 `bar0` 或 `dl`；再釋放 DMA buffer；最後拆 MMIO/PCI resource。

## 十四、remove：正常 unload 的 cleanup

原始碼：

```c
static void dl_edu_dma_remove(struct pci_dev *pdev)
{
	struct dl_edu_dma_dev *dl = pci_get_drvdata(pdev);

	if (dl) {
		free_irq(dl->irq_vector, dl);
		pci_free_irq_vectors(pdev);
		if (dl->dma_buf) {
			dma_free_coherent(&pdev->dev, DL_EDU_DMA_BUFFER_BYTES * 2,
					  dl->dma_buf, dl->dma_handle);
		}
		if (dl->bar0)
			pci_iounmap(pdev, dl->bar0);
	}

	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pci_disable_device(pdev);
	pr_info("device removed for %s\n", pci_name(pdev));
}
```

正常 `rmmod` 時，PCI core 呼叫 `remove()`。清理順序和 error path 一致：

```text
IRQ
IRQ vectors
coherent DMA buffer
MMIO mapping
BAR region
PCI device enable state
```

`dl` 不需要手動 `kfree()`，因為它是 `devm_kzalloc()` 配的。

## 十五、PCI ID table、driver struct、module macro

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
| `lspci -nn | grep 1234:11e8` | driver 尚未參與 | guest PCI bus 是否有 EDU |
| `insmod ./driver_lab_edu_dma.ko` | generated init -> `pci_register_driver()` | module 是否載入 |
| DMA mask | `dma_set_mask_and_coherent()` | `dma mask configured to 28 bits` |
| coherent allocation | `dma_alloc_coherent()` | `coherent buffer allocated: cpu=... dma=...` |
| IRQ setup | `pci_alloc_irq_vectors()` + `request_irq()` | `/proc/interrupts` 有 driver 名稱 |
| RAM -> EDU | `dl_edu_dma_run_once(... tx_dma, 0x40000, START | IRQ)` | `ram-to-edu transfer finished` |
| EDU -> RAM | `dl_edu_dma_run_once(... 0x40000, rx_dma, START | FROM_DEVICE | IRQ)` | `edu-to-ram transfer finished` |
| data verify | `memcmp(tx_buf, rx_buf, 256)` | `round-trip compare passed` |
| unload | `dl_edu_dma_remove()` | PCI driver sysfs directory 消失 |

## 常見卡點

- `probe()` 沒進來：先確認 guest 內 `lspci -nn | grep 1234:11e8` 有輸出。
- `dma_set_mask_and_coherent()` 失敗：不能繼續 DMA；先確認 EDU 預設 28-bit mask 與 guest DMA addressing 是否相容。
- `dma_alloc_coherent()` 失敗：先看 DMA mask 是否成功、guest memory 是否足夠、錯誤 log 是否明確。
- DMA timeout：先分辨是 `wait_for_completion_timeout()` timeout，還是 command start bit 沒清掉。
- `round-trip compare failed`：優先核對 source/destination、direction bit、count、EDU internal buffer offset。
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
| 為什麼要等 IRQ 又要等 command bit clear？ | IRQ 證明 completion event 到了；command bit clear 證明 device command 狀態回到 idle。 |
| `memcmp()` 驗證什麼？ | 驗證 tx pattern 經 RAM -> EDU -> RAM round-trip 後和 rx buffer 一致。 |
| cleanup 為什麼先 `free_irq()`？ | 避免 IRQ handler 在 DMA/MMIO resource 釋放後仍被呼叫。 |

## 查證來源

- Linux kernel documentation `Dynamic DMA mapping Guide`：CPU/DMA address 差異、`dma_set_mask_and_coherent()`、coherent DMA mapping、`dma_alloc_coherent()` 兩個回傳值與 `dma_free_coherent()`。<https://docs.kernel.org/core-api/dma-api-howto.html>
- Linux kernel documentation `Dynamic DMA mapping using the generic device`：`dma_addr_t` 不可由 CPU 直接 reference、`dma_alloc_coherent()` / `dma_free_coherent()` API 語意。<https://docs.kernel.org/core-api/dma-api.html>
- QEMU documentation `EDU device`：預設 28-bit `dma_mask`、DMA source/destination/count/command register、direction bit、completion IRQ bit、`0x40000` 內部 buffer。<https://www.qemu.org/docs/master/specs/edu.html>
