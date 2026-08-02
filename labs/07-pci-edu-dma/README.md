# 07 — QEMU EDU coherent DMA round-trip

> Run inside the x86_64 little-endian Linux guest that sees QEMU EDU `1234:11e8`.
>
> Current source is authoritative: [`driver_lab_edu_dma.c`](driver_lab_edu_dma.c). Generated companions may describe older code.

## Goal

```text
CPU fills coherent TX memory
→ EDU DMA: host RAM → EDU local RAM
→ EDU DMA: EDU local RAM → host RX memory
→ IRQ + command-idle evidence
→ CPU compares TX and RX
```

This lab teaches:

- CPU virtual pointer vs `dma_addr_t`;
- truthful 28-bit DMA mask;
- coherent allocation and ownership/ordering;
- Bus Master Enable timing;
- MMIO programming and IRQ acknowledgement;
- completion vs payload correctness;
- fail-safe teardown when DMA quiescence cannot be proven.

It does not teach streaming SG, user-pinned memory, multi-queue MSI-X, production reset recovery or real-board PHY/link behavior.

## Prerequisites

```sh
lspci -Dnn | grep '1234:11e8'
test -e /lib/modules/"$(uname -r)"/build
```

Complete Lab05/06 concepts first. No other driver should own EDU, and this module must not already be loaded when running the isolated test.

## Resource flow

```text
pci_enable_device
→ validate/request/map BAR0
→ validate EDU identification signature
→ dma_set_mask_and_coherent(28 bits)
→ dma_alloc_coherent(TX + RX)
→ allocate vector / request handler
→ pci_set_master (last, immediately before device-originated traffic)
→ run two transfers
```

Normal teardown:

```text
clear BME / stop new bus-master traffic
→ prove EDU command idle or reset function
→ acknowledge source
→ synchronize/free IRQ
→ free vector
→ free coherent mapping only if quiescence is proven
→ unmap/release BAR
→ disable device
```

BME is deliberately enabled late. It does not create a DMA mapping, start the engine or prove that an in-flight transaction has stopped.

## Two address views

```c
void *cpu_addr;
dma_addr_t dma_handle;

cpu_addr = dma_alloc_coherent(dev, size, &dma_handle, GFP_KERNEL);
```

- CPU uses `cpu_addr`.
- EDU source/destination registers use `dma_handle` for host memory.
- Values may differ due to IOMMU/platform translation.
- Never cast the CPU pointer or use `virt_to_phys()` as the DMA address.

The driver also checks that the complete two-buffer DMA range fits the EDU 28-bit mask. The DMA API should already enforce the mask; the check is a teaching assertion against accidental truncation or platform/API misuse.

## Host DMA address vs EDU local address

EDU DMA registers use two address domains depending on direction:

```text
RAM → EDU:
  source      = host DMA address
  destination = EDU-local RAM offset 0x40000

EDU → RAM:
  source      = EDU-local RAM offset 0x40000
  destination = host DMA address
```

`0x40000` is not a host DMA mapping. It is an address in EDU's internal 4 KiB buffer aperture. Direction/count/range mistakes may still generate completion; `memcmp()` is required.

## Why the mask is 28 bits

```c
ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(28));
```

The mask states what the hardware can represent. Claiming 64-bit support or truncating a returned address could DMA into unrelated memory.

QEMU's official EDU model defaults to 28 DMA address bits. Record the exact QEMU version in bug reports because model behavior changes: current QEMU master rejects invalid internal/host range combinations before transfer; older versions historically logged a bad range but continued.

## Ordering and completion

Coherent mapping removes per-transfer cache flush/invalidate, not ordering/completion/lifetime.

### RAM → device start

For this simple single-buffer path:

```text
CPU writes coherent TX bytes
→ normal iowrite32() programs registers/start command
```

The normal accessor supplies the documented prior-normal-memory→MMIO ordering. The code intentionally does **not** add a cargo-cult `wmb()` or `dma_wmb()` here.

A real descriptor ring is different:

```text
write descriptor fields
→ dma_wmb()
→ publish OWN/VALID/index
→ normal MMIO doorbell
```

`dma_wmb()` belongs between coherent descriptor data and ownership publication; it does not wait for DMA.

### Device → RAM consumption

```text
IRQ received and acknowledged
→ EDU START bit observed clear
→ dma_rmb()
→ CPU reads RX bytes
→ memcmp()
```

- IRQ proves a notification path.
- START clear is EDU-specific idle evidence.
- `dma_rmb()` orders coherent device writes before CPU consumption after completion has been established.
- `memcmp()` verifies address, direction, length and data content.

No single item substitutes for all the others.

## MMIO endianness boundary

Current QEMU EDU source declares the MMIO region `DEVICE_NATIVE_ENDIAN`, not universally little-endian. This repository's runtime target is explicitly an **x86_64 little-endian guest**, where `ioread32/iowrite32` match the tested model.

Porting to a big-endian guest requires re-validating QEMU target semantics and accessor choices; do not generalize this lab into a fixed-LE hardware specification.

## Critical timeout rule

A timeout cannot be followed by blindly freeing the mapping. The device may still hold/use the DMA address.

Current teaching fallback:

```text
clear bus mastering
→ bounded wait for command clear
→ if still active, try pci_reset_function()
→ acknowledge source
→ synchronize/free IRQ
```

- `pci_reset_function()` saves/restores PCI configuration state around the reset.
- It does not rebuild device-specific firmware, queues or driver software state.
- It is a QEMU EDU teaching fallback, not a universal hardware recovery plan.

The quiesce helper records whether the mapping is safe to free:

- command idle or reset success → free normally;
- reset failure and no proof of idle → log critical error, disable legacy INTx fallback and **intentionally retain the coherent allocation** rather than risk DMA use-after-free.

That leak requires reboot/platform recovery to reclaim. Production hardware needs a device-specific stop/abort/reset/reinit state machine.

## IRQ details

The handler:

```text
read status
→ handle only DMA bit 0x100
→ ACK only that bit (do not discard unrelated factorial bit)
→ read status back to complete/deassert ACK
→ complete waiter
```

The read-back matters for legacy INTx deassertion and posted write completion. Hard IRQ uses ratelimited debug logging rather than unconditional info logging.

## Run

```sh
cd labs/07-pci-edu-dma
./test.sh
```

The smoke test:

- refuses to unload a module it did not load;
- does not clear global kernel logs;
- checks only lines added during this run;
- verifies bind and `/proc/interrupts`;
- requires both transfer phases and `round-trip compare passed`;
- fails on warnings/sanitizer reports/unproven quiescence.

Expected evidence:

```text
dma mask configured to 28 bits
coherent buffer allocated
ram-to-edu transfer finished
edu-to-ram transfer finished
round-trip compare passed
device removed
```

## Debug order

| Symptom | First checks |
|---|---|
| Identity rejected | QEMU EDU model/version, MMIO endian/width, correct function |
| DMA mask fails | platform DMA layer, IOMMU/SWIOTLB, return code |
| Address-range assertion fails | mask contract, allocation size, accidental truncation |
| IRQ timeout | vector mode, BME, status bit, ACK, command write |
| Command never clears | source/destination domain, direction, count, QEMU model/reset path |
| Compare fails | DMA handles vs CPU pointer, local offset, direction/count, ordering |
| Unproven quiesce | do not free/reuse mapping; inspect reset/device state |

## Required follow-up before production claims

- deterministic IRQ/command timeout and failed-reset injection;
- repeated load/unload under KASAN/lockdep;
- streaming and SG DMA;
- descriptor ownership ring;
- multi-queue/vector/NUMA;
- device-specific reset/firmware recovery;
- hot-unplug/AER/PM;
- IOMMU security and pinned-user-memory lifetime;
- throughput/tail-latency tests.

## Self-check

1. Why is BME enabled after mappings/IRQ are ready rather than immediately after `pci_enable_device()`?
2. Which addresses are host DMA addresses and which are EDU-local offsets?
3. Why is there no `dma_wmb()` in this simple start path, but a descriptor ring may need one?
4. What does IRQ, START clear, `dma_rmb()` and `memcmp()` each prove?
5. Why retain a mapping when reset fails?
6. Why is EDU not described as universally little-endian?

<details>
<summary>Reference answers</summary>

1. BME authorizes device-originated memory transactions; enabling it only after buffers, handlers and state exist reduces the interval in which a misbehaving device can access host memory or signal MSI.
2. `dma_handle`/`dma_handle+256` are host DMA addresses; `0x40000` is the EDU internal RAM aperture address used by its engine.
3. Normal `iowrite32()` orders prior coherent CPU writes before the command for this single-buffer path. In a ring, `dma_wmb()` orders descriptor fields before the separate OWN/VALID publication; neither barrier waits for hardware.
4. IRQ proves notification, START clear proves EDU engine idle, `dma_rmb()` orders completed device writes before CPU reads, and `memcmp()` proves payload correctness.
5. If the device may still write, freeing lets memory be reused and creates DMA UAF/corruption. A leak is safer until reboot/recovery, though not a production solution.
6. Current QEMU models the region with `DEVICE_NATIVE_ENDIAN`; this lab promises only its tested x86_64 LE guest configuration. Cross-endian ports must revalidate.

</details>

## References

- QEMU EDU spec/source: <https://www.qemu.org/docs/master/specs/edu.html>, <https://github.com/qemu/qemu/blob/master/hw/misc/edu.c>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- PCI reset APIs: <https://docs.kernel.org/driver-api/pci/pci.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- Audit: [`../../docs/reference/accuracy-audit-2026-08.md`](../../docs/reference/accuracy-audit-2026-08.md)
