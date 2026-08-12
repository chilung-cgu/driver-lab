# `driver_lab_race.c` 詳解

## 結論

`labs/04-locking-and-races/driver_lab_race.c` 是 driver-lab 第一個專門讓你「看見 race，再用 mutex 修掉」的 kernel module。它沿用 Lab02/Lab03 的 char device 模型，但把學習焦點從「如何建立 `/dev` 入口」轉到「多條路徑同時碰 shared state 時會發生什麼」。

module 載入後會建立：

```text
/dev/driver_lab_race0
```

userspace CLI 會透過 `ioctl()` 操作它：

```text
status       -> DL_RACE_IOC_GET_STATUS
reset        -> DL_RACE_IOC_RESET_COUNTER
safe-mode    -> DL_RACE_IOC_SET_SAFE_MODE
inc/race     -> DL_RACE_IOC_INC_COUNTER
```

這份 driver 刻意提供兩種模式：

| 模式 | 行為 | 學習目的 |
|---|---|---|
| `safe_mode = 0` | `dl_race_increment_unlocked()` 故意不加鎖 | 觀察 lost update |
| `safe_mode = 1` | 用 `mutex` 包住 `dl_counter++` | 對照最小修正版 |

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- 本檔 source、[`driver_lab_race_uapi.h.md`](driver_lab_race_uapi.h.md)、[`test.sh.md`](test.sh.md)、[`Makefile.md`](Makefile.md)。
- userspace 施壓工具：[`../../tests/driver_lab_race_cli.c.md`](../../tests/driver_lab_race_cli.c.md)。
- repo 既有導讀：[`../../docs/concepts/concurrency-primer.md`](../../docs/concepts/concurrency-primer.md)、[`../../docs/guides/lab-04-walkthrough.md`](../../docs/guides/lab-04-study-order.md)。
- Linux kernel documentation：mutex/lock types、kthread、ioctl、READ_ONCE/WRITE_ONCE、device infrastructure。

這裡不把 spinlock、atomic、completion、waitqueue、KCSAN、lockdep 全部展開。Lab04 source 目前主要實作的是 mutex、kthread、ioctl ABI 與可觀測 lost update。

## 先理解這份檔案在 repo 的位置

Lab04 站在 Lab03 後面：

```text
Lab03:
  data/control/event/shared-memory path
  read/write/ioctl/poll/mmap

Lab04:
  shared state under concurrency
  userspace pthreads + kernel kthread
  unsafe mode vs safe mode

Lab05-07:
  PCI device resource + IRQ/DMA path
  concurrency and lifetime become more realistic
```

相關檔案：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_race.c`](driver_lab_race.c) | kernel driver，本 lab 的 race/safe-mode 核心 |
| [`driver_lab_race_uapi.h.md`](driver_lab_race_uapi.h.md) | kernel/userspace 共用 ioctl ABI |
| [`../../tests/driver_lab_race_cli.c.md`](../../tests/driver_lab_race_cli.c.md) | userspace CLI，用 pthread 製造壓力 |
| [`test.sh.md`](test.sh.md) | smoke test，對照 unsafe/safe 結果 |
| [`Makefile.md`](Makefile.md) | Lab04 external module kbuild 入口 |

## 這份檔案要解決什麼問題？

它要讓你看到這個錯誤不是理論：

```c
counter++;
```

看起來是一行，其實是 read-modify-write：

```text
1. 讀 counter
2. 加一
3. 寫回 counter
```

如果兩條路徑同時做：

```text
thread A 讀到 100
thread B 讀到 100
thread A 寫回 101
thread B 寫回 101
```

你原本期待加 2，最後只加 1。這就是 lost update。

Lab04 用三種來源同時碰 `dl_counter`：

- userspace single-thread `inc <count>`
- userspace multi-thread `race <threads> <loops>`
- kernel background kthread

## 它怎麼被 build / load / 呼叫？

Build：

```sh
cd labs/04-locking-and-races
make
```

Build CLI：

```sh
cc -Wall -Wextra -Werror -pthread \
  -o ../../tests/driver_lab_race_cli \
  ../../tests/driver_lab_race_cli.c
```

Load：

```sh
sudo insmod ./driver_lab_race.ko
```

呼叫：

```sh
../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 0
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50

../../tests/driver_lab_race_cli /dev/driver_lab_race0 safe-mode 1
../../tests/driver_lab_race_cli /dev/driver_lab_race0 reset
../../tests/driver_lab_race_cli /dev/driver_lab_race0 race 8 50
```

Unload：

```sh
sudo rmmod driver_lab_race
```

## 讀 source 的主線

第一次請照這個順序讀：

1. `dl_counter`、`dl_safe_mode`、`dl_worker_running`：先找共享 state。
2. `dl_race_increment_unlocked()`：看故意做壞的 read-modify-write。
3. `dl_race_increment_locked()`：看 mutex 保護的最小修正版。
4. `dl_race_increment()`：看 safe/unsafe 模式切換點。
5. `dl_race_worker_fn()`：看 kernel thread 如何也碰 shared state。
6. `dl_race_ioctl()`：看 CLI command 如何進 driver。
7. `driver_lab_race_init()` / `driver_lab_race_exit()`：看 char device resource 與 worker lifetime。

## 一、include 與常數

原始碼：

```c
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "driver_lab_race_uapi.h"
```

這些 header 對應到：

| Header | 本檔用到的概念 |
|---|---|
| `<linux/cdev.h>` / `<linux/device.h>` / `<linux/fs.h>` | char device、class/device、`file_operations` |
| `<linux/delay.h>` | `usleep_range()`、`msleep()`，刻意放大 race window |
| `<linux/kthread.h>` | `kthread_run()`、`kthread_should_stop()`、`kthread_stop()` |
| `<linux/mutex.h>` | `DEFINE_MUTEX()`、`mutex_lock()`、`mutex_unlock()` |
| `<linux/uaccess.h>` | `copy_from_user()`、`copy_to_user()` |
| `driver_lab_race_uapi.h` | ioctl command number 與 `struct dl_race_status` |

常數：

```c
#define DL_RACE_CLASS_NAME "driver_lab_race"
#define DL_RACE_DEVICE_NAME "driver_lab_race0"
#define DL_STATUS_BUFFER_BYTES 128
```

`DL_RACE_CLASS_NAME` 會用在 char device registration、class name 與 `/proc/devices` 識別；`DL_RACE_DEVICE_NAME` 通常對應 `/dev/driver_lab_race0`。

## 二、device resource 與 worker pointer

原始碼：

```c
static dev_t dl_race_devt;
static struct cdev dl_race_cdev;
static struct class *dl_race_class;
static struct device *dl_race_device;
static DEFINE_MUTEX(dl_race_lock);
static struct task_struct *dl_race_worker;
```

這裡混合兩類 resource：

| 變數 | 類型 | 誰建立 | 誰釋放 |
|---|---|---|---|
| `dl_race_devt` | major/minor | `alloc_chrdev_region()` | `unregister_chrdev_region()` |
| `dl_race_cdev` | char device object | `cdev_init()` / `cdev_add()` | `cdev_del()` |
| `dl_race_class` | device class | `class_create()` | `class_destroy()` |
| `dl_race_device` | device object | `device_create()` | `device_destroy()` |
| `dl_race_worker` | background kthread | `kthread_run()` | `kthread_stop()` |
| `dl_race_lock` | shared-state mutex | static initializer | module lifetime |

Lab04 比 Lab02 多出的關鍵是 `dl_race_worker`。卸載時要先停 worker，再拆 device resource，否則背景 thread 可能繼續碰已經進入 teardown 的 state。

## 三、shared state：先標出誰會被共享

原始碼：

```c
static unsigned int dl_counter;
static bool dl_safe_mode;
static bool dl_worker_running;
```

第一輪先畫表：

| State | 誰會讀 | 誰會寫 | 為什麼重要 |
|---|---|---|---|
| `dl_counter` | `read()`、`GET_STATUS`、CLI race 結果 | userspace `INC_COUNTER`、background worker、`RESET_COUNTER` | race 主角 |
| `dl_safe_mode` | increment path、worker、status | `SET_SAFE_MODE` | 決定走 unsafe 還是 safe |
| `dl_worker_running` | read/status | init/exit | 觀察 worker lifetime |

這個表比先背 API 更重要。你要先知道哪些 state 會被多條 execution path 碰到。

## 四、safe increment：mutex 保護 critical section

原始碼：

```c
static void dl_race_increment_locked(void)
{
	dl_counter++;
}
```

這個 function 名字裡的 `locked` 很重要：它假設 caller 已經拿到 `dl_race_lock`。

實際 safe path：

```c
mutex_lock(&dl_race_lock);
dl_race_increment_locked();
mutex_unlock(&dl_race_lock);
```

白話：

```text
同一時間只讓一條路徑進來改 counter
```

`mutex` 是 sleeping lock，只能在可睡眠的 task context 使用。Lab04 的 ioctl path 和 kthread path 都不是 IRQ handler，所以用 mutex 當第一版同步工具是合理的。

## 五、unsafe increment：故意拆開 read-modify-write

原始碼：

```c
static void dl_race_increment_unlocked(void)
{
	unsigned int snapshot;

	snapshot = dl_counter;
	usleep_range(1000, 2000);
	dl_counter = snapshot + 1;
}
```

這段是 Lab04 的教學核心。

它故意做三件事：

1. 讀 `dl_counter` 到 `snapshot`。
2. `usleep_range(1000, 2000)` 放大 race window。
3. 寫回 `snapshot + 1`。

如果多條 userspace thread 同時呼叫 `DL_RACE_IOC_INC_COUNTER`，就容易發生：

```text
A: snapshot = 10
B: snapshot = 10
A: dl_counter = 11
B: dl_counter = 11
```

這不是「加太慢」，而是「更新被覆蓋」。

## 六、`dl_race_increment()`：safe/unsafe 切換點

原始碼：

```c
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
```

`READ_ONCE(dl_safe_mode)` 的目的不是讓 `dl_counter++` 變成 atomic，也不是取代 lock。它只是在讀這個 mode flag 時，避免 compiler 對這個單一讀取做不適合 concurrent code 的最佳化。

真正保護 counter 的是：

```c
mutex_lock(&dl_race_lock);
...
mutex_unlock(&dl_race_lock);
```

請不要把 `READ_ONCE()` 誤解成 lock。

## 七、background kthread：driver 內部也會碰 state

原始碼：

```c
static int dl_race_worker_fn(void *unused)
{
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
```

這個 worker 每 20ms 也會 increment counter。

它的教學目的：

- race 不只來自 userspace threads。
- driver 內部背景工作也可能碰同一份 state。
- `expected_at_least` 不能期待等於精確值，因為 worker 會額外加 counter。

`kthread_should_stop()` 會在 `kthread_stop()` 被呼叫後讓 worker 知道該退出。這也是 exit path 必須先停 worker 的原因。

## 八、read path：給人看的文字狀態

原始碼：

```c
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
```

`read()` 回傳的是人類可讀文字：

```text
counter=123 safe_mode=1 worker_running=1
```

它和 ioctl `GET_STATUS` 讀的是同一份 state，所以也用同一把 mutex。

注意：`read()` 這裡不是 race 實驗的主路徑；CLI 主要透過 ioctl 取得結構化 status。`read()` 比較像方便你手動 `cat /dev/driver_lab_race0` 的觀測面。

## 九、ioctl：CLI control path

原始碼主線：

```c
static long dl_race_ioctl(struct file *file, unsigned int cmd,
						  unsigned long arg)
{
	struct dl_race_status status;
	unsigned int safe_mode;

	switch (cmd) {
	case DL_RACE_IOC_SET_SAFE_MODE:
		...
	case DL_RACE_IOC_GET_STATUS:
		...
	case DL_RACE_IOC_INC_COUNTER:
		...
	case DL_RACE_IOC_RESET_COUNTER:
		...
	default:
		return -ENOTTY;
	}

	return 0;
}
```

### `DL_RACE_IOC_SET_SAFE_MODE`

原始碼：

```c
if (copy_from_user(&safe_mode, (void __user *)arg, sizeof(safe_mode)))
	return -EFAULT;

mutex_lock(&dl_race_lock);
WRITE_ONCE(dl_safe_mode, safe_mode ? true : false);
mutex_unlock(&dl_race_lock);
```

方向：

```text
userspace arg -> kernel safe_mode
```

`WRITE_ONCE()` 和 `READ_ONCE()` 配對，避免對 mode flag 的單次讀寫被 compiler 做不適合 concurrent code 的轉換。它仍然不取代 mutex。

### `DL_RACE_IOC_GET_STATUS`

原始碼：

```c
mutex_lock(&dl_race_lock);
status.counter = dl_counter;
status.safe_mode = dl_safe_mode ? 1U : 0U;
status.worker_running = dl_worker_running ? 1U : 0U;
mutex_unlock(&dl_race_lock);

if (copy_to_user((void __user *)arg, &status, sizeof(status)))
	return -EFAULT;
```

方向：

```text
kernel status -> userspace arg
```

這是結構化 ABI，CLI 會把它印成：

```text
counter=... safe_mode=... worker_running=...
```

### `DL_RACE_IOC_INC_COUNTER`

原始碼：

```c
dl_race_increment();
```

這是 race experiment 的主角。CLI 的 `race 8 50` 會讓 8 條 pthread 各送 50 次這個 ioctl。

### `DL_RACE_IOC_RESET_COUNTER`

原始碼：

```c
mutex_lock(&dl_race_lock);
dl_counter = 0;
mutex_unlock(&dl_race_lock);
```

每次實驗前 reset，避免上一輪數值干擾觀察。

### default：`-ENOTTY`

未知 ioctl command 回 `-ENOTTY`，這是 Linux driver 常見做法，表示這個 fd 不支援該 ioctl。

## 十、file_operations

原始碼：

```c
static const struct file_operations dl_race_fops = {
	.owner = THIS_MODULE,
	.open = dl_race_open,
	.release = dl_race_release,
	.read = dl_race_read,
	.unlocked_ioctl = dl_race_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.llseek = noop_llseek,
};
```

對照 userspace：

| userspace 操作 | VFS 欄位 | driver function |
|---|---|---|
| `open("/dev/driver_lab_race0", O_RDWR)` | `.open` | `dl_race_open()` |
| `read(fd, ...)` / `cat` | `.read` | `dl_race_read()` |
| `ioctl(fd, DL_RACE_IOC_*)` | `.unlocked_ioctl` | `dl_race_ioctl()` |
| 32-bit userspace `ioctl()` | `.compat_ioctl` | `compat_ptr_ioctl()` 轉送到 `dl_race_ioctl()` |
| `close(fd)` | `.release` | `dl_race_release()` |

Lab04 沒有 `.write`。所有控制都走 ioctl；fixed-width、pointer-free payload
可由通用 compat helper 安全轉送，但仍需 32-on-64 runtime regression。

## 十一、init：建立 char device，再啟動 worker

原始碼摘要：

```c
ret = alloc_chrdev_region(&dl_race_devt, 0, 1, DL_RACE_CLASS_NAME);
cdev_init(&dl_race_cdev, &dl_race_fops);
ret = cdev_add(&dl_race_cdev, dl_race_devt, 1);
dl_race_class = class_create(DL_RACE_CLASS_NAME);
dl_race_device = device_create(dl_race_class, NULL, dl_race_devt, NULL,
							   DL_RACE_DEVICE_NAME);
dl_race_worker = kthread_run(dl_race_worker_fn, NULL, "dl_race_worker");
```

順序很重要：

```text
先建立 /dev userspace 入口
再啟動 background worker
最後初始化 counter / mode / worker_running
```

`kthread_run()` 回傳的是 `struct task_struct *` 或 error pointer，所以用 `IS_ERR()` / `PTR_ERR()` 判斷。

init 失敗時，error labels 會反向清理已取得的 resource：

```text
device_create() 成功後 worker 啟動失敗
  -> device_destroy()
  -> class_destroy()
  -> cdev_del()
  -> unregister_chrdev_region()
```

## 十二、exit：先停 worker，再拆 device

原始碼：

```c
static void __exit driver_lab_race_exit(void)
{
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
```

第一個重點：

```text
先停 background worker，再拆 device/class/cdev
```

如果反過來，worker 可能還在跑，卻開始碰即將被拆掉或狀態已不一致的 resource。

第二個重點：`dl_worker_running = false` 是給 status/read 觀測用；真正讓 worker 停下來的是 `kthread_stop()` 搭配 worker loop 裡的 `kthread_should_stop()`。

## source、CLI、test 對照

| CLI/test 操作 | ioctl command | driver path |
|---|---|---|
| `safe-mode 0/1` | `DL_RACE_IOC_SET_SAFE_MODE` | `copy_from_user()` -> set `dl_safe_mode` |
| `status` | `DL_RACE_IOC_GET_STATUS` | lock -> fill `struct dl_race_status` -> `copy_to_user()` |
| `reset` | `DL_RACE_IOC_RESET_COUNTER` | lock -> `dl_counter = 0` |
| `inc <count>` | `DL_RACE_IOC_INC_COUNTER` repeatedly | `dl_race_increment()` |
| `race <threads> <loops>` | `DL_RACE_IOC_INC_COUNTER` from many pthreads | unsafe/safe 對照 |
| `rmmod driver_lab_race` | module exit | stop worker -> destroy device/class/cdev/devt |

## 常見卡點

- `READ_ONCE()` / `WRITE_ONCE()` 不是 lock；本 lab 真正修 lost update 的是 mutex。
- unsafe mode 結果不一定每次都壞到同樣程度；race 本來就跟 timing 有關。
- safe mode 的 `observed` 不一定剛好等於 `threads * loops`，因為 background worker 也會加 counter。
- `mutex` 可以睡眠，不能拿去套在 IRQ handler；Lab04 還不是 IRQ path。
- `kthread_stop()` 必須能讓 worker 看到 `kthread_should_stop()` 後退出。
- `copy_from_user()` / `copy_to_user()` 的方向要分清楚：`SET_SAFE_MODE` 是 user -> kernel，`GET_STATUS` 是 kernel -> user。
- ioctl command number 是 ABI；改 `struct dl_race_status` 欄位順序或型別會影響 userspace CLI。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab04 的主要 shared state 是什麼？ | `dl_counter`、`dl_safe_mode`、`dl_worker_running`。 |
| 哪個 function 故意示範 lost update？ | `dl_race_increment_unlocked()`。 |
| safe mode 怎麼修？ | `dl_race_increment()` 在 `dl_safe_mode` 為 true 時用 `dl_race_lock` 包住 `dl_race_increment_locked()`。 |
| background worker 為什麼會讓結果不是精確值？ | worker 也會定期 increment `dl_counter`。 |
| CLI 的 `race 8 50` 最終打哪個 ioctl？ | 多條 pthread 反覆呼叫 `DL_RACE_IOC_INC_COUNTER`。 |
| `GET_STATUS` 如何回傳資料？ | driver 在 lock 內填好 `struct dl_race_status`，再用 `copy_to_user()` 複製回 userspace。 |
| 卸載時為什麼先停 worker？ | 避免 background thread 在 teardown 期間繼續碰共享 state 或 resource。 |

## 查證來源

- Linux kernel documentation `Lock types and their rules`：mutex 屬於 sleeping lock，使用 context 要注意。<https://docs.kernel.org/locking/locktypes.html>
- Linux kernel documentation `Generic Mutex Subsystem`：mutex 是 kernel 中用來序列化 shared memory access 的 locking primitive。<https://docs.kernel.org/locking/mutex-design.html>
- Linux kernel documentation `Driver Basics`：`kthread_run()`、`kthread_should_stop()`、`kthread_stop()` 與 module init/exit。<https://docs.kernel.org/driver-api/basics.html>
- Linux kernel documentation `ioctl based interfaces` 與 `Ioctl Numbers`：`_IO` / `_IOR` / `_IOW` command 定義規則。<https://docs.kernel.org/driver-api/ioctl.html>、<https://docs.kernel.org/userspace-api/ioctl/ioctl-number.html>
- Linux kernel documentation `Linux kernel memory barriers`：`READ_ONCE()` / `WRITE_ONCE()` 是針對單次存取的 wrapper，不等同 lock。<https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
