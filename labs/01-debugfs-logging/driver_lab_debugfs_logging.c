/*
 * 這一關示範「driver 不只靠 log，也要把狀態導出來」。
 * pr_fmt() 讓每一行 pr_info()/pr_debug() 自動帶 module 名稱，方便看 dmesg。
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define DL_DEBUGFS_DIR_NAME "driver_lab_debugfs"
#define DL_LAST_MESSAGE_LEN 64

static struct dentry *dl_root;
static DEFINE_MUTEX(dl_state_lock);

/*
 * 這些變數就是本 lab 的 in-kernel state。
 * debugfs 的 status/trigger_count/emit_debug 只是把它們開一個觀測入口給人看。
 */
static u32 dl_trigger_count;
static u32 dl_emit_debug = 1;
static char dl_last_message[DL_LAST_MESSAGE_LEN] = "not-triggered-yet";

static int dl_status_show(struct seq_file *m, void *unused)
{
    /* 把目前的 in-kernel state 轉成文字，方便直接觀測。 */
    mutex_lock(&dl_state_lock);
    seq_printf(m, "trigger_count=%u\n", dl_trigger_count);
    seq_printf(m, "emit_debug=%u\n", dl_emit_debug);
    seq_printf(m, "last_message=%s\n", dl_last_message);
    mutex_unlock(&dl_state_lock);

    return 0;
}

static int dl_status_open(struct inode *inode, struct file *file)
{
    /*
     * single_open() 是 seq_file 的簡化用法。
     * 新手先記：cat status 時，最後會呼叫 dl_status_show() 產生文字內容。
     */
    return single_open(file, dl_status_show, inode->i_private);
}

static ssize_t dl_trigger_write(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
    char local[DL_LAST_MESSAGE_LEN];
    size_t copy_len;
    ssize_t ret = count;
    char *trimmed;

    if (count == 0)
        return 0;

    copy_len = min(count, sizeof(local) - 1);

    if (copy_from_user(local, buf, copy_len))
        return -EFAULT;

    /*
     * userspace 傳進來的是 bytes，不保證自帶 C 字串結尾。
     * 所以要自己補 '\0'，再用 strim() 去掉前後空白與換行。
     */
    local[copy_len] = '\0';
    trimmed = strim(local);

    if (mutex_lock_interruptible(&dl_state_lock))
        return -ERESTARTSYS;

    /* 這個 lab 只保留最後一次 payload，讓狀態變化更容易看懂。 */
    strscpy(dl_last_message, trimmed[0] ? trimmed : "(empty)",
            sizeof(dl_last_message));
    dl_trigger_count++;

    pr_info("trigger #%u payload=\"%s\"\n",
            dl_trigger_count, dl_last_message);

    if (dl_emit_debug)
        pr_debug("dynamic-debug path count=%u len=%zu\n",
                 dl_trigger_count, strlen(dl_last_message));

    mutex_unlock(&dl_state_lock);

    return ret;
}

static const struct file_operations dl_status_fops = {
    .owner = THIS_MODULE,
    /* cat status -> open -> dl_status_open() -> dl_status_show()。 */
    .open = dl_status_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static const struct file_operations dl_trigger_fops = {
    .owner = THIS_MODULE,
    /* tee/printf 寫 trigger -> dl_trigger_write()。 */
    .write = dl_trigger_write,
    .llseek = no_llseek,
};

static int __init driver_lab_debugfs_logging_init(void)
{
    struct dentry *entry;
    int ret;

    /* 這個 lab 匯出的所有 debugfs 檔案都放在同一個目錄下。 */
    dl_root = debugfs_create_dir(DL_DEBUGFS_DIR_NAME, NULL);
    if (IS_ERR(dl_root))
        return PTR_ERR(dl_root);
    if (!dl_root)
        return -ENODEV;

    entry = debugfs_create_file("status", 0444, dl_root, NULL, &dl_status_fops);
    if (IS_ERR_OR_NULL(entry)) {
        ret = entry ? PTR_ERR(entry) : -ENOMEM;
        goto err_remove_debugfs;
    }

    entry = debugfs_create_file("trigger", 0200, dl_root, NULL, &dl_trigger_fops);
    if (IS_ERR_OR_NULL(entry)) {
        ret = entry ? PTR_ERR(entry) : -ENOMEM;
        goto err_remove_debugfs;
    }

    entry = debugfs_create_u32("trigger_count", 0444, dl_root, &dl_trigger_count);
    if (IS_ERR_OR_NULL(entry)) {
        ret = entry ? PTR_ERR(entry) : -ENOMEM;
        goto err_remove_debugfs;
    }

    entry = debugfs_create_u32("emit_debug", 0644, dl_root, &dl_emit_debug);
    if (IS_ERR_OR_NULL(entry)) {
        ret = entry ? PTR_ERR(entry) : -ENOMEM;
        goto err_remove_debugfs;
    }

    pr_info("debugfs directory created at /sys/kernel/debug/%s\n",
            DL_DEBUGFS_DIR_NAME);
    return 0;

err_remove_debugfs:
    debugfs_remove(dl_root);
    return ret;
}

static void __exit driver_lab_debugfs_logging_exit(void)
{
    /* debugfs_remove() 會移除整個目錄樹；不用逐一 remove 每個檔案。 */
    debugfs_remove(dl_root);
    pr_info("debugfs directory removed\n");
}

module_init(driver_lab_debugfs_logging_init);
module_exit(driver_lab_debugfs_logging_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 1 debugfs and dynamic debug lab");
