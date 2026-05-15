#ifndef DRIVER_LAB_RUNTIME_H
#define DRIVER_LAB_RUNTIME_H

#include <stddef.h>
#include <sys/types.h>

#include "driver_lab_uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * runtime handle 是 userspace 端持有 driver fd 的最小包裝。
 * 對新手來說，可以先把它想成「已開啟的 /dev/...」。
 */
struct dl_runtime_handle {
    int fd;
};

/* 開啟 lab driver 匯出的 device node，例如 /dev/driver_lab_char0。 */
int dl_runtime_open(struct dl_runtime_handle *handle, const char *path);
/* 需要 O_NONBLOCK 這類額外 flag 時使用，例如 poll 測試。 */
int dl_runtime_open_flags(struct dl_runtime_handle *handle, const char *path, int flags);
/* 關閉 fd，並把 handle 重設為無效狀態。 */
int dl_runtime_close(struct dl_runtime_handle *handle);
/* data path：對應 02/03 driver 的 read/write callback。 */
ssize_t dl_runtime_write(struct dl_runtime_handle *handle, const void *buf, size_t count);
ssize_t dl_runtime_read(struct dl_runtime_handle *handle, void *buf, size_t count);
/* control path：對應 03 driver 的 ioctl command。 */
int dl_runtime_ioctl_set_message(struct dl_runtime_handle *handle, const char *message);
int dl_runtime_ioctl_get_status(struct dl_runtime_handle *handle, struct dl_ioctl_status *status);
int dl_runtime_ioctl_trigger_event(struct dl_runtime_handle *handle);
int dl_runtime_ioctl_clear_buffer(struct dl_runtime_handle *handle);
/* event path：等待 driver 回報可讀資料或特殊事件。 */
int dl_runtime_poll_readable(struct dl_runtime_handle *handle, int timeout_ms, short *revents);
/* shared memory path：對應 03 driver 的 mmap shared page。 */
void *dl_runtime_mmap_shared(struct dl_runtime_handle *handle, size_t length);
int dl_runtime_munmap_shared(void *addr, size_t length);

#ifdef __cplusplus
}
#endif

#endif
