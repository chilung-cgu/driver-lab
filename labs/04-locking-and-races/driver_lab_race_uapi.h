/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DRIVER_LAB_RACE_UAPI_H
#define DRIVER_LAB_RACE_UAPI_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef __u32 dl_race_u32;
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint32_t dl_race_u32;
#endif

/*
 * This structure is shared by the kernel module and userspace CLI. Keep the
 * layout fixed-width and pointer-free; changing it changes the ioctl ABI.
 */
struct dl_race_status {
	dl_race_u32 counter;
	dl_race_u32 safe_mode;
	dl_race_u32 worker_running;
	dl_race_u32 reserved;
};

#define DL_RACE_IOCTL_TYPE 'R'

#define DL_RACE_IOC_SET_SAFE_MODE \
	_IOW(DL_RACE_IOCTL_TYPE, 0x01, dl_race_u32)
#define DL_RACE_IOC_GET_STATUS \
	_IOR(DL_RACE_IOCTL_TYPE, 0x02, struct dl_race_status)
#define DL_RACE_IOC_INC_COUNTER _IO(DL_RACE_IOCTL_TYPE, 0x03)
#define DL_RACE_IOC_RESET_COUNTER _IO(DL_RACE_IOCTL_TYPE, 0x04)

#endif
