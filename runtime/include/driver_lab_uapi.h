/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DRIVER_LAB_UAPI_H
#define DRIVER_LAB_UAPI_H

/*
 * UAPI = userspace API。
 *
 * 這份 header 會同時被 kernel module 與 userspace runtime/CLI include。
 * 因此它只能放雙方都同意的 ABI 內容，例如 ioctl command number、固定大小
 * struct、shared page layout。不要把只屬於 kernel 內部的 private state 放進來。
 */
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

/* 這些大小是 ABI 的一部分；改掉會影響 userspace 與 kernel 的相容性。 */
#define DL_MESSAGE_BYTES 256U
#define DL_MMAP_BYTES 4096U
#define DL_SHARED_MAGIC 0x444C4150U
#define DL_IOCTL_TYPE 'L'

/* DL_IOC_SET_MESSAGE 的 payload：userspace 把一段固定上限的文字送進 driver。 */
struct dl_ioctl_message {
	char text[DL_MESSAGE_BYTES];
};

/* DL_IOC_GET_STATUS 的回傳：讓 userspace 觀察 driver 目前狀態。 */
struct dl_ioctl_status {
	unsigned int buffer_len;
	unsigned int event_count;
	unsigned int event_pending;
	unsigned int mmap_size;
};

/*
 * mmap 共享頁面的 layout。
 * userspace mmap 後會直接讀這個 struct，因此欄位順序與大小也是 ABI。
 */
struct dl_shared_page {
	unsigned int magic;
	unsigned int version;
	unsigned int event_count;
	unsigned int event_pending;
	unsigned int buffer_len;
	char buffer[DL_MESSAGE_BYTES];
};

/*
 * ioctl command number。
 * _IOW：userspace 寫資料給 kernel。
 * _IOR：kernel 回資料給 userspace。
 * _IO：沒有額外 payload，只代表一個動作。
 */
#define DL_IOC_SET_MESSAGE _IOW(DL_IOCTL_TYPE, 0x01, struct dl_ioctl_message)
#define DL_IOC_GET_STATUS _IOR(DL_IOCTL_TYPE, 0x02, struct dl_ioctl_status)
#define DL_IOC_TRIGGER_EVENT _IO(DL_IOCTL_TYPE, 0x03)
#define DL_IOC_CLEAR_BUFFER _IO(DL_IOCTL_TYPE, 0x04)

#endif
