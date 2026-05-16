// SPDX-License-Identifier: GPL-2.0-only
/*
 * 第一個 PCI lab：讓 Linux PCI core 找到 QEMU EDU 裝置，呼叫 probe()，
 * 然後把 BAR0 map 成 CPU 可讀寫的 MMIO 位址。
 */
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

struct dl_edu_mmio_dev {
	struct pci_dev *pdev;
	/* BAR0 map 之後得到的 MMIO base。 */
	void __iomem *bar0;
	resource_size_t bar0_len;
	/* 讀回來的 identification register。 */
	u32 ident;
	u32 liveness_pattern;
	u32 liveness_result;
};

/*
 * probe() 不是 module 載入時無條件執行。
 * 它是在 PCI core 發現有裝置 match dl_edu_mmio_ids 時，才把 pdev 交給 driver。
 */
static int dl_edu_mmio_probe(struct pci_dev *pdev, const struct pci_device_id *id)
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

	/* 第一步先讓 PCI core 幫你把裝置 enable 起來。 */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed: %d\n", ret);
		return ret;
	}

	/* 這一關只碰 BAR0，所以只 request 第 0 個 resource。 */
	ret = pci_request_region(pdev, DL_EDU_BAR_INDEX, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_region BAR%d failed: %d\n",
				DL_EDU_BAR_INDEX, ret);
		goto err_disable_device;
	}

	/* MMIO map 成功後，後面所有 register access 都從這裡出發。 */
	dl->bar0_len = pci_resource_len(pdev, DL_EDU_BAR_INDEX);
	dl->bar0 = pci_iomap(pdev, DL_EDU_BAR_INDEX, 0);
	if (!dl->bar0) {
		dev_err(&pdev->dev, "pci_iomap BAR%d failed\n", DL_EDU_BAR_INDEX);
		ret = -ENOMEM;
		goto err_release_region;
	}

	dev_info(&pdev->dev, "BAR0 mapped, len=%llu bytes\n",
			 (unsigned long long)dl->bar0_len);

	/* 先做最單純的 read-only register 讀取。 */
	dl->ident = ioread32(dl->bar0 + DL_EDU_IDENT_REG);
	dev_info(&pdev->dev, "ident=0x%08x\n", dl->ident);

	/*
	 * QEMU EDU 的 liveness register 會回傳寫入值的 bitwise inverse。
	 * 這是第一個最小可驗證的 MMIO read/write 自我測試。
	 */
	dl->liveness_pattern = DL_EDU_LIVENESS_PATTERN;
	iowrite32(dl->liveness_pattern, dl->bar0 + DL_EDU_LIVENESS_REG);
	dl->liveness_result = ioread32(dl->bar0 + DL_EDU_LIVENESS_REG);
	expected = ~dl->liveness_pattern;

	if (dl->liveness_result != expected) {
		dev_err(&pdev->dev,
				"liveness check failed: wrote=0x%08x read=0x%08x expected=0x%08x\n",
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

	/* remove() 走的順序要跟 probe() 拿資源的順序相反。 */
	if (dl && dl->bar0)
		pci_iounmap(pdev, dl->bar0);

	pci_release_region(pdev, DL_EDU_BAR_INDEX);
	pci_disable_device(pdev);
	pr_info("device removed for %s\n", pci_name(pdev));
}

static const struct pci_device_id dl_edu_mmio_ids[] = {
	/* QEMU EDU device 的 vendor/device ID。 */
	{ PCI_DEVICE(DL_EDU_VENDOR_ID, DL_EDU_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, dl_edu_mmio_ids);

static struct pci_driver dl_edu_mmio_driver = {
	.name = KBUILD_MODNAME,
	.id_table = dl_edu_mmio_ids,
	/* PCI core match 到 id_table 後呼叫 probe；裝置移除或 module 卸載時呼叫 remove。 */
	.probe = dl_edu_mmio_probe,
	.remove = dl_edu_mmio_remove,
};

/* module_pci_driver() 會產生 module init/exit，負責註冊與反註冊 pci_driver。 */
module_pci_driver(dl_edu_mmio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 5 QEMU EDU MMIO lab for driver-lab");
