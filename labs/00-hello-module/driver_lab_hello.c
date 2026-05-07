#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

static char *who = "driver-lab";
module_param(who, charp, 0444);
MODULE_PARM_DESC(who, "Greeting target shown in kernel log");

static int repeat = 1;
module_param(repeat, int, 0444);
MODULE_PARM_DESC(repeat, "How many hello messages to emit (1-8)");

static int __init driver_lab_hello_init(void)
{
    int i;

    /* 第一個 lab 先保持規則單純，讓 smoke test 容易預測。 */
    if (repeat < 1 || repeat > 8)
        return -EINVAL;

    pr_info("init who=%s repeat=%d\n", who, repeat);

    for (i = 0; i < repeat; ++i)
        pr_info("hello %d/%d to %s\n", i + 1, repeat, who);

    return 0;
}

static void __exit driver_lab_hello_exit(void)
{
    pr_info("exit\n");
}

module_init(driver_lab_hello_init);
module_exit(driver_lab_hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 0 hello kernel module for driver-lab");
