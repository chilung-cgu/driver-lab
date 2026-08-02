# 06 — QEMU EDU IRQ

> Run in the x86_64 little-endian Linux guest that sees EDU `1234:11e8`. Complete Lab05 first.

## Goal

Extend the validated PCI/MMIO path with one interrupt vector and an EDU self-test:

```text
validate/map BAR0 + EDU identity
→ allocate one PCI IRQ vector
→ clear stale device status
→ request handler
→ enable BME only if MSI/MSI-X was selected
→ write EDU raise register
→ handler reads/filters/ACKs status
→ read-back completes ACK/deasserts legacy INTx
→ completion wakes probe
→ quiesce source/BME/handler before teardown
```

The lab demonstrates the common IRQ lifecycle, not multi-queue MSI-X affinity or production error recovery.

## Prerequisites

```sh
lspci -Dnn | grep '1234:11e8'
test -e /lib/modules/"$(uname -r)"/build
```

No other driver should own EDU, and the module must not already be loaded when the isolated test starts.

## Resources acquired

- enabled PCI function;
- claimed/mapped BAR0;
- one vector from `pci_alloc_irq_vectors()`;
- one Linux IRQ handler registered by `request_irq()`;
- Bus Master Enable only when the selected mode is MSI/MSI-X.

Pure legacy INTx does not require BME for the interrupt line/message emulation used here; MSI/MSI-X is a device-originated Memory Write Request and therefore needs the function authorized to master the bus.

## BAR and identity validation

Before registering IRQs, current source verifies:

- BAR0 is an MMIO resource;
- it covers the highest used register (`0x64` plus 4 bytes);
- identification low 16 bits equal EDU signature `0x00ed`.

This prevents a wrong model, wrong mapping, wrong width/endian assumption or short BAR from being accepted merely because a read returned some value.

Current QEMU EDU uses `DEVICE_NATIVE_ENDIAN`; this repository's runtime target is explicitly x86_64 little-endian. Porting to a big-endian guest requires revalidation.

## Vector allocation

```c
nvec = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
irq = pci_irq_vector(pdev, 0);
```

The actual mode can be MSI-X, MSI or legacy INTx depending on device/platform policy. The driver does not assume the Linux IRQ number equals a PCI vector index or a fixed value.

For legacy mode the handler is registered with `IRQF_SHARED`; the stable per-device state pointer is the `dev_id` used by both handler and `free_irq()`.

## Clear pending before `request_irq()`

EDU status lives at `0x24`, ACK at `0x64`. Before enabling the CPU-side handler:

```text
read status
→ ACK any stale bits
→ read status back
```

Requesting a handler while the source is already asserted can trigger immediately, before the rest of probe assumes a clean test state.

The read-back completes the posted ACK and is especially important for deasserting a level-like legacy INTx source before returning/teardown.

## Handler contract

```text
read status
→ handled = status & 0x1
→ if zero, IRQ_NONE
→ save minimal status/count
→ ACK only handled bit
→ read-back status
→ complete waiter
→ IRQ_HANDLED
```

Why ACK only the handled bit? EDU has another interrupt source (DMA/factorial-related bits). A handler should not discard unrelated events merely because they share the status register.

Hard IRQ constraints:

- no sleeping API;
- short and bounded work;
- no unconditional high-rate `dev_info()`;
- filter shared interrupts correctly;
- acknowledge/mask according to device protocol.

The lab uses ratelimited debug logging and a completion to hand the result to the sleepable probe path.

## BME timing

After the handler is installed:

```c
if (pdev->msi_enabled || pdev->msix_enabled)
    pci_set_master(pdev);
```

BME is enabled only for message-signaled mode and only after the handler/state are ready. It is cleared before handler/vector teardown.

BME does not allocate a vector, create an IRQ handler, trigger an event or guarantee delivery. It only authorizes device-originated memory transactions.

## Self-test

Probe writes bit `0x1` to EDU raise register `0x60`, then waits with a bounded timeout:

```text
reinit completion
→ iowrite32(0x1, RAISE)
→ wait_for_completion_timeout()
→ verify status bit is clear after ACK
```

Evidence layers:

- `request_irq ok`: CPU-side registration succeeded;
- handler status/count: the IRQ path executed;
- post-ACK status clear: device source was cleared;
- timeout bounded: failure cannot hang probe forever.

This does not prove sustained interrupt rate, affinity, no-loss under concurrency or payload correctness.

## Quiesce and remove

Current helper:

```text
ACK/read-back device source
→ if MSI/MSI-X, pci_clear_master()
→ synchronize_irq()
→ free_irq()
→ pci_free_irq_vectors()
→ unmap/release BAR
→ disable device
```

The dependency matters more than mechanically reversing API calls: stop the producer and wait for in-flight handler execution before freeing handler state/MMIO.

`free_irq()` itself has synchronization semantics, but the explicit `synchronize_irq()` documents and tests the intended boundary before the registration is removed.

## Run

```sh
cd labs/06-pci-edu-irq
./test.sh
```

The isolated test:

- refuses to unload a pre-existing module;
- does not clear global `dmesg`;
- verifies EDU/bind/`/proc/interrupts`;
- unloads and checks driver sysfs removal;
- gates only log lines added during this run;
- requires probe, request, handler status, self-test and remove;
- fails on timeout, uncleared source, kernel warning or sanitizer report.

Expected form:

```text
request_irq ok: vector=... mode=MSI/MSI-X|legacy INTx
irq status=0x... acknowledged; self-test passed count=1
device removed
```

The exact IRQ number and mode are platform-dependent.

## Debug order

1. Lab05 identity/BAR path still passes.
2. Vector allocation result and selected mode.
3. Stale status was cleared before request.
4. BME is enabled when MSI/MSI-X is selected.
5. Raise register write reached EDU.
6. Handler status contains test bit.
7. ACK/read-back clears it.
8. Completion and timeout behavior.
9. Quiesce produces no late handler/warning on repeated reload.

## Follow-up validation

- repeat event many times rather than only probe-time self-test;
- force legacy INTx vs MSI and compare behavior;
- repeated load/unload under lockdep/KASAN;
- concurrent reset/remove/fault injection;
- threaded IRQ or deferred work for sleepable processing;
- multiple vectors/per-queue state/affinity;
- interrupt coalescing and throughput/tail latency.

## Self-check

1. Why clear stale status before `request_irq()`?
2. Why does a shared handler return `IRQ_NONE` when its bit is absent?
3. Why ACK only the handled bit and then read back?
4. Why enable BME for MSI/MSI-X but not unconditionally for legacy INTx?
5. What does the completion self-test prove and not prove?
6. Why quiesce the source and synchronize the handler before unmapping BAR0?

<details>
<summary>Reference answers</summary>

1. Requesting the handler enables CPU-side delivery; an already asserted source could fire immediately and pollute probe state.
2. Shared IRQ infrastructure may call multiple handlers; returning `IRQ_NONE` states that this device did not cause the interrupt and allows spurious/shared accounting.
3. ACKing only owned bits preserves unrelated events. Read-back completes the posted write and ensures legacy level-like assertion is deasserted before return/teardown.
4. MSI/MSI-X is a device-originated Memory Write and needs bus mastering authorization; the legacy path does not need that memory write. Enabling BME only when required narrows exposure.
5. It proves one trigger reached the handler, was acknowledged and woke the waiter within a timeout. It does not prove sustained delivery, affinity, no loss or unrelated data correctness.
6. A late handler would access freed state or unmapped MMIO. Stop/clear the producer, then wait for all in-flight handlers before freeing dependencies.

</details>

## References

- MSI guide: <https://docs.kernel.org/PCI/msi-howto.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- PCI APIs: <https://docs.kernel.org/driver-api/pci/pci.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
