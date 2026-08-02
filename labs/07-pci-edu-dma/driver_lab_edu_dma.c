// SPDX-License-Identifier: GPL-2.0-only
/* PCI + MMIO + IRQ + coherent DMA round-trip on QEMU EDU. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>
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
#define DL_EDU_DMA_MMIO_MIN_LEN (DL_EDU_DMA_CMD_REG + sizeof(u32))

struct dl_edu_dma_dev {
	struct pci_dev *pdev;
	u8 __iomem *bar0;
	resource_size_t bar0_len;
	int irq_vector;
	unsigned long irq_flags;
	bool irq_registered;
	bool dma_in_flight;
	struct completion irq_done;
	u32 last_irq_status;
	u32 irq_count;
	void *dma_buf;
	dma_addr_t dma_handle;
	u8 *tx_buf;
	u8 *rx_buf;
};

static irqreturn_t dl_edu_dma_handler(int irq, void *opaque)
{
	struct dl_edu_dma_dev *dl = opaque;
	u32 status;

	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (!(status & DL_EDU_DMA_IRQ_MASK))
		return IRQ_NONE;

	dl->last_irq_status = status;
	dl->irq_count++;
	iowrite32(status, dl->bar0 + DL_EDU_IRQ_ACK_REG);
	complete(&dl->irq_done);
	dev_dbg_ratelimited(&dl->pdev->dev,
				"dma irq status=0x%08x acknowledged\n", status);
	return IRQ_HANDLED;
}

static void dl_edu_dma_ack_pending(struct dl_edu_dma_dev *dl)
{
	u32 status;

	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (status)
		iowrite32(status, dl->bar0 + DL_EDU_IRQ_ACK_REG);
	(void)ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
}

static int dl_edu_dma_wait_for_irq(struct dl_edu_dma_dev *dl,
						  const char *phase)
{
	unsigned long timeout;

	timeout = msecs_to_jiffies(DL_EDU_DMA_WAIT_TIMEOUT_MS);
	if (!wait_for_completion_timeout(&dl->irq_done, timeout)) {
		dev_err(&dl->pdev->dev, "%s IRQ timed out after %u ms\n",
			phase, DL_EDU_DMA_WAIT_TIMEOUT_MS);
		return -ETIMEDOUT;
	}
	return 0;
}

static int dl_edu_dma_wait_for_cmd_clear(struct dl_edu_dma_dev *dl,
								 const char *phase)
{
	unsigned long deadline;

	deadline = jiffies + msecs_to_jiffies(DL_EDU_DMA_WAIT_TIMEOUT_MS);
	while (time_before(jiffies, deadline)) {
		if (!(ioread32(dl->bar0 + DL_EDU_DMA_CMD_REG) &
		      DL_EDU_DMA_CMD_START))
			return 0;
		usleep_range(1000, 2000);
	}

	dev_err(&dl->pdev->dev, "%s command bit did not clear\n", phase);
	return -ETIMEDOUT;
}

static void dl_edu_dma_program_addrs(struct dl_edu_dma_dev *dl,
							 dma_addr_t src, dma_addr_t dst)
{
	/* EDU default DMA mask is 28 bits, so lower_32_bits is sufficient here. */
	iowrite32(lower_32_bits(src), dl->bar0 + DL_EDU_DMA_SRC_REG);
	iowrite32(lower_32_bits(dst), dl->bar0 + DL_EDU_DMA_DST_REG);
}

static int dl_edu_dma_run_once(struct dl_edu_dma_dev *dl, dma_addr_t src,
						   dma_addr_t dst, u32 cmd,
						   const char *phase)
{
	int ret;

	reinit_completion(&dl->irq_done);
	dl_edu_dma_program_addrs(dl, src, dst);
	iowrite32(DL_EDU_DMA_BUFFER_BYTES,
		  dl->bar0 + DL_EDU_DMA_COUNT_REG);

	/*
	 * Coherent mapping avoids explicit cache flush/invalidate, but ownership
	 * fields/data still need ordering. dma_wmb() publishes CPU writes before
	 * the device is told to start. Normal iowrite32() additionally orders prior
	 * normal-memory writes and prior MMIO writes for the default mapping.
	 */
	dma_wmb();
	WRITE_ONCE(dl->dma_in_flight, true);
	iowrite32(cmd, dl->bar0 + DL_EDU_DMA_CMD_REG);

	ret = dl_edu_dma_wait_for_irq(dl, phase);
	if (ret)
		return ret;
	ret = dl_edu_dma_wait_for_cmd_clear(dl, phase);
	if (ret)
		return ret;

	/* Device completion/ownership is observed before CPU consumes DMA data. */
	dma_rmb();
	WRITE_ONCE(dl->dma_in_flight, false);
	dev_info(&dl->pdev->dev, "%s finished\n", phase);
	return 0;
}

static void dl_edu_dma_fill_pattern(struct dl_edu_dma_dev *dl)
{
	size_t i;

	for (i = 0; i < DL_EDU_DMA_BUFFER_BYTES; ++i)
		dl->tx_buf[i] = (u8)(i ^ 0x5a);
	memset(dl->rx_buf, 0, DL_EDU_DMA_BUFFER_BYTES);
}

/*
 * A timeout cannot be followed by blindly freeing a buffer that hardware may
 * still address. First clear bus mastering, then try to bring the function to
 * a known state. Real hardware normally needs a device-specific stop/reset
 * sequence; the generic reset is only the safest available fallback for EDU.
 */
static void dl_edu_dma_quiesce(struct dl_edu_dma_dev *dl)
{
	int ret;

	if (!dl)
		return;

	pci_clear_master(dl->pdev);

	if (dl->bar0 && READ_ONCE(dl->dma_in_flight)) {
		ret = dl_edu_dma_wait_for_cmd_clear(dl, "teardown");
		if (ret) {
			ret = pci_reset_function(dl->pdev);
			if (ret)
				dev_warn(&dl->pdev->dev,
					 "generic function reset failed: %d; "
					 "real hardware needs a device-specific stop path\n",
					 ret);
		}
		WRITE_ONCE(dl->dma_in_flight, false);
	}

	if (dl->bar0)
		dl_edu_dma_ack_pending(dl);

	if (dl->irq_registered) {
		synchronize_irq(dl->irq_vector);
		free_irq(dl->irq_vector, dl);
		dl->irq_registered = false;
	}
}

static int dl_edu_dma_probe(struct pci_dev *pdev,
					const struct pci_device_id *id)
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
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
						 "pci_enable_device failed\n");
	pci_set_master(pdev);

	if (!(pci_resource_flags(pdev, DL_EDU_BAR_INDEX) & IORESOURCE_MEM)) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "BAR%d is not an MMIO resource\n",
			DL_EDU_BAR_INDEX);
		goto err_disable_device;
	}
	dl->bar0_len = pci_resource_len(pdev, DL_EDU_BAR_INDEX);
	if (dl->bar0_len < DL_EDU_DMA_MMIO_MIN_LEN) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "BAR%d too small: len=%llu need>=%u\n",
			DL_EDU_BAR_INDEX,
			(unsigned long long)dl->bar0_len,
			(unsigned int)DL_EDU_DMA_MMIO_MIN_LEN);
		goto err_disable_device;
	}

	ret = pci_request_region(pdev, DL_EDU_BAR_INDEX, KBUILD_MODNAME);
	if (ret)
		goto err_disable_device;
	dl->bar0 = pci_iomap(pdev, DL_EDU_BAR_INDEX, 0);
	if (!dl->bar0) {
		ret = -ENOMEM;
		goto err_release_region;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
	if (ret) {
		dev_err(&pdev->dev,
			"dma_set_mask_and_coherent(28) failed: %d\n", ret);
		goto err_iounmap;
	}
	dev_info(&pdev->dev, "dma mask configured to 28 bits\n");

	dl->dma_buf = dma_alloc_coherent(&pdev->dev,
					  DL_EDU_DMA_BUFFER_BYTES * 2,
					  &dl->dma_handle, GFP_KERNEL);
	if (!dl->dma_buf) {
		ret = -ENOMEM;
		goto err_iounmap;
	}
	dl->tx_buf = dl->dma_buf;
	dl->rx_buf = (u8 *)dl->dma_buf + DL_EDU_DMA_BUFFER_BYTES;
	tx_dma = dl->dma_handle;
	rx_dma = dl->dma_handle + DL_EDU_DMA_BUFFER_BYTES;

	dev_info(&pdev->dev,
		 "coherent buffer allocated: cpu=%p dma=%pad bytes=%u\n",
		 dl->dma_buf, &dl->dma_handle, DL_EDU_DMA_BUFFER_BYTES * 2);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		goto err_free_dma;
	dl->irq_vector = pci_irq_vector(pdev, 0);
	dl->irq_flags = (pdev->msi_enabled || pdev->msix_enabled) ?
				0 : IRQF_SHARED;

	dl_edu_dma_ack_pending(dl);
	ret = request_irq(dl->irq_vector, dl_edu_dma_handler, dl->irq_flags,
				  KBUILD_MODNAME, dl);
	if (ret)
		goto err_free_vectors;
	dl->irq_registered = true;

	dl_edu_dma_fill_pattern(dl);
	ret = dl_edu_dma_run_once(dl, tx_dma, DL_EDU_DEVICE_RAM_OFFSET,
				  DL_EDU_DMA_CMD_START | DL_EDU_DMA_CMD_IRQ,
				  "ram-to-edu transfer");
	if (ret)
		goto err_quiesce;

	ret = dl_edu_dma_run_once(dl, DL_EDU_DEVICE_RAM_OFFSET, rx_dma,
				  DL_EDU_DMA_CMD_START |
				  DL_EDU_DMA_CMD_FROM_DEVICE |
				  DL_EDU_DMA_CMD_IRQ,
				  "edu-to-ram transfer");
	if (ret)
		goto err_quiesce;

	if (memcmp(dl->tx_buf, dl->rx_buf, DL_EDU_DMA_BUFFER_BYTES) != 0) {
		dev_err(&pdev->dev, "round-trip compare failed\n");
		ret = -EIO;
		goto err_quiesce;
	}

	dev_info(&pdev->dev,
		 "round-trip compare passed, irq_count=%u last_status=0x%08x\n",
		 dl->irq_count, dl->last_irq_status);
	return 0;

err_quiesce:
	dl_edu_dma_quiesce(dl);
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
	pci_clear_master(pdev);
	pci_disable_device(pdev);
	return ret;
}

static void dl_edu_dma_remove(struct pci_dev *pdev)
{
	struct dl_edu_dma_dev *dl = pci_get_drvdata(pdev);

	dl_edu_dma_quiesce(dl);
	pci_free_irq_vectors(pdev);
	if (dl && dl->dma_buf)
		dma_free_coherent(&pdev->dev, DL_EDU_DMA_BUFFER_BYTES * 2,
					  dl->dma_buf, dl->dma_handle);
	if (dl && dl->bar0)
		pci_iounmap(pdev, dl->bar0);
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
	.probe = dl_edu_dma_probe,
	.remove = dl_edu_dma_remove,
};
module_pci_driver(dl_edu_dma_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("QEMU EDU coherent DMA lab with explicit ownership and quiesce");
