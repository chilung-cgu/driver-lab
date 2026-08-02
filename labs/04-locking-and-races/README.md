# 04 — Locking, races, and worker lifetime

> Read [`../../docs/guides/lab-04-study-order.md`](../../docs/guides/lab-04-study-order.md) first.

## Goal

Demonstrate a lost-update race, fix the increment with a mutex, and keep experiment phase changes and teardown deterministic:

```text
multiple userspace ioctl threads + one kernel thread
→ shared counter
→ unsafe read/sleep/write loses updates
→ safe mutex path preserves successful increments
→ mode/reset wait for old increments to finish
→ kthread_stop synchronizes exit
```

This lab does not implement every synchronization primitive listed in the broader concurrency primer. The current executable lesson is specifically:

- unsafe vs mutex-protected increment;
- startup ordering;
- quiescent mode/reset boundaries;
- worker lifetime and module cleanup;
- probabilistic test evidence vs proof.

## Interface

Loading creates:

```text
/dev/driver_lab_race0
```

The fixed-width UAPI supports:

```text
status
reset
safe-mode 0|1
inc <count>
race <threads> <loops>
```

`struct dl_race_status` uses 32-bit fields and a reserved field; it contains no pointers or native-width `unsigned long` ABI.

## Shared actors

| Actor | Path | Shared state |
|---|---|---|
| userspace CLI threads | `ioctl(INC_COUNTER)` | `dl_counter` |
| background kthread | `dl_race_worker_fn()` | `dl_counter` |
| control client | set mode/reset/status | counter/mode/worker state |
| module exit | stop worker then destroy resources | worker lifetime |

## Unsafe mode

```c
snapshot = READ_ONCE(dl_counter);
usleep_range(1000, 2000);
WRITE_ONCE(dl_counter, snapshot + 1);
```

`READ_ONCE/WRITE_ONCE` force individual accesses but do not make the read-modify-write atomic. Two actors can read the same old value and overwrite one another.

The sleep deliberately widens the race window. This function runs only in sleepable process/kthread context; it is not an IRQ example.

## Safe mode

```c
mutex_lock(&dl_race_lock);
dl_counter++;
mutex_unlock(&dl_race_lock);
```

The mutex protects the complete read-modify-write invariant. It is appropriate here because all increment paths may sleep.

Do not generalize this mutex directly to hard IRQ state. IRQ-shared data needs a design appropriate to its contexts, often short spinlock-based sections or per-queue state.

## Why there is also a phase gate

A subtle bug remains if `safe-mode` or `reset` changes while an old unsafe increment is sleeping:

```text
unsafe operation reads old counter
→ control path resets or switches mode
→ old operation wakes and writes stale value into the new phase
```

Current source uses an `rw_semaphore` as an **experiment phase gate**:

- every increment holds the read side;
- many unsafe increments can still run concurrently and race;
- mode/reset/status snapshot takes the write side;
- write-side acquisition waits for all old increments and blocks new ones.

The gate does not fix the unsafe increment. It only gives mode/reset a quiescent boundary so one experiment cannot contaminate the next.

## Startup order

Current init:

```text
initialize counter/mode/worker state
→ register cdev/class
→ start kthread
→ mark worker running
→ publish device node
```

A thread can run immediately after `kthread_run()`. Initializing state afterward could erase its first updates. Publishing the device node last prevents userspace from entering before the worker setup succeeds.

## Teardown

```text
kthread_stop()
→ wait for worker function to return
→ mark worker stopped
→ destroy device/class/cdev/devt
```

Setting a boolean alone is not enough: the worker might not have observed it and could still execute while resources are freed. `kthread_stop()` supplies the stop request plus join-like synchronization.

Open device descriptors hold a module reference because `.owner = THIS_MODULE`; normal `rmmod` should fail while clients still use the fops. The test does not use forced unload.

## Userspace CLI safety

The CLI now:

- validates numeric syntax/ranges;
- rejects `threads * loops` beyond the 32-bit teaching counter;
- checks `pthread_create` and `pthread_join` errors;
- propagates worker ioctl failures;
- uses `O_CLOEXEC`;
- rejects `safe-mode` values other than 0 or 1.

## Test

```sh
cd labs/04-locking-and-races
./test.sh
```

The test:

1. refuses to unload a module it did not load;
2. verifies the worker is running;
3. runs unsafe mode and records the result as probabilistic evidence;
4. switches mode/reset through quiescent boundaries;
5. requires safe mode to observe at least every successful userspace increment;
6. allows the background worker to make the count larger than the minimum;
7. rejects invalid mode input;
8. unloads and checks filesystem cleanup.

### What the test does not prove

- Unsafe mode may occasionally show no net deficit because the worker adds increments and scheduling varies.
- A finite run cannot prove absence of a data race.
- Safe counter correctness does not prove every possible lifetime/deadlock property.
- KCSAN, lockdep, KASAN and stress answer different questions.

Do not change the test into “retry until the desired unsafe number appears”; that hides nondeterminism instead of documenting it.

## Useful manual sequence

```sh
make
cc -Wall -Wextra -Werror -std=c11 -pthread \
  -o ../../tests/driver_lab_race_cli \
  ../../tests/driver_lab_race_cli.c
sudo insmod ./driver_lab_race.ko

CLI=../../tests/driver_lab_race_cli
DEV=/dev/driver_lab_race0
sudo "$CLI" "$DEV" safe-mode 0
sudo "$CLI" "$DEV" reset
sudo "$CLI" "$DEV" race 8 50
sudo "$CLI" "$DEV" safe-mode 1
sudo "$CLI" "$DEV" reset
sudo "$CLI" "$DEV" race 8 50

sudo rmmod driver_lab_race
```

## Debug order

Read [`debug-checklist.md`](debug-checklist.md). First distinguish:

- race not exposed in this run;
- wrong mode or missing reset;
- stale operation crossing a phase boundary;
- userspace thread/ioctl failure;
- worker startup/stop problem;
- module/device-node ownership issue.

## Follow-up validation

- KCSAN for unsynchronized memory-access evidence;
- lockdep for lock-order/context mistakes;
- KASAN for lifetime/UAF;
- repeated load/unload;
- signal/interruption tests around ioctl and module users;
- PREEMPT_RT-specific review only when targeting RT.

## Self-check

1. Why do `READ_ONCE/WRITE_ONCE` not fix the lost update?
2. What invariant does the mutex protect?
3. Why can unsafe increments still race even though there is an `rw_semaphore`?
4. Why must mode/reset take a quiescent boundary?
5. Why initialize state before `kthread_run()` and publish the device node afterward?
6. What does `kthread_stop()` guarantee that a boolean does not?
7. Why is the unsafe test result informational rather than a hard “must be below expected” gate?

<details>
<summary>Reference answers</summary>

1. They constrain each access, not the whole read-compute-write sequence; concurrent actors can still read the same value and overwrite one update.
2. It makes the complete counter increment mutually exclusive in safe mode, so each successful increment is based on the latest value.
3. Each increment takes the read side; multiple readers run concurrently. The write side is reserved for phase transitions, not for serializing increments.
4. An old unsafe operation could wake after reset/mode switch and write a stale value into the next experiment. The write lock waits for all old increments.
5. A new thread may execute immediately, and userspace may open immediately after the node appears. All state/worker resources must be ready before publication.
6. It requests stop and waits until the kthread function returns, establishing that the worker no longer touches state/resources.
7. Race manifestation depends on scheduling and the background worker also increments; finite absence of a deficit is not proof of safety.

</details>

## References

- Lock types: <https://docs.kernel.org/locking/locktypes.html>
- Mutex design: <https://docs.kernel.org/locking/mutex-design.html>
- Kthreads/driver basics: <https://docs.kernel.org/driver-api/basics.html>
- KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>
