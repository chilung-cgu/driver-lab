# 官方來源索引

> 優先收錄Linux kernel、QEMU與工具的直接文件。API signature與current behavior先查目標kernel tree/header與in-tree users，再用這份索引建立背景。

## Kernel / module / driver基礎

| 主題 | 直接來源 | 重點 |
|---|---|---|
| External modules | [Building External Modules](https://docs.kernel.org/kbuild/modules.html) | kbuild、matching headers/config |
| Driver basics | [Driver Basics](https://docs.kernel.org/driver-api/basics.html) | module entry/exit、work/kthread/wait基礎 |
| printk | [Message logging with printk](https://docs.kernel.org/core-api/printk-basics.html) | `pr_*`、log level、`pr_fmt` |
| Dynamic debug | [Dynamic debug](https://docs.kernel.org/admin-guide/dynamic-debug-howto.html) | runtime控制`pr_debug/dev_dbg` |
| License rules | [Linux kernel licensing rules](https://docs.kernel.org/process/license-rules.html) | SPDX、`MODULE_LICENSE`、taint |
| Module parameters | [Kernel parameters](https://docs.kernel.org/admin-guide/kernel-parameters.html) | module parameter模型；具體macro仍看headers |

## Concurrency / execution context / memory model

| 主題 | 直接來源 | 重點 |
|---|---|---|
| Lock types | [Lock types and rules](https://docs.kernel.org/locking/locktypes.html) | sleeping vs spinning、context、PREEMPT_RT邊界 |
| Mutex | [Generic Mutex Subsystem](https://docs.kernel.org/locking/mutex-design.html) | mutex contract與implementation概觀 |
| Memory barriers | [Linux kernel memory barriers](https://docs.kernel.org/core-api/wrappers/memory-barriers.html) | acquire/release、SMP、DMA、MMIO邊界 |
| Atomic types | [Atomic types](https://docs.kernel.org/core-api/wrappers/atomic_t.html) | atomic RMW與ordering |
| KCSAN | [Kernel Concurrency Sanitizer](https://docs.kernel.org/dev-tools/kcsan.html) | data-race detection |
| Lockdep | [Locking correctness validator](https://docs.kernel.org/locking/lockdep-design.html) | dependency、IRQ-safe lock classes |
| KASAN | [Kernel Address Sanitizer](https://docs.kernel.org/dev-tools/kasan.html) | UAF/OOB |

## User/kernel ABI與memory mapping

| 主題 | 直接來源 | 重點 |
|---|---|---|
| Character devices/VFS APIs | [Kernel API](https://docs.kernel.org/core-api/kernel-api.html) | file operations相關helper；實際signature看headers |
| ioctl numbering | [ioctl based interfaces](https://docs.kernel.org/userspace-api/ioctl/ioctl-number.html) | `_IO*`與UAPI設計 |
| mmap internals | [Device memory mapping](https://linux-mm.org/DeviceDriverMmap) | 概念輔助；current API仍看kernel MM docs/source |
| UAPI stability | [Adding a new system call / API guidance](https://docs.kernel.org/process/adding-syscalls.html) | extensible structs、flags、compat設計原則 |

## PCI / MMIO / IRQ

| 主題 | 直接來源 | 重點 |
|---|---|---|
| PCI driver guide | [How To Write Linux PCI Drivers](https://docs.kernel.org/PCI/pci.html) | enable/resources/DMA/IRQ/lifecycle骨架 |
| PCI support APIs | [PCI Support Library](https://docs.kernel.org/driver-api/pci/pci.html) | `pci_*` helpers、reset、resources |
| Device I/O | [Bus-Independent Device Accesses](https://docs.kernel.org/driver-api/device-io.html) | `__iomem`、accessors、relaxed、posted read-back |
| MSI/MSI-X | [MSI Driver Guide HOWTO](https://docs.kernel.org/PCI/msi-howto.html) | vector allocation、ordering、multi-vector locking |
| Generic IRQ | [Generic IRQ Handling](https://docs.kernel.org/core-api/genericirq.html) | request/free/synchronize/threaded IRQ |
| PCI error recovery | [PCI Error Recovery](https://docs.kernel.org/PCI/pci-error-recovery.html) | channel state、callbacks、reset/recovery |
| AER | [PCI Express AER HOWTO](https://docs.kernel.org/PCI/pcieaer-howto.html) | error classes、service driver |
| PCI endpoint test | [pci-endpoint-test](https://docs.kernel.org/misc-devices/pci-endpoint-test.html) | BAR/IRQ/copy驗證思路 |

## DMA / IOMMU / userspace device access

| 主題 | 直接來源 | 重點 |
|---|---|---|
| DMA API HOWTO | [Dynamic DMA mapping Guide](https://docs.kernel.org/core-api/dma-api-howto.html) | address views、mask、coherent/streaming、ownership |
| DMA API reference | [DMA API](https://docs.kernel.org/core-api/dma-api.html) | exact generic DMA interfaces |
| SWIOTLB | [DMA and swiotlb](https://docs.kernel.org/core-api/swiotlb.html) | bounce buffering、addressability |
| VFIO | [VFIO](https://docs.kernel.org/driver-api/vfio.html) | group/container與device access model |
| IOMMUFD | [IOMMUFD userspace API](https://docs.kernel.org/userspace-api/iommufd.html) | current I/O page-table/userspace direction |

## QEMU EDU與跨architecture

| 主題 | 直接來源 | 重點 |
|---|---|---|
| QEMU EDU | [EDU device](https://www.qemu.org/docs/master/specs/edu.html) | BAR0、register、IRQ、DMA engine |
| System invocation | [QEMU invocation](https://www.qemu.org/docs/master/system/invocation.html) | machine/device/network/drive arguments |
| TCG | [TCG documentation](https://www.qemu.org/docs/master/devel/tcg.html) | cross-ISA software translation |
| System emulation intro | [System Emulation](https://www.qemu.org/docs/master/system/introduction.html) | accelerator定位 |
| QEMU PCI test device | [pci-testdev](https://www.qemu.org/docs/master/specs/pci-testdev.html) | 另一個測試device，勿與EDU混淆 |

## Testing / quality

| 主題 | 直接來源 | 重點 |
|---|---|---|
| Driver debugging | [Driver development debugging guide](https://docs.kernel.org/process/debugging/driver_development_debugging_guide.html) | logging、trace、sanitizers、repro |
| KUnit | [KUnit](https://docs.kernel.org/dev-tools/kunit/) | in-kernel white-box tests |
| kselftest | [Linux Kernel Selftests](https://docs.kernel.org/dev-tools/kselftest.html) | userspace-facing regression tests |
| Fault injection | [Fault injection infrastructure](https://docs.kernel.org/fault-injection/fault-injection.html) | allocation/usercopy/error-path injection |
| Checkpatch | [Checkpatch](https://docs.kernel.org/dev-tools/checkpatch.html) | style與部分常見錯誤，不是correctness proof |
| Sparse | [Sparse Documentation](https://sparse.docs.kernel.org/en/latest/) | `__user`、`__iomem`等address-space analysis |
| ShellCheck | [ShellCheck wiki](https://www.shellcheck.net/wiki/) | shell診斷與個別rule說明 |

## 使用原則

1. 先確認目標kernel版本、architecture與config。
2. 查current header/source與in-tree driver，確認實際signature與call pattern。
3. 再看非版本鎖定docs理解contract。
4. `Linux Device Drivers, 3rd Edition`與部落格只當歷史/概念輔助，不作modern API authority。
5. Compile、static analysis、smoke、stress、fault injection回答不同問題；任何單一gate都不是完整correctness proof。
