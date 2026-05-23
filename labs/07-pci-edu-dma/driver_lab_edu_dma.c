// SPDX-License-Identifier: GPL-2.0-only
/*
 * 這一關在 PCI + IRQ 基礎上加入 DMA。
 * 核心觀念：不是 CPU 自己 memcpy，而是 driver 給裝置一個 device 可用的位址，
 * 由裝置去搬資料，再用 IRQ 通知完成。
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/string.h>

#define DL_EDU_VENDOR_ID 0x1234
#define DL_EDU_DEVICE_ID 0x11e8

#define DL_EDU_BAR_INDEX 0
#define DL_EDU_IRQ_STATUS_REG 0x24
#define DL_EDU_IRQ_ACK_REG 0x64
#define DL_EDU_DMA_SRC_REG 0x80
#define DL_EDU_DMA_DST_REG 0x88
#define DL_EDU_DMA_COUNT_REG 0x90
#define DL_EDU_DMA_CMD_REG 0x98

#define DL_EDU_DEVICE_RAM_OFFSET 0x00040000ULL
#define DL_EDU_DMA_IRQ_MASK 0x00000100U
#define DL_EDU_DMA_CMD_START 0x01U
#define DL_EDU_DMA_CMD_FROM_DEVICE 0x02U
#define DL_EDU_DMA_CMD_IRQ 0x04U
#define DL_EDU_DMA_WAIT_TIMEOUT_MS 1000
#define DL_EDU_DMA_BUFFER_BYTES 256

/*
 * DMA lab 的 per-device state。
 * 這裡把 PCI/MMIO/IRQ 資源和 coherent DMA buffer 放在同一個生命週期裡。
 */
struct dl_edu_dma_dev {
	struct pci_dev *pdev;
	void __iomem *bar0;
	int irq_vector;
	unsigned long irq_flags;
	/* DMA 完成事件會靠 completion 把 IRQ path 與 probe() 接起來。 */
	struct completion irq_done;
	u32 last_irq_status;
	u32 irq_count;
	/* 這一關用一塊 coherent buffer 切成 tx/rx 兩半做 round-trip。 */
	void *dma_buf;
	dma_addr_t dma_handle;
	u8 *tx_buf;
	u8 *rx_buf;
};

/*
 * DMA completion IRQ handler。
 * 它只做必要工作：確認 status、ack 裝置、喚醒等待 self-test 的 probe path。
 */
static irqreturn_t dl_edu_dma_handler(int irq, void *opaque)
{
	struct dl_edu_dma_dev *dl = opaque;
	u32 status;

	/* DMA 完成後，EDU 會丟出 0x100 interrupt。 */
	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (!(status & DL_EDU_DMA_IRQ_MASK))
		return IRQ_NONE;

	dl->last_irq_status = status;
	dl->irq_count++;
	iowrite32(status, dl->bar0 + DL_EDU_IRQ_ACK_REG);
	complete(&dl->irq_done);
	dev_info(&dl->pdev->dev, "dma irq status=0x%08x acknowledged\n", status);

	return IRQ_HANDLED;
}

/*
 * 等待 DMA completion interrupt。
 * phase 只用來讓錯誤 log 說清楚是 RAM->EDU 還是 EDU->RAM 失敗。
 */
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

/*
 * 等待 EDU command bit 清掉。
 * IRQ 代表事件抵達；command bit 清掉則代表裝置端狀態也回到 idle。
 */
static int dl_edu_dma_wait_for_cmd_clear(struct dl_edu_dma_dev *dl, const char *phase)
{
	unsigned long deadline;

	/* 官方範例會 poll command bit；這裡保留同樣概念，避免只靠 IRQ。 */
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

/*
 * 把 source/destination DMA address 寫進 EDU register。
 * 這裡寫的是裝置視角的位址，不是 CPU 直接 dereference 的 kernel pointer。
 */
static void dl_edu_dma_program_addrs(struct dl_edu_dma_dev *dl,
									 dma_addr_t src, dma_addr_t dst)
{
	/*
	 * QEMU EDU 預設 DMA mask 是 28 bits。
	 * 這個 lab 先用 coherent DMA 配低位址，並以 32-bit access 寫入 source/destination。
	 */
	iowrite32(lower_32_bits(src), dl->bar0 + DL_EDU_DMA_SRC_REG);
	iowrite32(lower_32_bits(dst), dl->bar0 + DL_EDU_DMA_DST_REG);
}

/*
 * 執行一次 DMA transaction。
 * probe() 會呼叫兩次：第一次 RAM->EDU，第二次 EDU->RAM。
 */
static int dl_edu_dma_run_once(struct dl_edu_dma_dev *dl, dma_addr_t src,
							   dma_addr_t dst, u32 cmd, const char *phase)
{
	int ret;

	/*
	 * 每次 DMA 都重新設定 source/destination/count/command。
	 * 對新手來說，這裡就是「把一張搬運單交給裝置」。
	 */
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

/*
 * 準備可預測的測試資料。
 * round-trip 後用 memcmp() 比對 tx/rx，驗證不是只有 IRQ 成功而已。
 */
static void dl_edu_dma_fill_pattern(struct dl_edu_dma_dev *dl)
{
	size_t i;

	/* 先準備一個固定 pattern，之後用來比對 round-trip 是否一致。 */
	for (i = 0; i < DL_EDU_DMA_BUFFER_BYTES; ++i)
		dl->tx_buf[i] = (u8)(i ^ 0x5a);

	memset(dl->rx_buf, 0, DL_EDU_DMA_BUFFER_BYTES);
}

/*
 * PCI probe callback。
 * 完整建立 PCI/MMIO/IRQ/DMA path，最後用 round-trip self-test 當載入驗收。
 */
static int dl_edu_dma_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct dl_edu_dma_dev *dl;
	dma_addr_t tx_dma;
	dma_addr_t rx_dma;
	int ret;

	pr_info("probe start for %s\n", pci_name(pdev));

	dl = devm_kzalloc(&pdev->dev, sizeof(*dl), GFP_KERNEL);
	if (!dl)
		return -ENOMEM;

	dl->pdev = pdev;
	init_completion(&dl->irq_done);
	pci_set_drvdata(pdev, dl);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed: %d\n", ret);
		return ret;
	}

	/* DMA 裝置要先成為 bus master，才能主動對主記憶體發 DMA。 */
	pci_set_master(pdev);

	ret = pci_request_region(pdev, DL_EDU_BAR_INDEX, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_region BAR%d failed: %d\n",
				DL_EDU_BAR_INDEX, ret);
		goto err_disable_device;
	}

	dl->bar0 = pci_iomap(pdev, DL_EDU_BAR_INDEX, 0);
	if (!dl->bar0) {
		dev_err(&pdev->dev, "pci_iomap BAR%d failed\n", DL_EDU_BAR_INDEX);
		ret = -ENOMEM;
		goto err_release_region;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_mask_and_coherent(28) failed: %d\n", ret);
		goto err_iounmap;
	}
	dev_info(&pdev->dev, "dma mask configured to 28 bits\n");

	/*
	 * 第一版先用 coherent buffer，避免一開始就把 sync 細節混進來。
	 * dma_handle 是給裝置看的 DMA address，dma_buf 是 CPU 在 kernel 裡用的指標。
	 */
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

	dev_info(&pdev->dev, "coherent buffer allocated: cpu=%p dma=%pad bytes=%u\n",
			 dl->dma_buf, &dl->dma_handle, DL_EDU_DMA_BUFFER_BYTES * 2);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0) {
		dev_err(&pdev->dev, "pci_alloc_irq_vectors failed: %d\n", ret);
		goto err_free_dma;
	}

	dl->irq_vector = pci_irq_vector(pdev, 0);
	dl->irq_flags = (pdev->msi_enabled || pdev->msix_enabled) ? 0 : IRQF_SHARED;

	ret = request_irq(dl->irq_vector, dl_edu_dma_handler, dl->irq_flags,
					  KBUILD_MODNAME, dl);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed: %d\n", ret);
		goto err_free_vectors;
	}

	dl_edu_dma_fill_pattern(dl);

	/* 第一次：把 tx_buf 的內容搬進 EDU 內部 0x40000 buffer。 */
	ret = dl_edu_dma_run_once(dl, tx_dma, DL_EDU_DEVICE_RAM_OFFSET,
							  DL_EDU_DMA_CMD_START | DL_EDU_DMA_CMD_IRQ,
							  "ram-to-edu transfer");
	if (ret)
		goto err_free_irq;

	/* 第二次：再把 EDU 內部 buffer 搬回 rx_buf。 */
	ret = dl_edu_dma_run_once(dl, DL_EDU_DEVICE_RAM_OFFSET, rx_dma,
							  DL_EDU_DMA_CMD_START |
							  DL_EDU_DMA_CMD_FROM_DEVICE |
							  DL_EDU_DMA_CMD_IRQ,
							  "edu-to-ram transfer");
	if (ret)
		goto err_free_irq;

	/* 最後才做資料正確性驗證，確認 round-trip 真的沒壞。 */
	if (memcmp(dl->tx_buf, dl->rx_buf, DL_EDU_DMA_BUFFER_BYTES) != 0) {
		dev_err(&pdev->dev, "round-trip compare failed\n");
		ret = -EIO;
		goto err_free_irq;
	}

	dev_info(&pdev->dev, "round-trip compare passed, irq_count=%u\n",
			 dl->irq_count);
	return 0;

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
}

/*
 * PCI remove callback。
 * cleanup 順序要保守：先停止 IRQ path，再釋放 DMA buffer，最後拆 MMIO/PCI。
 */
static void dl_edu_dma_remove(struct pci_dev *pdev)
{
	struct dl_edu_dma_dev *dl = pci_get_drvdata(pdev);

	/* 先停 IRQ，再釋放 DMA buffer，最後拆 MMIO 與 PCI resource。 */
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

static const struct pci_device_id dl_edu_dma_ids[] = {
	{ PCI_DEVICE(DL_EDU_VENDOR_ID, DL_EDU_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, dl_edu_dma_ids);

static struct pci_driver dl_edu_dma_driver = {
	.name = KBUILD_MODNAME,
	.id_table = dl_edu_dma_ids,
	/* probe 建立 PCI/MMIO/IRQ/DMA path；remove 反向釋放所有資源。 */
	.probe = dl_edu_dma_probe,
	.remove = dl_edu_dma_remove,
};

module_pci_driver(dl_edu_dma_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 7 QEMU EDU DMA lab for driver-lab");
