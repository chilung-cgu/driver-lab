# 官方來源索引

> 這份索引只收錄目前這個學習專案最常用、最值得反覆回看的直接來源。

## 核心文件

| 主題 | 來源 | 你要學什麼 |
|---|---|---|
| Linux PCI driver | [How To Write Linux PCI Drivers](https://docs.kernel.org/PCI/pci.html) | `probe/remove`、PCI 初始化、DMA、IRQ 骨架 |
| 外掛模組建置 | [Building External Modules](https://docs.kernel.org/kbuild/modules.html) | kbuild 正確用法 |
| Driver entry/exit | [Driver Basics](https://docs.kernel.org/driver-api/basics.html) | `module_init()` / `module_exit()` 的角色 |
| Kernel logging | [Message logging with printk](https://docs.kernel.org/core-api/printk-basics.html) | `printk`、`pr_info()`、`pr_debug()`、`pr_fmt()` |
| Module license | [Linux kernel licensing rules](https://docs.kernel.org/process/license-rules.html) | `MODULE_LICENSE()` 與 taint / GPL-compatible module 判斷 |
| Kernel/module parameters | [The kernel's command-line parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html) | `module_param()` 與 module parameter 觀念 |
| DMA API | [Dynamic DMA mapping Guide](https://docs.kernel.org/core-api/dma-api-howto.html) | coherent vs streaming DMA、DMA address 觀念 |
| Kernel lock types | [Lock types and their rules](https://docs.kernel.org/locking/locktypes.html) | sleeping lock、spinning lock、context 限制 |
| Mutex | [Generic Mutex Subsystem](https://docs.kernel.org/locking/mutex-design.html) | mutex 作為 shared memory serialization primitive 的定位 |
| Kernel thread | [Driver Basics](https://docs.kernel.org/driver-api/basics.html) | `kthread_run()`、`kthread_should_stop()`、`kthread_stop()` 與 worker lifetime |
| Compiler access wrappers | [Linux kernel memory barriers](https://docs.kernel.org/core-api/wrappers/memory-barriers.html) | `READ_ONCE()` / `WRITE_ONCE()` 不是 lock，只約束單次存取與 compiler/ordering 行為 |
| KUnit | [KUnit](https://docs.kernel.org/dev-tools/kunit/) | 白箱 kernel unit test |
| kselftest | [Linux Kernel Selftests](https://docs.kernel.org/dev-tools/kselftest.html) | 從 userspace 驗證 kernel 行為 |
| Kernel testing overview | [Kernel Testing Guide](https://docs.kernel.org/dev-tools/testing-overview.html) | KASAN、KCSAN、lockdep 等動態檢查工具定位 |
| KCSAN | [Kernel Concurrency Sanitizer](https://docs.kernel.org/dev-tools/kcsan.html) | data race 偵測與 Lab04 後續進階方向 |
| Driver debugging | [Debugging advice for driver development](https://docs.kernel.org/process/debugging/driver_development_debugging_guide.html) | debugfs、ftrace、sanitizers、lockdep |
| Dynamic debug | [Dynamic debug](https://docs.kernel.org/admin-guide/dynamic-debug-howto.html) | 精準開關 `pr_debug()` |
| Fault injection | [Fault injection capabilities infrastructure](https://docs.kernel.org/fault-injection/fault-injection.html) | error path 驗證 |

## PCI / Accelerator 相關

| 主題 | 來源 | 你要學什麼 |
|---|---|---|
| PCI endpoint test host driver | [pci-endpoint-test](https://docs.kernel.org/misc-devices/pci-endpoint-test.html) | BAR / IRQ / read / write / copy 驗證思路 |
| QEMU EDU device | [EDU device](https://www.qemu.org/docs/master/specs/edu.html) | 教學用 PCI 裝置，支援 MMIO / IRQ / DMA |
| QEMU host support | [About QEMU](https://www.qemu.org/docs/master/about/index.html) | 支援哪些 host OS，以及 QEMU 在 host/guest 各自扮演什麼角色 |
| QEMU accelerators | [System Emulation Introduction](https://www.qemu.org/docs/master/system/introduction.html) | `kvm`、`hvf`、`tcg` 的基本定位與可用平台 |
| QEMU PCI test device | [pci-testdev](https://www.qemu.org/docs/master/specs/pci-testdev.html) | 低階 IO 測試裝置 |
| Linux accel subsystem | [Compute Accelerators Introduction](https://docs.kernel.org/accel/introduction.html) | 現代 Linux AI/compute accelerator 驅動在 kernel 中的定位 |

## 品質與靜態檢查

| 主題 | 來源 | 你要學什麼 |
|---|---|---|
| Checkpatch | [Checkpatch](https://docs.kernel.org/dev-tools/checkpatch.html) | 基本風格與常見錯誤 |
| Sparse | [Sparse Documentation](https://sparse.docs.kernel.org/en/latest/) | kernel C 靜態分析 |

## 補充說明

- `Linux Device Drivers, 3rd Edition` 可以當概念輔助，但不能當 API 真相來源。
- 這份索引盡量使用 `docs.kernel.org` 的非版本鎖定 URL，降低文件連結過時的風險。
- 凡是 API signature、helper 行為、current best practice，優先看：
  1. `docs.kernel.org`
  2. kernel tree 內現行程式碼
  3. 目標 subsystem 的 in-tree driver
