# Accuracy audit — 2026-08

> Branch: `review/accuracy-audit-2026-08`
>
> Status: static/source audit plus CI work in progress. Runtime MMIO/IRQ/DMA validation still requires the intended Linux/QEMU guest.

## Conclusion

The curriculum order is useful, but several original explanations and lab paths blurred important contracts: task/context switching, poll wake-up semantics, mmap consistency, MMIO ordering vs posted completion, MSI transaction type, DMA address/ownership, and quiesce-before-free.

This branch corrects the primary source and learner-facing entry points first. It remains a teaching project, not a production accelerator driver.

## Earlier `codex/lab05-study-order` branch

The branch exists remotely and contains eight commits not present in the old `main`. It was not merged wholesale because it diverged from this audit branch and includes both useful study-order material and pre-audit simplifications/test-style changes.

Reviewed integration performed here:

- rewrote [`../concepts/pcie-primer.md`](../concepts/pcie-primer.md);
- added [`../guides/lab-04-study-order.md`](../guides/lab-04-study-order.md);
- added [`../guides/lab-05-study-order.md`](../guides/lab-05-study-order.md);
- added/corrected [`../../qemu/arm-host-x86-guest.md`](../../qemu/arm-host-x86-guest.md);
- aligned docs index, reading map, QEMU README, Lab04/05 debug guides and source index;
- retained the original branch as backup/history until this PR is merged.

Examples corrected during integration:

- device receives a DMA address from the DMA API, not an arbitrary CPU physical address;
- driver-first/device-first binding does not imply every physical PCIe device supports hotplug;
- BDF is not stable or fixed across QEMU topologies;
- normal MMIO ordering, PCI posted-write arrival and device command completion are separate;
- shared kernel `dmesg` is not cleared by smoke tests;
- cross-ISA arm64-host/x86_64-guest generally uses TCG, not KVM/HVF acceleration.

After this PR merges, `codex/lab05-study-order` should not be merged separately; doing so could reintroduce superseded versions of the same documents/tests.

## Authority hierarchy

When material disagrees:

1. observed behavior on the target kernel/QEMU setup and current source;
2. current Linux kernel/QEMU official documentation and relevant in-tree examples;
3. current Lab README, reviewed study-order guides and this audit;
4. generated/line-by-line companions and external tutorials.

A simplification is retained only when it does not imply unsafe API use, false portability or a false validation claim.

## High-impact findings and corrections

### Execution context / concurrency

- A syscall entry or hard IRQ entry is not automatically a task context switch.
- Hard IRQ cannot sleep because its execution-context contract forbids blocking/scheduling; the explanation is not “there is no `task_struct`.”
- `READ_ONCE()`/`WRITE_ONCE()` control individual accesses; they are not locks and do not protect a multi-operation invariant.
- Lab04 initializes worker-visible state before `kthread_run()` and uses `kthread_stop()` to synchronize exit before resource teardown.

### Lab03: ioctl / poll / mmap

- Wake-up asks poll/wait logic to re-evaluate readiness. Clearing readiness and waking does not make a blocking `poll()` successfully return `revents == 0`; it normally sleeps again until a positive event, timeout, signal or error.
- Multiple readers re-check the protected predicate after acquiring the mutex; another reader may consume the message first.
- The snapshot mapping is read-only and cannot be upgraded with `mprotect(PROT_WRITE)`.
- Normal RAM is mapped through page-aware VM APIs rather than treated as arbitrary device PFN I/O memory.
- A sequence publication protocol lets userspace detect/retry a concurrent snapshot update; a kernel mutex cannot directly lock arbitrary userspace loads.
- UAPI fields use fixed-width types and the mapping size is the actual `PAGE_SIZE`, not universally 4096.

### PCI/BAR/MMIO

- Raw BAR encoding, PCI core resource and `__iomem` mapping are different address views.
- BAR0 type and minimum register span are validated before access.
- MMIO bases are byte-addressed `u8 __iomem *` so offsets mean bytes.
- `pci_request_region()` claims ownership; `pci_iomap()` creates the I/O mapping.
- Normal I/O accessor ordering, relaxed accessors, PCI posted-write read-back and device operation completion are explained separately.
- A same-device read-back can provide a posted-write completion point; it does not prove a device command finished.

### IRQ

- MSI/MSI-X are Memory Write Requests, not generic PCIe Message TLPs.
- Pending device sources are cleared before handler registration where required by the EDU protocol.
- Hard IRQ avoids unconditional high-volume info logging.
- Teardown masks/acknowledges the source, synchronizes in-flight handler execution and only then frees IRQ/vector and MMIO/state.
- Shared IRQ `dev_id` requirements are not generalized incorrectly to every non-shared IRQ, although stable per-device/per-queue state remains the robust design.

### DMA

- CPU pointer, physical layout and `dma_addr_t` are separate views.
- DMA mask truthfully declares hardware address bits; it is not “larger is safer.”
- `dma_alloc_coherent()` provides coherent CPU/device views under the DMA API, not a universal promise that all underlying pages are physically contiguous/non-cached in a specific implementation.
- Coherence does not remove ownership, ordering, completion or teardown lifetime.
- `dma_wmb()`/`dma_rmb()` order coherent control data around ownership publication/consumption; they do not wait for hardware.
- Lab07 timeout path attempts to stop new bus-master traffic, waits boundedly and uses a function reset fallback. If quiescence still cannot be proven, the teaching fail-safe prefers retaining the mapping over a DMA use-after-free. Real devices require device-specific stop/abort/reset and complete reinitialization.

### Tests / scripts

- PCI tests refuse to unload a module they did not load.
- Tests isolate newly added kernel log lines instead of `dmesg -C`.
- Stress scripts no longer hide arbitrary failures behind broad `|| true`; only documented expected timeout behavior is accepted.
- Static checks cover shell syntax, ShellCheck, Markdown local links, userspace build, external-module compile and whitespace.

## Validation layers

### Source/static review completed

- Labs00–09 source, runtime/CLI, tests, QEMU launcher and major onboarding/guide/reference files.
- Cross-check against Linux locking, memory barrier, device I/O, generic IRQ, PCI, MSI, DMA, VFIO/IOMMUFD and QEMU EDU documentation.
- Selective review/integration of the eight-commit `codex/lab05-study-order` branch.

### CI/static/build gate

GitHub Actions is configured to run:

```sh
./scripts/quality.sh .
make -C runtime clean all
make -C labs/00-hello-module KDIR=<installed generic headers>
...
make -C labs/07-pci-edu-dma KDIR=<installed generic headers>
git diff --check
```

A green run proves those static/build commands on the CI kernel headers; it does not load modules or exercise EDU runtime behavior. The final PR head must be checked for a green run after all integration commits.

### Runtime still required before merge

Labs00–04 on Linux:

```sh
./scripts/check-kernel-env.sh

for lab in \
  labs/00-hello-module \
  labs/01-debugfs-logging \
  labs/02-char-device \
  labs/03-ioctl-poll-mmap \
  labs/04-locking-and-races; do
  (cd "$lab" && ./test.sh)
done
```

Inside a Linux guest that sees EDU:

```sh
uname -a
lspci -Dnnvv -d 1234:11e8

for lab in \
  labs/05-pci-edu-mmio \
  labs/06-pci-edu-irq \
  labs/07-pci-edu-dma; do
  (cd "$lab" && ./test.sh)
done

cat /proc/interrupts
sudo dmesg
```

Recommended additional checks:

- Lab03 two blocking readers/one message and concurrent mmap snapshot readers;
- read-only mapping plus attempted `mprotect(PROT_WRITE)`;
- repeated load/unload under lockdep/KASAN;
- Lab04 KCSAN/stress in addition to the probabilistic demo;
- Lab06 repeated event/teardown with no late handler;
- Lab07 controlled IRQ timeout, command timeout, reset success and reset failure;
- IOMMU on/off or SWIOTLB scenarios where practical.

## Non-goals / uncertainty

This audit does not prove behavior on real PCIe hardware or every kernel/architecture. Production code additionally needs device-specific firmware/queue/reset state machines, hot-unplug and PCI error recovery, security-reviewed UAPI/permissions, pinned-memory policy, PM, multi-queue MSI-X/NUMA and comprehensive fault injection.

Generated companion documents not explicitly rewritten may still contain old line numbers or behavior. They should be regenerated/reviewed after both repository PRs merge and runtime results are recorded.
