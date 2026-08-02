// SPDX-License-Identifier: GPL-2.0-only
/* 第一個 PCI lab：match QEMU EDU、map BAR0、做最小 MMIO round-trip。 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>

#define DL_EDU_VENDOR_ID 0x1234
#define DL_EDU_DEVICE_ID 0x11e8
#define DL_EDU_BAR_INDEX 0
#define DL_EDU_IDENT_REG 0x00
#define DL_EDU_LIVENESS_REG 0x04
#define DL_EDU_LIVENESS_PATTERN 0xa5a55a5aU
#define DL_EDU_MMIO_MIN_LEN (DL_EDU_LIVENESS_REG + sizeof(u32))

struct dl_edu_mmio_dev {
	struct pci_dev *pdev;
	/* Byte-addressed base，offset 的單位明確是 byte。 */
	u8 __iomem *bar0;
	resource_size_t bar0_len;
	u32 ident;
	u32 liveness_pattern;
	u32 liveness_result;
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
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed: %d\n", ret);
		return ret;
	}

	/* 先確認 BAR0 真的是 memory resource，且覆蓋會使用的 register。 */
	if (!(pci_resource_flags(pdev, DL_EDU_BAR_INDEX) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "BAR%d is not an MMIO resource\n",
			DL_EDU_BAR_INDEX);
		ret = -ENODEV;
		goto err_disable_device;
	}

	dl->bar0_len = pci_resource_len(pdev, DL_EDU_BAR_INDEX);
	if (dl->bar0_len < DL_EDU_MMIO_MIN_LEN) {
		dev_err(&pdev->dev,
			"BAR%d too small: len=%llu need>=%u\n",
			DL_EDU_BAR_INDEX,
			(unsigned long long)dl->bar0_len,
			(unsigned int)DL_EDU_MMIO_MIN_LEN);
		ret = -ENODEV;
		goto err_disable_device;
	}

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

	dev_info(&pdev->dev, "BAR0 mapped, len=%llu bytes\n",
		 (unsigned long long)dl->bar0_len);

	dl->ident = ioread32(dl->bar0 + DL_EDU_IDENT_REG);
	dev_info(&pdev->dev, "ident=0x%08x\n", dl->ident);

	/*
	 * EDU liveness register 讀回寫入值的 bitwise inverse。這個 read 同時
	 * 讓前一筆 PCI posted write 完成到可被同一裝置 read ordering 觀察的點；
	 * 它不代表所有一般裝置命令都已完成，真實硬體仍要看 status/IRQ。
	 */
	dl->liveness_pattern = DL_EDU_LIVENESS_PATTERN;
	iowrite32(dl->liveness_pattern, dl->bar0 + DL_EDU_LIVENESS_REG);
	dl->liveness_result = ioread32(dl->bar0 + DL_EDU_LIVENESS_REG);
	expected = ~dl->liveness_pattern;
	if (dl->liveness_result != expected) {
		dev_err(&pdev->dev,
			"liveness failed: wrote=0x%08x read=0x%08x expected=0x%08x\n",
			dl->liveness_pattern, dl->liveness_result, expected);
		ret = -EIO;
		goto err_iounmap;
	}

	dev_info(&pdev->dev, "liveness check passed\n");
	return 0;

err_iounmap:
	pci_iounmap(pdev, dl->bar0);
err_release_region:
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
err_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void dl_edu_mmio_remove(struct pci_dev *pdev)
{
	struct dl_edu_mmio_dev *dl = pci_get_drvdata(pdev);

	if (dl && dl->bar0)
		pci_iounmap(pdev, dl->bar0);
	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pci_disable_device(pdev);
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
MODULE_DESCRIPTION("QEMU EDU MMIO lab with validated BAR access");
