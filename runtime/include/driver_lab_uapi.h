#ifndef DRIVER_LAB_UAPI_H
#define DRIVER_LAB_UAPI_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#define DL_MESSAGE_BYTES 256U
#define DL_MMAP_BYTES 4096U
#define DL_SHARED_MAGIC 0x444C4150U
#define DL_IOCTL_TYPE 'L'

struct dl_ioctl_message {
    char text[DL_MESSAGE_BYTES];
};

struct dl_ioctl_status {
    unsigned int buffer_len;
    unsigned int event_count;
    unsigned int event_pending;
    unsigned int mmap_size;
};

struct dl_shared_page {
    unsigned int magic;
    unsigned int version;
    unsigned int event_count;
    unsigned int event_pending;
    unsigned int buffer_len;
    char buffer[DL_MESSAGE_BYTES];
};

#define DL_IOC_SET_MESSAGE _IOW(DL_IOCTL_TYPE, 0x01, struct dl_ioctl_message)
#define DL_IOC_GET_STATUS _IOR(DL_IOCTL_TYPE, 0x02, struct dl_ioctl_status)
#define DL_IOC_TRIGGER_EVENT _IO(DL_IOCTL_TYPE, 0x03)
#define DL_IOC_CLEAR_BUFFER _IO(DL_IOCTL_TYPE, 0x04)

#endif
