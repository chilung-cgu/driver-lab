// SPDX-License-Identifier: GPL-2.0-only
/*
 * 這一關刻意保留「會 race」與「用 mutex 修正」兩種模式。
 * 目的不是寫一支產品 driver，而是讓 lost update 可以被觀察、被解釋、再被修正。
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/compiler.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
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

/*
 * 這些變數會同時被 userspace ioctl/read path 與背景 kthread 使用。
 * 新手讀這關時，先把「誰會讀/寫這些 state」標出來。
 */
static unsigned int dl_counter;
static bool dl_safe_mode;
static bool dl_worker_running;

/* safe mode 的核心：用外層 mutex 保護這個 counter increment。 */
static void dl_race_increment_locked(void)
{
	/* 這是修正後的最小版本：一次只讓一條路徑修改 counter。 */
	dl_counter++;
}

/* unsafe mode 的核心：故意不保護 read-modify-write，用來示範 lost update。 */
static void dl_race_increment_unlocked(void)
{
	unsigned int snapshot;

	/*
	 * 故意把「讀 -> 等一下 -> 寫回」拆開，
	 * 讓多條 userspace 路徑更容易踩出 lost update。
	 */
	snapshot = dl_counter;
	usleep_range(1000, 2000);
	dl_counter = snapshot + 1;
}

/*
 * 共用 increment 入口。
 * userspace ioctl 與背景 kthread 都走這裡，再依 safe_mode 切換安全/不安全路徑。
 */
static void dl_race_increment(void)
{
	/*
	 * 這裡刻意只示範「counter 沒有被妥善保護」的 race。
	 * safe_mode 本身則用 READ_ONCE/WRITE_ONCE 包起來，
	 * 避免新手把「示範 race」和「額外的未同步旗標讀寫」混為一談。
	 */
	if (READ_ONCE(dl_safe_mode)) {
		mutex_lock(&dl_race_lock);
		dl_race_increment_locked();
		mutex_unlock(&dl_race_lock);
		return;
	}

	dl_race_increment_unlocked();
}

/*
 * 背景 kernel thread。
 * 它模擬 driver 內部也會碰共享 state，因此 race 不只來自 userspace。
 */
static int dl_race_worker_fn(void *unused)
{
	/* 模擬「就算 userspace 沒下指令，driver 背後也可能有工作在跑」。 */
	while (!kthread_should_stop()) {
		if (READ_ONCE(dl_safe_mode)) {
			mutex_lock(&dl_race_lock);
			dl_race_increment_locked();
			mutex_unlock(&dl_race_lock);
		} else {
			dl_race_increment_unlocked();
		}

		msleep(20);
	}

	return 0;
}

/* open/release 目前只作為 /dev/driver_lab_race0 被使用的觀測點。 */
static int dl_race_open(struct inode *inode, struct file *file)
{
	pr_info("device opened\n");
	return 0;
}

static int dl_race_release(struct inode *inode, struct file *file)
{
	pr_info("device released\n");
	return 0;
}

/* read path：輸出人類可讀的 counter/safe_mode/worker 狀態。 */
static ssize_t dl_race_read(struct file *file, char __user *buf,
							size_t count, loff_t *ppos)
{
	char status[DL_STATUS_BUFFER_BYTES];
	int len;

	/* read() 只負責把目前狀態白話地吐回 userspace。 */
	mutex_lock(&dl_race_lock);
	len = scnprintf(status, sizeof(status),
					"counter=%u safe_mode=%u worker_running=%u\n",
					dl_counter, dl_safe_mode ? 1 : 0,
					dl_worker_running ? 1 : 0);
	mutex_unlock(&dl_race_lock);

	return simple_read_from_buffer(buf, count, ppos, status, len);
}

/*
 * control path：CLI 的 safe-mode/reset/inc/status 都會變成這裡的 ioctl command。
 * 這裡故意保留 unsafe increment，讓測試能比較 race 修正前後。
 */
static long dl_race_ioctl(struct file *file, unsigned int cmd,
						  unsigned long arg)
{
	struct dl_race_status status;
	unsigned int safe_mode;

	switch (cmd) {
	case DL_RACE_IOC_SET_SAFE_MODE:
		/*
		 * 讓 userspace 可以切換「故意示範 race」與「用 mutex 修正」兩種模式。
		 * 參數角色：&safe_mode 是 kernel destination，arg 是 userspace source。
		 */
		if (copy_from_user(&safe_mode, (void __user *)arg, sizeof(safe_mode)))
			return -EFAULT;

		/* 參數角色：&dl_race_lock 是保護共享 state 的 mutex。 */
		mutex_lock(&dl_race_lock);
		WRITE_ONCE(dl_safe_mode, safe_mode ? true : false);
		mutex_unlock(&dl_race_lock);
		break;

	case DL_RACE_IOC_GET_STATUS:
		/*
		 * ioctl status 回傳的是結構化 ABI；read() 回傳的是給人看的文字。
		 * 兩者都讀同一份 state，所以都要用同一把 lock 保護。
		 */
		mutex_lock(&dl_race_lock);
		status.counter = dl_counter;
		status.safe_mode = dl_safe_mode ? 1U : 0U;
		status.worker_running = dl_worker_running ? 1U : 0U;
		mutex_unlock(&dl_race_lock);

		/* 參數角色：arg 是 userspace destination，&status 是 kernel source。 */
		if (copy_to_user((void __user *)arg, &status, sizeof(status)))
			return -EFAULT;
		break;

	case DL_RACE_IOC_INC_COUNTER:
		/* 這是 race 實驗的主角：多條 thread 會反覆打這個 ioctl。 */
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
	/* /dev/driver_lab_race0 的 read/ioctl 入口。 */
	.open = dl_race_open,
	.release = dl_race_release,
	.read = dl_race_read,
	.unlocked_ioctl = dl_race_ioctl,
	.llseek = noop_llseek,
};

/*
 * module 載入入口。
 * 先建立 char device 入口，再啟動背景 worker，讓 race lab 一載入就有內部競爭來源。
 */
static int __init driver_lab_race_init(void)
{
	int ret;

	/* 參數角色同 02：&dl_race_devt 是 output，後面 cdev/device 會使用。 */
	ret = alloc_chrdev_region(&dl_race_devt, 0, 1, DL_RACE_CLASS_NAME);
	if (ret)
		return ret;

	/* 參數角色：把 read/ioctl callback table 接到 race cdev。 */
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

	/* 建立 sysfs device entry；/dev/driver_lab_race0 通常由 devtmpfs 補出。 */
	dl_race_device = device_create(dl_race_class, NULL, dl_race_devt, NULL,
								   DL_RACE_DEVICE_NAME);
	if (IS_ERR(dl_race_device)) {
		ret = PTR_ERR(dl_race_device);
		goto err_destroy_class;
	}

	/*
	 * 啟動背景 worker，讓 race 不只來自 userspace，也來自 driver 內部工作。
	 * 參數角色：function、private data(NULL)、thread name。
	 */
	dl_race_worker = kthread_run(dl_race_worker_fn, NULL, "dl_race_worker");
	if (IS_ERR(dl_race_worker)) {
		ret = PTR_ERR(dl_race_worker);
		goto err_destroy_device;
	}

	mutex_lock(&dl_race_lock);
	dl_counter = 0;
	WRITE_ONCE(dl_safe_mode, false);
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

/* module 卸載入口：先停 worker，再拆 /dev 資源，避免 lifetime race。 */
static void __exit driver_lab_race_exit(void)
{
	/* 先停 worker，再拆裝置資源，避免背景 thread 繼續碰已釋放的物件。 */
	mutex_lock(&dl_race_lock);
	dl_worker_running = false;
	mutex_unlock(&dl_race_lock);

	if (dl_race_worker)
		kthread_stop(dl_race_worker);

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
MODULE_DESCRIPTION("Week 4 locking and races lab for driver-lab");
