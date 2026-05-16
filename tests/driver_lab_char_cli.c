// SPDX-License-Identifier: GPL-2.0-only
#include "driver_lab_runtime.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * 這支 CLI 是教學用 userspace client。
 * 它刻意把每個 subcommand 對到一條 driver path，方便你觀察：
 * write/read -> data path，ioctl-* -> control path，poll -> event path，
 * mmap-read -> shared memory path。
 */
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
	char buffer[256];
	struct dl_ioctl_status status;
	struct dl_shared_page *shared;
	short revents = 0;
	ssize_t ret;
	int timeout_ms = -1;
	int open_flags = O_RDWR;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[2], "poll") == 0)
		open_flags |= O_NONBLOCK;

	/*
	 * 所有 subcommand 都先打開同一個 device node。
	 * 差異在後面呼叫 runtime 的哪一個 helper。
	 */
	if (dl_runtime_open_flags(&handle, argv[1], open_flags) != 0) {
		perror("dl_runtime_open");
		return 1;
	}

	if (strcmp(argv[2], "write") == 0) {
		if (argc != 4) {
			usage(argv[0]);
			dl_runtime_close(&handle);
			return 1;
		}

		/* data path：對應 driver 的 .write callback。 */
		ret = dl_runtime_write(&handle, argv[3], strlen(argv[3]));
		if (ret < 0) {
			perror("dl_runtime_write");
			dl_runtime_close(&handle);
			return 1;
		}

		printf("wrote %zd bytes\n", ret);
	} else if (strcmp(argv[2], "ioctl-write") == 0) {
		if (argc != 4) {
			usage(argv[0]);
			dl_runtime_close(&handle);
			return 1;
		}

		/* control path：用 ioctl 設定 driver 內部 message。 */
		if (dl_runtime_ioctl_set_message(&handle, argv[3]) != 0) {
			perror("dl_runtime_ioctl_set_message");
			dl_runtime_close(&handle);
			return 1;
		}

		printf("ioctl message updated\n");
	} else if (strcmp(argv[2], "read") == 0) {
		/* data path：對應 driver 的 .read callback，把 kernel buffer 讀回來。 */
		ret = dl_runtime_read(&handle, buffer, sizeof(buffer) - 1);
		if (ret < 0) {
			perror("dl_runtime_read");
			dl_runtime_close(&handle);
			return 1;
		}

		buffer[ret] = '\0';
		printf("read %zd bytes: %s\n", ret, buffer);
	} else if (strcmp(argv[2], "status") == 0) {
		/* control path：用 ioctl 讀回結構化狀態。 */
		if (dl_runtime_ioctl_get_status(&handle, &status) != 0) {
			perror("dl_runtime_ioctl_get_status");
			dl_runtime_close(&handle);
			return 1;
		}

		printf("buffer_len=%u event_count=%u event_pending=%u mmap_size=%u\n",
			   status.buffer_len, status.event_count,
			   status.event_pending, status.mmap_size);
	} else if (strcmp(argv[2], "trigger") == 0) {
		/* event path：請 driver 產生一個事件，通常用來喚醒 poll。 */
		if (dl_runtime_ioctl_trigger_event(&handle) != 0) {
			perror("dl_runtime_ioctl_trigger_event");
			dl_runtime_close(&handle);
			return 1;
		}

		printf("event triggered\n");
	} else if (strcmp(argv[2], "clear") == 0) {
		/* control path：清掉 driver buffer 與 pending event 狀態。 */
		if (dl_runtime_ioctl_clear_buffer(&handle) != 0) {
			perror("dl_runtime_ioctl_clear_buffer");
			dl_runtime_close(&handle);
			return 1;
		}

		printf("buffer cleared\n");
	} else if (strcmp(argv[2], "poll") == 0) {
		if (argc != 4) {
			usage(argv[0]);
			dl_runtime_close(&handle);
			return 1;
		}

		/* event path：等待 driver 回報可讀資料或事件，不要 busy loop。 */
		timeout_ms = atoi(argv[3]);
		ret = dl_runtime_poll_readable(&handle, timeout_ms, &revents);
		if (ret < 0) {
			perror("dl_runtime_poll_readable");
			dl_runtime_close(&handle);
			return 1;
		}

		printf("poll ret=%zd revents=0x%x\n", ret, (unsigned int)revents);
	} else if (strcmp(argv[2], "mmap-read") == 0) {
		/* shared memory path：直接讀 driver mmap 出來的一頁 shared state。 */
		shared = dl_runtime_mmap_shared(&handle, DL_MMAP_BYTES);
		if (shared == MAP_FAILED) {
			perror("dl_runtime_mmap_shared");
			dl_runtime_close(&handle);
			return 1;
		}

		printf("magic=0x%x version=%u event_count=%u event_pending=%u buffer_len=%u buffer=%s\n",
			   shared->magic, shared->version, shared->event_count,
			   shared->event_pending, shared->buffer_len, shared->buffer);

		if (dl_runtime_munmap_shared(shared, DL_MMAP_BYTES) != 0) {
			perror("dl_runtime_munmap_shared");
			dl_runtime_close(&handle);
			return 1;
		}
	} else {
		usage(argv[0]);
		dl_runtime_close(&handle);
		return 1;
	}

	if (dl_runtime_close(&handle) != 0) {
		perror("dl_runtime_close");
		return 1;
	}

	return 0;
}
