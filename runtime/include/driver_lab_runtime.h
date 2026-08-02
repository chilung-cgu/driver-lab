/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DRIVER_LAB_RUNTIME_H
#define DRIVER_LAB_RUNTIME_H

#include <stddef.h>
#include <sys/types.h>

#include "driver_lab_uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A handle owns exactly one file descriptor. Initialize it with
 * DL_RUNTIME_HANDLE_INIT before the first open. Copying an open handle would
 * duplicate the integer without duplicating ownership and is therefore invalid.
 */
struct dl_runtime_handle {
	int fd;
};

#define DL_RUNTIME_HANDLE_INIT { .fd = -1 }

/* dl_runtime_open() uses O_RDWR | O_CLOEXEC. */
int dl_runtime_open(struct dl_runtime_handle *handle, const char *path);
/* Caller-supplied flags are passed to open(); an already-open handle returns EBUSY. */
int dl_runtime_open_flags(struct dl_runtime_handle *handle, const char *path, int flags);
/* Invalidates handle->fd before close(); never retry close() through the handle. */
int dl_runtime_close(struct dl_runtime_handle *handle);

/* POSIX-style wrappers: may return a short count; callers decide whether to retry. */
ssize_t dl_runtime_write(struct dl_runtime_handle *handle, const void *buf, size_t count);
ssize_t dl_runtime_read(struct dl_runtime_handle *handle, void *buf, size_t count);

int dl_runtime_ioctl_set_message(struct dl_runtime_handle *handle, const char *message);
int dl_runtime_ioctl_get_status(struct dl_runtime_handle *handle,
								struct dl_ioctl_status *status);
int dl_runtime_ioctl_trigger_event(struct dl_runtime_handle *handle);
int dl_runtime_ioctl_clear_buffer(struct dl_runtime_handle *handle);

/* Returns poll()'s count/timeout/error result and always initializes *revents if supplied. */
int dl_runtime_poll_readable(struct dl_runtime_handle *handle, int timeout_ms,
							 short *revents);

/*
 * mmap only creates a read-only shared-page mapping. Obtain the length from
 * DL_IOC_GET_STATUS.mmap_size; do not assume PAGE_SIZE == 4096.
 */
void *dl_runtime_mmap_shared(struct dl_runtime_handle *handle, size_t length);
int dl_runtime_munmap_shared(void *addr, size_t length);

/*
 * Read a consistent copy using dl_shared_page.seq. The mapped source is read
 * through volatile byte loads between sequence checks so the compiler cannot
 * replace/reuse the shared-memory accesses. Returns EAGAIN after bounded retry.
 */
int dl_runtime_read_shared_snapshot(const struct dl_shared_page *mapped,
									struct dl_shared_page *snapshot);

#ifdef __cplusplus
}
#endif

#endif
