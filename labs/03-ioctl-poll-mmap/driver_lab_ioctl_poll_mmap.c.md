# `driver_lab_ioctl_poll_mmap.c` 詳解

## 結論

`labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c` 是 Lab03 的 kernel module 本體。它把 Lab02 的 char device 從單純 `read/write` 擴充成四條 userspace 介面路徑：

```text
同一個 /dev/driver_lab_ctl0
  -> read/write      data path
  -> ioctl           control path
  -> poll            event path
  -> mmap            shared memory path
```

這份檔案最重要的學習目標不是「背 API」，而是看懂：

- 同一個 `file_operations` 如何掛上多個 userspace entry point。
- kernel driver 如何維護 shared state。
- userspace pointer 為什麼要經過 `copy_from_user()` / `copy_to_user()`。
- blocking read、non-blocking read、waitqueue、poll 之間怎麼配合。
- `mmap()` 為什麼只能映射 driver 控制的一頁 shared snapshot，不是任意 kernel memory。
- init 失敗路徑和 exit cleanup 如何對稱。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- 本檔 source、[`README.md`](README.md)、[`test.sh`](test.sh)、[`Makefile`](Makefile)。
- 共用 ABI：[`../../runtime/include/driver_lab_uapi.h`](../../runtime/include/driver_lab_uapi.h)。
- userspace runtime 與 CLI：[`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md)、[`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)。
- Linux kernel documentation 的 driver API basics、MM API、kbuild external modules，以及 Linux man-pages 的 `poll(2)` / `mmap(2)`。

沒有在這份文件中展開完整 VFS internals、page fault 處理、或 `struct cdev` 的 kernel 內部實作；這裡只解釋讀懂 Lab03 必要的層次。

## 先理解這份檔案在 repo 的位置

Lab03 的主線是：

```text
tests/driver_lab_char_cli.c
  -> runtime/src/driver_lab_runtime.c
  -> syscall: read/write/ioctl/poll/mmap
  -> /dev/driver_lab_ctl0
  -> driver_lab_ioctl_poll_mmap.c 的 file_operations callback
```

相關檔案分工：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_ioctl_poll_mmap.c`](driver_lab_ioctl_poll_mmap.c) | kernel module 本體 |
| [`../../runtime/include/driver_lab_uapi.h.md`](../../runtime/include/driver_lab_uapi.h.md) | kernel/userspace 共用 ABI |
| [`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md) | userspace syscall wrapper |
| [`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md) | userspace CLI |
| [`test.sh.md`](test.sh.md) | Linux smoke test |
| [`Makefile.md`](Makefile.md) | kbuild external module 入口 |

## 這份檔案要解決什麼問題？

Lab02 讓你知道 `/dev/...` 可以接 `read/write`。Lab03 的問題是：

> 真實 driver 不會只有讀寫 bytes。它通常還需要控制命令、事件通知、狀態查詢，以及較低成本的 shared state 暴露方式。

所以這份 driver 做出四種路徑：

| 路徑 | userspace 看到的形式 | driver callback | 這條路徑在 Lab03 中做什麼 |
|---|---|---|---|
| data path | `read()` / `write()` | `dl_read()` / `dl_write()` | 寫入/讀出目前 message |
| control path | `ioctl()` | `dl_unlocked_ioctl()` | 設 message、查 status、trigger event、clear state |
| event path | `poll()` | `dl_poll()` | 等待可讀資料或 pending event |
| shared memory path | `mmap()` | `dl_mmap()` | 讓 userspace 讀一頁 shared snapshot |

## 它怎麼被 build / load / 呼叫

Build 由同目錄 [`Makefile`](Makefile) 交給 kbuild：

```sh
make
```

會產生：

```text
driver_lab_ioctl_poll_mmap.ko
```

載入後：

```sh
sudo insmod ./driver_lab_ioctl_poll_mmap.ko
```

driver 建立：

```text
/dev/driver_lab_ctl0
/sys/class/driver_lab_ctl/driver_lab_ctl0
```

userspace 操作範例：

```sh
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 ioctl-write hello-03
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 status
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 read
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 mmap-read
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 poll 3000
../../tests/driver_lab_char_cli /dev/driver_lab_ctl0 trigger
```

## 讀 source 的主線

第一次請照這個順序讀，不要從上到下硬掃：

1. `dl_fops`：確認 userspace syscall 會接到哪些 callback。
2. `driver_lab_ioctl_poll_mmap_init()`：看 `/dev` entry 和 shared page 怎麼建立。
3. shared state：`dl_buffer`、`dl_event_count`、`dl_event_pending`、`dl_shared_page_addr`。
4. `dl_publish_message_locked()`：看狀態更新集中點。
5. `dl_write()` / `dl_read()`：data path。
6. `dl_unlocked_ioctl()`：control path。
7. `dl_poll()`：event path。
8. `dl_mmap()`：shared memory path。
9. `driver_lab_ioctl_poll_mmap_exit()`：cleanup 對稱性。

## 一、include 與 UAPI

原始碼：

```c
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "../../runtime/include/driver_lab_uapi.h"
```

重點 include：

| header | 為什麼需要 |
|---|---|
| `<linux/cdev.h>` | `struct cdev`、`cdev_init()`、`cdev_add()` |
| `<linux/device.h>` | `class_create()`、`device_create()` |
| `<linux/fs.h>` | `struct file_operations`、`struct file` |
| `<linux/mm.h>` | `struct vm_area_struct`、`remap_pfn_range()` |
| `<linux/mutex.h>` | `DEFINE_MUTEX()`、`mutex_lock_interruptible()` |
| `<linux/poll.h>` | `poll_wait()`、`POLLIN`、`POLLPRI` |
| `<linux/uaccess.h>` | `copy_from_user()`、`copy_to_user()` |
| `<linux/wait.h>` | waitqueue、`wait_event_interruptible()`、`wake_up_interruptible()` |

最關鍵的是 UAPI：

```c
#include "../../runtime/include/driver_lab_uapi.h"
```

這代表 kernel driver 和 userspace runtime/CLI 使用同一份 ABI 定義。像 `DL_IOC_SET_MESSAGE`、`struct dl_ioctl_status`、`struct dl_shared_page` 都不是 driver 自己私下定義的，它們必須和 userspace 一致。

## 二、device 名稱與 char device resource

原始碼：

```c
#define DL_IOCTL_CLASS_NAME "driver_lab_ctl"
#define DL_IOCTL_DEVICE_NAME "driver_lab_ctl0"

static dev_t dl_devt;
static struct cdev dl_cdev;
static struct class *dl_class;
static struct device *dl_device;
```

這四個全域 resource 對應到 char device 建立流程：

| 變數 | 角色 | userspace 可觀察結果 |
|---|---|---|
| `dl_devt` | major/minor number | `/proc/devices`、sysfs `dev` 檔 |
| `dl_cdev` | 把 `file_operations` 掛到 char device | syscall 能進入 `dl_fops` |
| `dl_class` | device model class | `/sys/class/driver_lab_ctl` |
| `dl_device` | class device instance | `/sys/class/.../driver_lab_ctl0`，通常也讓 `/dev/driver_lab_ctl0` 出現 |

白話講：

```text
alloc_chrdev_region 拿號碼
cdev_add 掛 callback table
class_create/device_create 讓系統看得到這個 device
```

## 三、shared state 與 locking

原始碼：

```c
static DEFINE_MUTEX(dl_lock);
static DECLARE_WAIT_QUEUE_HEAD(dl_read_wq);
static DECLARE_WAIT_QUEUE_HEAD(dl_event_wq);

static char dl_buffer[DL_MESSAGE_BYTES];
static size_t dl_buffer_len;
static unsigned int dl_event_count;
static bool dl_event_pending;
static unsigned long dl_shared_page_addr;
```

這是 Lab03 的核心狀態。

| 狀態 | 意義 |
|---|---|
| `dl_buffer` | 目前 message 內容 |
| `dl_buffer_len` | 目前 message 長度 |
| `dl_event_count` | 累積事件數 |
| `dl_event_pending` | 目前是否有 pending event |
| `dl_shared_page_addr` | `mmap()` 暴露給 userspace 的 shared page |

`dl_lock` 保護這些狀態，避免不同 callback 同時更新時互相踩到。

兩個 waitqueue 分工：

| waitqueue | 用途 |
|---|---|
| `dl_read_wq` | blocking read 等待 buffer 有資料 |
| `dl_event_wq` | poll 等待 event pending 或狀態變化 |

## 四、`dl_sync_shared_page_locked()`：把 driver state 發布到 mmap page

原始碼：

```c
static void dl_sync_shared_page_locked(void)
{
	struct dl_shared_page *page;

	page = (struct dl_shared_page *)dl_shared_page_addr;
	memset(page, 0, sizeof(*page));
	page->magic = DL_SHARED_MAGIC;
	page->version = 1;
	page->event_count = dl_event_count;
	page->event_pending = dl_event_pending ? 1U : 0U;
	page->buffer_len = dl_buffer_len;
	memcpy(page->buffer, dl_buffer, dl_buffer_len);
}
```

這個 helper 把 driver 內部狀態轉成 userspace `mmap-read` 會看到的 `struct dl_shared_page`。

名稱裡的 `_locked` 很重要：它不是語意裝飾，而是告訴你呼叫前必須持有 `dl_lock`。原因是它會同時讀多個 shared state 欄位；如果沒有 lock，userspace 可能看到半更新的 snapshot。

白話講：

```text
driver 內部 state 是真相
shared page 是給 userspace 看的一份快照
每次 state 改變後，就同步更新快照
```

## 五、`dl_publish_message_locked()`：統一更新 message 與 event

原始碼：

```c
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
```

這是 Lab03 最重要的狀態更新 helper。

`write()` 和 `DL_IOC_SET_MESSAGE` 都會走到這裡，所以 data path 和 control path 對 message/event 的語意一致。

它做五件事：

1. 清空舊 buffer。
2. 複製新 message。
3. 補上 `'\0'`，方便 shared page / debug 觀察。
4. 增加 event count，設定 pending event。
5. 更新 mmap shared page。

白話講：

```text
只要有新 message
就同時代表 data 更新、event 發生、shared page 也要刷新
```

## 六、`dl_open()` / `dl_release()`：目前只當觀測點

原始碼：

```c
static int dl_open(struct inode *inode, struct file *file)
{
	pr_info("device opened\n");
	return 0;
}

static int dl_release(struct inode *inode, struct file *file)
{
	pr_info("device released\n");
	return 0;
}
```

目前這兩個 callback 不建立 per-open private state，只印 log。

為什麼還保留？

- 讓你在 `dmesg` 看到 userspace 何時打開/關閉 device。
- 未來如果要做 per-open state，例如每個 fd 有自己的 buffer 或 mode，通常會從 `.open` / `.release` 開始。

白話講：

```text
現在 open/release 只是觀測點
但它們是 driver file instance 生命週期的入口與出口
```

## 七、`dl_read()`：blocking / non-blocking data path

原始碼主體：

```c
if ((file->f_flags & O_NONBLOCK) && READ_ONCE(dl_buffer_len) == 0)
	return -EAGAIN;

ret = wait_event_interruptible(dl_read_wq, READ_ONCE(dl_buffer_len) > 0);
if (ret)
	return ret;

if (mutex_lock_interruptible(&dl_lock))
	return -ERESTARTSYS;

ret = simple_read_from_buffer(buf, count, ppos, dl_buffer, dl_buffer_len);
if (ret > 0 && *ppos >= dl_buffer_len) {
	memset(dl_buffer, 0, sizeof(dl_buffer));
	dl_buffer_len = 0;
	dl_event_pending = false;
	dl_sync_shared_page_locked();
	wake_up_interruptible(&dl_event_wq);
}
```

這段分成三層。

第一層：non-blocking fd 沒資料就立刻回 `-EAGAIN`。

```c
if ((file->f_flags & O_NONBLOCK) && READ_ONCE(dl_buffer_len) == 0)
	return -EAGAIN;
```

第二層：blocking fd 會睡在 `dl_read_wq`，等到 `dl_buffer_len > 0`。

```c
wait_event_interruptible(dl_read_wq, READ_ONCE(dl_buffer_len) > 0);
```

第三層：拿 lock 後用 `simple_read_from_buffer()` 複製資料給 userspace。

```c
simple_read_from_buffer(buf, count, ppos, dl_buffer, dl_buffer_len);
```

Lab03 的 read 是「消費型」語意：如果完整讀完 message，就清 buffer、清 pending event、更新 shared page。

白話講：

```text
沒有資料：
  non-blocking read 直接 -EAGAIN
  blocking read 睡著等

有資料：
  copy 給 userspace
  完整讀完後把 buffer 當作被消費掉
```

## 八、`dl_write()`：把 userspace bytes 發布成新 message

原始碼主體：

```c
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
```

它先處理邊界：

- `count == 0`：空寫入，回 0。
- `count > DL_MESSAGE_BYTES - 1`：太長，回 `-EMSGSIZE`。

接著用 `simple_write_to_buffer()` 從 userspace `buf` 複製到 kernel stack 上的 `local`。

為什麼先複製到 local，再 publish？

```text
copy 成功後再集中更新 dl_buffer/event/shared page
可以讓狀態更新集中在 dl_publish_message_locked()
```

成功後喚醒：

- `dl_read_wq`：blocking read 可以醒。
- `dl_event_wq`：poll 可以看到 event。

白話講：

```text
write 不是 append
它是用新 message 覆蓋舊 message
並把這次更新視為一個 event
```

## 九、`dl_poll()`：把 fd 接到 waitqueue，回報可讀或事件

原始碼：

```c
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
```

新手最容易誤會的是 `poll_wait()`。

它不是「在這一行立刻睡著」。它是把目前 `file` 和 waitqueue 關聯起來，讓 userspace 的 `poll()` 如果需要等待，知道該等哪些 waitqueue。

Lab03 註冊兩個等待點：

```c
poll_wait(file, &dl_read_wq, wait);
poll_wait(file, &dl_event_wq, wait);
```

接著檢查目前狀態：

| 條件 | 回報給 userspace |
|---|---|
| `dl_buffer_len > 0` | `POLLIN | POLLRDNORM` |
| `dl_event_pending` | `POLLPRI` |

白話講：

```text
poll 的 callback 要做兩件事：
1. 告訴 kernel：如果要睡，請睡在這些 waitqueue 上
2. 告訴 userspace：現在是否已經有事件
```

## 十、`dl_unlocked_ioctl()`：control path dispatcher

原始碼骨架：

```c
switch (cmd) {
case DL_IOC_SET_MESSAGE:
	...
	break;
case DL_IOC_GET_STATUS:
	...
	break;
case DL_IOC_TRIGGER_EVENT:
	...
	break;
case DL_IOC_CLEAR_BUFFER:
	...
	break;
default:
	ret = -ENOTTY;
	break;
}
```

這是 control path 的核心。userspace runtime 傳進來的 `cmd` 會被分派到四種行為。

### `DL_IOC_SET_MESSAGE`

```c
if (copy_from_user(&msg, (void __user *)arg, sizeof(msg)))
	return -EFAULT;

len = strnlen(msg.text, sizeof(msg.text));
if (len == sizeof(msg.text))
	len = sizeof(msg.text) - 1;
dl_publish_message_locked(msg.text, len);
```

`arg` 是 userspace pointer，不能直接當 kernel pointer 解參考，所以要 `copy_from_user()`。

收到 struct 後，driver 用 `strnlen()` 限制在固定 ABI buffer 大小內，再交給 `dl_publish_message_locked()`。

### `DL_IOC_GET_STATUS`

```c
status.buffer_len = dl_buffer_len;
status.event_count = dl_event_count;
status.event_pending = dl_event_pending ? 1U : 0U;
status.mmap_size = DL_MMAP_BYTES;

if (copy_to_user((void __user *)arg, &status, sizeof(status)))
	return -EFAULT;
```

這條路徑把 kernel state 整理成 `struct dl_ioctl_status`，再複製回 userspace。

### `DL_IOC_TRIGGER_EVENT`

```c
dl_event_count++;
dl_event_pending = true;
dl_sync_shared_page_locked();
wake_up_interruptible(&dl_event_wq);
```

它不改 message，只產生 event，主要用來喚醒 poll。

### `DL_IOC_CLEAR_BUFFER`

```c
memset(dl_buffer, 0, sizeof(dl_buffer));
dl_buffer_len = 0;
dl_event_pending = false;
dl_sync_shared_page_locked();
wake_up_interruptible(&dl_event_wq);
```

它清掉 buffer 和 pending state，讓後續 test 可以從乾淨狀態開始。

### unknown command

```c
ret = -ENOTTY;
```

這是 ioctl 不認得 command 時常見的錯誤回傳。

## 十一、`dl_mmap()`：只映射 driver 控制的一頁 snapshot

原始碼：

```c
size = vma->vm_end - vma->vm_start;
if (vma->vm_pgoff != 0)
	return -EINVAL;
if (size > PAGE_SIZE)
	return -EINVAL;

pfn = virt_to_phys((void *)dl_shared_page_addr) >> PAGE_SHIFT;
return remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);
```

這裡有兩個安全邊界：

| 檢查 | 意義 |
|---|---|
| `vma->vm_pgoff != 0` | 只允許 offset 0 |
| `size > PAGE_SIZE` | 最多只允許一頁 |

接著把 `dl_shared_page_addr` 轉成 PFN，交給 `remap_pfn_range()` 映射到 userspace VMA。

白話講：

```text
mmap 不是讓 userspace 看任意 kernel memory
Lab03 只允許 userspace 看 driver 預先配置的一頁 shared snapshot
```

userspace CLI 會把這頁當成：

```c
struct dl_shared_page *shared;
```

layout 由 [`../../runtime/include/driver_lab_uapi.h`](../../runtime/include/driver_lab_uapi.h) 定義。

## 十二、`dl_fops`：syscall 到 callback 的路由表

原始碼：

```c
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
```

這是整份 driver 的路由表。userspace 對 `/dev/driver_lab_ctl0` 做不同 syscall 時，VFS 會依這張表呼叫對應 callback。

| userspace syscall | callback |
|---|---|
| `open()` | `dl_open()` |
| `close()` | `dl_release()` |
| `read()` | `dl_read()` |
| `write()` | `dl_write()` |
| `poll()` | `dl_poll()` |
| `ioctl()` | `dl_unlocked_ioctl()` |
| `mmap()` | `dl_mmap()` |

第一次讀 driver 時，先找到 `file_operations`，通常就能抓住 userspace 入口。

## 十三、init：建立 shared page 與 char device

原始碼主線：

```c
dl_shared_page_addr = __get_free_page(GFP_KERNEL | __GFP_ZERO);
...
ret = alloc_chrdev_region(&dl_devt, 0, 1, DL_IOCTL_CLASS_NAME);
...
cdev_init(&dl_cdev, &dl_fops);
ret = cdev_add(&dl_cdev, dl_devt, 1);
...
dl_class = class_create(DL_IOCTL_CLASS_NAME);
...
dl_device = device_create(dl_class, NULL, dl_devt, NULL,
						  DL_IOCTL_DEVICE_NAME);
```

建立順序：

1. 配一頁 shared page。
2. 初始化 shared page snapshot。
3. 取得 major/minor。
4. 初始化並加入 cdev。
5. 建 class。
6. 建 device。

失敗路徑用 `goto err_*` 逐層回收：

```text
device_create 失敗 -> destroy class -> del cdev -> unregister devt -> free page
class_create 失敗 -> del cdev -> unregister devt -> free page
cdev_add 失敗 -> unregister devt -> free page
alloc_chrdev_region 失敗 -> free page
```

這是 kernel driver 很重要的 pattern：resource 拿到一半失敗時，要只釋放已成功取得的 resource。

## 十四、exit：反向釋放 resource

原始碼：

```c
static void __exit driver_lab_ioctl_poll_mmap_exit(void)
{
	device_destroy(dl_class, dl_devt);
	class_destroy(dl_class);
	cdev_del(&dl_cdev);
	unregister_chrdev_region(dl_devt, 1);
	free_page(dl_shared_page_addr);
	pr_info("device removed\n");
}
```

這是 init 成功路徑的反向清理：

```text
device_create      -> device_destroy
class_create       -> class_destroy
cdev_add           -> cdev_del
alloc_chrdev_region -> unregister_chrdev_region
__get_free_page    -> free_page
```

smoke test 的退場驗證會檢查 `/dev/driver_lab_ctl0` 和 `/sys/class/...` 是否消失，目的就是確認 cleanup 不只在 source 看起來對稱，實際 filesystem surface 也退場。

## runtime / CLI / driver 對照

| CLI subcommand | runtime helper | syscall / command | driver callback |
|---|---|---|---|
| `write <msg>` | `dl_runtime_write()` | `write()` | `dl_write()` |
| `read` | `dl_runtime_read()` | `read()` | `dl_read()` |
| `ioctl-write <msg>` | `dl_runtime_ioctl_set_message()` | `DL_IOC_SET_MESSAGE` | `dl_unlocked_ioctl()` |
| `status` | `dl_runtime_ioctl_get_status()` | `DL_IOC_GET_STATUS` | `dl_unlocked_ioctl()` |
| `trigger` | `dl_runtime_ioctl_trigger_event()` | `DL_IOC_TRIGGER_EVENT` | `dl_unlocked_ioctl()` |
| `clear` | `dl_runtime_ioctl_clear_buffer()` | `DL_IOC_CLEAR_BUFFER` | `dl_unlocked_ioctl()` |
| `poll <timeout>` | `dl_runtime_poll_readable()` | `poll()` | `dl_poll()` |
| `mmap-read` | `dl_runtime_mmap_shared()` | `mmap()` | `dl_mmap()` |

## 關鍵 API / 參數角色

| API | 參數角色 | 在本檔的意義 |
|---|---|---|
| `alloc_chrdev_region(&dl_devt, 0, 1, name)` | output dev_t、first minor、count、name | 取得 char device major/minor |
| `cdev_init(&dl_cdev, &dl_fops)` | cdev、callback table | 把 file operations 接到 cdev |
| `device_create(dl_class, NULL, dl_devt, NULL, name)` | class、parent、devt、drvdata、name | 建立 device model entry |
| `wait_event_interruptible(dl_read_wq, condition)` | waitqueue、條件 | blocking read 等資料 |
| `wake_up_interruptible(&dl_event_wq)` | waitqueue | 喚醒 poll/event 等待者 |
| `copy_from_user(&msg, user_arg, sizeof(msg))` | kernel destination、userspace source、大小 | ioctl payload 進 kernel |
| `copy_to_user(user_arg, &status, sizeof(status))` | userspace destination、kernel source、大小 | ioctl status 回 userspace |
| `poll_wait(file, &dl_event_wq, wait)` | file、waitqueue、poll context | 把 fd 和 waitqueue 關聯 |
| `remap_pfn_range(vma, start, pfn, size, prot)` | VMA、userspace address、PFN、大小、權限 | mmap shared page |

## 常見卡點

- `poll_wait()` 不是立刻睡著；它是註冊等待點。
- `copy_from_user()` / `copy_to_user()` 不是普通 `memcpy()`；它們處理 userspace pointer。
- `read()` 在這個 lab 是消費型，完整讀完會清 buffer。
- `DL_IOC_TRIGGER_EVENT` 不改 message，只改 event state。
- `mmap()` 看到的是 `struct dl_shared_page` snapshot，不是 driver 全部內部狀態。
- `dl_lock` 保護的是 driver state 一致性，不是保護 userspace pointer。
- init failure path 和 exit path 都要看；只看成功路徑會漏掉 driver 最常見的 bug 類型。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab03 的 userspace 入口是哪個 device node？ | `/dev/driver_lab_ctl0`。 |
| syscall 到 callback 的路由表在哪？ | `dl_fops`。 |
| `write()` 和 `DL_IOC_SET_MESSAGE` 最後都集中到哪個 helper？ | `dl_publish_message_locked()`。 |
| blocking read 沒資料時睡在哪個 waitqueue？ | `dl_read_wq`。 |
| `poll()` 為什麼能被 `trigger` 喚醒？ | `dl_poll()` 註冊 `dl_event_wq`，`DL_IOC_TRIGGER_EVENT` 會設定 pending 並 `wake_up_interruptible(&dl_event_wq)`。 |
| `mmap()` 最多允許映射多少？ | 一頁，`size > PAGE_SIZE` 會回 `-EINVAL`。 |
| shared page layout 在哪定義？ | [`../../runtime/include/driver_lab_uapi.h`](../../runtime/include/driver_lab_uapi.h) 的 `struct dl_shared_page`。 |
| init 中 `device_create()` 失敗時要釋放哪些 resource？ | destroy class、del cdev、unregister devt、free shared page。 |

## 查證來源

- Linux kernel documentation `Driver Basics`：waitqueue、`wait_event_interruptible()` 等 driver 基本 API。<https://docs.kernel.org/driver-api/basics.html>
- Linux kernel documentation `Memory Management APIs`：`remap_pfn_range()`。<https://docs.kernel.org/core-api/mm-api.html>
- Linux kernel documentation `Building External Modules`：kbuild external module 流程。<https://docs.kernel.org/kbuild/modules.html>
- Linux man-pages `poll(2)`：`poll()` / `POLLIN` / `POLLPRI` / `revents` 語意。<https://man7.org/linux/man-pages/man2/poll.2.html>
- Linux man-pages `mmap(2)`：`mmap()` / `MAP_SHARED` / `munmap()` userspace 語意。<https://man7.org/linux/man-pages/man2/mmap.2.html>
