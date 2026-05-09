# 從 0 到可練 PCIe AI 加速卡 Host Driver 的 16 週路線

> 如果你目前完全沒有 driver 開發經驗，請先看 [`beginner-primer.md`](beginner-primer.md)，再回來看這份路線圖。

## 核心原則

- 一開始 `不需要實體硬體`
- 先把 `Linux host driver` 的共通骨架練起來
- 把 `QEMU edu` 當成第一個完整的 PCI/MMIO/IRQ/DMA 練習裝置
- 真卡驗證留到後段，避免太早被 vendor-specific 細節卡住

## 第 0 階段，第 1 週

目標：建立可重複 build / load / unload / 看 log 的 Linux lab。

- Lab: [`../labs/00-hello-module`](../labs/00-hello-module)
- 主題：
  - kernel headers
  - kbuild
  - `insmod` / `rmmod` / `modprobe`
  - `dmesg`
  - taint 與 module signing 風險

## 第 1 階段，第 2-3 週

目標：熟悉 kernel module 最基本的觀測與 debug 手段。

- Lab: [`../labs/01-debugfs-logging`](../labs/01-debugfs-logging)
- 主題：
  - `module_init` / `module_exit`
  - `pr_info` / `pr_debug`
  - debugfs
  - dynamic debug
  - 簡單 cleanup pattern

## 第 2 階段，第 4-5 週

目標：熟悉 user-kernel 邊界。

- Lab: [`../labs/02-char-device`](../labs/02-char-device)
- 下個目標 Lab: [`../labs/03-ioctl-poll-mmap`](../labs/03-ioctl-poll-mmap)
- 主題：
  - char device
  - `read` / `write`
  - `copy_to_user` / `copy_from_user`
  - `ioctl`
  - `poll`
  - `mmap`
  - user-space runtime 骨架

> [!NOTE]
> `03-ioctl-poll-mmap` 目前已經有第一版可用實作，可直接當成下一關繼續做。

## 第 3 階段，第 6-7 週

目標：把 race、locking、等待機制練熟。

- Lab: [`../labs/04-locking-and-races`](../labs/04-locking-and-races)
- 前導：[`concurrency-primer.md`](concurrency-primer.md)
- 導讀：[`lab-04-walkthrough.md`](lab-04-walkthrough.md)
- 主題：
  - mutex
  - spinlock
  - atomic
  - completion
  - waitqueue
  - workqueue
  - kthread
  - KASAN / KCSAN / lockdep

## 第 4 階段，第 8-9 週

目標：先讀懂 PCIe / DMA / IRQ 骨架，再開始寫。

- 重點文檔：[`source-index.md`](source-index.md)
- 重點閱讀：[`code-reading-guide.md`](code-reading-guide.md)
- 前導：[`pcie-primer.md`](pcie-primer.md)
- 主題：
  - `pci_register_driver()`
  - `probe/remove`
  - BAR / `pci_iomap()`
  - DMA mask
  - coherent vs streaming DMA
  - INTx / MSI / MSI-X
  - IOMMU 基本觀念

## 第 5 階段，第 10-12 週

目標：使用 QEMU `edu` 裝置完成第一個像樣的 PCI driver。

- Lab: [`../labs/05-pci-edu-mmio`](../labs/05-pci-edu-mmio)
- Lab: [`../labs/06-pci-edu-irq`](../labs/06-pci-edu-irq)
- Lab: [`../labs/07-pci-edu-dma`](../labs/07-pci-edu-dma)
- QEMU 參考：[`../qemu/README.md`](../qemu/README.md)
- 新手導讀：[`qemu-edu-first-pass.md`](qemu-edu-first-pass.md)

> [!NOTE]
> 這三關現在都已有第一版 driver code 與 smoke test。
> 但真正的 build / load / 驗證，仍然必須在 Linux guest 內完成。

## 第 6 階段，第 13-14 週

目標：把「能跑」升級成「能驗證」。

- Lab: [`../labs/08-runtime-library`](../labs/08-runtime-library)
- Lab: [`../labs/09-stress-and-fault-injection`](../labs/09-stress-and-fault-injection)
- 主題：
  - runtime library 補齊與測試
  - 目前 repo 已有：`03` 專用 repeated load-unload / parallel stress
  - 下一步擴充題：`KUnit`
  - 下一步擴充題：`kselftest`
  - 下一步擴充題：`failslab`
  - 下一步擴充題：`fail_page_alloc`
  - 下一步擴充題：`fail_usercopy`
  - stress / regression 紀律

## 第 7 階段，第 15-16 週

目標：把前面練的內容翻譯成 AI 加速卡 Host Driver 語言。

- 文件：[`accelerator-driver-architecture.md`](accelerator-driver-architecture.md)
- 主題：
  - command queue
  - doorbell
  - completion queue
  - firmware loading
  - runtime layering
  - IOMMU / SVA / PASID / SR-IOV 的角色
