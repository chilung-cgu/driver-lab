// SPDX-License-Identifier: GPL-2.0-only
#include "driver_lab_runtime.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s <device> write <message>\n", prog);
	fprintf(stderr, "  %s <device> read\n", prog);
	fprintf(stderr, "  %s <device> ioctl-write <message>\n", prog);
	fprintf(stderr, "  %s <device> status\n", prog);
	fprintf(stderr, "  %s <device> trigger\n", prog);
	fprintf(stderr, "  %s <device> clear\n", prog);
	fprintf(stderr, "  %s <device> poll <timeout-ms>\n", prog);
	fprintf(stderr, "  %s <device> mmap-read\n", prog);
}

int main(int argc, char **argv)
{
	struct dl_runtime_handle handle = { .fd = -1 };
	struct dl_ioctl_status status;
	struct dl_shared_page *mapped;
	struct dl_shared_page snapshot;
	char buffer[DL_MESSAGE_BYTES];
	short revents = 0;
	ssize_t ret;
	int timeout_ms;
	int open_flags = O_RDWR;
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
		if (argc != 4) {
			usage(argv[0]);
			goto fail;
		}
		ret = dl_runtime_write(&handle, argv[3], strlen(argv[3]));
		if (ret < 0) {
			perror("dl_runtime_write");
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
		ret = dl_runtime_read(&handle, buffer, sizeof(buffer) - 1);
		if (ret < 0) {
			perror("dl_runtime_read");
			goto fail;
		}
		buffer[ret] = '\0';
		printf("read %zd bytes: %s\n", ret, buffer);
	} else if (strcmp(argv[2], "status") == 0) {
		if (dl_runtime_ioctl_get_status(&handle, &status) != 0) {
			perror("dl_runtime_ioctl_get_status");
			goto fail;
		}
		printf("buffer_len=%u event_count=%u event_pending=%u mmap_size=%u\n",
			   status.buffer_len, status.event_count,
			   status.event_pending, status.mmap_size);
	} else if (strcmp(argv[2], "trigger") == 0) {
		if (dl_runtime_ioctl_trigger_event(&handle) != 0) {
			perror("dl_runtime_ioctl_trigger_event");
			goto fail;
		}
		printf("event triggered\n");
	} else if (strcmp(argv[2], "clear") == 0) {
		if (dl_runtime_ioctl_clear_buffer(&handle) != 0) {
			perror("dl_runtime_ioctl_clear_buffer");
			goto fail;
		}
		printf("buffer cleared\n");
	} else if (strcmp(argv[2], "poll") == 0) {
		if (argc != 4) {
			usage(argv[0]);
			goto fail;
		}
		timeout_ms = atoi(argv[3]);
		ret = dl_runtime_poll_readable(&handle, timeout_ms, &revents);
		if (ret < 0) {
			perror("dl_runtime_poll_readable");
			goto fail;
		}
		printf("poll ret=%zd revents=0x%x\n", ret, (unsigned int)revents);
	} else if (strcmp(argv[2], "mmap-read") == 0) {
		if (dl_runtime_ioctl_get_status(&handle, &status) != 0) {
			perror("dl_runtime_ioctl_get_status");
			goto fail;
		}
		map_len = status.mmap_size;
		if (map_len < sizeof(snapshot)) {
			fprintf(stderr, "invalid mmap_size=%zu\n", map_len);
			goto fail;
		}

		mapped = dl_runtime_mmap_shared(&handle, map_len);
		if (mapped == MAP_FAILED) {
			perror("dl_runtime_mmap_shared");
			goto fail;
		}
		if (dl_runtime_read_shared_snapshot(mapped, &snapshot) != 0) {
			perror("dl_runtime_read_shared_snapshot");
			dl_runtime_munmap_shared(mapped, map_len);
			goto fail;
		}
		if (snapshot.magic != DL_SHARED_MAGIC ||
		    snapshot.version != DL_SHARED_VERSION ||
		    snapshot.buffer_len >= DL_MESSAGE_BYTES) {
			fprintf(stderr,
				"invalid shared snapshot: magic=0x%x version=%u len=%u\n",
				snapshot.magic, snapshot.version, snapshot.buffer_len);
			dl_runtime_munmap_shared(mapped, map_len);
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

	if (dl_runtime_close(&handle) != 0) {
		perror("dl_runtime_close");
		return 1;
	}
	return 0;

fail:
	dl_runtime_close(&handle);
	return 1;
}
