#include "../labs/04-locking-and-races/driver_lab_race_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct worker_args {
    /* 多條 userspace thread 共用同一個 device fd。 */
    int fd;
    /* 每條 thread 要重複送多少次 ioctl increment。 */
    int loops;
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

static void *worker_thread(void *opaque)
{
    struct worker_args *args = opaque;
    int i;

    /* 故意狂送 ioctl，讓 kernel 端更容易踩出 lost update。 */
    for (i = 0; i < args->loops; ++i) {
        if (ioctl(args->fd, DL_RACE_IOC_INC_COUNTER) != 0) {
            perror("DL_RACE_IOC_INC_COUNTER");
            return (void *)1;
        }
    }

    return NULL;
}

int main(int argc, char **argv)
{
    struct dl_race_status status;
    int fd;

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (strcmp(argv[2], "status") == 0) {
        if (ioctl(fd, DL_RACE_IOC_GET_STATUS, &status) != 0) {
            perror("DL_RACE_IOC_GET_STATUS");
            close(fd);
            return 1;
        }

        printf("counter=%u safe_mode=%u worker_running=%u\n",
               status.counter, status.safe_mode, status.worker_running);
    } else if (strcmp(argv[2], "reset") == 0) {
        if (ioctl(fd, DL_RACE_IOC_RESET_COUNTER) != 0) {
            perror("DL_RACE_IOC_RESET_COUNTER");
            close(fd);
            return 1;
        }
    } else if (strcmp(argv[2], "safe-mode") == 0) {
        unsigned int value;

        if (argc != 4) {
            usage(argv[0]);
            close(fd);
            return 1;
        }

        value = (unsigned int)strtoul(argv[3], NULL, 10);
        if (ioctl(fd, DL_RACE_IOC_SET_SAFE_MODE, &value) != 0) {
            perror("DL_RACE_IOC_SET_SAFE_MODE");
            close(fd);
            return 1;
        }
    } else if (strcmp(argv[2], "inc") == 0) {
        int count;
        int i;

        if (argc != 4) {
            usage(argv[0]);
            close(fd);
            return 1;
        }

        count = atoi(argv[3]);
        for (i = 0; i < count; ++i) {
            if (ioctl(fd, DL_RACE_IOC_INC_COUNTER) != 0) {
                perror("DL_RACE_IOC_INC_COUNTER");
                close(fd);
                return 1;
            }
        }
    } else if (strcmp(argv[2], "race") == 0) {
        int threads;
        int loops;
        pthread_t *ids;
        struct worker_args args;
        int i;

        if (argc != 5) {
            usage(argv[0]);
            close(fd);
            return 1;
        }

        threads = atoi(argv[3]);
        loops = atoi(argv[4]);
        if (threads <= 0 || loops <= 0) {
            errno = EINVAL;
            perror("race arguments");
            close(fd);
            return 1;
        }

        /* 建多條 thread，同時對同一個 driver state 施壓。 */
        ids = calloc((size_t)threads, sizeof(*ids));
        if (!ids) {
            perror("calloc");
            close(fd);
            return 1;
        }

        args.fd = fd;
        args.loops = loops;

        for (i = 0; i < threads; ++i) {
            if (pthread_create(&ids[i], NULL, worker_thread, &args) != 0) {
                perror("pthread_create");
                free(ids);
                close(fd);
                return 1;
            }
        }

        for (i = 0; i < threads; ++i)
            pthread_join(ids[i], NULL);

        free(ids);

        if (ioctl(fd, DL_RACE_IOC_GET_STATUS, &status) != 0) {
            perror("DL_RACE_IOC_GET_STATUS");
            close(fd);
            return 1;
        }

        /*
         * expected_at_least 只計算 userspace 自己送出的 increment。
         * 背景 worker 也會加 counter，所以實際 observed 可能更高。
         */
        printf("expected_at_least=%d observed=%u safe_mode=%u\n",
               threads * loops, status.counter, status.safe_mode);
    } else {
        usage(argv[0]);
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
