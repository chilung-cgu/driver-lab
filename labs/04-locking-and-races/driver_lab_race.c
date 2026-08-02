// SPDX-License-Identifier: GPL-2.0-only
/*
 * 這一關刻意保留「會 race」與「用 mutex 修正」兩種模式。
 * 目的不是產品 driver，而是讓 lost update 可以被觀察、解釋、修正。
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "driver_lab_race_uapi.h"

#define DL_RACE_CLASS_NAME "driver_lab_race"
#define DL_RACE_DEVICE_NAME "driver_lab_race0"
#define DL_STATUS_BUFFER_BYTES 128

static dev_t dl_race_devt;
static struct cdev dl_race_cdev;
static struct class *dl_race_class;
static struct device *dl_race_device;
static DEFINE_MUTEX(dl_race_lock);
static struct task_struct *dl_race_worker;

static unsigned int dl_counter;
static bool dl_safe_mode;
static bool dl_worker_running;

static void dl_race_increment_locked(void)
{
	dl_counter++;
}

static void dl_race_increment_unlocked(void)
{
	unsigned int snapshot;

	/* 故意拉長 read-modify-write 的競爭視窗。 */
	snapshot = dl_counter;
	usleep_range(1000, 2000);
	dl_counter = snapshot + 1;
}

static void dl_race_increment(void)
{
	if (READ_ONCE(dl_safe_mode)) {
		mutex_lock(&dl_race_lock);
		dl_race_increment_locked();
		mutex_unlock(&dl_race_lock);
		return;
	}

	dl_race_increment_unlocked();
}

static int dl_race_worker_fn(void *unused)
{
	while (!kthread_should_stop()) {
		dl_race_increment();
		msleep(20);
	}
	return 0;
}

static int dl_race_open(struct inode *inode, struct file *file)
{
	pr_debug("device opened\n");
	return 0;
}

static int dl_race_release(struct inode *inode, struct file *file)
{
	pr_debug("device released\n");
	return 0;
}

static ssize_t dl_race_read(struct file *file, char __user *buf,
							size_t count, loff_t *ppos)
{
	char status[DL_STATUS_BUFFER_BYTES];
	int len;

	mutex_lock(&dl_race_lock);
	len = scnprintf(status, sizeof(status),
					"counter=%u safe_mode=%u worker_running=%u\n",
					dl_counter, dl_safe_mode ? 1 : 0,
					dl_worker_running ? 1 : 0);
	mutex_unlock(&dl_race_lock);

	return simple_read_from_buffer(buf, count, ppos, status, len);
}

static long dl_race_ioctl(struct file *file, unsigned int cmd,
						  unsigned long arg)
{
	struct dl_race_status status;
	unsigned int safe_mode;

	switch (cmd) {
	case DL_RACE_IOC_SET_SAFE_MODE:
		if (copy_from_user(&safe_mode, (void __user *)arg,
					   sizeof(safe_mode)))
			return -EFAULT;
		mutex_lock(&dl_race_lock);
		WRITE_ONCE(dl_safe_mode, safe_mode ? true : false);
		mutex_unlock(&dl_race_lock);
		break;

	case DL_RACE_IOC_GET_STATUS:
		mutex_lock(&dl_race_lock);
		status.counter = dl_counter;
		status.safe_mode = dl_safe_mode ? 1U : 0U;
		status.worker_running = dl_worker_running ? 1U : 0U;
		mutex_unlock(&dl_race_lock);
		if (copy_to_user((void __user *)arg, &status, sizeof(status)))
			return -EFAULT;
		break;

	case DL_RACE_IOC_INC_COUNTER:
		dl_race_increment();
		break;

	case DL_RACE_IOC_RESET_COUNTER:
		mutex_lock(&dl_race_lock);
		dl_counter = 0;
		mutex_unlock(&dl_race_lock);
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations dl_race_fops = {
	.owner = THIS_MODULE,
	.open = dl_race_open,
	.release = dl_race_release,
	.read = dl_race_read,
	.unlocked_ioctl = dl_race_ioctl,
	.llseek = noop_llseek,
};

static int __init driver_lab_race_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dl_race_devt, 0, 1, DL_RACE_CLASS_NAME);
	if (ret)
		return ret;

	cdev_init(&dl_race_cdev, &dl_race_fops);
	dl_race_cdev.owner = THIS_MODULE;
	ret = cdev_add(&dl_race_cdev, dl_race_devt, 1);
	if (ret)
		goto err_unregister_chrdev;

	dl_race_class = class_create(DL_RACE_CLASS_NAME);
	if (IS_ERR(dl_race_class)) {
		ret = PTR_ERR(dl_race_class);
		goto err_del_cdev;
	}

	dl_race_device = device_create(dl_race_class, NULL, dl_race_devt, NULL,
								   DL_RACE_DEVICE_NAME);
	if (IS_ERR(dl_race_device)) {
		ret = PTR_ERR(dl_race_device);
		goto err_destroy_class;
	}

	/*
	 * 先把 worker 會讀寫的 state 放到已知狀態，再啟動 thread。
	 * 舊順序先 kthread_run()、後把 counter 清零，worker 可能已 increment
	 * 卻被 init 路徑覆寫；那不是本 lab 想示範的 race。
	 */
	mutex_lock(&dl_race_lock);
	dl_counter = 0;
	WRITE_ONCE(dl_safe_mode, false);
	dl_worker_running = false;
	mutex_unlock(&dl_race_lock);

	dl_race_worker = kthread_run(dl_race_worker_fn, NULL, "dl_race_worker");
	if (IS_ERR(dl_race_worker)) {
		ret = PTR_ERR(dl_race_worker);
		dl_race_worker = NULL;
		goto err_destroy_device;
	}

	mutex_lock(&dl_race_lock);
	dl_worker_running = true;
	mutex_unlock(&dl_race_lock);

	pr_info("created /dev/%s (major=%d minor=%d)\n",
			DL_RACE_DEVICE_NAME, MAJOR(dl_race_devt), MINOR(dl_race_devt));
	return 0;

err_destroy_device:
	device_destroy(dl_race_class, dl_race_devt);
err_destroy_class:
	class_destroy(dl_race_class);
err_del_cdev:
	cdev_del(&dl_race_cdev);
err_unregister_chrdev:
	unregister_chrdev_region(dl_race_devt, 1);
	return ret;
}

static void __exit driver_lab_race_exit(void)
{
	/* 先停活體，再拆它可能觸及的裝置資源。 */
	if (dl_race_worker)
		kthread_stop(dl_race_worker);

	mutex_lock(&dl_race_lock);
	dl_worker_running = false;
	mutex_unlock(&dl_race_lock);

	device_destroy(dl_race_class, dl_race_devt);
	class_destroy(dl_race_class);
	cdev_del(&dl_race_cdev);
	unregister_chrdev_region(dl_race_devt, 1);
	pr_info("device removed\n");
}

module_init(driver_lab_race_init);
module_exit(driver_lab_race_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 4 locking and races lab");
