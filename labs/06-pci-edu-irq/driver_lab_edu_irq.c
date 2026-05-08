#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/completion.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#define DL_EDU_VENDOR_ID 0x1234
#define DL_EDU_DEVICE_ID 0x11e8

#define DL_EDU_BAR_INDEX 0
#define DL_EDU_IRQ_STATUS_REG 0x24
#define DL_EDU_IRQ_RAISE_REG 0x60
#define DL_EDU_IRQ_ACK_REG 0x64
#define DL_EDU_TEST_IRQ_MASK 0x00000001U
#define DL_EDU_IRQ_TIMEOUT_MS 1000

struct dl_edu_irq_dev {
    struct pci_dev *pdev;
    void __iomem *bar0;
    resource_size_t bar0_len;
    /* Linux 分配給這顆裝置的 IRQ vector。 */
    int irq_vector;
    unsigned long irq_flags;
    /* 用 completion 讓 probe() 可以等待自我測試中斷完成。 */
    struct completion irq_done;
    u32 last_irq_status;
    u32 irq_count;
};

static irqreturn_t dl_edu_irq_handler(int irq, void *opaque)
{
    struct dl_edu_irq_dev *dl = opaque;
    u32 status;

    /* 先讀 interrupt status，再決定這是不是我們要處理的事件。 */
    status = ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG);
    if (!(status & DL_EDU_TEST_IRQ_MASK))
        return IRQ_NONE;

    dl->last_irq_status = status;
    dl->irq_count++;

    /* EDU 即使用 MSI，也仍然需要明確寫 acknowledge register 清中斷。 */
    iowrite32(status, dl->bar0 + DL_EDU_IRQ_ACK_REG);
    complete(&dl->irq_done);
    dev_info(&dl->pdev->dev, "irq status=0x%08x acknowledged\n", status);

    return IRQ_HANDLED;
}

static int dl_edu_irq_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct dl_edu_irq_dev *dl;
    unsigned long timeout_jiffies;
    int ret;

    pr_info("probe start for %s\n", pci_name(pdev));

    dl = devm_kzalloc(&pdev->dev, sizeof(*dl), GFP_KERNEL);
    if (!dl)
        return -ENOMEM;

    dl->pdev = pdev;
    init_completion(&dl->irq_done);
    pci_set_drvdata(pdev, dl);

    /* 先把最基本的 PCI enable / BAR map 做好，IRQ 才有地方可操作。 */
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "pci_enable_device failed: %d\n", ret);
        return ret;
    }

    ret = pci_request_region(pdev, DL_EDU_BAR_INDEX, KBUILD_MODNAME);
    if (ret) {
        dev_err(&pdev->dev, "pci_request_region BAR%d failed: %d\n",
                DL_EDU_BAR_INDEX, ret);
        goto err_disable_device;
    }

    dl->bar0_len = pci_resource_len(pdev, DL_EDU_BAR_INDEX);
    dl->bar0 = pci_iomap(pdev, DL_EDU_BAR_INDEX, 0);
    if (!dl->bar0) {
        dev_err(&pdev->dev, "pci_iomap BAR%d failed\n", DL_EDU_BAR_INDEX);
        ret = -ENOMEM;
        goto err_release_region;
    }

    /*
     * 先向 PCI core 要 1 條向量。
     * 若平台支援 MSI，通常會先拿到 MSI；否則退回 legacy INTx。
     */
    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
    if (ret < 0) {
        dev_err(&pdev->dev, "pci_alloc_irq_vectors failed: %d\n", ret);
        goto err_iounmap;
    }

    dl->irq_vector = pci_irq_vector(pdev, 0);
    dl->irq_flags = (pdev->msi_enabled || pdev->msix_enabled) ? 0 : IRQF_SHARED;

    ret = request_irq(dl->irq_vector, dl_edu_irq_handler, dl->irq_flags,
                      KBUILD_MODNAME, dl);
    if (ret) {
        dev_err(&pdev->dev, "request_irq failed: %d\n", ret);
        goto err_free_vectors;
    }

    dev_info(&pdev->dev, "request_irq ok: vector=%d flags=0x%lx\n",
             dl->irq_vector, dl->irq_flags);

    /* 自我測試：直接寫 interrupt raise register，把事件打進 handler。 */
    reinit_completion(&dl->irq_done);
    iowrite32(DL_EDU_TEST_IRQ_MASK, dl->bar0 + DL_EDU_IRQ_RAISE_REG);

    timeout_jiffies = msecs_to_jiffies(DL_EDU_IRQ_TIMEOUT_MS);
    if (!wait_for_completion_timeout(&dl->irq_done, timeout_jiffies)) {
        dev_err(&pdev->dev, "interrupt self-test timed out after %u ms\n",
                DL_EDU_IRQ_TIMEOUT_MS);
        ret = -ETIMEDOUT;
        goto err_free_irq;
    }

    if (ioread32(dl->bar0 + DL_EDU_IRQ_STATUS_REG) & DL_EDU_TEST_IRQ_MASK) {
        dev_err(&pdev->dev, "interrupt status bit still set after acknowledge\n");
        ret = -EIO;
        goto err_free_irq;
    }

    dev_info(&pdev->dev, "irq self-test passed, count=%u\n", dl->irq_count);
    return 0;

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
}

static void dl_edu_irq_remove(struct pci_dev *pdev)
{
    struct dl_edu_irq_dev *dl = pci_get_drvdata(pdev);

    /* IRQ 相關資源要先拆，再拆 MMIO 與 PCI resource。 */
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
MODULE_DESCRIPTION("Week 6 QEMU EDU IRQ lab for driver-lab");
