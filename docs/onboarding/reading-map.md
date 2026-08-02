# 閱讀地圖

> 這份文件只回答「現在先看什麼、做到什麼才能前進」。文件位置看[`../README.md`](../README.md)，能力gate看[`learning-dashboard.md`](learning-dashboard.md)。

## 使用原則

每一關都走：

```text
預覽目標
→ 讀current README/source
→ 實際執行
→ 保存觀測證據
→ 闔上文件回答問題
→ 再看companion與延伸
```

若source、README、companion不一致，以current source與accuracy audit為準。

## 起步：Lab00～Lab02

先讀：

1. [`learning-dashboard.md`](learning-dashboard.md)
2. [`beginner-primer.md`](beginner-primer.md)
3. [`lab-file-roles.md`](lab-file-roles.md)
4. [`linux-host-setup.md`](linux-host-setup.md)
5. [`check-kernel-env-explained.md`](check-kernel-env-explained.md)

第一個gate：

- Lab00 build/load/unload/dmesg；
- Lab01 debugfs與logging；
- Lab02 `/dev`、VFS、`file_operations.read/write`；
- 能解釋init/error/exit resource lifetime。

搭配：

- [`00-to-01-debugfs-bridge.md`](00-to-01-debugfs-bridge.md)
- [`01-to-03-user-kernel-abi-bridge.md`](01-to-03-user-kernel-abi-bridge.md)
- [`kernel-filesystem-surfaces.md`](kernel-filesystem-surfaces.md)
- [`kernel-api-parameter-roles.md`](kernel-api-parameter-roles.md)

## Lab03：多條ABI path

先讀[`01-to-03-user-kernel-abi-bridge.md`](01-to-03-user-kernel-abi-bridge.md)，再讀Lab03 README/source/test。

前進前要能說明：

- `read/write/ioctl/poll/mmap`各自從哪個syscall進來；
- waitqueue wake只讓條件重新評估，不保證poll以`revents=0`返回；
- 多reader為什麼在取得mutex後仍要重新檢查條件；
- mmap page的permission、lifetime與consistent snapshot為什麼是ABI設計問題。

## Lab04：先練concurrency與lifetime

入口：[`../guides/lab-04-study-order.md`](../guides/lab-04-study-order.md)

不要只記「unsafe加mutex就好」。要能回答：

- 哪些execution paths共享`dl_counter`；
- lost update如何發生；
- `READ_ONCE()`為何不是lock；
- init為何先初始化再啟動kthread；
- exit為何用`kthread_stop()`同步退出。

Lab04 test是probabilistic teaching gate，不是data-race absence proof。

## Lab05：進PCI前先切清host/guest

入口順序：

1. [`../concepts/pcie-primer.md`](../concepts/pcie-primer.md)
2. [`03-to-05-concurrency-pci-bridge.md`](03-to-05-concurrency-pci-bridge.md)
3. [`../guides/lab-05-study-order.md`](../guides/lab-05-study-order.md)
4. [`../guides/qemu-edu-first-pass.md`](../guides/qemu-edu-first-pass.md)
5. [`../../qemu/README.md`](../../qemu/README.md)
6. 跨architecture時看[`../../qemu/arm-host-x86-guest.md`](../../qemu/arm-host-x86-guest.md)
7. [`../guides/linux-guest-05-to-07-walkthrough.md`](../guides/linux-guest-05-to-07-walkthrough.md)

進source前至少要成立：

```sh
uname -m
uname -r
lspci -Dnn | grep 1234:11e8
test -e "/lib/modules/$(uname -r)/build"
```

前進到Lab06前，要能分清：

- raw BAR、PCI resource、`__iomem` mapping；
- request region與iomap；
- normal MMIO accessor、posted write與read-back completion；
- liveness pass能證明與不能證明的事。

## Lab06：IRQ

先讀[`05-to-07-pci-irq-dma-bridge.md`](05-to-07-pci-irq-dma-bridge.md)，再讀Lab06 README/source/test。

前進前要能說明：

- vector allocation、Linux IRQ number與handler registration；
- shared INTx時`IRQ_NONE`與unique `dev_id`；
- request前清pending source；
- handler的ack、短工作與non-sleeping contract；
- remove為什麼先mask/ack/synchronize再free。

## Lab07：DMA

先重新讀[`../concepts/pcie-primer.md`](../concepts/pcie-primer.md)的DMA段，再讀Lab07 README/source/test。

完成gate：

- truthful DMA mask；
- CPU pointer與`dma_addr_t`分離；
- coherent不等於免ordering/completion；
- RAM→EDU→RAM source/destination方向正確；
- IRQ、command idle與`memcmp()`分別驗不同層；
- timeout後不能在未證明quiesce時直接free mapping。

## Lab08～Lab09

完成Lab05～07或至少熟悉Lab03 ABI後，再讀：

1. [`07-to-09-runtime-validation-bridge.md`](07-to-09-runtime-validation-bridge.md)
2. [`../../runtime/README.md`](../../runtime/README.md)
3. Lab08 README/source/test
4. Lab09 README/stress scripts

注意：

- Lab08是userspace wrapper，不是新`.ko`；
- partial I/O、errno、poll error bits、fd/mapping lifetime仍需處理；
- Lab09目前主要是Lab03 reload/parallel stress，不等於完整KUnit/kselftest/fault-injection framework。

## Walkthrough、Checklist、Companion差異

- **Walkthrough**：第一次做，解釋因果。
- **Checklist**：已跑過後速查，不取代理解。
- **Companion `.c.md/.sh.md`**：貼source旁讀，可能落後current source。
- **Accuracy audit**：記錄已知錯誤模型與runtime gap。

## Workflow / Meta

以下不是driver主線，可晚看：

- [`../workflow/ai-agent-git-checkpoint-policy.md`](../workflow/ai-agent-git-checkpoint-policy.md)
- `.githooks/`
- [`../../scripts/install-git-hooks.sh`](../../scripts/install-git-hooks.sh)
