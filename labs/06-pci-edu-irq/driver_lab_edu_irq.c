// SPDX-License-Identifier: GPL-2.0-only
/* QEMU EDU interrupt path built on the Lab05 PCI/MMIO foundation. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>

#define DL_EDU_VENDOR_ID 0x1234
#define DL_EDU_DEVICE_ID 0x11e8
#define DL_EDU_BAR_INDEX 0
#define DL_EDU_IDENT_REG 0x00
#define DL_EDU_IDENT_SIGNATURE_MASK 0x0000ffffU
#define DL_EDU_IDENT_SIGNATURE 0x000000edU
#define DL_EDU_FACTORIAL_STATUS_REG 0x20
#define DL_EDU_IRQ_STATUS_REG 0x24
#define DL_EDU_IRQ_RAISE_REG 0x60
#define DL_EDU_IRQ_ACK_REG 0x64
#define DL_EDU_DMA_CMD_REG 0x98
#define DL_EDU_FACTORIAL_IRQ_MASK 0x00000001U
#define DL_EDU_TEST_IRQ_MASK 0x00000002U
#define DL_EDU_DMA_IRQ_MASK 0x00000100U
#define DL_EDU_KNOWN_IRQ_MASK \
	(DL_EDU_FACTORIAL_IRQ_MASK | DL_EDU_TEST_IRQ_MASK | DL_EDU_DMA_IRQ_MASK)
#define DL_EDU_DMA_CMD_RUN 0x01U
#define DL_EDU_IRQ_TIMEOUT_MS 1000
#define DL_EDU_IRQ_MMIO_MIN_LEN (DL_EDU_DMA_CMD_REG + sizeof(u32))

struct dl_edu_irq_dev {
	struct pci_dev *pdev;
	u8 __iomem *bar0;
	resource_size_t bar0_len;
	int irq_vector;
	unsigned long irq_flags;
	bool irq_registered;
	bool bus_master_enabled;
	bool legacy_intx_enabled;
	struct completion irq_done;
	u32 last_irq_status;
	u32 irq_count;
};

static void dl_edu_irq_ack_pending(struct dl_edu_irq_dev *dl)
{
	u32 pending;
	u32 status;

	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (status == ~0U)
		return;
	pending = status & DL_EDU_KNOWN_IRQ_MASK;
	if (pending)
		iowrite32(pending, dl->bar0 + DL_EDU_IRQ_ACK_REG);

	/* Complete the posted acknowledge before changing IRQ ownership/state. */
	(void)ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
}

static void dl_edu_irq_disable_factorial(struct dl_edu_irq_dev *dl)
{
	iowrite32(0, dl->bar0 + DL_EDU_FACTORIAL_STATUS_REG);
	(void)ioread32(dl->bar0 + DL_EDU_FACTORIAL_STATUS_REG);
}

static int dl_edu_irq_wait_for_dma_idle(struct dl_edu_irq_dev *dl)
{
	unsigned long deadline;

	deadline = jiffies + msecs_to_jiffies(DL_EDU_IRQ_TIMEOUT_MS);
	while (time_before(jiffies, deadline)) {
		if (!(ioread32(dl->bar0 + DL_EDU_DMA_CMD_REG) &
		      DL_EDU_DMA_CMD_RUN))
			return 0;
		usleep_range(1000, 2000);
	}

	dev_err(&dl->pdev->dev,
		"probe takeover found DMA command still running after %u ms\n",
		DL_EDU_IRQ_TIMEOUT_MS);
	return -ETIMEDOUT;
}

static irqreturn_t dl_edu_irq_handler(int irq, void *opaque)
{
	struct dl_edu_irq_dev *dl = opaque;
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
	if (pending & ~DL_EDU_TEST_IRQ_MASK)
		dev_warn_ratelimited(&dl->pdev->dev,
			"acknowledged unexpected EDU IRQ status=0x%08x\n",
			status);
	if (!(pending & DL_EDU_TEST_IRQ_MASK))
		return IRQ_HANDLED;

	dl->last_irq_status = status;
	dl->irq_count++;
	complete(&dl->irq_done);
	dev_dbg_ratelimited(&dl->pdev->dev,
				"irq status=0x%08x acknowledged\n", status);
	return IRQ_HANDLED;
}

static void dl_edu_irq_quiesce(struct dl_edu_irq_dev *dl)
{
	if (!dl)
		return;

	if (dl->legacy_intx_enabled) {
		pci_intx(dl->pdev, 0);
		dl->legacy_intx_enabled = false;
	}

	/* Disable the independent producer before clearing pending sources. */
	if (dl->bar0) {
		dl_edu_irq_disable_factorial(dl);
		dl_edu_irq_ack_pending(dl);
	}

	/* MSI/MSI-X are device-originated Memory Writes and require BME. */
	if (dl->bus_master_enabled) {
		pci_clear_master(dl->pdev);
		dl->bus_master_enabled = false;
	}

	if (dl->irq_registered) {
		synchronize_irq(dl->irq_vector);
		free_irq(dl->irq_vector, dl);
		dl->irq_registered = false;
	}
}

static int dl_edu_irq_probe(struct pci_dev *pdev,
					const struct pci_device_id *id)
{
	struct dl_edu_irq_dev *dl;
	unsigned long timeout;
	u32 ident;
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
	pci_clear_master(pdev);
	pci_intx(pdev, 0);

	if (!(pci_resource_flags(pdev, DL_EDU_BAR_INDEX) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "BAR%d is not an MMIO resource\n",
			DL_EDU_BAR_INDEX);
		ret = -ENODEV;
		goto err_disable_device;
	}
	dl->bar0_len = pci_resource_len(pdev, DL_EDU_BAR_INDEX);
	if (dl->bar0_len < DL_EDU_IRQ_MMIO_MIN_LEN) {
		dev_err(&pdev->dev, "BAR%d too small: len=%llu need>=%u\n",
			DL_EDU_BAR_INDEX,
			(unsigned long long)dl->bar0_len,
			(unsigned int)DL_EDU_IRQ_MMIO_MIN_LEN);
		ret = -ENODEV;
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

	dl_edu_irq_disable_factorial(dl);
	ret = dl_edu_irq_wait_for_dma_idle(dl);
	if (ret)
		goto err_iounmap;
	dev_info(&pdev->dev,
		 "probe takeover confirmed DMA command idle with BME disabled\n");
	dl_edu_irq_ack_pending(dl);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		goto err_iounmap;

	dl->irq_vector = pci_irq_vector(pdev, 0);
	dl->irq_flags = (pdev->msi_enabled || pdev->msix_enabled) ?
				0 : IRQF_SHARED;

	/* Quiesce known producers before enabling CPU-side delivery. */
	dl_edu_irq_disable_factorial(dl);
	dl_edu_irq_ack_pending(dl);
	ret = request_irq(dl->irq_vector, dl_edu_irq_handler, dl->irq_flags,
				  KBUILD_MODNAME, dl);
	if (ret)
		goto err_free_vectors;
	dl->irq_registered = true;

	if (pdev->msi_enabled || pdev->msix_enabled) {
		pci_set_master(pdev);
		dl->bus_master_enabled = true;
	} else {
		pci_intx(pdev, 1);
		dl->legacy_intx_enabled = true;
	}

	dev_info(&pdev->dev,
		 "request_irq ok: vector=%d flags=0x%lx mode=%s\n",
		 dl->irq_vector, dl->irq_flags,
		 (pdev->msi_enabled || pdev->msix_enabled) ?
		 "MSI/MSI-X" : "legacy INTx");

	reinit_completion(&dl->irq_done);
	iowrite32(DL_EDU_TEST_IRQ_MASK, dl->bar0 + DL_EDU_IRQ_RAISE_REG);
	timeout = msecs_to_jiffies(DL_EDU_IRQ_TIMEOUT_MS);
	if (!wait_for_completion_timeout(&dl->irq_done, timeout)) {
		dev_err(&pdev->dev, "interrupt self-test timed out after %u ms\n",
			DL_EDU_IRQ_TIMEOUT_MS);
		ret = -ETIMEDOUT;
		goto err_quiesce;
	}

	if (ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG) &
	    DL_EDU_TEST_IRQ_MASK) {
		dev_err(&pdev->dev,
			"interrupt status bit still set after acknowledge\n");
		ret = -EIO;
		goto err_quiesce;
	}

	dev_info(&pdev->dev,
		 "irq status=0x%08x acknowledged; self-test passed count=%u\n",
		 dl->last_irq_status, dl->irq_count);
	return 0;

err_quiesce:
	dl_edu_irq_quiesce(dl);
err_free_vectors:
	pci_free_irq_vectors(pdev);
err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_release_region:
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
err_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void dl_edu_irq_remove(struct pci_dev *pdev)
{
	struct dl_edu_irq_dev *dl = pci_get_drvdata(pdev);

	dl_edu_irq_quiesce(dl);
	pci_free_irq_vectors(pdev);
	if (dl && dl->bar0)
		pci_iounmap(pdev, dl->bar0);
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pci_disable_device(pdev);
	pr_info("device removed for %s\n", pci_name(pdev));
}

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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("QEMU EDU IRQ lab with identity and quiesce validation");
