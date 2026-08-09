// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include "../labs/04-locking-and-races/driver_lab_race_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	dl_race_u32 invalid_mode = 2;
	int saved_errno;
	int fd;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <device>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	errno = 0;
	if (ioctl(fd, DL_RACE_IOC_SET_SAFE_MODE, &invalid_mode) == 0) {
		fprintf(stderr, "invalid safe-mode ioctl unexpectedly succeeded\n");
		close(fd);
		return 1;
	}
	saved_errno = errno;

	if (close(fd) != 0) {
		perror("close");
		return 1;
	}
	if (saved_errno != EINVAL) {
		fprintf(stderr, "invalid safe-mode ioctl returned %s, expected EINVAL\n",
			strerror(saved_errno));
		return 1;
	}

	printf("invalid safe-mode ioctl rejected with EINVAL\n");
	return 0;
}
