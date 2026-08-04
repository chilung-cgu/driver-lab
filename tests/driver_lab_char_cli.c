// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include "driver_lab_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s <device> write <message>\n", prog);
	fprintf(stderr, "  %s <device> read\n", prog);
	fprintf(stderr, "  %s <device> ioctl-write <message>\n", prog);
	fprintf(stderr, "  %s <device> status\n", prog);
	fprintf(stderr, "  %s <device> trigger\n", prog);
	fprintf(stderr, "  %s <device> clear\n", prog);
	fprintf(stderr, "  %s <device> poll <-1|timeout-ms>\n", prog);
	fprintf(stderr, "  %s <device> mmap-read\n", prog);
}

static int parse_timeout_ms(const char *text, int *timeout_ms)
{
	char *end = NULL;
	long value;

	if (!text || !timeout_ms || text[0] == '\0') {
		errno = EINVAL;
		return -1;
	}

	errno = 0;
	value = strtol(text, &end, 10);
	if (errno != 0 || !end || *end != '\0' ||
	    value < -1 || value > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	*timeout_ms = (int)value;
	return 0;
}

static int close_handle(struct dl_runtime_handle *handle, int status)
{
	if (dl_runtime_close(handle) != 0 && status == 0) {
		perror("dl_runtime_close");
		return 1;
	}
	return status;
}

int main(int argc, char **argv)
{
	struct dl_runtime_handle handle = DL_RUNTIME_HANDLE_INIT;
	struct dl_ioctl_status status;
	struct dl_shared_page *mapped;
	struct dl_shared_page snapshot;
	char buffer[DL_MESSAGE_BYTES];
	long page_size;
	short revents = 0;
	ssize_t ret;
	int timeout_ms;
	int open_flags = O_RDWR | O_CLOEXEC;
	size_t map_len;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[2], "poll") == 0)
		open_flags |= O_NONBLOCK;

	if (dl_runtime_open_flags(&handle, argv[1], open_flags) != 0) {
		perror("dl_runtime_open");
		return 1;
	}

	if (strcmp(argv[2], "write") == 0) {
		size_t length;

		if (argc != 4) {
			usage(argv[0]);
			goto fail;
		}
		length = strlen(argv[3]);
		ret = dl_runtime_write(&handle, argv[3], length);
		if (ret < 0) {
			perror("dl_runtime_write");
			goto fail;
		}
		if ((size_t)ret != length) {
			fprintf(stderr, "short write: requested=%zu completed=%zd\n",
				length, ret);
			goto fail;
		}
		printf("wrote %zd bytes\n", ret);
	} else if (strcmp(argv[2], "ioctl-write") == 0) {
		if (argc != 4) {
			usage(argv[0]);
			goto fail;
		}
		if (dl_runtime_ioctl_set_message(&handle, argv[3]) != 0) {
			perror("dl_runtime_ioctl_set_message");
			goto fail;
		}
		printf("ioctl message updated\n");
	} else if (strcmp(argv[2], "read") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			goto fail;
		}
		ret = dl_runtime_read(&handle, buffer, sizeof(buffer) - 1U);
		if (ret < 0) {
			perror("dl_runtime_read");
			goto fail;
		}
		if ((size_t)ret >= sizeof(buffer)) {
			fprintf(stderr, "driver returned oversized record: %zd\n", ret);
			goto fail;
		}
		buffer[ret] = '\0';
		printf("read %zd bytes: %.*s\n", ret, (int)ret, buffer);
	} else if (strcmp(argv[2], "status") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			goto fail;
		}
		if (dl_runtime_ioctl_get_status(&handle, &status) != 0) {
			perror("dl_runtime_ioctl_get_status");
			goto fail;
		}
		printf("buffer_len=%u event_count=%u event_pending=%u mmap_size=%u\n",
			   status.buffer_len, status.event_count,
			   status.event_pending, status.mmap_size);
	} else if (strcmp(argv[2], "trigger") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			goto fail;
		}
		if (dl_runtime_ioctl_trigger_event(&handle) != 0) {
			perror("dl_runtime_ioctl_trigger_event");
			goto fail;
		}
		printf("event triggered\n");
	} else if (strcmp(argv[2], "clear") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			goto fail;
		}
		if (dl_runtime_ioctl_clear_buffer(&handle) != 0) {
			perror("dl_runtime_ioctl_clear_buffer");
			goto fail;
		}
		printf("buffer cleared\n");
	} else if (strcmp(argv[2], "poll") == 0) {
		if (argc != 4 || parse_timeout_ms(argv[3], &timeout_ms) != 0) {
			usage(argv[0]);
			if (argc == 4)
				perror("timeout-ms");
			goto fail;
		}
		ret = dl_runtime_poll_readable(&handle, timeout_ms, &revents);
		if (ret < 0) {
			perror("dl_runtime_poll_readable");
			goto fail;
		}
		printf("poll ret=%zd revents=0x%x\n", ret,
			   (unsigned int)(unsigned short)revents);
		if (ret > 0 && (revents & (POLLERR | POLLHUP | POLLNVAL))) {
			fprintf(stderr, "poll returned error events: 0x%x\n",
				(unsigned int)(unsigned short)revents);
			goto fail;
		}
	} else if (strcmp(argv[2], "mmap-read") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			goto fail;
		}
		if (dl_runtime_ioctl_get_status(&handle, &status) != 0) {
			perror("dl_runtime_ioctl_get_status");
			goto fail;
		}
		page_size = sysconf(_SC_PAGESIZE);
		if (page_size <= 0) {
			perror("sysconf(_SC_PAGESIZE)");
			goto fail;
		}
		map_len = status.mmap_size;
		if (map_len != (size_t)page_size || map_len < sizeof(snapshot)) {
			fprintf(stderr,
				"invalid mmap_size=%zu userspace_page_size=%ld snapshot=%zu\n",
				map_len, page_size, sizeof(snapshot));
			goto fail;
		}

		mapped = dl_runtime_mmap_shared(&handle, map_len);
		if (mapped == MAP_FAILED) {
			perror("dl_runtime_mmap_shared");
			goto fail;
		}
		if (dl_runtime_read_shared_snapshot(mapped, &snapshot) != 0) {
			perror("dl_runtime_read_shared_snapshot");
			(void)dl_runtime_munmap_shared(mapped, map_len);
			goto fail;
		}
		if (snapshot.magic != DL_SHARED_MAGIC ||
		    snapshot.version != DL_SHARED_VERSION ||
		    snapshot.event_pending > 1U ||
		    snapshot.buffer_len > DL_MESSAGE_BYTES - 1U) {
			fprintf(stderr,
				"invalid shared snapshot: magic=0x%x version=%u pending=%u len=%u\n",
				snapshot.magic, snapshot.version,
				snapshot.event_pending, snapshot.buffer_len);
			(void)dl_runtime_munmap_shared(mapped, map_len);
			goto fail;
		}

		printf("seq=%u magic=0x%x version=%u event_count=%u "
		       "event_pending=%u buffer_len=%u buffer=%.*s\n",
			   snapshot.seq, snapshot.magic, snapshot.version,
			   snapshot.event_count, snapshot.event_pending,
			   snapshot.buffer_len, (int)snapshot.buffer_len,
			   snapshot.buffer);

		if (dl_runtime_munmap_shared(mapped, map_len) != 0) {
			perror("dl_runtime_munmap_shared");
			goto fail;
		}
	} else {
		usage(argv[0]);
		goto fail;
	}

	return close_handle(&handle, 0);

fail:
	return close_handle(&handle, 1);
}
