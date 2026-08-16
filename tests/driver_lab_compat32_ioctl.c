// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include "../labs/04-locking-and-races/driver_lab_race_uapi.h"
#include "../runtime/include/driver_lab_uapi.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

_Static_assert(sizeof(struct dl_ioctl_message) == DL_MESSAGE_BYTES,
	       "dl_ioctl_message ABI changed");
_Static_assert(sizeof(struct dl_ioctl_status) == 4U * sizeof(dl_u32),
	       "dl_ioctl_status ABI changed");
_Static_assert(sizeof(struct dl_race_status) == 4U * sizeof(dl_race_u32),
	       "dl_race_status ABI changed");

static int run_lab03(int fd)
{
	static const char message[] = "compat32-pointer-payload";
	struct dl_ioctl_message payload = { 0 };
	struct dl_ioctl_status status;
	char buffer[sizeof(message)];
	ssize_t len;

	memcpy(payload.text, message, sizeof(message));
	if (ioctl(fd, DL_IOC_SET_MESSAGE, &payload) != 0) {
		perror("DL_IOC_SET_MESSAGE");
		return 1;
	}
	if (ioctl(fd, DL_IOC_GET_STATUS, &status) != 0) {
		perror("DL_IOC_GET_STATUS");
		return 1;
	}
	if (status.buffer_len != sizeof(message) - 1U) {
		fprintf(stderr, "unexpected buffer_len=%u\n", status.buffer_len);
		return 1;
	}

	len = read(fd, buffer, sizeof(buffer));
	if (len != (ssize_t)(sizeof(message) - 1U) ||
	    memcmp(buffer, message, sizeof(message) - 1U) != 0) {
		fprintf(stderr, "compat32 message payload mismatch\n");
		return 1;
	}

	printf("Lab03 32-bit compat ioctl passed\n");
	return 0;
}

static int run_lab04(int fd)
{
	struct dl_race_status status;
	dl_race_u32 safe_mode = 1U;

	if (ioctl(fd, DL_RACE_IOC_SET_SAFE_MODE, &safe_mode) != 0) {
		perror("DL_RACE_IOC_SET_SAFE_MODE");
		return 1;
	}
	if (ioctl(fd, DL_RACE_IOC_RESET_COUNTER) != 0) {
		perror("DL_RACE_IOC_RESET_COUNTER");
		return 1;
	}
	if (ioctl(fd, DL_RACE_IOC_INC_COUNTER) != 0) {
		perror("DL_RACE_IOC_INC_COUNTER");
		return 1;
	}
	if (ioctl(fd, DL_RACE_IOC_GET_STATUS, &status) != 0) {
		perror("DL_RACE_IOC_GET_STATUS");
		return 1;
	}
	if (status.safe_mode != 1U || status.counter == 0U) {
		fprintf(stderr, "unexpected status: counter=%u safe_mode=%u\n",
			status.counter, status.safe_mode);
		return 1;
	}

	printf("Lab04 32-bit compat ioctl passed\n");
	return 0;
}

int main(int argc, char **argv)
{
	int fd;
	int status;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <lab03|lab04> <device>\n", argv[0]);
		return 1;
	}
	if (sizeof(void *) != 4U) {
		fprintf(stderr, "expected a 32-bit executable\n");
		return 1;
	}

	fd = open(argv[2], O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (strcmp(argv[1], "lab03") == 0)
		status = run_lab03(fd);
	else if (strcmp(argv[1], "lab04") == 0)
		status = run_lab04(fd);
	else {
		fprintf(stderr, "unknown lab: %s\n", argv[1]);
		status = 1;
	}

	if (close(fd) != 0 && status == 0) {
		perror("close");
		return 1;
	}
	return status;
}
