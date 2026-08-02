/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef DRIVER_LAB_UAPI_H
#define DRIVER_LAB_UAPI_H

/*
 * UAPI = userspace API。
 *
 * 這份 header 會同時被 kernel module 與 userspace runtime/CLI include。
 * 因此只能放雙方都同意的固定寬度 ABI：ioctl command number、固定大小
 * struct 與 shared-page layout。不要把 kernel private pointer 或 PAGE_SIZE
 * 這類平台常數寫死在 UAPI。
 */
#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef __u32 dl_u32;
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint32_t dl_u32;
#endif

#define DL_MESSAGE_BYTES 256U
#define DL_SHARED_MAGIC 0x444C4150U
#define DL_SHARED_VERSION 2U
#define DL_IOCTL_TYPE 'L'

/* DL_IOC_SET_MESSAGE 的 payload：userspace 把一段固定上限的文字送進 driver。 */
struct dl_ioctl_message {
	char text[DL_MESSAGE_BYTES];
};

/* DL_IOC_GET_STATUS 的回傳：讓 userspace 觀察 driver 目前狀態。 */
struct dl_ioctl_status {
	dl_u32 buffer_len;
	dl_u32 event_count;
	dl_u32 event_pending;
	/* 真正可 mmap 的長度由 kernel 的 PAGE_SIZE 決定，不固定為 4096。 */
	dl_u32 mmap_size;
};

/*
 * mmap 共享頁面的 layout。
 *
 * seq 使用「奇數 = kernel 正在更新、偶數 = 穩定」的 publication protocol。
 * userspace 必須先讀 seq、複製整份 snapshot，再確認 seq 仍是相同偶數；
 * 若不同就重試。這避免 userspace 在 kernel 更新一半時讀到 torn snapshot。
 */
struct dl_shared_page {
	dl_u32 seq;
	dl_u32 magic;
	dl_u32 version;
	dl_u32 event_count;
	dl_u32 event_pending;
	dl_u32 buffer_len;
	dl_u32 reserved[2];
	char buffer[DL_MESSAGE_BYTES];
};

#define DL_IOC_SET_MESSAGE _IOW(DL_IOCTL_TYPE, 0x01, struct dl_ioctl_message)
#define DL_IOC_GET_STATUS _IOR(DL_IOCTL_TYPE, 0x02, struct dl_ioctl_status)
#define DL_IOC_TRIGGER_EVENT _IO(DL_IOCTL_TYPE, 0x03)
#define DL_IOC_CLEAR_BUFFER _IO(DL_IOCTL_TYPE, 0x04)

#endif
