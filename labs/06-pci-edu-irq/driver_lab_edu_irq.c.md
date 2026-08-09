# `driver_lab_edu_irq.c` 詳解

## 結論

`labs/06-pci-edu-irq/driver_lab_edu_irq.c` 是在 Lab05 PCI/MMIO 基礎上加入 interrupt path 的 kernel driver。Lab05 證明 driver 可以 bind QEMU EDU、map BAR0、讀寫 MMIO register；Lab06 進一步驗證：

```text
driver 可以向 PCI core 要 IRQ vector
driver 可以用 request_irq() 註冊 handler
EDU 可以 raise interrupt
handler 可以讀 status、acknowledge、喚醒等待者
probe() 可以用 completion 等到 IRQ self-test 完成
```

這一關仍然不建立 `/dev`，也不碰 DMA。它只專心回答一個問題：

```text
裝置主動通知 driver 時，driver 能不能接住並清掉這個事件？
```

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- 本檔 source、[`README.md`](README.md)、[`test.sh.md`](test.sh.md)、[`Makefile.md`](Makefile.md)。
- 05-07 導讀：[`../../docs/onboarding/05-to-07-pci-irq-dma-bridge.md`](../../docs/onboarding/START-HERE.md)、[`../../docs/guides/linux-guest-05-to-07-walkthrough.md`](../../docs/guides/linux-guest-05-to-07-walkthrough.md)。
- Linux kernel documentation：PCI MSI HOWTO、generic IRQ、completion API。
- QEMU 官方 `EDU device` spec。

這裡只解釋 Lab06 實際使用的一條 IRQ vector、自我測試 interrupt、top-half handler 和 cleanup 順序。不展開 interrupt affinity、threaded IRQ、MSI-X 多 vector、真實硬體 masking policy 或 DMA completion；那些不是這份 source 的第一輪主線。

## 先理解這份檔案在 repo 的位置

Lab05、Lab06、Lab07 是同一條 PCI EDU 路線：

```text
05-pci-edu-mmio
  PCI probe/remove + BAR0 + MMIO liveness

06-pci-edu-irq
  在 05 的 BAR0 基礎上加 IRQ vector、handler、acknowledge、completion self-test

07-pci-edu-dma
  在 06 的 IRQ 基礎上加 coherent DMA buffer 與 DMA completion
```

相關檔案：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_edu_irq.c`](driver_lab_edu_irq.c) | Lab06 PCI IRQ driver 本體 |
| [`test.sh.md`](test.sh.md) | Linux guest smoke test |
| [`Makefile.md`](Makefile.md) | external module kbuild 入口 |
| [`../05-pci-edu-mmio/driver_lab_edu_mmio.c.md`](../05-pci-edu-mmio/driver_lab_edu_mmio.c.md) | Lab05 PCI/MMIO 前置主線 |
| [`../../qemu/edu-bringup-checklist.md`](../../qemu/edu-bringup-checklist.md) | guest 看不到 EDU 時的環境排查 |

## 這份檔案要解決什麼問題？

Lab05 是 CPU 主動讀寫 device register：

```text
CPU -> iowrite32()/ioread32() -> EDU BAR0 register
```

Lab06 加入反方向的通知：

```text
EDU raises interrupt
  -> Linux IRQ core calls dl_edu_irq_handler()
  -> handler reads status
  -> handler acknowledges EDU register
  -> handler complete(&irq_done)
  -> probe() 的 wait_for_completion_timeout() 回來
```

第一輪請把 IRQ 想成「device 敲門」。handler 不能只印 log；它要判斷是不是自己的事件，並把 device 端的 pending bit 清掉。QEMU EDU 官方文件也明確說，interrupt acknowledge register 要在 ISR 裡寫，否則 interrupt 會持續產生。

## 它怎麼被 build / load / 呼叫？

Build：

```sh
cd labs/06-pci-edu-irq
make
```

產物：

```text
driver_lab_edu_irq.ko
```

Load：

```sh
sudo insmod ./driver_lab_edu_irq.ko
```

呼叫流程：

```text
insmod
  -> module_pci_driver() generated init
  -> pci_register_driver()
  -> PCI core 找到 guest 裡的 1234:11e8
  -> call dl_edu_irq_probe()
  -> probe() 建立 PCI/MMIO/IRQ path
  -> probe() 寫 DL_EDU_IRQ_RAISE_REG 觸發 self-test interrupt
  -> Linux IRQ core call dl_edu_irq_handler()
```

Unload：

```sh
sudo rmmod driver_lab_edu_irq
```

呼叫流程：

```text
rmmod
  -> pci_unregister_driver()
  -> PCI core call dl_edu_irq_remove()
  -> free_irq()
  -> pci_free_irq_vectors()
  -> unmap/release/disable PCI resource
```

## 讀 source 的主線

第一次請照這個順序讀：

1. EDU IRQ register offset：先知道 `0x24`、`0x60`、`0x64` 各自是什麼。
2. `struct dl_edu_irq_dev`：看 per-device state 比 Lab05 多了哪些 IRQ 狀態。
3. `dl_edu_irq_handler()`：先讀 handler，理解 IRQ 來時 driver 做什麼。
4. `dl_edu_irq_probe()` 前半：沿用 Lab05 的 enable、request BAR、map BAR。
5. `dl_edu_irq_probe()` IRQ setup：`pci_alloc_irq_vectors()`、`pci_irq_vector()`、`request_irq()`。
6. `dl_edu_irq_probe()` self-test：raise interrupt、wait completion、檢查 status 已清掉。
7. error labels 與 `dl_edu_irq_remove()`：確認 cleanup 順序與 probe 成功取得 resource 的順序相反。

## 一、EDU IRQ register offset

原始碼：

```c
#define DL_EDU_IRQ_STATUS_REG 0x24
#define DL_EDU_IRQ_RAISE_REG 0x60
#define DL_EDU_IRQ_ACK_REG 0x64
#define DL_EDU_TEST_IRQ_MASK 0x00000001U
#define DL_EDU_IRQ_TIMEOUT_MS 1000
```

對照 QEMU EDU spec：

| Register | 權限 | Lab06 用途 |
|---|---|---|
| `0x24` interrupt status | read-only | handler 讀它，知道哪些 bit raise 了 interrupt。 |
| `0x60` interrupt raise | write-only | probe self-test 寫它，要求 EDU raise interrupt。 |
| `0x64` interrupt acknowledge | write-only | handler 寫它，清掉 status 裡對應 bit。 |

`DL_EDU_TEST_IRQ_MASK` 是 Lab06 自我測試使用的 bit。它不是 Linux IRQ number，而是 EDU device 自己的 interrupt status bit。

## 二、private state：IRQ 讓狀態更多了

原始碼：

```c
struct dl_edu_irq_dev {
	struct pci_dev *pdev;
	void __iomem *bar0;
	resource_size_t bar0_len;
	int irq_vector;
	unsigned long irq_flags;
	struct completion irq_done;
	u32 last_irq_status;
	u32 irq_count;
};
```

和 Lab05 相比，多了這些欄位：

| 欄位 | 意義 |
|---|---|
| `irq_vector` | Linux 分配給這顆 PCI device 的 IRQ number/vector。 |
| `irq_flags` | 傳給 `request_irq()` 的 flags；legacy shared IRQ 時使用 `IRQF_SHARED`。 |
| `irq_done` | probe 等 handler 的同步點。 |
| `last_irq_status` | handler 最近一次讀到的 EDU interrupt status。 |
| `irq_count` | handler 成功處理 self-test interrupt 的次數。 |

這份 state 同時被 probe path 和 IRQ handler 使用，所以 `request_irq()` 的最後一個參數傳入 `dl`：

```c
request_irq(..., KBUILD_MODNAME, dl);
```

handler 收到的 `opaque` 就是同一個 `dl`。

## 三、IRQ handler：先判斷，再 acknowledge

原始碼：

```c
static irqreturn_t dl_edu_irq_handler(int irq, void *opaque)
{
	struct dl_edu_irq_dev *dl = opaque;
	u32 status;

	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (!(status & DL_EDU_TEST_IRQ_MASK))
		return IRQ_NONE;

	dl->last_irq_status = status;
	dl->irq_count++;

	iowrite32(status, dl->bar0 + DL_EDU_IRQ_ACK_REG);
	complete(&dl->irq_done);
	dev_info(&dl->pdev->dev, "irq status=0x%08x acknowledged\n", status);

	return IRQ_HANDLED;
}
```

這段有四個層次。

第一，取回 per-device state：

```c
struct dl_edu_irq_dev *dl = opaque;
```

`opaque` 來自 `request_irq()` 的 `dev_id` 參數。這也是為什麼 shared IRQ 不能傳 NULL：IRQ core 之後要用同一個 cookie 找 handler 的裝置狀態，也要用它做 `free_irq()` 對應。

第二，讀 device status：

```c
status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
```

這是 MMIO read，不是一般記憶體讀取。`status` 是 EDU register 目前 pending 的 interrupt bit。

第三，判斷這是不是自己的事件：

```c
if (!(status & DL_EDU_TEST_IRQ_MASK))
	return IRQ_NONE;
```

`IRQ_NONE` 的意思是「這次 interrupt 看起來不是我處理的事件」。legacy INTx 可能 shared，所以 handler 需要先檢查 device status，不能看到 handler 被叫就直接宣稱處理成功。

第四，處理並清掉事件：

```c
iowrite32(status, dl->bar0 + DL_EDU_IRQ_ACK_REG);
complete(&dl->irq_done);
return IRQ_HANDLED;
```

這裡的順序很重要：

```text
讀 status
  -> 確認 self-test bit
  -> 寫 ACK register 清 device pending bit
  -> complete() 喚醒等待者
  -> 回報 IRQ_HANDLED
```

如果少了 acknowledge，同一個 interrupt 可能一直重進。這也是 Lab06 最重要的學習點。

## 四、probe 前半：沿用 Lab05 PCI/MMIO bring-up

原始碼：

```c
dl = devm_kzalloc(&pdev->dev, sizeof(*dl), GFP_KERNEL);
if (!dl)
	return -ENOMEM;

dl->pdev = pdev;
init_completion(&dl->irq_done);
pci_set_drvdata(pdev, dl);

ret = pci_enable_device(pdev);
...
pci_set_master(pdev);
...
ret = pci_request_region(pdev, DL_EDU_BAR_INDEX, KBUILD_MODNAME);
...
dl->bar0 = pci_iomap(pdev, DL_EDU_BAR_INDEX, 0);
```

這裡大部分和 Lab05 相同：

| 步驟 | 目的 |
|---|---|
| `devm_kzalloc()` | 配置 per-device state。 |
| `init_completion()` | 初始化 self-test 等待點。 |
| `pci_set_drvdata()` | 讓 remove path 可取回 `dl`。 |
| `pci_enable_device()` | 啟用 PCI device。 |
| `pci_request_region()` | 宣告 BAR0 resource ownership。 |
| `pci_iomap()` | map BAR0 MMIO register window。 |

Lab06 新增一個關鍵步驟：

```c
pci_set_master(pdev);
```

MSI 是 device 對特殊位址發出的 memory write。若 PCI core 配到 MSI，EDU 需要能主動發起 bus transaction。這就是為什麼 Lab06 在 enable device 後呼叫 `pci_set_master()`。

第一輪不用把 MSI message 格式背起來；先記住：

```text
Lab06 允許 PCI_IRQ_ALL_TYPES
如果最後走 MSI，device 需要 bus mastering 才能送出 MSI
```

## 五、`pci_alloc_irq_vectors()`：向 PCI core 要 IRQ

原始碼：

```c
ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
if (ret < 0) {
	dev_err(&pdev->dev, "pci_alloc_irq_vectors failed: %d\n", ret);
	goto err_iounmap;
}

dl->irq_vector = pci_irq_vector(pdev, 0);
```

參數角色：

| 參數 | Lab06 的值 | 意義 |
|---|---|---|
| `pdev` | EDU PCI device | 要替哪顆 device 配 IRQ。 |
| `min_vecs` | `1` | 至少要 1 條，否則失敗。 |
| `max_vecs` | `1` | 這個 lab 只練一條 IRQ。 |
| `flags` | `PCI_IRQ_ALL_TYPES` | 允許 PCI core 選 INTx/MSI/MSI-X。 |

`pci_alloc_irq_vectors()` 成功時回傳分配到的 vector 數量。這裡要求 min=max=1，所以成功代表拿到一條。

接著：

```c
dl->irq_vector = pci_irq_vector(pdev, 0);
```

把 PCI vector index 0 轉成 Linux IRQ number，後面傳給 `request_irq()` / `free_irq()`。

## 六、`IRQF_SHARED`：只有 legacy INTx 需要

原始碼：

```c
dl->irq_flags = (pdev->msi_enabled || pdev->msix_enabled) ? 0 : IRQF_SHARED;
```

MSI/MSI-X 不共享同一條 pin-based interrupt line；legacy INTx 則可能 shared。Lab06 的策略是：

| 情況 | flags |
|---|---|
| PCI core 配到 MSI 或 MSI-X | `0` |
| PCI core 退回 legacy INTx | `IRQF_SHARED` |

這也解釋了 handler 為什麼要先讀 `DL_EDU_IRQ_STATUS_REG`。如果是 shared INTx，Linux 可能呼叫多個 handler；每個 handler 都要檢查自己的 device 是否真的有 pending event。

## 七、`request_irq()`：把 vector 接到 handler

原始碼：

```c
ret = request_irq(dl->irq_vector, dl_edu_irq_handler, dl->irq_flags,
		  KBUILD_MODNAME, dl);
if (ret) {
	dev_err(&pdev->dev, "request_irq failed: %d\n", ret);
	goto err_free_vectors;
}
```

參數角色：

| 參數 | 角色 |
|---|---|
| `dl->irq_vector` | Linux IRQ number。 |
| `dl_edu_irq_handler` | interrupt 發生時被 IRQ core 呼叫的 top-half handler。 |
| `dl->irq_flags` | 是否 shared，以及其他 IRQ flags。 |
| `KBUILD_MODNAME` | `/proc/interrupts` 裡常看到的名稱。 |
| `dl` | `dev_id` cookie，handler 會用它找回 per-device state。 |

成功後，`test.sh` 會用 `/proc/interrupts` 檢查是否看得到 `driver_lab_edu_irq`。這只是輔助觀測，真正的成功仍然要看 self-test interrupt 有沒有進 handler 並被 acknowledge。

## 八、self-test：用 completion 把 IRQ 變成可等待事件

原始碼：

```c
reinit_completion(&dl->irq_done);
iowrite32(DL_EDU_TEST_IRQ_MASK, dl->bar0 + DL_EDU_IRQ_RAISE_REG);

timeout_jiffies = msecs_to_jiffies(DL_EDU_IRQ_TIMEOUT_MS);
if (!wait_for_completion_timeout(&dl->irq_done, timeout_jiffies)) {
	dev_err(&pdev->dev, "interrupt self-test timed out after %u ms\n",
		DL_EDU_IRQ_TIMEOUT_MS);
	ret = -ETIMEDOUT;
	goto err_free_irq;
}
```

這段是 Lab06 的核心設計。

`reinit_completion()` 先把 `irq_done` 重設成 not done。接著 driver 寫：

```c
iowrite32(DL_EDU_TEST_IRQ_MASK, dl->bar0 + DL_EDU_IRQ_RAISE_REG);
```

QEMU EDU 會把這個 bit OR 到 interrupt status register，並產生 interrupt。handler 收到後會：

```c
complete(&dl->irq_done);
```

所以 probe 可以用：

```c
wait_for_completion_timeout(...)
```

等待 handler 證明 interrupt 真的抵達。

白話流程：

```text
probe: 我先把等待點清空
probe: 我請 EDU raise 一次 interrupt
handler: 我收到 interrupt、ack 掉它、通知 probe
probe: 我等到了，所以 self-test passed
```

這比 `msleep(100)` 後猜測有沒有發生更可靠，因為 completion 是明確的事件同步。

## 九、確認 acknowledge 真的清掉 status

原始碼：

```c
if (ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG) & DL_EDU_TEST_IRQ_MASK) {
	dev_err(&pdev->dev, "interrupt status bit still set after acknowledge\n");
	ret = -EIO;
	goto err_free_irq;
}
```

這個檢查很重要。`complete()` 只代表 handler 跑過，不代表 device 端狀態已經正確清掉。

Lab06 另外讀一次 status，確認 self-test bit 已經消失：

```text
handler 跑過
  不夠

handler 跑過，而且 ACK 後 status bit 清掉
  才是這一關要的成功
```

## 十、error path：IRQ resource 要先拆

原始碼：

```c
err_free_irq:
	free_irq(dl->irq_vector, dl);
err_free_vectors:
	pci_free_irq_vectors(pdev);
err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_release_region:
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
err_disable_device:
	pci_disable_device(pdev);
	return ret;
```

成功取得 resource 的順序是：

```text
pci_enable_device()
pci_request_region()
pci_iomap()
pci_alloc_irq_vectors()
request_irq()
```

失敗時反向釋放：

```text
free_irq()
pci_free_irq_vectors()
pci_iounmap()
pci_release_region()
pci_disable_device()
```

為什麼 IRQ 要先拆？因為 handler 會使用 `dl->bar0`。如果先 `pci_iounmap()`，但 interrupt 還可能進來，handler 可能碰到已經 unmap 的 MMIO base。

## 十一、remove：正常卸載也要先停 IRQ

原始碼：

```c
static void dl_edu_irq_remove(struct pci_dev *pdev)
{
	struct dl_edu_irq_dev *dl = pci_get_drvdata(pdev);

	if (dl) {
		free_irq(dl->irq_vector, dl);
		pci_free_irq_vectors(pdev);
		if (dl->bar0)
			pci_iounmap(pdev, dl->bar0);
	}

	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pci_disable_device(pdev);
	pr_info("device removed for %s\n", pci_name(pdev));
}
```

正常 `rmmod` 時，PCI core 會 call `dl_edu_irq_remove()`。

清理順序同樣是：

```text
先讓 IRQ handler 不會再被呼叫
再釋放 IRQ vectors
再 unmap MMIO
再 release BAR0
再 disable device
```

這份 source 不手動 `kfree(dl)`，因為 `dl` 是 `devm_kzalloc()` 配置，會跟著 `pdev->dev` lifecycle 釋放。

## 十二、PCI ID table、driver struct、module macro

原始碼：

```c
static const struct pci_device_id dl_edu_irq_ids[] = {
	{ PCI_DEVICE(DL_EDU_VENDOR_ID, DL_EDU_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, dl_edu_irq_ids);

static struct pci_driver dl_edu_irq_driver = {
	.name = KBUILD_MODNAME,
	.id_table = dl_edu_irq_ids,
	.probe = dl_edu_irq_probe,
	.remove = dl_edu_irq_remove,
};

module_pci_driver(dl_edu_irq_driver);
```

這和 Lab05 相同：PCI core 用 `id_table` match QEMU EDU `1234:11e8`，match 後 call `probe()`；module unload 或 device unbind 時 call `remove()`。

`module_pci_driver()` 產生 init/exit：

```text
module init -> pci_register_driver()
module exit -> pci_unregister_driver()
```

## source、test、觀測點對照

| 操作 | driver path | 觀測點 |
|---|---|---|
| `lspci -nn | grep 1234:11e8` | driver 尚未參與 | guest PCI bus 是否有 EDU |
| `insmod ./driver_lab_edu_irq.ko` | generated init -> `pci_register_driver()` | module 是否載入 |
| PCI ID match | `dl_edu_irq_probe()` | `dmesg` 的 `probe start` |
| IRQ vector allocation | `pci_alloc_irq_vectors()` | 失敗時看 `pci_alloc_irq_vectors failed` |
| handler registration | `request_irq()` | `dmesg` 的 `request_irq ok`、`/proc/interrupts` |
| self-test raise | `iowrite32(... 0x60)` | EDU 產生 interrupt |
| handler | `dl_edu_irq_handler()` | `dmesg` 的 `irq status=... acknowledged` |
| completion wait | `wait_for_completion_timeout()` | `irq self-test passed` 或 timeout |
| `rmmod` | `dl_edu_irq_remove()` | PCI driver sysfs directory 消失 |

## 常見卡點

- `probe()` 沒進來：先確認 guest 內 `lspci -nn | grep 1234:11e8` 有輸出。
- `request_irq()` 失敗：先看 `dmesg` 的錯誤碼，確認 `pci_alloc_irq_vectors()` 是否成功。
- handler 沒進來：確認 `pci_set_master()`、IRQ vector allocation、`request_irq()` log，以及 EDU raise register 是否真的被寫。
- handler 一直重進：優先檢查是否有寫 `DL_EDU_IRQ_ACK_REG`。
- `wait_for_completion_timeout()` timeout：代表 probe 沒等到 handler 的 `complete()`，要往 IRQ delivery 或 handler status 判斷查。
- `/proc/interrupts` 沒有名字：可能 `request_irq()` 沒成功，或目前平台顯示格式不同；仍要以 `dmesg` self-test 結果為主。
- 不要在 hard IRQ handler 裡做會 sleep 的事；這份 Lab06 handler 只做 MMIO read/write、更新簡單狀態、`complete()`、log。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab06 建立在哪一關之上？ | Lab05 的 PCI enable、BAR0 request/map、MMIO accessor 基礎。 |
| EDU 的 interrupt status / raise / acknowledge register 分別在哪裡？ | `0x24`、`0x60`、`0x64`。 |
| handler 為什麼可能回 `IRQ_NONE`？ | 因為 shared IRQ 或非目標事件時，status 沒有 `DL_EDU_TEST_IRQ_MASK`，這次 interrupt 不該由本 handler 宣稱處理。 |
| `request_irq()` 最後一個 `dl` 參數做什麼？ | 作為 `dev_id` cookie 傳回 handler，也用於 `free_irq()` 對應釋放。 |
| `pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES)` 代表什麼？ | 向 PCI core 要剛好 1 條 IRQ vector，允許 INTx/MSI/MSI-X。 |
| 為什麼 Lab06 呼叫 `pci_set_master()`？ | 若使用 MSI，device 需要能主動發起 memory write 送出 MSI。 |
| completion 在這裡等的是什麼？ | 等 IRQ handler 收到 self-test interrupt 並呼叫 `complete()`。 |
| 為什麼 self-test 後還要讀 status register？ | 確認 handler 的 acknowledge 真的清掉 device pending bit。 |
| cleanup 為什麼先 `free_irq()`？ | 避免 handler 在 MMIO 已 unmap 後仍被呼叫。 |

## 查證來源

- Linux kernel documentation `The MSI Driver Guide HOWTO`：MSI 是 device 對特殊位址的 write；`pci_alloc_irq_vectors()`、`PCI_IRQ_ALL_TYPES`、`pci_irq_vector()`、`pci_free_irq_vectors()` 的使用方式。<https://docs.kernel.org/PCI/msi-howto.html>
- Linux kernel documentation `Linux generic IRQ handling`：`request_threaded_irq()` / `request_irq()` 參數角色、shared IRQ 的 `dev_id` 要求、handler 需要判斷 interrupt 是否源自本裝置。<https://docs.kernel.org/core-api/genericirq.html>
- Linux kernel documentation `Completions - wait for completion barrier APIs`：completion 的 init/wait/signal 模型，`complete()` 可安全從 IRQ context 呼叫。<https://docs.kernel.org/scheduler/completion.html>
- QEMU documentation `EDU device`：interrupt status `0x24`、raise `0x60`、acknowledge `0x64`，以及 INTx/MSI 都需要 acknowledge register。<https://www.qemu.org/docs/master/specs/edu.html>
