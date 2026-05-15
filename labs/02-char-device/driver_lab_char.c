/*
 * 這一關是第一個真正建立 /dev node 的 lab。
 * userspace 對 /dev/driver_lab_char0 做 read/write 時，VFS 會轉呼叫本檔案的
 * file_operations callback。
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define DL_CHAR_CLASS_NAME "driver_lab_char"
#define DL_CHAR_DEVICE_NAME "driver_lab_char0"
#define DL_CHAR_BUFFER_SIZE 256

static dev_t dl_char_devt;
static struct cdev dl_char_cdev;
static struct class *dl_char_class;
static struct device *dl_char_device;

/*
 * read/write 會共享同一份 kernel buffer。
 * 即使教學範例很小，也先用 mutex 養成「共享狀態要保護」的習慣。
 */
static DEFINE_MUTEX(dl_char_lock);
static char dl_char_buffer[DL_CHAR_BUFFER_SIZE];
static size_t dl_char_buffer_len;

static int dl_char_open(struct inode *inode, struct file *file)
{
    /* 目前還沒有 per-open private state，這裡先當成觀測點。 */
    pr_info("device opened\n");
    return 0;
}

static int dl_char_release(struct inode *inode, struct file *file)
{
    pr_info("device released\n");
    return 0;
}

static ssize_t dl_char_read(struct file *file, char __user *buf,
                            size_t count, loff_t *ppos)
{
    ssize_t ret;

    /*
     * buf 是 userspace pointer，不能當成一般 kernel pointer 直接解參考。
     * simple_read_from_buffer() 內部會處理 copy_to_user() 這類安全複製。
     */
    if (mutex_lock_interruptible(&dl_char_lock))
        return -ERESTARTSYS;

    /* 讓 read 行為接近一般檔案，只是底層資料來自 kernel buffer。 */
    ret = simple_read_from_buffer(buf, count, ppos,
                                  dl_char_buffer, dl_char_buffer_len);
    if (ret > 0)
        pr_info("read %zd bytes\n", ret);

out:
    mutex_unlock(&dl_char_lock);
    return ret;
}

static ssize_t dl_char_write(struct file *file, const char __user *buf,
                             size_t count, loff_t *ppos)
{
    ssize_t ret;
    loff_t pos = 0;

    if (count == 0)
        return 0;

    if (count > DL_CHAR_BUFFER_SIZE - 1)
        return -EMSGSIZE;

    /*
     * 這裡的 buf 也是 userspace pointer。
     * simple_write_to_buffer() 會透過安全路徑把資料搬進 kernel buffer。
     */
    if (mutex_lock_interruptible(&dl_char_lock))
        return -ERESTARTSYS;

    /*
     * 為了讓這個 lab 的語意保持單純，
     * 每次 write 都直接覆蓋整個 buffer，而不是依目前檔案位置做 append。
     */
    ret = simple_write_to_buffer(dl_char_buffer, DL_CHAR_BUFFER_SIZE - 1,
                                 &pos, buf, count);
    if (ret < 0)
        goto out;

    dl_char_buffer[ret] = '\0';
    dl_char_buffer_len = ret;
    *ppos = 0;
    pr_info("wrote %zu bytes\n", dl_char_buffer_len);

out:
    mutex_unlock(&dl_char_lock);
    return ret;
}

static const struct file_operations dl_char_fops = {
    .owner = THIS_MODULE,
    /* /dev/driver_lab_char0 的 open/read/write 最後會走到這些 callback。 */
    .open = dl_char_open,
    .release = dl_char_release,
    .read = dl_char_read,
    .write = dl_char_write,
    .llseek = no_llseek,
};

static int __init driver_lab_char_init(void)
{
    int ret;

    /* 為這個 char device 配一組 major/minor。 */
    ret = alloc_chrdev_region(&dl_char_devt, 0, 1, DL_CHAR_CLASS_NAME);
    if (ret)
        return ret;

    cdev_init(&dl_char_cdev, &dl_char_fops);
    dl_char_cdev.owner = THIS_MODULE;

    ret = cdev_add(&dl_char_cdev, dl_char_devt, 1);
    if (ret)
        goto err_unregister_region;

    /* class/device 這一組會讓 udev 幫我們建立 /dev/driver_lab_char0。 */
    dl_char_class = class_create(DL_CHAR_CLASS_NAME);
    if (IS_ERR(dl_char_class)) {
        ret = PTR_ERR(dl_char_class);
        goto err_del_cdev;
    }

    dl_char_device = device_create(dl_char_class, NULL, dl_char_devt, NULL,
                                   DL_CHAR_DEVICE_NAME);
    if (IS_ERR(dl_char_device)) {
        ret = PTR_ERR(dl_char_device);
        goto err_destroy_class;
    }

    pr_info("created /dev/%s (major=%d minor=%d)\n",
            DL_CHAR_DEVICE_NAME, MAJOR(dl_char_devt), MINOR(dl_char_devt));
    return 0;

err_destroy_class:
    class_destroy(dl_char_class);
err_del_cdev:
    cdev_del(&dl_char_cdev);
err_unregister_region:
    unregister_chrdev_region(dl_char_devt, 1);
    return ret;
}

static void __exit driver_lab_char_exit(void)
{
    /*
     * cleanup 順序要跟 init 拿資源的順序相反：
     * 先移除 /dev node，再 class，再 cdev，最後釋放 major/minor。
     */
    device_destroy(dl_char_class, dl_char_devt);
    class_destroy(dl_char_class);
    cdev_del(&dl_char_cdev);
    unregister_chrdev_region(dl_char_devt, 1);
    pr_info("device removed\n");
}

module_init(driver_lab_char_init);
module_exit(driver_lab_char_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 2 simple char device for driver-lab");
