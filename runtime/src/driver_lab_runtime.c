// SPDX-License-Identifier: GPL-2.0-only
#include "driver_lab_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/*
 * runtime 的目的不是取代 driver，而是把 scattered syscall 包成一致 API。
 * 這讓 CLI / sample app 不需要每次都自己處理 open/read/ioctl/poll/mmap 細節。
 */
int dl_runtime_open(struct dl_runtime_handle *handle, const char *path)
{
	return dl_runtime_open_flags(handle, path, O_RDWR);
}

/*
 * runtime 的開檔入口。
 * 呼叫成功後，handle->fd 就代表 userspace 持有的一個 driver file instance。
 */
int dl_runtime_open_flags(struct dl_runtime_handle *handle, const char *path, int flags)
{
	/*
	 * userspace library 習慣用 -1 + errno 表示失敗。
	 * 這和 kernel 內部常見的負 errno 不完全一樣，新手不要混淆。
	 */
	if (!handle || !path) {
		errno = EINVAL;
		return -1;
	}

	handle->fd = open(path, flags);
	if (handle->fd < 0)
		return -1;

	return 0;
}

int dl_runtime_close(struct dl_runtime_handle *handle)
{
	int ret;

	if (!handle) {
		errno = EINVAL;
		return -1;
	}

	if (handle->fd < 0)
		return 0;

	/* close() 成功後把 fd 設回 -1，避免呼叫者誤用已關閉 fd。 */
	ret = close(handle->fd);
	if (ret < 0)
		return -1;

	handle->fd = -1;
	return 0;
}

/*
 * data path helper。
 * 這裡不解讀 payload，單純把 bytes 交給 driver 的 .write callback。
 */
ssize_t dl_runtime_write(struct dl_runtime_handle *handle, const void *buf, size_t count)
{
	if (!handle || handle->fd < 0 || !buf) {
		errno = EINVAL;
		return -1;
	}

	/* 前期 lab 先用最單純的 read/write，之後才引入 ioctl/poll/mmap。 */
	return write(handle->fd, buf, count);
}

/*
 * data path helper。
 * driver 回傳多少 bytes，runtime 就原樣交回給呼叫者。
 */
ssize_t dl_runtime_read(struct dl_runtime_handle *handle, void *buf, size_t count)
{
	if (!handle || handle->fd < 0 || !buf) {
		errno = EINVAL;
		return -1;
	}

	return read(handle->fd, buf, count);
}

/*
 * control path helper。
 * 把 C 字串整理成 lab 03 定義的 UAPI struct，再送進 ioctl。
 */
int dl_runtime_ioctl_set_message(struct dl_runtime_handle *handle, const char *message)
{
	struct dl_ioctl_message msg;
	size_t len;

	if (!handle || handle->fd < 0 || !message) {
		errno = EINVAL;
		return -1;
	}

	memset(&msg, 0, sizeof(msg));
	len = strlen(message);
	if (len >= sizeof(msg.text)) {
		errno = EMSGSIZE;
		return -1;
	}

	/*
	 * runtime 在這裡把 C 字串包成 UAPI struct，再交給 ioctl。
	 * driver 端收到的是 struct dl_ioctl_message，不是原始 char *。
	 */
	memcpy(msg.text, message, len);
	return ioctl(handle->fd, DL_IOC_SET_MESSAGE, &msg);
}

/* control path helper：讀回 driver 的結構化狀態。 */
int dl_runtime_ioctl_get_status(struct dl_runtime_handle *handle,
								struct dl_ioctl_status *status)
{
	if (!handle || handle->fd < 0 || !status) {
		errno = EINVAL;
		return -1;
	}

	return ioctl(handle->fd, DL_IOC_GET_STATUS, status);
}

/* event path helper：要求 driver 標記一個 pending event。 */
int dl_runtime_ioctl_trigger_event(struct dl_runtime_handle *handle)
{
	if (!handle || handle->fd < 0) {
		errno = EINVAL;
		return -1;
	}

	return ioctl(handle->fd, DL_IOC_TRIGGER_EVENT);
}

/* control path helper：清掉 driver buffer 與 event state。 */
int dl_runtime_ioctl_clear_buffer(struct dl_runtime_handle *handle)
{
	if (!handle || handle->fd < 0) {
		errno = EINVAL;
		return -1;
	}

	return ioctl(handle->fd, DL_IOC_CLEAR_BUFFER);
}

/*
 * event path helper。
 * poll() 會睡眠等待 fd 變成可讀或有 POLLPRI 事件，不需要 userspace busy loop。
 */
int dl_runtime_poll_readable(struct dl_runtime_handle *handle, int timeout_ms,
							 short *revents)
{
	struct pollfd pfd;
	int ret;

	if (!handle || handle->fd < 0) {
		errno = EINVAL;
		return -1;
	}

	pfd.fd = handle->fd;
	/*
	 * POLLIN：一般可讀資料。
	 * POLLPRI：本 lab 用來表示 driver event pending。
	 */
	pfd.events = POLLIN | POLLPRI;
	pfd.revents = 0;

	ret = poll(&pfd, 1, timeout_ms);
	if (ret >= 0 && revents)
		*revents = pfd.revents;

	return ret;
}

/*
 * shared memory helper。
 * mmap 成功後，回傳值是 userspace pointer，可轉成 struct dl_shared_page 讀 snapshot。
 */
void *dl_runtime_mmap_shared(struct dl_runtime_handle *handle, size_t length)
{
	if (!handle || handle->fd < 0 || length == 0) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	/*
	 * MAP_SHARED 表示 userspace 看到的是 driver 映射出來的 shared page，
	 * 不是 runtime 自己複製出來的一份 buffer。
	 */
	return mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0);
}

/* shared memory helper：解除 dl_runtime_mmap_shared() 建立的 mapping。 */
int dl_runtime_munmap_shared(void *addr, size_t length)
{
	if (!addr || length == 0) {
		errno = EINVAL;
		return -1;
	}

	return munmap(addr, length);
}
