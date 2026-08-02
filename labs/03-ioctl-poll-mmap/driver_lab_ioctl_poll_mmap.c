// SPDX-License-Identifier: GPL-2.0-only
/*
 * 同一個 /dev node 提供四條教學路徑：
 * data path(read/write)、control path(ioctl)、event path(poll)、
 * read-only shared snapshot(mmap)。
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "../../runtime/include/driver_lab_uapi.h"

#define DL_IOCTL_CLASS_NAME "driver_lab_ctl"
#define DL_IOCTL_DEVICE_NAME "driver_lab_ctl0"

static dev_t dl_devt;
static struct cdev dl_cdev;
static struct class *dl_class;
static struct device *dl_device;

static DEFINE_MUTEX(dl_lock);
static DECLARE_WAIT_QUEUE_HEAD(dl_read_wq);
static DECLARE_WAIT_QUEUE_HEAD(dl_event_wq);

static char dl_buffer[DL_MESSAGE_BYTES];
static size_t dl_buffer_len;
static dl_u32 dl_event_count;
static bool dl_event_pending;

/* 正常 RAM page；mmap 時以 vm_insert_page() 映射給 userspace。 */
static struct page *dl_shared_page;
static struct dl_shared_page *dl_shared;

/*
 * 呼叫者必須持有 dl_lock。
 *
 * Kernel mutex 只能序列化 kernel writers，無法讓任意 userspace reader 一起
 * 拿鎖。因此 shared page 使用簡化的 sequence publication protocol：
 * odd seq 代表 writer 正在更新，even seq 代表穩定。userspace 要複製後再
 * 確認 seq 沒有改變。
 */
static void dl_sync_shared_page_locked(void)
{
	dl_u32 seq;

	seq = READ_ONCE(dl_shared->seq);
	if (seq & 1U)
		seq++;

	WRITE_ONCE(dl_shared->seq, seq + 1U);
	smp_wmb();

	WRITE_ONCE(dl_shared->magic, DL_SHARED_MAGIC);
	WRITE_ONCE(dl_shared->version, DL_SHARED_VERSION);
	WRITE_ONCE(dl_shared->event_count, dl_event_count);
	WRITE_ONCE(dl_shared->event_pending, dl_event_pending ? 1U : 0U);
	WRITE_ONCE(dl_shared->buffer_len, (dl_u32)dl_buffer_len);
	memset(dl_shared->reserved, 0, sizeof(dl_shared->reserved));
	memset(dl_shared->buffer, 0, sizeof(dl_shared->buffer));
	memcpy(dl_shared->buffer, dl_buffer, dl_buffer_len);

	smp_wmb();
	WRITE_ONCE(dl_shared->seq, seq + 2U);
}

static void dl_publish_message_locked(const char *src, size_t len)
{
	memset(dl_buffer, 0, sizeof(dl_buffer));
	memcpy(dl_buffer, src, len);
	dl_buffer[len] = '\0';
	dl_buffer_len = len;
	dl_event_count++;
	dl_event_pending = true;
	dl_sync_shared_page_locked();
}

static int dl_open(struct inode *inode, struct file *file)
{
	pr_debug("device opened\n");
	return 0;
}

static int dl_release(struct inode *inode, struct file *file)
{
	pr_debug("device released\n");
	return 0;
}

static ssize_t dl_read(struct file *file, char __user *buf,
					   size_t count, loff_t *ppos)
{
	ssize_t ret;

	if (count == 0)
		return 0;

	for (;;) {
		if (file->f_flags & O_NONBLOCK) {
			if (READ_ONCE(dl_buffer_len) == 0)
				return -EAGAIN;
		} else {
			ret = wait_event_interruptible(
				dl_read_wq, READ_ONCE(dl_buffer_len) > 0);
			if (ret)
				return ret;
		}

		if (mutex_lock_interruptible(&dl_lock))
			return -ERESTARTSYS;

		/*
		 * Waitqueue condition 和 mutex acquisition 之間，另一個 reader 可能
		 * 已經把 message 消費掉。拿鎖後必須重新檢查，不能錯把競爭結果
		 * 當成 EOF。
		 */
		if (dl_buffer_len == 0) {
			mutex_unlock(&dl_lock);
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;
			continue;
		}

		ret = simple_read_from_buffer(buf, count, ppos,
								  dl_buffer, dl_buffer_len);
		if (ret > 0 && *ppos >= dl_buffer_len) {
			memset(dl_buffer, 0, sizeof(dl_buffer));
			dl_buffer_len = 0;
			dl_event_pending = false;
			*ppos = 0;
			dl_sync_shared_page_locked();
		}

		mutex_unlock(&dl_lock);
		return ret;
	}
}

static ssize_t dl_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	char local[DL_MESSAGE_BYTES];
	ssize_t ret;
	loff_t pos = 0;

	if (count == 0)
		return 0;
	if (count > DL_MESSAGE_BYTES - 1)
		return -EMSGSIZE;
	if (mutex_lock_interruptible(&dl_lock))
		return -ERESTARTSYS;

	ret = simple_write_to_buffer(local, sizeof(local) - 1, &pos, buf, count);
	if (ret >= 0) {
		local[ret] = '\0';
		dl_publish_message_locked(local, ret);
		*ppos = 0;
	}
	mutex_unlock(&dl_lock);

	if (ret >= 0) {
		wake_up_interruptible(&dl_read_wq);
		wake_up_interruptible(&dl_event_wq);
	}
	return ret;
}

static __poll_t dl_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &dl_read_wq, wait);
	poll_wait(file, &dl_event_wq, wait);

	mutex_lock(&dl_lock);
	if (dl_buffer_len > 0)
		mask |= POLLIN | POLLRDNORM;
	if (dl_event_pending)
		mask |= POLLPRI;
	mutex_unlock(&dl_lock);
	return mask;
}

static long dl_unlocked_ioctl(struct file *file, unsigned int cmd,
							  unsigned long arg)
{
	struct dl_ioctl_message msg;
	struct dl_ioctl_status status;
	long ret = 0;

	switch (cmd) {
	case DL_IOC_SET_MESSAGE: {
		size_t len;

		if (copy_from_user(&msg, (void __user *)arg, sizeof(msg)))
			return -EFAULT;
		if (mutex_lock_interruptible(&dl_lock))
			return -ERESTARTSYS;

		len = strnlen(msg.text, sizeof(msg.text));
		if (len == sizeof(msg.text))
			len = sizeof(msg.text) - 1;
		dl_publish_message_locked(msg.text, len);
		mutex_unlock(&dl_lock);
		wake_up_interruptible(&dl_read_wq);
		wake_up_interruptible(&dl_event_wq);
		break;
	}

	case DL_IOC_GET_STATUS:
		if (mutex_lock_interruptible(&dl_lock))
			return -ERESTARTSYS;
		status.buffer_len = (dl_u32)dl_buffer_len;
		status.event_count = dl_event_count;
		status.event_pending = dl_event_pending ? 1U : 0U;
		status.mmap_size = (dl_u32)PAGE_SIZE;
		mutex_unlock(&dl_lock);
		if (copy_to_user((void __user *)arg, &status, sizeof(status)))
			return -EFAULT;
		break;

	case DL_IOC_TRIGGER_EVENT:
		if (mutex_lock_interruptible(&dl_lock))
			return -ERESTARTSYS;
		dl_event_count++;
		dl_event_pending = true;
		dl_sync_shared_page_locked();
		mutex_unlock(&dl_lock);
		wake_up_interruptible(&dl_event_wq);
		break;

	case DL_IOC_CLEAR_BUFFER:
		if (mutex_lock_interruptible(&dl_lock))
			return -ERESTARTSYS;
		memset(dl_buffer, 0, sizeof(dl_buffer));
		dl_buffer_len = 0;
		dl_event_pending = false;
		dl_sync_shared_page_locked();
		mutex_unlock(&dl_lock);
		/*
		 * readiness 由 true 變 false 不需要喚醒 poll waiter；wake-up 只會讓
		 * poll 重新評估，條件仍為 false 時它會繼續等待。
		 */
		break;

	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static int dl_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;

	if (vma->vm_pgoff != 0 || size > PAGE_SIZE)
		return -EINVAL;
	if (vma->vm_flags & VM_WRITE)
		return -EPERM;

	/* 防止之後用 mprotect() 把 snapshot 升級成 writable mapping。 */
	vm_flags_clear(vma, VM_MAYWRITE);
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	return vm_insert_page(vma, vma->vm_start, dl_shared_page);
}

static const struct file_operations dl_fops = {
	.owner = THIS_MODULE,
	.open = dl_open,
	.release = dl_release,
	.read = dl_read,
	.write = dl_write,
	.poll = dl_poll,
	.unlocked_ioctl = dl_unlocked_ioctl,
	.mmap = dl_mmap,
	.llseek = noop_llseek,
};

static int __init driver_lab_ioctl_poll_mmap_init(void)
{
	int ret;

	dl_shared_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!dl_shared_page)
		return -ENOMEM;
	dl_shared = page_address(dl_shared_page);
	if (!dl_shared) {
		ret = -ENOMEM;
		goto err_free_page;
	}

	mutex_lock(&dl_lock);
	dl_sync_shared_page_locked();
	mutex_unlock(&dl_lock);

	ret = alloc_chrdev_region(&dl_devt, 0, 1, DL_IOCTL_CLASS_NAME);
	if (ret)
		goto err_free_page;

	cdev_init(&dl_cdev, &dl_fops);
	dl_cdev.owner = THIS_MODULE;
	ret = cdev_add(&dl_cdev, dl_devt, 1);
	if (ret)
		goto err_unregister_chrdev;

	dl_class = class_create(DL_IOCTL_CLASS_NAME);
	if (IS_ERR(dl_class)) {
		ret = PTR_ERR(dl_class);
		goto err_del_cdev;
	}

	dl_device = device_create(dl_class, NULL, dl_devt, NULL,
							  DL_IOCTL_DEVICE_NAME);
	if (IS_ERR(dl_device)) {
		ret = PTR_ERR(dl_device);
		goto err_destroy_class;
	}

	pr_info("created /dev/%s (major=%d minor=%d page_size=%lu)\n",
			DL_IOCTL_DEVICE_NAME, MAJOR(dl_devt), MINOR(dl_devt), PAGE_SIZE);
	return 0;

err_destroy_class:
	class_destroy(dl_class);
err_del_cdev:
	cdev_del(&dl_cdev);
err_unregister_chrdev:
	unregister_chrdev_region(dl_devt, 1);
err_free_page:
	__free_page(dl_shared_page);
	return ret;
}

static void __exit driver_lab_ioctl_poll_mmap_exit(void)
{
	device_destroy(dl_class, dl_devt);
	class_destroy(dl_class);
	cdev_del(&dl_cdev);
	unregister_chrdev_region(dl_devt, 1);
	__free_page(dl_shared_page);
	pr_info("device removed\n");
}

module_init(driver_lab_ioctl_poll_mmap_init);
module_exit(driver_lab_ioctl_poll_mmap_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 3 ioctl/poll/read-only mmap snapshot lab");
