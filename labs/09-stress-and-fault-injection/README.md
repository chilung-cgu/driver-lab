# 09 — Stress and fault-injection roadmap

## Current scope

The repository currently implements two repeatable **Lab03 stress gates**:

1. repeated module load/unload;
2. parallel ioctl/status/mmap/read/trigger activity.

It does **not** yet implement a complete KUnit/kselftest/failslab/fail_page_alloc/fail_usercopy framework, nor dedicated Lab05–07 IRQ/DMA fault injection. The directory name is a roadmap as well as a current test suite.

## Why stress is different from proof

- Smoke: one intended path works once.
- Stress: increases the chance of exposing timing/lifetime/resource bugs.
- Regression: the same gates run after every change.
- Fault injection: deliberately forces a specific failure/error path.
- Static reasoning/sanitizers: answer different correctness questions.

A stress pass is evidence, not proof that no race exists. A failure must preserve its first error and evidence rather than be retried until green.

## Run

```sh
cd labs/09-stress-and-fault-injection
./test.sh
```

The suite runs reload first, then parallel stress. Environment variables can scale the individual scripts:

```sh
ITERATIONS=100 ./stress-03-reload.sh

WORKERS=8 \
ITERATIONS=100 \
READ_TIMEOUT_SECONDS=2 \
./stress-03-parallel.sh
```

All values must be positive integers.

## Module ownership

Both scripts refuse to start if `driver_lab_ioctl_poll_mmap` is already loaded. They track whether **this test** loaded the module and only unload their own instance in cleanup.

This avoids destroying a developer's pre-existing debug state and prevents false evidence from an unknown module build/configuration.

## Reload stress

```text
build Lab03
→ record kernel-log baseline
→ repeat N times:
     insmod
     verify /dev + sysfs + /proc/devices
     rmmod
     verify /dev + sysfs disappear
→ isolate new dmesg lines
→ fail on warning/sanitizer signatures
```

This is most useful for:

- init/exit symmetry;
- leaked cdev/class/devt/page references;
- unload lifetime warnings;
- state that survives unexpectedly across reload.

It does not keep fds/VMAs active during unload; that needs separate targeted tests.

## Parallel stress

```text
build Lab03 + runtime
→ load/verify module
→ start W workers
→ each repeats:
     ioctl-write unique record
     status
     mmap-read sequenced snapshot
     bounded blocking read
     trigger event
→ propagate every worker failure
→ clear/unload
→ scan only new kernel logs
```

The read path is record-oriented and another worker may consume the record first. Therefore a bounded read accepts only:

- exit 0: it consumed a record;
- exit 124: GNU `timeout` expired while another reader won.

Any other status is a real failure. The script no longer uses broad `|| true` to hide crashes, `EIO`, invalid CLI arguments or permission errors.

The snapshot read adds pressure to the sequence-publication path while writers update state.

## Kernel-log gate

The scripts do not run `dmesg -C`. They record a line baseline and inspect messages added during the run for:

```text
BUG:
WARNING:
KASAN:
KCSAN:
Oops:
use-after-free
general protection fault
```

If the ring buffer wraps and the baseline cannot be isolated, the test fails honestly rather than presenting incomplete logs as evidence.

On a busy non-isolated system, unrelated warnings can still cause a failure. Run kernel-driver validation in a controlled VM/guest and preserve the complete log.

## What is still missing

### Lab03 targeted fault/lifetime tests

- active fd and VMA while attempting unload;
- signal interruption of blocking read/poll;
- continuous writer causing snapshot retry/EAGAIN;
- allocation and usercopy fault injection;
- compat ioctl and ABI-version regression.

### Lab04

- KCSAN run that observes the intentional unsafe data race;
- lockdep/KASAN repeated reload;
- controlled transition/reset concurrency.

### Lab05–07

- repeated QEMU EDU bind/unbind/load/unload;
- IRQ no-ACK and late-handler tests;
- DMA IRQ timeout, command timeout, reset success/failure;
- IOMMU on/off and SWIOTLB scenarios;
- no-free guarantee when DMA quiescence cannot be proven.

### Test frameworks

- KUnit for pure kernel helpers/state machines;
- kselftest-style userspace ABI tests;
- fault-injection config/fixtures;
- matrix across kernel/QEMU versions, architectures and configs;
- long soak/performance/tail-latency runs.

## Evidence to save

```text
kernel version/config
QEMU version and command line
both repository commit SHAs
sanitizer/IOMMU state
exact stress parameters
stdout/stderr + full new dmesg
resource/IRQ state before/after
first failure, hypothesis, experiment, fix, regression
```

Do not record only the final “passed” line.

## Debug order

### Reload failure

1. first failing iteration;
2. first kernel warning/error;
3. filesystem surface that remained or failed to appear;
4. module ownership/refcount/open fd/VMA;
5. init/error-unwind/exit dependency order.

### Parallel worker failure

1. worker exit status and command;
2. distinguish accepted read timeout 124 from other errors;
3. inspect CLI/runtime validation error;
4. inspect Lab03 record/poll/mmap state;
5. inspect new dmesg before rerunning.

## Self-check

1. Why is a stress pass not proof that no race exists?
2. Why do scripts refuse a pre-loaded module?
3. Why is read timeout 124 accepted but arbitrary `|| true` forbidden?
4. Why add `mmap-read` to parallel workers?
5. Why avoid `dmesg -C`, and what happens if the ring wraps?
6. Which tests would turn the directory into real fault injection rather than stress only?

<details>
<summary>Reference answers</summary>

1. Stress samples a finite set of schedules/states; unobserved interleavings and formal invariant violations may remain.
2. The test cannot claim ownership/version/state and must not unload or destroy someone else's debugging session.
3. Another worker legitimately may consume the single record, so bounded waiting can expire. Other errors indicate a broken command/path and must propagate.
4. It exercises the sequence snapshot concurrently with writers rather than stressing only syscall control/data paths.
5. Kernel log is global diagnostic state. Tests isolate new lines; if wrapping makes isolation unreliable, they fail instead of deleting or misattributing evidence.
6. Deliberately force allocation/usercopy/IRQ/DMA/reset failures and assert exact unwind/quiesce behavior, ideally through KUnit/kselftest/fault-injection fixtures.

</details>
