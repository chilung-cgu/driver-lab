// SPDX-License-Identifier: GPL-2.0-only
/* 在 Lab05 的 PCI/MMIO 基礎上加入 QEMU EDU interrupt path。 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>

#define DL_EDU_VENDOR_ID 0x1234
#define DL_EDU_DEVICE_ID 0x11e8
#define DL_EDU_BAR_INDEX 0
#define DL_EDU_IRQ_STATUS_REG 0x24
#define DL_EDU_IRQ_RAISE_REG 0x60
#define DL_EDU_IRQ_ACK_REG 0x64
#define DL_EDU_TEST_IRQ_MASK 0x00000001U
#define DL_EDU_IRQ_TIMEOUT_MS 1000
#define DL_EDU_IRQ_MMIO_MIN_LEN (DL_EDU_IRQ_ACK_REG + sizeof(u32))

struct dl_edu_irq_dev {
	struct pci_dev *pdev;
	u8 __iomem *bar0;
	resource_size_t bar0_len;
	int irq_vector;
	unsigned long irq_flags;
	bool irq_registered;
	struct completion irq_done;
	u32 last_irq_status;
	u32 irq_count;
};

static void dl_edu_irq_ack_pending(struct dl_edu_irq_dev *dl)
{
	u32 status;

	status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
	if (status)
		iowrite32(status, dl->bar0 + DL_EDU_IRQ_ACK_REG);

	/* Read-back completes the posted acknowledge before teardown proceeds. */
	(void)ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
}

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

	/* 不在 hard IRQ 每次印 info；高頻裝置會被 log 本身拖垮。 */
	dev_dbg_ratelimited(&dl->pdev->dev,
				"irq status=0x%08x acknowledged\n", status);
	return IRQ_HANDLED;
}

static void dl_edu_irq_release(struct dl_edu_irq_dev *dl)
{
	if (!dl)
		return;

	/* 先停止/清掉裝置端事件來源，再拆 handler 與其依賴的 MMIO。 */
	if (dl->bar0)
		dl_edu_irq_ack_pending(dl);

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

	/* MSI 是 device-originated memory write；允許裝置成為 bus master。 */
	pci_set_master(pdev);

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

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		goto err_iounmap;

	dl->irq_vector = pci_irq_vector(pdev, 0);
	dl->irq_flags = (pdev->msi_enabled || pdev->msix_enabled) ?
				0 : IRQF_SHARED;

	/* request_irq() 會啟用 handler；註冊前先清掉 pending source。 */
	dl_edu_irq_ack_pending(dl);
	ret = request_irq(dl->irq_vector, dl_edu_irq_handler, dl->irq_flags,
				  KBUILD_MODNAME, dl);
	if (ret)
		goto err_free_vectors;
	dl->irq_registered = true;

	dev_info(&pdev->dev, "request_irq ok: vector=%d flags=0x%lx\n",
		 dl->irq_vector, dl->irq_flags);

	reinit_completion(&dl->irq_done);
	iowrite32(DL_EDU_TEST_IRQ_MASK, dl->bar0 + DL_EDU_IRQ_RAISE_REG);
	timeout = msecs_to_jiffies(DL_EDU_IRQ_TIMEOUT_MS);
	if (!wait_for_completion_timeout(&dl->irq_done, timeout)) {
		dev_err(&pdev->dev, "interrupt self-test timed out after %u ms\n",
			DL_EDU_IRQ_TIMEOUT_MS);
		ret = -ETIMEDOUT;
		goto err_release_irq;
	}

	if (ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG) &
	    DL_EDU_TEST_IRQ_MASK) {
		dev_err(&pdev->dev,
			"interrupt status bit still set after acknowledge\n");
		ret = -EIO;
		goto err_release_irq;
	}

	dev_info(&pdev->dev,
		 "irq status=0x%08x acknowledged; self-test passed count=%u\n",
		 dl->last_irq_status, dl->irq_count);
	return 0;

err_release_irq:
	dl_edu_irq_release(dl);
err_free_vectors:
	pci_free_irq_vectors(pdev);
err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_release_region:
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
err_disable_device:
	pci_clear_master(pdev);
	pci_disable_device(pdev);
	return ret;
}

static void dl_edu_irq_remove(struct pci_dev *pdev)
{
	struct dl_edu_irq_dev *dl = pci_get_drvdata(pdev);

	dl_edu_irq_release(dl);
	pci_free_irq_vectors(pdev);
	if (dl && dl->bar0)
		pci_iounmap(pdev, dl->bar0);
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pci_clear_master(pdev);
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
MODULE_DESCRIPTION("QEMU EDU IRQ lab with explicit quiesce lifecycle");
