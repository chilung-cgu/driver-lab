# Accuracy audit — 2026-08

> Status: static review in progress on branch `review/accuracy-audit-2026-08`.
>
> This document records what was checked, what was corrected, and what still requires a real Linux/QEMU run. It is deliberately stricter than a normal tutorial review: statements are compared against current Linux kernel documentation and the current QEMU EDU specification.

## Source hierarchy

When documents disagree, use this order:

1. the current lab source and its observed behavior on the target kernel;
2. current Linux kernel documentation and QEMU EDU documentation;
3. the lab README and companion documents;
4. generated summaries or external tutorials.

A tutorial simplification is retained only when it does not imply an unsafe API use or a false portability guarantee.

## High-impact findings

### Lab03: `ioctl` / `poll` / `mmap`

- A wake-up only asks the poll core to re-evaluate readiness. Clearing a condition and calling `wake_up_interruptible()` does **not** make a blocking `poll()` return successfully with `revents == 0`; it normally goes back to sleep until a positive event, timeout, signal, or error.
- Multiple blocking readers must re-check the protected condition after taking the mutex. Another reader can consume the message between the waitqueue condition and lock acquisition.
- The shared page is a kernel-to-userspace snapshot, so the userspace mapping should be read-only.
- A page allocated as normal RAM is mapped with `vm_insert_page()` instead of treating it as a raw I/O PFN.
- A sequence field is used so userspace can retry when it races a kernel update; the mutex protects kernel writers but cannot lock an arbitrary userspace reader.
- The actual mapping size is `PAGE_SIZE`; it is not hard-coded to 4096 bytes in the UAPI.

### Lab04: locking demo

- Shared state is initialized before starting the background kthread. Starting the worker first allowed it to increment the counter and then have the initialization path overwrite the result.

### Labs05–07: PCI / MMIO / IRQ / DMA

- BAR0 is checked for `IORESOURCE_MEM` and for the minimum register span before it is accessed.
- MMIO bases use a byte-addressed `u8 __iomem *` so register offsets do not rely on GNU `void *` arithmetic.
- Hard IRQ paths avoid an unconditional `dev_info()` per interrupt.
- IRQ teardown first quiesces/acknowledges the EDU interrupt source, then synchronizes and frees the handler before MMIO is unmapped.
- DMA ordering is explained as separate contracts: `dma_wmb()`/`dma_rmb()` order ownership data in coherent DMA memory; normal `iowrite32()`/`ioread32()` provide the default normal-memory↔MMIO ordering required by the Linux I/O accessor contract. A posted-write read-back is a separate completion issue.

## Validation performed in this review

- Static source review of labs 00–09, the userspace runtime, tests, QEMU launcher, onboarding documents, and the primary README files.
- Cross-check against current Linux documentation for locking, IRQ APIs, DMA mappings, memory barriers, MMIO accessors, PCI driver lifecycle, MSI, and VFIO/IOMMUFD.
- Cross-check against the current QEMU EDU register and DMA specification.

## Validation still required after merge

This environment cannot build or load kernel modules and cannot boot the repository's QEMU guest. The following checks must therefore run in a Linux environment before treating the branch as runtime-verified:

```sh
./scripts/quality.sh .
./scripts/check-kernel-env.sh

make -C runtime clean all

for lab in \
  labs/00-hello-module \
  labs/01-debugfs-logging \
  labs/02-char-device \
  labs/03-ioctl-poll-mmap \
  labs/04-locking-and-races; do
  (cd "$lab" && ./test.sh)
done
```

Inside the QEMU EDU Linux guest:

```sh
for lab in \
  labs/05-pci-edu-mmio \
  labs/06-pci-edu-irq \
  labs/07-pci-edu-dma; do
  (cd "$lab" && ./test.sh)
done
```

Recommended additional checks:

- run Lab03 with two simultaneous blocking readers and confirm only one consumes each published message while the other keeps waiting;
- verify the Lab03 `mmap()` call fails for a writable mapping and succeeds for a read-only mapping;
- repeat load/unload under KASAN and lockdep;
- run Labs06–07 repeatedly and confirm IRQ counts stay bounded and no interrupt arrives after teardown;
- inject or simulate DMA timeout/reset paths before extending Lab07 to real hardware.

## Non-goals

This audit does not turn the teaching labs into production drivers. Production code would additionally require device-specific reset/error recovery, hot-unplug handling, ABI versioning, permissions, per-open lifetime management, comprehensive fault injection, and kernel-version compatibility policy.
