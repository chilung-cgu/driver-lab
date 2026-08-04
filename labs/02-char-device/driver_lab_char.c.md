# `driver_lab_char.c` 詳解

## 結論

`labs/02-char-device/driver_lab_char.c` 是 driver-lab 第一個真正建立 `/dev` 入口的 kernel module。Lab00 只練 load/unload；Lab01 用 debugfs 做觀測；Lab02 開始讓 userspace 對一個 device node 做一般檔案操作：

```text
/dev/driver_lab_char0
  open()   -> dl_char_open()
  read()   -> dl_char_read()
  write()  -> dl_char_write()
  close()  -> dl_char_release()
```

這份 driver 的核心是兩條主線：

1. module init 建立 char device resource，讓 `/dev/driver_lab_char0` 出現。
2. userspace `read()` / `write()` 經過 VFS 轉呼叫本檔案的 `file_operations` callback。

你讀懂這份檔案後，後面的 Lab03 `ioctl/poll/mmap` 才會比較自然，因為 Lab03 只是把同一個 `/dev` 入口擴充成更多 syscall path。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- 本檔 source、[`README.md`](README.md)、[`test.sh`](test.sh)、[`Makefile`](Makefile)。
- repo 內的 [`../../docs/onboarding/01-to-03-user-kernel-abi-bridge.md`](../../docs/onboarding/START-HERE.md) 與 [`../../docs/onboarding/kernel-filesystem-surfaces.md`](../../docs/onboarding/kernel-interfaces.md)。
- Linux kernel documentation：device driver infrastructure、kernel API helpers、sysfs、allocated devices。
- Linux man-pages：`open(2)`、`read(2)`、`write(2)`。

這裡不展開 VFS 如何管理 `struct inode` / `struct file` 的完整 lifetime，也不把 udev/devtmpfs 規則當成 Lab02 的主題。文件只根據本 repo 的 source 與 Linux 官方文件解釋這支教學 driver。

## 先理解這份檔案在 repo 的位置

Lab02 是 `01 -> 03` 之間的橋：

```text
Lab01:
  debugfs entry
  /sys/kernel/debug/driver_lab_debugfs/*

Lab02:
  char device
  /dev/driver_lab_char0

Lab03:
  char device + ioctl + poll + mmap
  /dev/driver_lab_ctl0
```

相關檔案：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_char.c`](driver_lab_char.c) | kernel char device driver 本體 |
| [`Makefile.md`](Makefile.md) | Lab02 external module kbuild 入口 |
| [`test.sh.md`](test.sh.md) | Lab02 smoke test，驗 `/dev`、sysfs、read/write、cleanup |
| [`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md) | userspace CLI，後續可拿來手動打 `/dev/driver_lab_char0` |

## 這份檔案要解決什麼問題？

userspace 不能直接呼叫 kernel function。driver 若要提供正式資料通道，通常會暴露某種 kernel-managed interface。Lab02 選擇最小 char device：

```text
userspace process
  write(fd, "hello", 5)
        |
        v
VFS 根據 /dev node 的 major/minor 找到 cdev
        |
        v
dl_char_write()
        |
        v
driver 的 kernel buffer
```

反方向：

```text
driver 的 kernel buffer
        |
        v
dl_char_read()
        |
        v
read(fd, user_buffer, count)
```

這一關刻意把資料模型做得很簡單：

- 只有一個 global buffer。
- 每次 `write()` 都覆蓋整個 buffer，不做 append。
- `read()` 使用檔案式 offset，所以同一個 fd 讀到尾端後，再讀會像普通檔案一樣回傳 `0`。
- 用 mutex 保護 shared buffer，先建立「共享狀態要上鎖」的習慣。

## 它怎麼被 build / load / 呼叫？

Build：

```sh
cd labs/02-char-device
make
```

產物：

```text
driver_lab_char.ko
```

Load：

```sh
sudo insmod ./driver_lab_char.ko
```

init 成功後，通常會看到：

```text
/dev/driver_lab_char0
/sys/class/driver_lab_char/driver_lab_char0
/proc/devices 裡的 driver_lab_char
```

呼叫：

```sh
printf '%s' 'hello-char-device' | sudo tee /dev/driver_lab_char0 >/dev/null
sudo dd if=/dev/driver_lab_char0 bs=1 count=17 status=none
```

Unload：

```sh
sudo rmmod driver_lab_char
```

## 讀 source 的主線

第一次請照這個順序讀：

1. 全域 resource：`dl_char_devt`、`dl_char_cdev`、`dl_char_class`、`dl_char_device`。
2. 全域資料：`dl_char_buffer`、`dl_char_buffer_len`、`dl_char_lock`。
3. `dl_char_fops`：確認 `/dev` 操作會接到哪些 callback。
4. `driver_lab_char_init()`：看 device number、cdev、class、device 怎麼建立。
5. `dl_char_write()`：看 userspace payload 怎麼進 kernel buffer。
6. `dl_char_read()`：看 kernel buffer 怎麼回 userspace。
7. `driver_lab_char_exit()`：確認 cleanup 是否和 init 對稱。

## 一、include 與常數

原始碼：

```c
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define DL_CHAR_CLASS_NAME "driver_lab_char"
#define DL_CHAR_DEVICE_NAME "driver_lab_char0"
#define DL_CHAR_BUFFER_SIZE 256
```

這幾個 header 對應到本檔案的概念：

| Header | 本檔用到的概念 |
|---|---|
| `<linux/cdev.h>` | `struct cdev`、`cdev_init()`、`cdev_add()`、`cdev_del()` |
| `<linux/device.h>` | `struct class`、`struct device`、`class_create()`、`device_create()` |
| `<linux/fs.h>` | `struct file_operations`、`simple_read_from_buffer()`、`simple_write_to_buffer()`、`noop_llseek` |
| `<linux/mutex.h>` | `DEFINE_MUTEX()`、`mutex_lock_interruptible()`、`mutex_unlock()` |
| `<linux/uaccess.h>` | userspace pointer / user copy 相關語意；本檔透過 `simple_*_buffer()` helper 間接處理 |

常數：

| 常數 | 用途 |
|---|---|
| `DL_CHAR_CLASS_NAME` | class 名字，也會出現在 `/proc/devices` 與 `/sys/class/driver_lab_char`。 |
| `DL_CHAR_DEVICE_NAME` | device 名字，通常對應 `/dev/driver_lab_char0`。 |
| `DL_CHAR_BUFFER_SIZE` | 這支教學 driver 的 buffer 大小，固定 256 bytes。 |

## 二、四個 char device resource

原始碼：

```c
static dev_t dl_char_devt;
static struct cdev dl_char_cdev;
static struct class *dl_char_class;
static struct device *dl_char_device;
```

第一輪先把它們想成一條 resource pipeline：

| 變數 | 第一輪理解 | 誰建立 | 誰釋放 |
|---|---|---|---|
| `dl_char_devt` | major/minor device number | `alloc_chrdev_region()` | `unregister_chrdev_region()` |
| `dl_char_cdev` | VFS 找得到的 char device object | `cdev_init()` + `cdev_add()` | `cdev_del()` |
| `dl_char_class` | device model class | `class_create()` | `class_destroy()` |
| `dl_char_device` | 實際 device object / sysfs class entry | `device_create()` | `device_destroy()` |

白話：

```text
dev_t 是門牌號碼
cdev 是接待櫃台
class 是分類資料夾
device 是讓 userspace 看得到的裝置身分
```

## 三、shared buffer 與 mutex

原始碼：

```c
static DEFINE_MUTEX(dl_char_lock);
static char dl_char_buffer[DL_CHAR_BUFFER_SIZE];
static size_t dl_char_buffer_len;
```

這支 driver 的資料很單純：

- `dl_char_buffer` 保存最近一次 `write()` 進來的 payload。
- `dl_char_buffer_len` 記錄目前有效長度。
- `dl_char_lock` 保護這兩個 shared state。

為什麼要 lock？

因為 `/dev/driver_lab_char0` 可能被多個 process 同時開啟。即使這只是教學範例，也要避免一邊 `write()` 改 buffer、另一邊 `read()` 同時讀 buffer，造成不一致。

## 四、open/release：目前只是觀測點

原始碼：

```c
static int dl_char_open(struct inode *inode, struct file *file)
{
	pr_info("device opened\n");
	return 0;
}

static int dl_char_release(struct inode *inode, struct file *file)
{
	pr_info("device released\n");
	return 0;
}
```

userspace：

```c
fd = open("/dev/driver_lab_char0", O_RDWR);
close(fd);
```

kernel path：

```text
open()  -> dl_char_open()
close() -> dl_char_release()
```

這一關沒有 per-open state，所以 `inode` 和 `file` 目前沒有被使用。它們仍然很重要：後面更真實的 driver 會把 per-file context 放在 `file->private_data`。

## 五、read：kernel buffer 複製回 userspace

原始碼：

```c
static ssize_t dl_char_read(struct file *file, char __user *buf,
							size_t count, loff_t *ppos)
{
	ssize_t ret;

	if (mutex_lock_interruptible(&dl_char_lock))
		return -ERESTARTSYS;

	ret = simple_read_from_buffer(buf, count, ppos,
								  dl_char_buffer, dl_char_buffer_len);
	if (ret > 0)
		pr_info("read %zd bytes\n", ret);

	mutex_unlock(&dl_char_lock);
	return ret;
}
```

先看 callback signature：

| 參數 | 角色 |
|---|---|
| `struct file *file` | VFS 的 opened file context；本 lab 沒用它保存 private state。 |
| `char __user *buf` | userspace 提供的目的地 buffer。不能直接當 kernel pointer 解參考。 |
| `size_t count` | userspace 希望最多讀幾個 bytes。 |
| `loff_t *ppos` | 目前檔案 offset。`simple_read_from_buffer()` 會更新它。 |

`simple_read_from_buffer()` 的方向是：

```text
dl_char_buffer -> userspace buf
```

為什麼要 `ppos`？

因為這支 driver 的 read 行為刻意接近普通檔案。例如 buffer 裡是 `hello`：

```text
第一次 read count=2 -> 回 "he"，ppos 變 2
第二次 read count=2 -> 回 "ll"，ppos 變 4
第三次 read count=2 -> 回 "o"， ppos 變 5
第四次 read      -> EOF，回 0
```

這就是為什麼 test 用 `dd count=${#MESSAGE}` 讀固定長度，而不是無限制 `cat`。

## 六、write：userspace payload 覆蓋 kernel buffer

原始碼：

```c
static ssize_t dl_char_write(struct file *file, const char __user *buf,
							 size_t count, loff_t *ppos)
{
	ssize_t ret;
	loff_t pos = 0;

	if (count == 0)
		return 0;

	if (count > DL_CHAR_BUFFER_SIZE - 1)
		return -EMSGSIZE;

	if (mutex_lock_interruptible(&dl_char_lock))
		return -ERESTARTSYS;

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
```

這段有幾個教學點。

### `count == 0`

```c
if (count == 0)
	return 0;
```

寫 0 bytes 就回 0，表示沒有寫入任何資料。

### buffer size guard

```c
if (count > DL_CHAR_BUFFER_SIZE - 1)
	return -EMSGSIZE;
```

buffer 是 256 bytes，但 driver 保留 1 byte 放 `'\0'`，所以最多接受 255 bytes。

如果 userspace 寫太長，直接回 `-EMSGSIZE`。這比默默截斷更適合教學，因為錯誤會明確暴露。

### `loff_t pos = 0`

這是本 lab 的刻意語意：

```text
每次 write 都從 kernel buffer 開頭寫
所以每次 write 都覆蓋前一次內容
```

它沒有使用 VFS 傳進來的 `*ppos` 來 append。這讓 readback 測試容易預測。

### `simple_write_to_buffer()`

方向是：

```text
userspace buf -> dl_char_buffer
```

`buf` 有 `__user` 標記，代表它是 userspace pointer。driver 不能直接做：

```c
dl_char_buffer[0] = buf[0];   /* 錯誤心智模型 */
```

應該透過 kernel user-copy helper 或包裝 helper。這裡使用 `simple_write_to_buffer()`，它替本 lab 處理基本複製與 offset 邏輯。

### `dl_char_buffer[ret] = '\0'`

這不是 char device 必然需要的規則，而是本 lab 為了讓 buffer 可被當成字串觀察，補上 NUL terminator。

`dl_char_buffer_len = ret` 才是 read path 使用的有效長度。

### `*ppos = 0`

寫完後把目前 file offset 重設為 0，讓同一個 file descriptor 如果接著 read，可以從 buffer 開頭讀。

## 七、file_operations：VFS callback 表

原始碼：

```c
static const struct file_operations dl_char_fops = {
	.owner = THIS_MODULE,
	.open = dl_char_open,
	.release = dl_char_release,
	.read = dl_char_read,
	.write = dl_char_write,
	.llseek = noop_llseek,
};
```

對照 userspace：

| userspace 操作 | VFS 找的欄位 | 本 driver function |
|---|---|---|
| `open("/dev/driver_lab_char0", ...)` | `.open` | `dl_char_open()` |
| `read(fd, buf, count)` | `.read` | `dl_char_read()` |
| `write(fd, buf, count)` | `.write` | `dl_char_write()` |
| `close(fd)` | `.release` | `dl_char_release()` |
| `lseek(fd, ...)` | `.llseek` | `noop_llseek` |

`.owner = THIS_MODULE` 的目的是讓 kernel 在 file operations 使用期間能處理 module reference，避免 module 在 callback 還可能被使用時被卸載。

## 八、init：建立 `/dev/driver_lab_char0`

原始碼主線：

```c
ret = alloc_chrdev_region(&dl_char_devt, 0, 1, DL_CHAR_CLASS_NAME);
...
cdev_init(&dl_char_cdev, &dl_char_fops);
dl_char_cdev.owner = THIS_MODULE;
...
ret = cdev_add(&dl_char_cdev, dl_char_devt, 1);
...
dl_char_class = class_create(DL_CHAR_CLASS_NAME);
...
dl_char_device = device_create(dl_char_class, NULL, dl_char_devt, NULL,
							   DL_CHAR_DEVICE_NAME);
```

把它讀成 pipeline：

```text
alloc_chrdev_region()
  取得 major/minor

cdev_init()
  把 cdev 和 dl_char_fops 接起來

cdev_add()
  讓 major/minor 真的對到這個 cdev

class_create()
  建立 /sys/class/driver_lab_char

device_create()
  建立 /sys/class/driver_lab_char/driver_lab_char0
  通常也讓 /dev/driver_lab_char0 出現
```

### `alloc_chrdev_region()`

原始碼：

```c
ret = alloc_chrdev_region(&dl_char_devt, 0, 1, DL_CHAR_CLASS_NAME);
if (ret)
	return ret;
```

參數角色：

| 參數 | 角色 |
|---|---|
| `&dl_char_devt` | output，kernel 填入 major/minor。 |
| `0` | 起始 minor。 |
| `1` | 申請一個 device number。 |
| `DL_CHAR_CLASS_NAME` | 名字，會輔助出現在 `/proc/devices`。 |

### `cdev_init()` / `cdev_add()`

原始碼：

```c
cdev_init(&dl_char_cdev, &dl_char_fops);
dl_char_cdev.owner = THIS_MODULE;

ret = cdev_add(&dl_char_cdev, dl_char_devt, 1);
if (ret)
	goto err_unregister_region;
```

`cdev_init()` 只是在 kernel object 內掛上 callback table；`cdev_add()` 成功後，這個 cdev 才成為 live char device。

所以 error path 也很精準：

```text
cdev_add() 失敗
  還沒有 live cdev
  只需要 unregister_chrdev_region()
```

### `class_create()` / `device_create()`

原始碼：

```c
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
```

這兩個 API 失敗時不是回傳 `NULL` 或負數，而是回傳 error pointer，所以要用：

```c
IS_ERR(ptr)
PTR_ERR(ptr)
```

`device_create()` 成功後，測試會檢查：

```text
/dev/driver_lab_char0
/sys/class/driver_lab_char/driver_lab_char0
/sys/class/driver_lab_char/driver_lab_char0/dev
/proc/devices 裡的 driver_lab_char
```

## 九、error labels：拿到什麼就反向釋放什麼

原始碼：

```c
err_destroy_class:
	class_destroy(dl_char_class);
err_del_cdev:
	cdev_del(&dl_char_cdev);
err_unregister_region:
	unregister_chrdev_region(dl_char_devt, 1);
	return ret;
```

這段是 kernel driver 很重要的習慣。請照「已成功拿到哪些 resource」來讀：

| 失敗點 | 已成功拿到 | cleanup label |
|---|---|---|
| `cdev_add()` 失敗 | `dev_t` | `err_unregister_region` |
| `class_create()` 失敗 | `dev_t`、live `cdev` | `err_del_cdev` -> `err_unregister_region` |
| `device_create()` 失敗 | `dev_t`、live `cdev`、class | `err_destroy_class` -> `err_del_cdev` -> `err_unregister_region` |

這比「每個失敗點都手寫完整 cleanup」更不容易漏。

## 十、exit：反向拆 resource

原始碼：

```c
static void __exit driver_lab_char_exit(void)
{
	device_destroy(dl_char_class, dl_char_devt);
	class_destroy(dl_char_class);
	cdev_del(&dl_char_cdev);
	unregister_chrdev_region(dl_char_devt, 1);
	pr_info("device removed\n");
}
```

對照 init：

| init | exit |
|---|---|
| `alloc_chrdev_region()` | `unregister_chrdev_region()` |
| `cdev_add()` | `cdev_del()` |
| `class_create()` | `class_destroy()` |
| `device_create()` | `device_destroy()` |

順序大致是反向：

```text
先移除 userspace/sysfs 看得到的 device
再移除 class
再移除 cdev
最後釋放 major/minor
```

test 也會驗證卸載後：

```text
/dev/driver_lab_char0 不存在
/sys/class/driver_lab_char/driver_lab_char0 不存在
```

## source、命令與 driver path 對照

| userspace 命令 | 主要 driver path | 觀察點 |
|---|---|---|
| `sudo insmod ./driver_lab_char.ko` | `driver_lab_char_init()` | `dmesg`、`/dev/driver_lab_char0`、`/sys/class/...` |
| `printf ... | sudo tee /dev/driver_lab_char0` | `dl_char_open()` -> `dl_char_write()` -> `dl_char_release()` | `dmesg` 的 `wrote ... bytes` |
| `sudo dd if=/dev/driver_lab_char0 ...` | `dl_char_open()` -> `dl_char_read()` -> `dl_char_release()` | readback file 內容 |
| `sudo rmmod driver_lab_char` | `driver_lab_char_exit()` | `/dev` 與 sysfs entry 消失 |

## 常見卡點

- `/dev/driver_lab_char0` 沒出現時，不要只盯 `/dev`；也要看 `/sys/class/driver_lab_char/driver_lab_char0` 和 `dmesg`。
- `cdev_add()` 不會直接建立 `/dev` node；它只讓 VFS 能用 major/minor 找到 callback。
- `device_create()` 建立 device model entry；現代系統通常由 devtmpfs 建 `/dev` node，udev 可能再調整權限。
- `__user *buf` 不能直接解參考；要透過 user-copy helper 或這裡的 `simple_*_buffer()`。
- 這支 driver 每次 `write()` 都覆蓋 buffer，不是 append。
- 寫超過 255 bytes 會回 `-EMSGSIZE`。
- 同一個 fd 連續 read 會受 `ppos` 影響；讀到 EOF 後回 `0` 是合理行為。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| `/dev/driver_lab_char0` 的 `.read` 接到哪個 function？ | `dl_char_fops.read = dl_char_read`。 |
| `.write` 接到哪個 function？ | `dl_char_fops.write = dl_char_write`。 |
| `alloc_chrdev_region()` 產生什麼 resource？ | 一組 major/minor device number，存在 `dl_char_devt`。 |
| `cdev_add()` 成功後代表什麼？ | major/minor 已對應到 `dl_char_cdev` 與 `dl_char_fops`，VFS 可以分派 callback。 |
| `device_create()` 成功後通常會看到哪些路徑？ | `/sys/class/driver_lab_char/driver_lab_char0`，以及通常由 devtmpfs 建出的 `/dev/driver_lab_char0`。 |
| 為什麼 read/write 要 mutex？ | 因為它們共享同一份 global buffer 與 length。 |
| 為什麼 `write()` 裡用 `loff_t pos = 0`？ | 讓每次 write 都從 buffer 開頭覆蓋，而不是依 `*ppos` append。 |
| 卸載時 cleanup 順序是什麼？ | `device_destroy()`、`class_destroy()`、`cdev_del()`、`unregister_chrdev_region()`。 |

## 查證來源

- Linux kernel documentation `Device drivers infrastructure`：`class_create()`、`device_create()`、device/class model 背景。<https://docs.kernel.org/driver-api/infrastructure.html>
- Linux kernel documentation `Kernel API`：`simple_read_from_buffer()`、`simple_write_to_buffer()`、`kstrto*` 等常用 helper 的 API 參考入口。<https://docs.kernel.org/core-api/kernel-api.html>
- Linux kernel documentation `sysfs`：`/sys` 是 kernel object/device model 的 userspace view。<https://docs.kernel.org/filesystems/sysfs.html>
- Linux kernel documentation `Linux allocated devices`：major/minor device number 背景。<https://docs.kernel.org/admin-guide/devices.html>
- Linux man-pages `open(2)`、`read(2)`、`write(2)`：userspace file descriptor 與 read/write syscall 語意。<https://man7.org/linux/man-pages/man2/open.2.html>、<https://man7.org/linux/man-pages/man2/read.2.html>、<https://man7.org/linux/man-pages/man2/write.2.html>
