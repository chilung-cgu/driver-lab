// SPDX-License-Identifier: GPL-2.0-only
#include "../labs/04-locking-and-races/driver_lab_race_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DL_MAX_RACE_THREADS 1024UL
#define DL_MAX_RACE_LOOPS 1000000UL

struct worker_args {
	int fd;
	unsigned long loops;
};

static void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s <device> status\n", prog);
	fprintf(stderr, "  %s <device> reset\n", prog);
	fprintf(stderr, "  %s <device> safe-mode <0|1>\n", prog);
	fprintf(stderr, "  %s <device> inc <count>\n", prog);
	fprintf(stderr, "  %s <device> race <threads> <loops>\n", prog);
}

static int parse_ulong(const char *text, unsigned long min,
					   unsigned long max, unsigned long *value)
{
	char *end = NULL;
	unsigned long parsed;

	if (!text || !value || text[0] == '\0') {
		errno = EINVAL;
		return -1;
	}

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno != 0 || !end || *end != '\0' || parsed < min || parsed > max) {
		errno = EINVAL;
		return -1;
	}

	*value = parsed;
	return 0;
}

static void *worker_thread(void *opaque)
{
	const struct worker_args *args = opaque;
	unsigned long i;

	for (i = 0; i < args->loops; ++i) {
		if (ioctl(args->fd, DL_RACE_IOC_INC_COUNTER) != 0) {
			perror("DL_RACE_IOC_INC_COUNTER");
			return (void *)(intptr_t)1;
		}
	}

	return NULL;
}

static int close_with_status(int fd, int status)
{
	if (close(fd) != 0 && status == 0) {
		perror("close");
		return 1;
	}
	return status;
}

int main(int argc, char **argv)
{
	struct dl_race_status status;
	int fd;
	int exit_status = 0;

	if (argc < 3) {
		usage(argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (strcmp(argv[2], "status") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			exit_status = 1;
			goto out;
		}
		if (ioctl(fd, DL_RACE_IOC_GET_STATUS, &status) != 0) {
			perror("DL_RACE_IOC_GET_STATUS");
			exit_status = 1;
			goto out;
		}

		printf("counter=%u safe_mode=%u worker_running=%u\n",
			   status.counter, status.safe_mode, status.worker_running);
	} else if (strcmp(argv[2], "reset") == 0) {
		if (argc != 3) {
			usage(argv[0]);
			exit_status = 1;
			goto out;
		}
		if (ioctl(fd, DL_RACE_IOC_RESET_COUNTER) != 0) {
			perror("DL_RACE_IOC_RESET_COUNTER");
			exit_status = 1;
			goto out;
		}
	} else if (strcmp(argv[2], "safe-mode") == 0) {
		unsigned long parsed;
		dl_race_u32 value;

		if (argc != 4 || parse_ulong(argv[3], 0, 1, &parsed) != 0) {
			usage(argv[0]);
			if (argc == 4)
				perror("safe-mode argument");
			exit_status = 1;
			goto out;
		}
		value = (dl_race_u32)parsed;
		if (ioctl(fd, DL_RACE_IOC_SET_SAFE_MODE, &value) != 0) {
			perror("DL_RACE_IOC_SET_SAFE_MODE");
			exit_status = 1;
			goto out;
		}
	} else if (strcmp(argv[2], "inc") == 0) {
		unsigned long count;
		unsigned long i;

		if (argc != 4 ||
		    parse_ulong(argv[3], 1, DL_MAX_RACE_LOOPS, &count) != 0) {
			usage(argv[0]);
			if (argc == 4)
				perror("inc argument");
			exit_status = 1;
			goto out;
		}

		for (i = 0; i < count; ++i) {
			if (ioctl(fd, DL_RACE_IOC_INC_COUNTER) != 0) {
				perror("DL_RACE_IOC_INC_COUNTER");
				exit_status = 1;
				goto out;
			}
		}
	} else if (strcmp(argv[2], "race") == 0) {
		unsigned long threads;
		unsigned long loops;
		unsigned long created = 0;
		unsigned long i;
		unsigned long long expected;
		pthread_t *ids = NULL;
		struct worker_args args;
		int create_error = 0;
		int worker_error = 0;

		if (argc != 5 ||
		    parse_ulong(argv[3], 1, DL_MAX_RACE_THREADS, &threads) != 0 ||
		    parse_ulong(argv[4], 1, DL_MAX_RACE_LOOPS, &loops) != 0) {
			usage(argv[0]);
			if (argc == 5)
				perror("race arguments");
			exit_status = 1;
			goto out;
		}

		expected = (unsigned long long)threads * loops;
		if (expected > UINT32_MAX) {
			fprintf(stderr,
				"ERROR: threads * loops exceeds the 32-bit teaching counter.\n");
			exit_status = 1;
			goto out;
		}

		ids = calloc(threads, sizeof(*ids));
		if (!ids) {
			perror("calloc");
			exit_status = 1;
			goto out;
		}

		args.fd = fd;
		args.loops = loops;
		for (i = 0; i < threads; ++i) {
			int rc = pthread_create(&ids[i], NULL, worker_thread, &args);

			if (rc != 0) {
				fprintf(stderr, "pthread_create: %s\n", strerror(rc));
				create_error = 1;
				break;
			}
			created++;
		}

		for (i = 0; i < created; ++i) {
			void *thread_result = NULL;
			int rc = pthread_join(ids[i], &thread_result);

			if (rc != 0) {
				fprintf(stderr, "pthread_join: %s\n", strerror(rc));
				worker_error = 1;
			} else if (thread_result != NULL) {
				worker_error = 1;
			}
		}
		free(ids);

		if (create_error || worker_error || created != threads) {
			exit_status = 1;
			goto out;
		}

		if (ioctl(fd, DL_RACE_IOC_GET_STATUS, &status) != 0) {
			perror("DL_RACE_IOC_GET_STATUS");
			exit_status = 1;
			goto out;
		}

		printf("expected_at_least=%llu observed=%u safe_mode=%u\n",
			   expected, status.counter, status.safe_mode);
	} else {
		usage(argv[0]);
		exit_status = 1;
	}

out:
	return close_with_status(fd, exit_status);
}
