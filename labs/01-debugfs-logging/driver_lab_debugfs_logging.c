// SPDX-License-Identifier: GPL-2.0-only
/*
 * 這一關示範 debugfs + dynamic debug。debugfs 是開發/診斷介面，
 * 不是穩定的產品 UAPI；即使是教學介面，並行讀寫仍必須有同步規則。
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define DL_DEBUGFS_DIR_NAME "driver_lab_debugfs"
#define DL_LAST_MESSAGE_LEN 64

static struct dentry *dl_root;
static DEFINE_MUTEX(dl_message_lock);

/*
 * debugfs_create_atomic_t() 會直接讀寫 atomic_t，因此 scalar state 也用
 * atomic API 存取。last_message 是多 byte object，仍由 mutex 保護。
 */
static atomic_t dl_trigger_count = ATOMIC_INIT(0);
static atomic_t dl_emit_debug = ATOMIC_INIT(1);
static char dl_last_message[DL_LAST_MESSAGE_LEN] = "not-triggered-yet";

static int dl_status_show(struct seq_file *m, void *unused)
{
	seq_printf(m, "trigger_count=%d\n", atomic_read(&dl_trigger_count));
	seq_printf(m, "emit_debug=%d\n", atomic_read(&dl_emit_debug));

	mutex_lock(&dl_message_lock);
	seq_printf(m, "last_message=%s\n", dl_last_message);
	mutex_unlock(&dl_message_lock);
	return 0;
}

static int dl_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, dl_status_show, inode->i_private);
}

static ssize_t dl_trigger_write(struct file *file, const char __user *buf,
								size_t count, loff_t *ppos)
{
	char local[DL_LAST_MESSAGE_LEN];
	char *trimmed;
	size_t copy_len;
	int trigger_count;

	if (count == 0)
		return 0;

	copy_len = min(count, sizeof(local) - 1);
	if (copy_from_user(local, buf, copy_len))
		return -EFAULT;

	local[copy_len] = '\0';
	trimmed = strim(local);

	if (mutex_lock_interruptible(&dl_message_lock))
		return -ERESTARTSYS;
	strscpy(dl_last_message, trimmed[0] ? trimmed : "(empty)",
		  sizeof(dl_last_message));
	trigger_count = atomic_inc_return(&dl_trigger_count);
	pr_info("trigger #%d payload=\"%s\"\n",
		trigger_count, dl_last_message);
	if (atomic_read(&dl_emit_debug))
		pr_debug("dynamic-debug path count=%d len=%zu\n",
			 trigger_count, strlen(dl_last_message));
	mutex_unlock(&dl_message_lock);

	return count;
}

static const struct file_operations dl_status_fops = {
	.owner = THIS_MODULE,
	.open = dl_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dl_trigger_fops = {
	.owner = THIS_MODULE,
	.write = dl_trigger_write,
	.llseek = noop_llseek,
};

static int __init driver_lab_debugfs_logging_init(void)
{
	struct dentry *entry;
	int ret;

	dl_root = debugfs_create_dir(DL_DEBUGFS_DIR_NAME, NULL);
	if (IS_ERR(dl_root))
		return PTR_ERR(dl_root);

	entry = debugfs_create_file("status", 0444, dl_root, NULL,
					&dl_status_fops);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto err_remove_debugfs;
	}

	entry = debugfs_create_file("trigger", 0200, dl_root, NULL,
					&dl_trigger_fops);
	if (IS_ERR(entry)) {
		ret = PTR_ERR(entry);
		goto err_remove_debugfs;
	}

	debugfs_create_atomic_t("trigger_count", 0444, dl_root,
					&dl_trigger_count);
	debugfs_create_atomic_t("emit_debug", 0644, dl_root,
					&dl_emit_debug);

	pr_info("debugfs directory created at /sys/kernel/debug/%s\n",
		DL_DEBUGFS_DIR_NAME);
	return 0;

err_remove_debugfs:
	debugfs_remove(dl_root);
	return ret;
}

static void __exit driver_lab_debugfs_logging_exit(void)
{
	/* Current debugfs_remove() recursively removes children. */
	debugfs_remove(dl_root);
	pr_info("debugfs directory removed\n");
}

module_init(driver_lab_debugfs_logging_init);
module_exit(driver_lab_debugfs_logging_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 1 debugfs and dynamic debug lab");
