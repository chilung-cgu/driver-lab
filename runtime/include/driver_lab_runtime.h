/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DRIVER_LAB_RUNTIME_H
#define DRIVER_LAB_RUNTIME_H

#include <stddef.h>
#include <sys/types.h>

#include "driver_lab_uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dl_runtime_handle {
	int fd;
};

int dl_runtime_open(struct dl_runtime_handle *handle, const char *path);
int dl_runtime_open_flags(struct dl_runtime_handle *handle, const char *path, int flags);
int dl_runtime_close(struct dl_runtime_handle *handle);

ssize_t dl_runtime_write(struct dl_runtime_handle *handle, const void *buf, size_t count);
ssize_t dl_runtime_read(struct dl_runtime_handle *handle, void *buf, size_t count);

int dl_runtime_ioctl_set_message(struct dl_runtime_handle *handle, const char *message);
int dl_runtime_ioctl_get_status(struct dl_runtime_handle *handle,
								struct dl_ioctl_status *status);
int dl_runtime_ioctl_trigger_event(struct dl_runtime_handle *handle);
int dl_runtime_ioctl_clear_buffer(struct dl_runtime_handle *handle);

int dl_runtime_poll_readable(struct dl_runtime_handle *handle, int timeout_ms,
							 short *revents);

/*
 * mmap 只建立 read-only shared-page mapping。mapping length 應先由
 * DL_IOC_GET_STATUS 的 mmap_size 取得，不要假設 PAGE_SIZE == 4096。
 */
void *dl_runtime_mmap_shared(struct dl_runtime_handle *handle, size_t length);
int dl_runtime_munmap_shared(void *addr, size_t length);

/*
 * 依 dl_shared_page.seq publication protocol 取得一致 snapshot。
 * 成功回 0；若 writer 長時間無法收斂則回 -1 並設 errno=EAGAIN。
 */
int dl_runtime_read_shared_snapshot(const struct dl_shared_page *mapped,
									struct dl_shared_page *snapshot);

#ifdef __cplusplus
}
#endif

#endif
