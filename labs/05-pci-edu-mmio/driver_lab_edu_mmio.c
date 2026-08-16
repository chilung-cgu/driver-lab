// SPDX-License-Identifier: GPL-2.0-only
/* Minimal PCI + MMIO driver for QEMU EDU. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>

#define DL_EDU_VENDOR_ID 0x1234
#define DL_EDU_DEVICE_ID 0x11e8
#define DL_EDU_BAR_INDEX 0
#define DL_EDU_IDENT_REG 0x00
#define DL_EDU_LIVENESS_REG 0x04
#define DL_EDU_IDENT_SIGNATURE_MASK 0x0000ffffU
#define DL_EDU_IDENT_SIGNATURE 0x000000edU
#define DL_EDU_LIVENESS_PATTERN 0x12345678U
#define DL_EDU_MMIO_MIN_LEN (DL_EDU_LIVENESS_REG + sizeof(u32))

struct dl_edu_mmio_dev {
	struct pci_dev *pdev;
	u8 __iomem *bar0;
	resource_size_t bar0_len;
	u32 ident;
	u32 liveness_written;
	u32 liveness_read;
};

static int dl_edu_mmio_probe(struct pci_dev *pdev,
					 const struct pci_device_id *id)
{
	struct dl_edu_mmio_dev *dl;
	u32 expected;
	int ret;

	pr_info("probe start for %s\n", pci_name(pdev));

	dl = devm_kzalloc(&pdev->dev, sizeof(*dl), GFP_KERNEL);
	if (!dl)
		return -ENOMEM;
	dl->pdev = pdev;
	pci_set_drvdata(pdev, dl);

	ret = pci_enable_device(pdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
						 "pci_enable_device failed\n");

	if (!(pci_resource_flags(pdev, DL_EDU_BAR_INDEX) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "BAR%d is not an MMIO resource\n",
			DL_EDU_BAR_INDEX);
		ret = -ENODEV;
		goto err_disable_device;
	}

	dl->bar0_len = pci_resource_len(pdev, DL_EDU_BAR_INDEX);
	if (dl->bar0_len < DL_EDU_MMIO_MIN_LEN) {
		dev_err(&pdev->dev, "BAR%d too small: len=%llu need>=%u\n",
			DL_EDU_BAR_INDEX,
			(unsigned long long)dl->bar0_len,
			(unsigned int)DL_EDU_MMIO_MIN_LEN);
		ret = -ENODEV;
		goto err_disable_device;
	}

	ret = pci_request_region(pdev, DL_EDU_BAR_INDEX, KBUILD_MODNAME);
	if (ret)
		goto err_disable_device;

	dl->bar0 = pci_iomap(pdev, DL_EDU_BAR_INDEX, 0);
	if (!dl->bar0) {
		ret = -ENOMEM;
		goto err_disable_and_release;
	}

	dev_info(&pdev->dev, "BAR%d mapped, len=%llu bytes\n",
		 DL_EDU_BAR_INDEX, (unsigned long long)dl->bar0_len);

	dl->ident = ioread32(dl->bar0 + DL_EDU_IDENT_REG);
	dev_info(&pdev->dev, "ident=0x%08x\n", dl->ident);
	if ((dl->ident & DL_EDU_IDENT_SIGNATURE_MASK) !=
	    DL_EDU_IDENT_SIGNATURE) {
		dev_err(&pdev->dev,
			"unexpected EDU identification signature: ident=0x%08x\n",
			dl->ident);
		ret = -ENODEV;
		goto err_iounmap;
	}

	dl->liveness_written = DL_EDU_LIVENESS_PATTERN;
	iowrite32(dl->liveness_written, dl->bar0 + DL_EDU_LIVENESS_REG);
	dl->liveness_read = ioread32(dl->bar0 + DL_EDU_LIVENESS_REG);
	expected = ~dl->liveness_written;

	dev_info(&pdev->dev,
		 "liveness wrote=0x%08x read=0x%08x expected=0x%08x\n",
		 dl->liveness_written, dl->liveness_read, expected);
	if (dl->liveness_read != expected) {
		dev_err(&pdev->dev, "liveness check failed\n");
		ret = -EIO;
		goto err_iounmap;
	}

	dev_info(&pdev->dev, "liveness check passed\n");
	return 0;

err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_disable_and_release:
	pci_disable_device(pdev);
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	return ret;
err_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void dl_edu_mmio_remove(struct pci_dev *pdev)
{
	struct dl_edu_mmio_dev *dl = pci_get_drvdata(pdev);

	if (dl && dl->bar0)
		pci_iounmap(pdev, dl->bar0);
	pci_disable_device(pdev);
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pr_info("device removed for %s\n", pci_name(pdev));
}

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
module_pci_driver(dl_edu_mmio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("QEMU EDU PCI/MMIO lab with BAR and identity validation");
