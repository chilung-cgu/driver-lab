// SPDX-License-Identifier: GPL-2.0-only
#include "driver_lab_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int dl_runtime_open(struct dl_runtime_handle *handle, const char *path)
{
	return dl_runtime_open_flags(handle, path, O_RDWR);
}

int dl_runtime_open_flags(struct dl_runtime_handle *handle, const char *path, int flags)
{
	if (!handle || !path) {
		errno = EINVAL;
		return -1;
	}

	handle->fd = open(path, flags);
	return handle->fd < 0 ? -1 : 0;
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

	ret = close(handle->fd);
	if (ret < 0)
		return -1;

	handle->fd = -1;
	return 0;
}

ssize_t dl_runtime_write(struct dl_runtime_handle *handle, const void *buf, size_t count)
{
	if (!handle || handle->fd < 0 || !buf) {
		errno = EINVAL;
		return -1;
	}
	return write(handle->fd, buf, count);
}

ssize_t dl_runtime_read(struct dl_runtime_handle *handle, void *buf, size_t count)
{
	if (!handle || handle->fd < 0 || !buf) {
		errno = EINVAL;
		return -1;
	}
	return read(handle->fd, buf, count);
}

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
	memcpy(msg.text, message, len);
	return ioctl(handle->fd, DL_IOC_SET_MESSAGE, &msg);
}

int dl_runtime_ioctl_get_status(struct dl_runtime_handle *handle,
								struct dl_ioctl_status *status)
{
	if (!handle || handle->fd < 0 || !status) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, DL_IOC_GET_STATUS, status);
}

int dl_runtime_ioctl_trigger_event(struct dl_runtime_handle *handle)
{
	if (!handle || handle->fd < 0) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, DL_IOC_TRIGGER_EVENT);
}

int dl_runtime_ioctl_clear_buffer(struct dl_runtime_handle *handle)
{
	if (!handle || handle->fd < 0) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, DL_IOC_CLEAR_BUFFER);
}

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
	pfd.events = POLLIN | POLLPRI;
	pfd.revents = 0;
	ret = poll(&pfd, 1, timeout_ms);
	if (ret >= 0 && revents)
		*revents = pfd.revents;
	return ret;
}

void *dl_runtime_mmap_shared(struct dl_runtime_handle *handle, size_t length)
{
	if (!handle || handle->fd < 0 || length == 0) {
		errno = EINVAL;
		return MAP_FAILED;
	}

	/* Snapshot 是 kernel -> userspace 的觀測介面，不開放 userspace 寫回。 */
	return mmap(NULL, length, PROT_READ, MAP_SHARED, handle->fd, 0);
}

int dl_runtime_munmap_shared(void *addr, size_t length)
{
	if (!addr || addr == MAP_FAILED || length == 0) {
		errno = EINVAL;
		return -1;
	}
	return munmap(addr, length);
}

int dl_runtime_read_shared_snapshot(const struct dl_shared_page *mapped,
									struct dl_shared_page *snapshot)
{
	unsigned int attempt;
	dl_u32 begin;
	dl_u32 end;

	if (!mapped || !snapshot) {
		errno = EINVAL;
		return -1;
	}

	for (attempt = 0; attempt < 1000; ++attempt) {
		begin = __atomic_load_n(&mapped->seq, __ATOMIC_ACQUIRE);
		if (begin & 1U) {
			sched_yield();
			continue;
		}

		memcpy(snapshot, mapped, sizeof(*snapshot));
		end = __atomic_load_n(&mapped->seq, __ATOMIC_ACQUIRE);
		if (begin == end && !(end & 1U))
			return 0;
	}

	errno = EAGAIN;
	return -1;
}
