// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include "../runtime/include/driver_lab_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>

static int expect(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s (errno=%d)\n", message, errno);
		return -1;
	}
	return 0;
}

int main(void)
{
	struct dl_runtime_handle handle = DL_RUNTIME_HANDLE_INIT;
	struct dl_runtime_handle invalid = DL_RUNTIME_HANDLE_INIT;
	struct dl_shared_page snapshot;
	short revents = 123;
	int failures = 0;

	failures += expect(handle.fd == -1,
				   "DL_RUNTIME_HANDLE_INIT must set fd=-1") != 0;

	failures += expect(dl_runtime_open(&handle, "/dev/null") == 0,
				   "first open(/dev/null) must succeed") != 0;
	failures += expect(handle.fd >= 0,
				   "successful open must publish a valid fd") != 0;

	errno = 0;
	failures += expect(dl_runtime_open(&handle, "/dev/null") == -1 &&
				   errno == EBUSY,
				   "opening an already-owned handle must return EBUSY") != 0;

	failures += expect(dl_runtime_close(&handle) == 0,
				   "close(/dev/null) must succeed") != 0;
	failures += expect(handle.fd == -1,
				   "close must invalidate handle ownership") != 0;
	failures += expect(dl_runtime_close(&handle) == 0,
				   "closing an invalidated handle must be idempotent") != 0;

	errno = 0;
	failures += expect(dl_runtime_open(NULL, "/dev/null") == -1 &&
				   errno == EINVAL,
				   "NULL handle must return EINVAL") != 0;
	errno = 0;
	failures += expect(dl_runtime_open(&invalid, NULL) == -1 &&
				   errno == EINVAL,
				   "NULL path must return EINVAL") != 0;

	errno = 0;
	failures += expect(dl_runtime_write(&invalid, NULL, 0) == -1 &&
				   errno == EINVAL,
				   "I/O on an invalid handle must return EINVAL") != 0;

	errno = 0;
	failures += expect(dl_runtime_poll_readable(&invalid, 0, &revents) == -1 &&
				   errno == EINVAL && revents == 0,
				   "poll failure must initialize revents to zero") != 0;

	errno = 0;
	failures += expect(dl_runtime_mmap_shared(&invalid, 0) == MAP_FAILED &&
				   errno == EINVAL,
				   "invalid mmap request must return EINVAL") != 0;
	errno = 0;
	failures += expect(dl_runtime_munmap_shared(MAP_FAILED, 4096) == -1 &&
				   errno == EINVAL,
				   "munmap(MAP_FAILED) must return EINVAL") != 0;

	errno = 0;
	failures += expect(dl_runtime_read_shared_snapshot(&snapshot, &snapshot) == -1 &&
				   errno == EINVAL,
				   "snapshot source and destination must not alias") != 0;

	if (failures != 0)
		return 1;

	printf("runtime unit tests passed.\n");
	return 0;
}
