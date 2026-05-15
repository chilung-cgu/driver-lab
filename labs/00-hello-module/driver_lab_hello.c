/*
 * pr_fmt() 會被 pr_info()/pr_err() 這類 logging macro 使用。
 * 這裡把 module 名稱自動加到每一行 log 前面，之後看 dmesg 時比較容易知道
 * 這行訊息是誰印的。
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*
 * kernel module 不能直接使用一般 userspace 的 C library。
 * 這些 <linux/...> header 提供的是 kernel 內部 API 與 module 巨集。
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

/*
 * module parameter 讓你可以在載入 module 時傳值，例如：
 *
 *     sudo insmod ./driver_lab_hello.ko who=linux repeat=2
 *
 * charp 代表字串指標，0444 代表載入後這個參數可被讀取。
 * MODULE_PARM_DESC() 是給 modinfo / 文件看的參數說明。
 */
static char *who = "driver-lab";
module_param(who, charp, 0444);
MODULE_PARM_DESC(who, "Greeting target shown in kernel log");

static int repeat = 1;
module_param(repeat, int, 0444);
MODULE_PARM_DESC(repeat, "How many hello messages to emit (1-8)");

/*
 * 這不是一般 C 程式，所以沒有 main()。
 * module_init() 會把這個函式登記成 module 載入入口；insmod 成功載入時，
 * kernel 會呼叫它。
 *
 * __init 是給 kernel 的提示：這段程式碼只在初始化期間需要。
 */
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

/*
 * module_exit() 會把這個函式登記成 module 卸載入口；rmmod 時 kernel 會呼叫它。
 *
 * __exit 是給 kernel 的提示：這段程式碼只在 module 卸載時需要。
 */
static void __exit driver_lab_hello_exit(void)
{
    pr_info("exit\n");
}

/* 明確告訴 kernel：載入與卸載時分別要呼叫哪兩個函式。 */
module_init(driver_lab_hello_init);
module_exit(driver_lab_hello_exit);

/*
 * MODULE_* 是 module metadata，可用 modinfo 查看。
 * MODULE_LICENSE("GPL") 不只是說明文字；kernel module loader 會使用它判斷
 * license 類型，並影響 taint 與 GPL-only symbol 使用。
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Week 0 hello kernel module for driver-lab");
