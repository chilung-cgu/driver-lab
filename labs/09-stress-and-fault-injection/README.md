# 09 - stress and fault-injection scaffold

## Current implemented scope

This directory currently exercises **Lab03 only**:

- [`stress-03-reload.sh`](stress-03-reload.sh)
  - Builds Lab03 once.
  - Repeats isolated load/surface-check/unload cycles.
  - Refuses to remove a module that was already loaded by another session.
  - Supports `ITERATIONS=<positive integer>`; default 20.
- [`stress-03-parallel.sh`](stress-03-parallel.sh)
  - Loads Lab03.
  - Runs four concurrent userspace workers through ioctl/status/read/trigger paths.
  - Accepts only successful reads or a bounded GNU `timeout` status 124.
  - Stops/reaps background workers on interruption and does not hide arbitrary failures with `|| true`.
- [`test.sh`](test.sh)
  - Runs the two scripts above.

This is **not yet** a complete fault-injection framework. It does not currently automate failslab/fail_page_alloc, KUnit/kselftest, IRQ/DMA timeout injection, QEMU EDU reset/error tests, or long-duration soak testing.

## Why this lab exists

A single happy-path run does not exercise:

- resource reacquisition after unload;
- concurrent callback interleavings;
- cleanup when a test is interrupted;
- stale `/dev`/sysfs/module state;
- error codes hidden by shell pipelines;
- late IRQ/DMA/work users;
- sanitizer-only failures.

Stress raises the probability of finding a bug. It does not prove the absence of races or lifetime errors.

## Run

Linux only:

```sh
./test.sh
```

Reload only:

```sh
ITERATIONS=100 ./stress-03-reload.sh
```

Parallel only:

```sh
./stress-03-parallel.sh
```

The scripts require the same kernel-module build prerequisites as Lab03 and use `sudo` when not already root. GNU `timeout` is required by the parallel test.

## What a pass currently proves

A pass gives evidence that, in the tested environment:

- Lab03 can be built and loaded repeatedly;
- its character-device and sysfs surfaces appear/disappear;
- multiple userspace clients can exercise the shared state without an immediately observed shell/driver failure;
- expected blocking-read contention is bounded by timeout;
- all worker processes are reaped and the module unloads.

It does **not** prove:

- no data race exists;
- every worker read succeeded;
- mmap lifetime is safe under unload;
- all kernel warnings were absent unless dmesg is reviewed;
- Lab05–07 IRQ/DMA teardown is safe;
- failure labels were executed;
- performance targets are met.

## Regressions added by the 2026-08 audit

The Lab03 smoke test now directly checks:

- writable mmap is rejected;
- empty poll times out rather than returning a false event;
- two blocking readers awakened by one message do not both return;
- the second reader remains waiting for a second message;
- no blocking reader reports a false EOF.

See [`../03-ioctl-poll-mmap/test.sh`](../03-ioctl-poll-mmap/test.sh).

## Next fault-injection work

### Lab03

- Add deterministic concurrent writer/mmap-reader snapshot tests.
- Keep a mapping open during unload attempts and verify the documented lifetime behavior.
- Run with KASAN, KCSAN and lockdep configurations.
- Add targeted allocation failure around page/cdev/class/device acquisition.

### Lab05

- Bind/resource conflict.
- BAR type/length failure.
- Liveness mismatch and repeated bind/unbind.

### Lab06

- IRQ allocation/request failure.
- Missing/incorrect ACK in an isolated VM.
- Timeout plus a deliberately late event.
- Remove while status is pending.

### Lab07

- DMA mask/allocation failure.
- Wrong address/count/direction and data mismatch.
- Timeout followed by successful function reset.
- Timeout with failed reset: verify the mapping is intentionally **not freed**.
- IOMMU on/off and SWIOTLB environments.
- Repeated DMA round-trips and unload after a failed transfer.

## Test discipline

For each run, record:

```text
kernel release/config
repository commit
QEMU/device version where applicable
IOMMU and sanitizer state
exact command/iteration count/random seed
stdout/stderr and full dmesg
resource/IRQ state before and after
```

A useful bug diary format is:

```text
symptom → hypothesis → experiment → evidence → root cause → fix → regression
```

## Safety

- Run aggressive IRQ/DMA/fault injection in a disposable VM or test machine.
- Do not use `rmmod -f` to hide open references or broken teardown.
- Do not enable broad system-wide allocation failure without filters.
- A userspace `timeout` only bounds the process; it is not a kernel/device cancellation protocol.
- If DMA quiesce cannot be proven, leaking a mapping is safer than freeing memory a device may still address.

## References

- Kernel fault injection: <https://docs.kernel.org/fault-injection/index.html>
- KUnit: <https://docs.kernel.org/dev-tools/kunit/index.html>
- kselftest: <https://docs.kernel.org/dev-tools/kselftest.html>
- KASAN/KCSAN/KFENCE: <https://docs.kernel.org/dev-tools/index.html>
- Audit scope: [`../../docs/reference/accuracy-audit-2026-08.md`](../../docs/reference/accuracy-audit-2026-08.md)
