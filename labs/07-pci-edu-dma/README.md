# 07 - PCI EDU coherent DMA round-trip

> This lab must run in the Linux guest that sees QEMU EDU (`1234:11e8`).
>
> Current source is authoritative: [`driver_lab_edu_dma.c`](driver_lab_edu_dma.c). The older generated companion may describe pre-audit behavior; use the audited study guide in `pcie-study/docs/phase3-driverlab-guides/08-lab07-guide.md` until companions are regenerated.

## Goal

Build one complete host-side DMA loop:

```text
CPU fills coherent TX memory
→ device DMA: host RAM → EDU local RAM
→ device DMA: EDU local RAM → host RX memory
→ interrupt/status completion
→ CPU compares TX and RX
```

The lab teaches:

- CPU virtual pointer vs device DMA address;
- the QEMU EDU 28-bit DMA mask;
- bus mastering;
- coherent DMA allocation;
- MMIO programming and IRQ acknowledgement;
- data validation, not only interrupt validation;
- fail-safe teardown when DMA quiesce cannot be proven.

It does not teach streaming scatter-gather, user-pinned memory, multi-queue MSI-X, production reset recovery or real-board PCIe PHY behavior.

## Prerequisites

- Lab05 and Lab06 concepts are understood.
- In the guest:

```sh
lspci -Dnn | grep '1234:11e8'
test -e /lib/modules/"$(uname -r)"/build
```

- No other driver owns EDU and `driver_lab_edu_dma` is not already loaded.

## Resource flow

```text
pci_enable_device
→ pci_set_master
→ validate/request/map BAR0
→ dma_set_mask_and_coherent(28 bits)
→ dma_alloc_coherent(TX + RX)
→ allocate vector / request handler
→ run two transfers
```

Normal teardown:

```text
stop/confirm DMA
→ clear bus mastering
→ acknowledge/synchronize/free IRQ
→ free coherent mapping
→ free vectors
→ unmap/release BAR
→ disable device
```

## Two address views

```c
void *cpu_addr;
dma_addr_t dma_handle;

cpu_addr = dma_alloc_coherent(dev, size, &dma_handle, GFP_KERNEL);
```

- Kernel CPU code uses `cpu_addr`.
- EDU registers receive `dma_handle`.
- The values may differ because of IOMMU/platform DMA translation.
- Never cast the CPU pointer or use `virt_to_phys()` as a DMA address.

## Why the mask is 28 bits

QEMU EDU's default DMA engine accepts 28 address bits. The driver must state the real capability and check the return value:

```c
ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
if (ret)
	return ret;
```

Claiming 64-bit support would allow the DMA layer to return an address the EDU engine cannot represent. Truncating such an address would corrupt unrelated memory.

## Ordering and completion

Coherent mapping removes per-transfer cache flush/invalidate requirements, but does not replace ordering or completion:

- `dma_wmb()` publishes CPU-written DMA data before the start/ownership transition.
- Normal `iowrite32()` supplies the default normal-memory-to-MMIO ordering.
- IRQ and command-bit clear establish EDU-specific completion/idle evidence.
- `dma_rmb()` orders CPU consumption after device completion.
- `memcmp()` proves the payload made a correct round-trip.

Do not interpret `dma_wmb()` as “wait for DMA,” or an IRQ as proof that the data is correct.

## Critical timeout rule

A DMA timeout cannot be followed by blindly freeing the mapping. The device may still hold or use the address.

The current teaching fallback is:

```text
clear bus mastering
→ bounded wait for EDU command to clear
→ if still active, try pci_reset_function()
→ acknowledge/synchronize/free IRQ
```

`dl_edu_dma_quiesce()` returns whether the mapping is proven safe to free.

- If EDU stops or reset succeeds, the mapping is freed normally.
- If reset also fails, the driver logs a critical error and **intentionally leaks the coherent mapping** instead of risking DMA use-after-free.

A leak is not a production recovery strategy. It is the fail-safe choice when the only alternatives are “retain inaccessible memory until reboot/platform recovery” or “free memory the device may still overwrite.” Real hardware needs a device-specific stop/abort/reset design and a tested recovery scope.

## Run

```sh
cd labs/07-pci-edu-dma
./test.sh
```

The smoke test:

- refuses to unload a module it did not load;
- does not clear the global kernel log;
- checks only log lines added during this run;
- verifies driver bind and `/proc/interrupts`;
- requires both transfer phases and `round-trip compare passed`;
- fails on kernel warnings, sanitizer reports or unproven DMA quiesce.

Expected evidence includes:

```text
dma mask configured to 28 bits
coherent buffer allocated
ram-to-edu transfer finished
edu-to-ram transfer finished
round-trip compare passed
device removed
```

## What to debug first

| Symptom | First checks |
|---|---|
| DMA mask fails | EDU capability, DMA layer/IOMMU/SWIOTLB, return code |
| IRQ timeout | vector allocation, status bit, ACK, bus mastering, command write |
| Command never clears | address/count/direction, device state, reset path |
| Compare fails | source/destination, direction bit, count, DMA handles, ordering |
| Remove warns about unproven quiesce | do not free/reuse the mapping; inspect reset/device state |

## Required follow-up work before calling this production-ready

- deterministic timeout and failed-reset injection;
- streaming and scatter-gather DMA;
- descriptor ownership ring;
- multiple queues/vectors;
- real-device stop/abort/reset specification;
- hot-unplug/AER/power management;
- IOMMU security and user-buffer lifetime;
- KASAN/lockdep/IOMMU fault testing;
- throughput and tail-latency measurement.

## References

- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- DMA API: <https://docs.kernel.org/core-api/dma-api.html>
- PCI reset APIs: <https://docs.kernel.org/driver-api/pci/pci.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- Audit: [`../../docs/reference/accuracy-audit-2026-08.md`](../../docs/reference/accuracy-audit-2026-08.md)
