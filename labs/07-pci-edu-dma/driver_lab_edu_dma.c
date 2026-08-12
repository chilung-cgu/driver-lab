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
#define DL_EDU_IDENT_REG 0x00
#define DL_EDU_IDENT_SIGNATURE_MASK 0x0000ffffU
#define DL_EDU_IDENT_SIGNATURE 0x000000edU
#define DL_EDU_FACTORIAL_STATUS_REG 0x20
#define DL_EDU_IRQ_STATUS_REG 0x24
#define DL_EDU_IRQ_ACK_REG 0x64
#define DL_EDU_DMA_SRC_REG 0x80
#define DL_EDU_DMA_DST_REG 0x88
#define DL_EDU_DMA_COUNT_REG 0x90
#define DL_EDU_DMA_CMD_REG 0x98
#define DL_EDU_DEVICE_RAM_OFFSET 0x00040000ULL
#define DL_EDU_DEVICE_RAM_BYTES 4096U
#define DL_EDU_DMA_ADDRESS_BITS 28U
#define DL_EDU_FACTORIAL_IRQ_MASK 0x00000001U
#define DL_EDU_LAB06_TEST_IRQ_MASK 0x00000002U
#define DL_EDU_DMA_IRQ_MASK 0x00000100U
#define DL_EDU_KNOWN_IRQ_MASK \
	(DL_EDU_FACTORIAL_IRQ_MASK | DL_EDU_LAB06_TEST_IRQ_MASK | \
	 DL_EDU_DMA_IRQ_MASK)
#define DL_EDU_DMA_CMD_START 0x01U
#define DL_EDU_DMA_CMD_FROM_DEVICE 0x02U
#define DL_EDU_DMA_CMD_IRQ 0x04U
#define DL_EDU_DMA_WAIT_TIMEOUT_MS 1000
#define DL_EDU_DMA_BUFFER_BYTES 256U
#define DL_EDU_DMA_TOTAL_BYTES (DL_EDU_DMA_BUFFER_BYTES * 2U)
#define DL_EDU_DMA_MMIO_MIN_LEN (DL_EDU_DMA_CMD_REG + sizeof(u32))

struct dl_edu_dma_dev {
	struct pci_dev *pdev;
	u8 __iomem *bar0;
	resource_size_t bar0_len;
	int irq_vector;
	unsigned long irq_flags;
	bool irq_registered;
	bool bus_master_enabled;
	bool legacy_intx_enabled;
	bool dma_in_flight;
	bool dma_mapping_safe_to_free;
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
	u32 pending;
	u32 status;

	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (status == ~0U)
		return IRQ_NONE;
	pending = status & DL_EDU_KNOWN_IRQ_MASK;
	if (!pending)
		return IRQ_NONE;

	iowrite32(pending, dl->bar0 + DL_EDU_IRQ_ACK_REG);
	/* Deassert legacy INTx / complete the posted acknowledge before return. */
	(void)ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (pending & ~DL_EDU_DMA_IRQ_MASK)
		dev_warn_ratelimited(&dl->pdev->dev,
			"acknowledged unexpected EDU IRQ status=0x%08x\n",
			status);
	if (!(pending & DL_EDU_DMA_IRQ_MASK))
		return IRQ_HANDLED;

	dl->last_irq_status = status;
	dl->irq_count++;
	complete(&dl->irq_done);
	dev_dbg_ratelimited(&dl->pdev->dev,
				"dma irq status=0x%08x acknowledged\n", status);
	return IRQ_HANDLED;
}

static void dl_edu_dma_ack_pending(struct dl_edu_dma_dev *dl)
{
	u32 pending;
	u32 status;

	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (status == ~0U)
		return;
	pending = status & DL_EDU_KNOWN_IRQ_MASK;
	if (pending)
		iowrite32(pending, dl->bar0 + DL_EDU_IRQ_ACK_REG);
	(void)ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
}

static void dl_edu_dma_disable_factorial(struct dl_edu_dma_dev *dl)
{
	iowrite32(0, dl->bar0 + DL_EDU_FACTORIAL_STATUS_REG);
	(void)ioread32(dl->bar0 + DL_EDU_FACTORIAL_STATUS_REG);
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

static int dl_edu_dma_program_addrs(struct dl_edu_dma_dev *dl,
							 dma_addr_t src, dma_addr_t dst)
{
	/* EDU is configured with a 28-bit mask, so the 32-bit access is sufficient. */
	if ((u64)src > DMA_BIT_MASK(DL_EDU_DMA_ADDRESS_BITS) ||
	    (u64)dst > DMA_BIT_MASK(DL_EDU_DMA_ADDRESS_BITS)) {
		dev_err(&dl->pdev->dev,
			"DMA address exceeds EDU mask: src=%pad dst=%pad\n",
			&src, &dst);
		return -ERANGE;
	}

	iowrite32(lower_32_bits(src), dl->bar0 + DL_EDU_DMA_SRC_REG);
	iowrite32(lower_32_bits(dst), dl->bar0 + DL_EDU_DMA_DST_REG);
	return 0;
}

static int dl_edu_dma_run_once(struct dl_edu_dma_dev *dl, dma_addr_t src,
						   dma_addr_t dst, u32 cmd,
						   const char *phase)
{
	int ret;

	if (!dl->bus_master_enabled)
		return -EIO;

	reinit_completion(&dl->irq_done);
	ret = dl_edu_dma_program_addrs(dl, src, dst);
	if (ret)
		return ret;

	iowrite32(DL_EDU_DMA_BUFFER_BYTES,
		  dl->bar0 + DL_EDU_DMA_COUNT_REG);

	/*
	 * A normal iowrite32() on the default mapping orders prior coherent-memory
	 * CPU writes before the MMIO start command. Do not add a cargo-cult wmb().
	 * A descriptor ring would still use dma_wmb() between descriptor fields and
	 * its OWN/VALID publication before the normal MMIO doorbell.
	 */
	WRITE_ONCE(dl->dma_in_flight, true);
	iowrite32(cmd, dl->bar0 + DL_EDU_DMA_CMD_REG);

	ret = dl_edu_dma_wait_for_irq(dl, phase);
	if (ret)
		return ret;
	ret = dl_edu_dma_wait_for_cmd_clear(dl, phase);
	if (ret)
		return ret;

	/* Completion is established first; then order device writes before CPU use. */
	if (cmd & DL_EDU_DMA_CMD_FROM_DEVICE)
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
 * Stop new device-originated transactions, prove the command engine is idle or
 * reset the function, then detach the IRQ. If neither idle nor reset can be
 * established, retain the coherent allocation rather than risk DMA UAF.
 * Real hardware needs a device-specific stop/abort/reset/reinit state machine.
 */
static void dl_edu_dma_quiesce(struct dl_edu_dma_dev *dl)
{
	bool safe_to_free = true;
	int ret;

	if (!dl)
		return;

	if (dl->legacy_intx_enabled) {
		pci_intx(dl->pdev, 0);
		dl->legacy_intx_enabled = false;
	}

	if (dl->bar0) {
		dl_edu_dma_disable_factorial(dl);
		dl_edu_dma_ack_pending(dl);
	}

	if (dl->bus_master_enabled) {
		pci_clear_master(dl->pdev);
		dl->bus_master_enabled = false;
	}

	if (dl->bar0 && READ_ONCE(dl->dma_in_flight)) {
		ret = dl_edu_dma_wait_for_cmd_clear(dl, "teardown");
		if (ret) {
			/*
			 * pci_reset_function() saves/restores PCI config, including BAR/MSI
			 * state, but it does not rebuild device-specific queues/firmware.
			 */
			ret = pci_reset_function(dl->pdev);
			if (ret) {
				safe_to_free = false;
				dev_crit(&dl->pdev->dev,
					 "cannot prove DMA quiescence: reset failed: %d; retaining coherent mapping until reboot/platform recovery\n",
					 ret);
			} else {
				dev_warn(&dl->pdev->dev,
					 "function reset used to quiesce EDU; production hardware needs device-specific recovery\n");
			}
		}
		if (safe_to_free)
			WRITE_ONCE(dl->dma_in_flight, false);
	}

	if (dl->bar0)
		dl_edu_dma_ack_pending(dl);

	if (!safe_to_free) {
		/* Prevent a late QEMU EDU event falling back to legacy INTx. */
		pci_intx(dl->pdev, 0);
	}

	if (dl->irq_registered) {
		synchronize_irq(dl->irq_vector);
		free_irq(dl->irq_vector, dl);
		dl->irq_registered = false;
	}

	dl->dma_mapping_safe_to_free = safe_to_free;
}

static void dl_edu_dma_free_irq_vectors(struct dl_edu_dma_dev *dl)
{
	pci_free_irq_vectors(dl->pdev);
	if (!dl->dma_mapping_safe_to_free)
		pci_intx(dl->pdev, 0);
}

static void dl_edu_dma_free_buffer(struct dl_edu_dma_dev *dl)
{
	if (!dl || !dl->dma_buf)
		return;

	if (!dl->dma_mapping_safe_to_free) {
		dev_crit(&dl->pdev->dev,
			 "coherent allocation intentionally retained to avoid DMA use-after-free\n");
		return;
	}

	dma_free_coherent(&dl->pdev->dev, DL_EDU_DMA_TOTAL_BYTES,
				  dl->dma_buf, dl->dma_handle);
	dl->dma_buf = NULL;
}

static int dl_edu_dma_probe(struct pci_dev *pdev,
					const struct pci_device_id *id)
{
	struct dl_edu_dma_dev *dl;
	dma_addr_t tx_dma;
	dma_addr_t rx_dma;
	u32 ident;
	int ret;

	pr_info("probe start for %s\n", pci_name(pdev));

	dl = devm_kzalloc(&pdev->dev, sizeof(*dl), GFP_KERNEL);
	if (!dl)
		return -ENOMEM;
	dl->pdev = pdev;
	dl->dma_mapping_safe_to_free = true;
	init_completion(&dl->irq_done);
	pci_set_drvdata(pdev, dl);

	ret = pci_enable_device(pdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
						 "pci_enable_device failed\n");
	pci_clear_master(pdev);
	pci_intx(pdev, 0);

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

	ident = ioread32(dl->bar0 + DL_EDU_IDENT_REG);
	if ((ident & DL_EDU_IDENT_SIGNATURE_MASK) != DL_EDU_IDENT_SIGNATURE) {
		dev_err(&pdev->dev,
			"unexpected EDU identification signature: ident=0x%08x\n",
			ident);
		ret = -ENODEV;
		goto err_iounmap;
	}

	dl_edu_dma_disable_factorial(dl);
	ret = dl_edu_dma_wait_for_cmd_clear(dl, "probe takeover");
	if (ret)
		goto err_iounmap;
	dev_info(&pdev->dev,
		 "probe takeover confirmed DMA command idle with BME disabled\n");
	dl_edu_dma_ack_pending(dl);

	ret = dma_set_mask_and_coherent(&pdev->dev,
						DMA_BIT_MASK(DL_EDU_DMA_ADDRESS_BITS));
	if (ret) {
		dev_err(&pdev->dev,
			"dma_set_mask_and_coherent(%u) failed: %d\n",
			DL_EDU_DMA_ADDRESS_BITS, ret);
		goto err_iounmap;
	}
	dev_info(&pdev->dev, "dma mask configured to %u bits\n",
		 DL_EDU_DMA_ADDRESS_BITS);

	dl->dma_buf = dma_alloc_coherent(&pdev->dev, DL_EDU_DMA_TOTAL_BYTES,
					  &dl->dma_handle, GFP_KERNEL);
	if (!dl->dma_buf) {
		ret = -ENOMEM;
		goto err_iounmap;
	}

	if ((u64)dl->dma_handle >
	    DMA_BIT_MASK(DL_EDU_DMA_ADDRESS_BITS) -
	    (DL_EDU_DMA_TOTAL_BYTES - 1U)) {
		dev_err(&pdev->dev,
			"coherent DMA range exceeds EDU mask: base=%pad bytes=%u\n",
			&dl->dma_handle, DL_EDU_DMA_TOTAL_BYTES);
		ret = -ERANGE;
		goto err_free_dma;
	}

	dl->tx_buf = dl->dma_buf;
	dl->rx_buf = (u8 *)dl->dma_buf + DL_EDU_DMA_BUFFER_BYTES;
	tx_dma = dl->dma_handle;
	rx_dma = dl->dma_handle + DL_EDU_DMA_BUFFER_BYTES;

	dev_info(&pdev->dev,
		 "coherent buffer allocated: cpu=%p dma=%pad bytes=%u\n",
		 dl->dma_buf, &dl->dma_handle, DL_EDU_DMA_TOTAL_BYTES);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		goto err_free_dma;
	dl->irq_vector = pci_irq_vector(pdev, 0);
	dl->irq_flags = (pdev->msi_enabled || pdev->msix_enabled) ?
				0 : IRQF_SHARED;

	dl_edu_dma_disable_factorial(dl);
	dl_edu_dma_ack_pending(dl);
	ret = request_irq(dl->irq_vector, dl_edu_dma_handler, dl->irq_flags,
				  KBUILD_MODNAME, dl);
	if (ret)
		goto err_free_vectors;
	dl->irq_registered = true;
	if (!(pdev->msi_enabled || pdev->msix_enabled)) {
		pci_intx(pdev, 1);
		dl->legacy_intx_enabled = true;
	}

	/* DMA and MSI are device-originated memory transactions; enable BME last. */
	pci_set_master(pdev);
	dl->bus_master_enabled = true;

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
	dl_edu_dma_free_irq_vectors(dl);
err_free_dma:
	dl_edu_dma_free_buffer(dl);
err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_release_region:
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
err_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void dl_edu_dma_remove(struct pci_dev *pdev)
{
	struct dl_edu_dma_dev *dl = pci_get_drvdata(pdev);

	dl_edu_dma_quiesce(dl);
	dl_edu_dma_free_irq_vectors(dl);
	dl_edu_dma_free_buffer(dl);
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
MODULE_DESCRIPTION("QEMU EDU coherent DMA lab with validated identity and quiesce");
