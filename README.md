# driver-lab

> Linux host-driver learning labs: module lifecycle → debugfs → char-device UAPI → concurrency → PCI MMIO → IRQ → DMA → userspace runtime → stress.

> [!IMPORTANT]
> Technical accuracy audit branch: `review/accuracy-audit-2026-08`.
> Read [`docs/reference/accuracy-audit-2026-08.md`](docs/reference/accuracy-audit-2026-08.md). Current source and reproduced behavior are authoritative; generated/line-by-line `.c.md/.h.md/.sh.md` companions may still describe pre-audit code.

## Current integration status

The useful material from the earlier `codex/lab05-study-order` branch has been reviewed and selectively integrated here rather than merged wholesale. In particular:

- [`docs/concepts/pcie-primer.md`](docs/concepts/pcie-primer.md) now distinguishes DMA address from CPU/physical address and separates ordering, posted-write arrival, device completion and payload correctness.
- [`docs/guides/lab-04-study-order.md`](docs/guides/lab-04-study-order.md) and [`docs/guides/lab-05-study-order.md`](docs/guides/lab-05-study-order.md) preserve the study-order idea with corrected concurrency, hotplug, BAR and MMIO details.
- [`qemu/arm-host-x86-guest.md`](qemu/arm-host-x86-guest.md) keeps the cross-architecture workflow with corrected TCG/KVM/HVF, image-format, BDF, headers and sync-path caveats.

The original branch remains useful as history/backup until this PR is merged, but it should not be merged separately into `main` afterward.

## What this repository is

A sequence of deliberately small labs. Each lab should answer:

1. Who calls the code path?
2. Which resources become live, and when?
3. Which contexts can access state concurrently?
4. What observable evidence proves the path worked?
5. How does every partial failure unwind?
6. How are producers and in-flight users stopped before resources are freed?

This is not a production PCIe driver. QEMU EDU teaches the Linux PCI software model, not real-board signal integrity, LTSSM/equalization, vendor firmware, hotplug, SR-IOV or production reset recovery.

## Authority order

When files disagree:

1. Reproduced behavior on the target kernel and current source.
2. Current Linux kernel/QEMU official documentation and relevant in-tree drivers.
3. Current Lab README, reviewed guides and audit documents.
4. Generated summaries and source companions.

Do not copy a line solely because a companion calls it a rule; identify the API/context/device contract.

## Environment model

```text
Host
  └─ runs QEMU and owns guest image/network

Linux guest
  ├─ sees QEMU EDU (1234:11e8)
  ├─ has build tree matching uname -r
  ├─ builds/loads Labs05–07
  └─ runs lspci, dmesg, smoke/stress tests
```

- Labs00–04 can run on a suitable Linux host or guest.
- Labs05–07 require a Linux PCI hierarchy containing EDU.
- macOS can be editor/QEMU host but cannot load Linux `.ko` files.
- Cross-architecture QEMU normally uses TCG; do not assume KVM/HVF accelerates another ISA.

## Recommended first pass

1. [`docs/onboarding/reading-map.md`](docs/onboarding/reading-map.md)
2. [`docs/onboarding/learning-dashboard.md`](docs/onboarding/learning-dashboard.md)
3. [`docs/onboarding/linux-host-setup.md`](docs/onboarding/linux-host-setup.md)
4. Lab00–03
5. [`docs/guides/lab-04-study-order.md`](docs/guides/lab-04-study-order.md) → Lab04
6. [`docs/concepts/pcie-primer.md`](docs/concepts/pcie-primer.md)
7. [`docs/guides/lab-05-study-order.md`](docs/guides/lab-05-study-order.md) + [`qemu/README.md`](qemu/README.md)
8. Lab05 → Lab06 → Lab07
9. Runtime/CLI and Lab09 stress

Read companions after reading source, not instead of source.

## Lab matrix

| Lab | Main concept | Current first-pass validation | Important boundary |
|---|---|---|---|
| 00 | external module lifecycle | init/exit, parameters, logs | failed init must unwind itself; exit is not called |
| 01 | debugfs, seq_file, dynamic debug | trigger/status/atomic knobs | debugfs is not stable product UAPI; helper/driver access needs shared synchronization |
| 02 | cdev + read/write | global text-like buffer | each open has its own `f_pos`; not a multi-client queue |
| 03 | ioctl/poll/blocking read/mmap | read-only sequenced page snapshot | wake only re-evaluates readiness; mutex cannot protect userspace mmap loads |
| 04 | lost update and mutex | unsafe/safe comparison | timing demo is probabilistic; initialize before worker start and synchronize stop |
| 05 | PCI match/BAR/MMIO | BAR validation, ident, liveness | request vs map differ; read-back is not device-command completion |
| 06 | IRQ vector/status/ack | one EDU event with timeout | quiesce/ack and synchronize handler before teardown |
| 07 | coherent DMA round-trip | truthful 28-bit mask, RAM→EDU→RAM, compare | CPU pointer ≠ DMA address; prove quiesce before free |
| 08 | userspace runtime/CLI | POSIX wrappers/UAPI packaging | partial I/O, handle copying, ABI/version/lifetime still matter |
| 09 | stress scaffold | Lab03 reload + parallel workers | not a complete KUnit/kselftest/fault-injection suite |

## Static/build gates

```sh
./scripts/quality.sh .
./scripts/check-kernel-env.sh
make -C runtime clean all
```

GitHub Actions on this PR is configured to run shell syntax/ShellCheck, Markdown local-link checks, runtime/CLI build, Labs00–07 external-module compile and whitespace checks. A green compile/static run is necessary but not runtime proof.

## Runtime gates

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

Labs05–07 inside EDU guest:

```sh
lspci -Dnn | grep '1234:11e8'

for lab in \
  labs/05-pci-edu-mmio \
  labs/06-pci-edu-irq \
  labs/07-pci-edu-dma; do
  (cd "$lab" && ./test.sh)
done
```

Do not describe this PR as runtime-verified until these commands have run on the intended kernel/QEMU setup and logs are attached.

## High-value regression/fault tests

- Lab03: two blocking readers/one message; one consumes, the other remains waiting.
- Lab03: writable mmap and `mprotect(PROT_WRITE)` rejected.
- Lab03: concurrent snapshots never accept odd/changing sequence.
- Lab04: startup does not erase worker updates; use KCSAN/lockdep to supplement probabilistic demo.
- Lab06: pending source cleared before request; repeated teardown has no late handler.
- Lab07: timeout/reset failure never frees mapping while device may still access it.
- All labs: repeated load/unload under lockdep/KASAN where practical.

See [`labs/09-stress-and-fault-injection/README.md`](labs/09-stress-and-fault-injection/README.md) for current scaffold and gaps.

## Bug diary evidence

```text
kernel/QEMU/repository commit
kernel config + sanitizer/IOMMU state
exact command sequence
expected vs observed
full dmesg/stdout/stderr
resource/IRQ state before and after
hypothesis → experiment → evidence → fix → regression
```

A single “passed” line is not enough for IRQ/DMA correctness; verify status/ack and payload integrity.

## Direct references

- [`docs/reference/source-index.md`](docs/reference/source-index.md)
- Linux PCI guide: <https://docs.kernel.org/PCI/pci.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- Lock types: <https://docs.kernel.org/locking/locktypes.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>

## Beyond these labs

A production accelerator driver additionally needs device-specific queue/reset state machines, hot-unplug/error recovery, stable/security-reviewed UAPI, pinned-user-memory policy, IOMMU isolation, multi-queue MSI-X/NUMA, PM, comprehensive fault injection and upstream-compatible design/testing.
