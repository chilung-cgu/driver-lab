/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DRIVER_LAB_RACE_UAPI_H
#define DRIVER_LAB_RACE_UAPI_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

/*
 * 這份 struct 是 userspace CLI 和 kernel driver 共同理解的 ABI。
 * 欄位順序與型別改變會影響相容性；不要把 kernel private pointer 放進來。
 */
struct dl_race_status {
	/* 目前共享 counter 的值。 */
	unsigned int counter;
	/* 0: 故意不加鎖，1: 用 mutex 保護。 */
	unsigned int safe_mode;
	/* 背景 worker thread 是否仍在運作。 */
	unsigned int worker_running;
};

#define DL_RACE_IOCTL_TYPE 'R'

#define DL_RACE_IOC_SET_SAFE_MODE _IOW(DL_RACE_IOCTL_TYPE, 0x01, unsigned int)
#define DL_RACE_IOC_GET_STATUS _IOR(DL_RACE_IOCTL_TYPE, 0x02, struct dl_race_status)
#define DL_RACE_IOC_INC_COUNTER _IO(DL_RACE_IOCTL_TYPE, 0x03)
#define DL_RACE_IOC_RESET_COUNTER _IO(DL_RACE_IOCTL_TYPE, 0x04)

#endif
