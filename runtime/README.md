# driver-lab userspace runtime

The runtime is a small POSIX wrapper around the teaching device UAPI. It reduces repeated `open/read/write/ioctl/poll/mmap` boilerplate; it does **not** hide kernel ABI semantics, partial I/O, device lifetime or concurrency.

## Build

```sh
make -C runtime clean all
```

This builds:

```text
tests/driver_lab_char_cli
```

Lab08's own test is only a build/usage gate. End-to-end runtime behavior is exercised by the Lab03 Linux test.

## Handle ownership

```c
struct dl_runtime_handle handle = DL_RUNTIME_HANDLE_INIT;
```

Rules:

- initialize before first open;
- one handle owns exactly one fd;
- do not copy an open handle by value;
- opening an already-open handle returns `EBUSY`;
- default `dl_runtime_open()` uses `O_RDWR | O_CLOEXEC`;
- `dl_runtime_close()` invalidates `handle.fd` **before** calling `close()`;
- never retry `close()` through the handle after an error, because Linux may already have released/reused the descriptor number.

If ownership must be transferred or duplicated, design an explicit API using move/`dup()` semantics; this teaching runtime does not provide one.

## POSIX return conventions

Runtime functions follow userspace conventions:

```text
success → nonnegative value / 0
failure → -1 and errno
```

They do not convert errors to kernel-style negative errno values.

`read()` and `write()` wrappers return the underlying count and may be short. The CLI treats a short write as an error for its record-oriented command, but the generic runtime does not silently loop because retry policy depends on:

- interruption (`EINTR`);
- nonblocking mode (`EAGAIN`);
- partial progress;
- message/record vs byte-stream semantics;
- device-specific cancellation/lifetime.

## API map

| Runtime function | Syscall/UAPI | Lab03 path |
|---|---|---|
| `dl_runtime_open[_flags]` | `open()` | file instance |
| `dl_runtime_read/write` | `read/write` | one global message record |
| `dl_runtime_ioctl_set_message` | `DL_IOC_SET_MESSAGE` | publish message/event |
| `dl_runtime_ioctl_get_status` | `DL_IOC_GET_STATUS` | fixed-width status + actual page size |
| `dl_runtime_ioctl_trigger_event` | `DL_IOC_TRIGGER_EVENT` | event only |
| `dl_runtime_ioctl_clear_buffer` | `DL_IOC_CLEAR_BUFFER` | clear readiness |
| `dl_runtime_poll_readable` | `poll()` | wait for `POLLIN`/`POLLPRI` |
| `dl_runtime_mmap_shared` | `mmap(PROT_READ)` | read-only snapshot page |
| `dl_runtime_read_shared_snapshot` | sequence retry | coherent snapshot copy |
| `dl_runtime_munmap_shared` | `munmap()` | mapping lifetime |

## Message strings

`dl_runtime_ioctl_set_message()` zero-fills `struct dl_ioctl_message`, rejects strings that cannot fit with a terminating NUL, and sends the whole fixed-width structure.

The kernel also rejects a raw 256-byte non-NUL payload. Neither side silently reports success after truncation.

## Poll

```c
short revents = 0;
int rc = dl_runtime_poll_readable(&handle, timeout_ms, &revents);
```

- `rc > 0`: one fd has events; inspect `revents`.
- `rc == 0`: timeout; `revents` remains 0.
- `rc < 0`: error; inspect `errno`.
- `POLLIN|POLLRDNORM`: record available.
- `POLLPRI`: event pending.
- `POLLERR|POLLHUP|POLLNVAL`: error/lifetime conditions; the CLI treats them as failure.

A wake-up can lead to a predicate recheck and sleep again. It does not guarantee a successful `poll()` return.

## Mmap length and permissions

Do not assume a 4096-byte page:

```c
struct dl_ioctl_status status;
dl_runtime_ioctl_get_status(&handle, &status);
```

The CLI verifies:

```text
status.mmap_size == sysconf(_SC_PAGESIZE)
status.mmap_size >= sizeof(struct dl_shared_page)
```

`dl_runtime_mmap_shared()` requests `PROT_READ | MAP_SHARED`. The driver rejects writable/executable mappings and later `mprotect()` upgrades.

The mapping may outlive a direct user call to close the fd; the kernel VMA/file reference model affects unload. Applications must explicitly `munmap()` and must not access after device removal/revocation in a production design.

## Shared snapshot protocol

Kernel writer publishes:

```text
seq = odd
→ write fields and buffer
→ seq = next even
```

Reader:

```text
acquire-load begin seq
→ odd: retry
→ volatile byte-copy complete struct
→ full/read fence
→ acquire-load end seq
→ accept only begin == end == copied seq and even
```

Why volatile byte loads? The page is modified asynchronously outside the C abstract-machine view of the userspace thread. A plain `memcpy()` could be optimized/reused in ways that obscure the intended repeated shared-memory observations. The sequence checks and compiler/CPU fences establish a practical Linux shared-page protocol for this fixed scalar/byte layout.

Limits:

- one kernel writer is serialized by its mutex;
- bounded 1000 retries can return `EAGAIN` under continuous updates;
- this does not protect pointers or objects whose lifetime ends during the read;
- the protocol still requires architecture/runtime testing;
- it is not a replacement for a full userspace ABI/versioning design.

## CLI validation

The CLI additionally:

- uses strict argument counts;
- parses timeout with range checks (`-1` or nonnegative ms);
- uses `O_CLOEXEC`;
- checks short writes and oversized reads;
- validates poll error bits;
- validates snapshot magic/version/pending/length;
- prints strings with bounded precision rather than trusting NUL termination;
- uses the actual mapping size.

## Tests

```sh
# Build/usage only
./labs/08-runtime-library/test.sh

# End-to-end runtime and UAPI behavior on Linux
./labs/03-ioctl-poll-mmap/test.sh
```

High-value follow-ups:

- open an already-open handle and expect `EBUSY`;
- double close is harmless after first invalidation;
- injected `close()` error does not leave an apparently-owned fd;
- nonblocking `EAGAIN` and signal interruption;
- continuous snapshot writer causing bounded retries;
- active mapping plus attempted module unload;
- ABI compatibility/version tests.

## Self-check

1. Why must a handle be initialized and not copied while open?
2. Why invalidate the fd before `close()` rather than after success?
3. Why doesn't the generic wrapper loop on short read/write?
4. How should `poll()` timeout and error bits be interpreted?
5. Why query `mmap_size` and compare it to the userspace page size?
6. Why do sequence checks need actual repeated loads from the mapped page?

<details>
<summary>Reference answers</summary>

1. The integer represents unique ownership; an uninitialized/copy can look like a valid fd and cause leaks or double-close. Use `DL_RUNTIME_HANDLE_INIT` and explicit ownership.
2. Linux may release the descriptor even when `close()` reports a later error. Retrying the same number could close an unrelated newly reused fd.
3. Retry/cancellation policy differs for byte streams, records, nonblocking I/O, EINTR and partial progress; the caller must decide.
4. Zero is timeout, positive requires inspecting `revents`, negative is syscall error. `POLLERR/HUP/NVAL` are not normal data readiness.
5. Kernel `PAGE_SIZE` is architecture/config dependent. A fixed 4096 assumption can create invalid/truncated mappings.
6. Another execution context changes memory asynchronously; sequence values only detect tearing if the payload copy and final sequence check really observe memory in the intended order.

</details>
