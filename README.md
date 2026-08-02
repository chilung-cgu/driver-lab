# driver-lab

> Linux host-driver learning labs: module lifecycle → debugfs → char-device UAPI → concurrency → PCI MMIO → IRQ → DMA → userspace runtime → stress.

> [!IMPORTANT]
> A technical accuracy audit is in progress on branch `review/accuracy-audit-2026-08`.
> Read [`docs/reference/accuracy-audit-2026-08.md`](docs/reference/accuracy-audit-2026-08.md) before using companion documents. **Current `.c/.h/.sh` source and observed test results are authoritative; older `.c.md/.h.md/.sh.md` companions may still describe pre-audit behavior until they are regenerated/reviewed.**

## What this repository is

This is a sequence of deliberately small labs. Each lab should answer:

1. Who calls the code path?
2. Which resources become live, and when?
3. Which contexts can access the state concurrently?
4. What observable result proves the path worked?
5. How does every partial failure unwind?
6. How are producers and in-flight users stopped before resources are freed?

It is not a production PCIe driver. The QEMU EDU labs teach the Linux PCI software model, not real-board signal integrity, LTSSM equalization, vendor firmware, hot-plug, SR-IOV, or production reset recovery.

## Authority order

When files disagree, use this order:

1. Current source plus behavior reproduced on the target kernel.
2. Current Linux kernel and QEMU EDU documentation.
3. Lab README and audited reference documents.
4. Companion walkthroughs and generated summaries.

Do not copy a code line solely because a companion calls it a “rule.” Check the API contract and the current source.

## Environment model

```text
Host
  └─ runs QEMU and owns the guest image

Linux guest
  ├─ sees QEMU EDU (1234:11e8)
  ├─ has the running kernel's matching build tree
  ├─ builds/loads Labs05–07
  └─ runs lspci, dmesg, smoke/stress tests
```

- Labs00–04 can run on a suitable Linux host or guest.
- Labs05–07 must run in a Linux environment whose PCI hierarchy contains QEMU EDU.
- macOS can be the QEMU/editor host, but cannot load Linux `.ko` files.
- Cross-architecture QEMU often needs TCG; do not assume HVF/KVM can accelerate a guest of another CPU architecture.

## Recommended first pass

1. [`docs/onboarding/reading-map.md`](docs/onboarding/reading-map.md)
2. [`docs/onboarding/learning-dashboard.md`](docs/onboarding/learning-dashboard.md)
3. [`docs/onboarding/linux-host-setup.md`](docs/onboarding/linux-host-setup.md)
4. Lab00, then Lab01–04
5. [`qemu/README.md`](qemu/README.md) and [`qemu/launch-edu-vm.sh`](qemu/launch-edu-vm.sh)
6. Lab05 → Lab06 → Lab07
7. Runtime/CLI and Lab09 stress

Use the companion after reading the source, not instead of reading it.

## Lab matrix

| Lab | Main concept | Current first-pass validation | Important boundary |
|---|---|---|---|
| 00 | External module build/load/unload | init/exit, parameters, logs | Init failure must unwind itself; exit is not called for a failed load |
| 01 | debugfs, seq_file, dynamic debug | trigger/status/atomic knobs | debugfs is not a stable product UAPI; helper access must share synchronization with driver paths |
| 02 | cdev + read/write | global text-like buffer | Each open file has its own `f_pos`; this is not a multi-client message queue |
| 03 | ioctl/poll/blocking read/mmap | read-only page snapshot with sequence retry | Wake-up only re-evaluates readiness; mutex cannot protect a userspace mmap reader |
| 04 | lost update and mutex | unsafe/safe counter comparison | The sleep only widens the race window; state is initialized before the worker starts |
| 05 | PCI match/BAR/MMIO | BAR validation, ident, liveness | Pure host MMIO does not require bus mastering; read-back is not device-command completion |
| 06 | IRQ vectors/status/ack | one EDU event with timeout | Quiesce/ack and synchronize the handler before MMIO/state teardown |
| 07 | coherent DMA round-trip | 28-bit mask, RAM→EDU→RAM, `memcmp` | CPU pointer is not DMA address; timeout requires quiesce/reset before freeing a possibly active mapping |
| 08 | userspace runtime/CLI | POSIX syscall wrappers and UAPI packaging | Library is not a trust boundary; handle copying, partial I/O and ABI versioning remain design issues |
| 09 | stress scaffold | Lab03 reload + parallel workers | Not yet a complete failslab/KUnit/kselftest or Labs05–07 fault-injection suite |

## Build and quality checks

Repository/static checks:

```sh
./scripts/quality.sh .
./scripts/check-kernel-env.sh
make -C runtime clean all
```

Labs00–04 on Linux:

```sh
for lab in \
  labs/00-hello-module \
  labs/01-debugfs-logging \
  labs/02-char-device \
  labs/03-ioctl-poll-mmap \
  labs/04-locking-and-races; do
  (cd "$lab" && ./test.sh)
done
```

Labs05–07 inside the QEMU EDU guest:

```sh
lspci -Dnn | grep '1234:11e8'

for lab in \
  labs/05-pci-edu-mmio \
  labs/06-pci-edu-irq \
  labs/07-pci-edu-dma; do
  (cd "$lab" && ./test.sh)
done
```

The current audit environment could review and modify source through GitHub, but could not compile modules or boot the guest. A PR must not be described as runtime-verified until the commands above actually run and their logs are attached.

## High-value regression tests to add

- Lab03: two blocking readers, one message; only one consumes, the other keeps waiting.
- Lab03: writable mmap and `mprotect(PROT_WRITE)` are rejected.
- Lab03: concurrent writer/readers never accept an odd or changing snapshot sequence.
- Lab04: safe mode never loses successful increments; startup does not overwrite a worker update.
- Lab06: pending IRQ is cleared before handler registration; repeated teardown has no late handler.
- Lab07: timeout/reset path never frees a mapping while the device may still access it.
- All labs: repeated load/unload under lockdep/KASAN where practical.

See [`labs/09-stress-and-fault-injection/README.md`](labs/09-stress-and-fault-injection/README.md) for the current scaffold and missing work.

## Debugging evidence

Keep a bug diary with:

```text
kernel/QEMU/repository commit
kernel config and sanitizer/IOMMU state
exact command sequence
expected vs observed
full dmesg / stdout / stderr
resource and IRQ state before/after
hypothesis → experiment → evidence → fix → regression
```

A single “passed” log line is not sufficient evidence for IRQ or DMA correctness. Verify status/acknowledgement and payload integrity.

## Technical references

- Linux PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
- Device I/O accessors: <https://docs.kernel.org/driver-api/device-io.html>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- Generic IRQ API: <https://docs.kernel.org/core-api/genericirq.html>
- Locking rules: <https://docs.kernel.org/locking/locktypes.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>

## Scope beyond these labs

A production accelerator driver would additionally need device-specific queue and reset state machines, hot-unplug/error recovery, ABI compatibility, security/permissions, pinned-user-memory policy, IOMMU isolation, multi-queue MSI-X/NUMA, power management, comprehensive fault injection and upstream-compatible coding/testing practices.
