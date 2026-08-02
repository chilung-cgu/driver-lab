// SPDX-License-Identifier: GPL-2.0-only
/*
 * This lab intentionally keeps an unsafe lost-update mode and a mutex-corrected
 * mode. The phase gate makes experiment transitions deterministic without
 * serializing increments within a phase.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
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
static DECLARE_RWSEM(dl_phase_gate);
static struct task_struct *dl_race_worker;

static dl_race_u32 dl_counter;
static bool dl_safe_mode;
static bool dl_worker_running;

static void dl_race_increment_locked(void)
{
	dl_counter++;
}

static void dl_race_increment_unlocked(void)
{
	dl_race_u32 snapshot;

	/* Deliberately enlarge the read-modify-write race window. */
	snapshot = READ_ONCE(dl_counter);
	usleep_range(1000, 2000);
	WRITE_ONCE(dl_counter, snapshot + 1U);
}

static void dl_race_increment(void)
{
	/*
	 * Multiple increments may hold the read side concurrently, so unsafe mode
	 * still races. Mode changes and reset take the write side, which waits for
	 * every in-flight increment and blocks new ones. This prevents an old unsafe
	 * increment from waking after reset/mode switch and corrupting the next phase.
	 */
	down_read(&dl_phase_gate);
	if (dl_safe_mode) {
		mutex_lock(&dl_race_lock);
		dl_race_increment_locked();
		mutex_unlock(&dl_race_lock);
	} else {
		dl_race_increment_unlocked();
	}
	up_read(&dl_phase_gate);
}

static void dl_race_get_status(struct dl_race_status *status)
{
	/* A write-side snapshot excludes every increment and every phase change. */
	down_write(&dl_phase_gate);
	mutex_lock(&dl_race_lock);
	status->counter = dl_counter;
	status->safe_mode = dl_safe_mode ? 1U : 0U;
	status->worker_running = dl_worker_running ? 1U : 0U;
	status->reserved = 0;
	mutex_unlock(&dl_race_lock);
	up_write(&dl_phase_gate);
}

static void dl_race_set_safe_mode(bool safe_mode)
{
	down_write(&dl_phase_gate);
	mutex_lock(&dl_race_lock);
	dl_safe_mode = safe_mode;
	mutex_unlock(&dl_race_lock);
	up_write(&dl_phase_gate);
}

static void dl_race_reset_counter(void)
{
	down_write(&dl_phase_gate);
	mutex_lock(&dl_race_lock);
	dl_counter = 0;
	mutex_unlock(&dl_race_lock);
	up_write(&dl_phase_gate);
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
	struct dl_race_status snapshot;
	char status[DL_STATUS_BUFFER_BYTES];
	int len;

	dl_race_get_status(&snapshot);
	len = scnprintf(status, sizeof(status),
					"counter=%u safe_mode=%u worker_running=%u\n",
					snapshot.counter, snapshot.safe_mode,
					snapshot.worker_running);

	return simple_read_from_buffer(buf, count, ppos, status, len);
}

static long dl_race_ioctl(struct file *file, unsigned int cmd,
						  unsigned long arg)
{
	struct dl_race_status status;
	dl_race_u32 safe_mode;

	switch (cmd) {
	case DL_RACE_IOC_SET_SAFE_MODE:
		if (copy_from_user(&safe_mode, (void __user *)arg,
					   sizeof(safe_mode)))
			return -EFAULT;
		if (safe_mode > 1U)
			return -EINVAL;
		dl_race_set_safe_mode(safe_mode == 1U);
		break;

	case DL_RACE_IOC_GET_STATUS:
		dl_race_get_status(&status);
		if (copy_to_user((void __user *)arg, &status, sizeof(status)))
			return -EFAULT;
		break;

	case DL_RACE_IOC_INC_COUNTER:
		dl_race_increment();
		break;

	case DL_RACE_IOC_RESET_COUNTER:
		dl_race_reset_counter();
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

	/* Initialize every worker-visible field before publishing/starting users. */
	mutex_lock(&dl_race_lock);
	dl_counter = 0;
	dl_safe_mode = false;
	dl_worker_running = false;
	mutex_unlock(&dl_race_lock);

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

	/* Start the worker before publishing the class device to userspace. */
	dl_race_worker = kthread_run(dl_race_worker_fn, NULL, "dl_race_worker");
	if (IS_ERR(dl_race_worker)) {
		ret = PTR_ERR(dl_race_worker);
		dl_race_worker = NULL;
		goto err_destroy_class;
	}

	down_write(&dl_phase_gate);
	mutex_lock(&dl_race_lock);
	dl_worker_running = true;
	mutex_unlock(&dl_race_lock);
	up_write(&dl_phase_gate);

	dl_race_device = device_create(dl_race_class, NULL, dl_race_devt, NULL,
								   DL_RACE_DEVICE_NAME);
	if (IS_ERR(dl_race_device)) {
		ret = PTR_ERR(dl_race_device);
		goto err_stop_worker;
	}

	pr_info("created /dev/%s (major=%d minor=%d)\n",
			DL_RACE_DEVICE_NAME, MAJOR(dl_race_devt), MINOR(dl_race_devt));
	return 0;

err_stop_worker:
	kthread_stop(dl_race_worker);
	dl_race_worker = NULL;
	down_write(&dl_phase_gate);
	mutex_lock(&dl_race_lock);
	dl_worker_running = false;
	mutex_unlock(&dl_race_lock);
	up_write(&dl_phase_gate);
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
	/* Stop the live actor and wait for its function to return before teardown. */
	if (dl_race_worker) {
		kthread_stop(dl_race_worker);
		dl_race_worker = NULL;
	}

	down_write(&dl_phase_gate);
	mutex_lock(&dl_race_lock);
	dl_worker_running = false;
	mutex_unlock(&dl_race_lock);
	up_write(&dl_phase_gate);

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
MODULE_DESCRIPTION("Week 4 locking and races lab with quiescent phase changes");
