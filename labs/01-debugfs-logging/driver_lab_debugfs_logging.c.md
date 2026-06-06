# `driver_lab_debugfs_logging.c` 詳解

## 結論

`labs/01-debugfs-logging/driver_lab_debugfs_logging.c` 是 driver-lab 的第一個「可觀測狀態」kernel module。Lab00 只會在 `dmesg` 印 log；Lab01 開始把 kernel 內部 state 導出到 debugfs：

```text
/sys/kernel/debug/driver_lab_debugfs/
  status         -> 讀整體狀態
  trigger        -> 寫入 payload，觸發 driver 行為
  trigger_count  -> 直接讀 counter
  emit_debug     -> 控制是否呼叫 pr_debug()
```

這一關的核心不是「debugfs 很神秘」，而是：

> driver 需要可觀測性。log 是一種觀測，debugfs 則能把目前 state 變成 userspace 可讀/可寫的 debug interface。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- 本檔 source、[`README.md`](README.md)、[`test.sh`](test.sh)、[`Makefile`](Makefile)。
- Linux kernel documentation：debugfs、seq_file、dynamic debug、printk。
- 本 repo 的 [`../../scripts/mount-debugfs.sh`](../../scripts/mount-debugfs.sh) 與 [`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。

這裡不展開 VFS 的完整 `struct inode` / `struct file` lifetime，也不把 debugfs 當產品 ABI 設計範例。debugfs 官方文件明確指出它不是穩定 userspace ABI。

## 先理解這份檔案在 repo 的位置

Lab01 站在 Lab00 後面：

```text
Lab00:
  module_init/module_exit + dmesg

Lab01:
  module_init/module_exit + dmesg + debugfs files + dynamic debug
```

相關檔案：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_debugfs_logging.c`](driver_lab_debugfs_logging.c) | kernel module 本體 |
| [`Makefile.md`](Makefile.md) | kbuild external module 入口 |
| [`test.sh.md`](test.sh.md) | debugfs smoke test |
| [`../../scripts/mount-debugfs.sh`](../../scripts/mount-debugfs.sh) | 確保 debugfs 掛載 |

## 這份檔案要解決什麼問題？

只靠 `pr_info()` 有兩個問題：

- 只能看事件發生時印出的 log，不容易查「目前狀態」。
- debug log 如果一直開著，會太吵；如果完全關掉，又不容易查問題。

Lab01 用三種方式補足：

| 機制 | 用途 |
|---|---|
| `status` debugfs file | 把目前 state 整理成文字 |
| `trigger` debugfs file | 讓 userspace 寫入 payload 觸發 driver path |
| dynamic debug + `pr_debug()` | 需要時才打開較細 debug log |

## 讀 source 的主線

第一次請照這個順序讀：

1. 全域 state：`dl_trigger_count`、`dl_emit_debug`、`dl_last_message`。
2. `dl_status_show()`：`cat status` 時輸出什麼。
3. `dl_trigger_write()`：`tee > trigger` 後 state 怎麼改。
4. `dl_status_fops` / `dl_trigger_fops`：debugfs file 怎麼接到 callback。
5. `driver_lab_debugfs_logging_init()`：建立 debugfs 目錄與檔案。
6. `driver_lab_debugfs_logging_exit()`：卸載時移除 debugfs。

## 一、include 與常數

原始碼：

```c
#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define DL_DEBUGFS_DIR_NAME "driver_lab_debugfs"
#define DL_LAST_MESSAGE_LEN 64
```

重點：

| include | 用途 |
|---|---|
| `<linux/debugfs.h>` | `debugfs_create_dir()` / `debugfs_create_file()` / `debugfs_create_u32()` / `debugfs_remove()` |
| `<linux/mutex.h>` | 保護 shared state |
| `<linux/seq_file.h>` | 產生 `cat status` 的文字輸出 |
| `<linux/string.h>` | `strim()` / `strscpy()` |
| `<linux/uaccess.h>` | `copy_from_user()` |

`DL_LAST_MESSAGE_LEN` 是 Lab01 保留最後一次 trigger payload 的固定長度，目前是 64 bytes。

## 二、debugfs root 與 shared state

原始碼：

```c
static struct dentry *dl_root;
static DEFINE_MUTEX(dl_state_lock);

static u32 dl_trigger_count;
static u32 dl_emit_debug = 1;
static char dl_last_message[DL_LAST_MESSAGE_LEN] = "not-triggered-yet";
```

逐一看：

| state | 意義 |
|---|---|
| `dl_root` | debugfs 目錄 `/sys/kernel/debug/driver_lab_debugfs` 的 dentry |
| `dl_state_lock` | 保護 counter、debug flag、last message |
| `dl_trigger_count` | trigger 被寫入幾次 |
| `dl_emit_debug` | 是否呼叫 `pr_debug()` |
| `dl_last_message` | 最近一次 trigger payload |

白話講：

```text
debugfs file 只是入口
真正的 driver state 是這幾個 static variable
```

## 三、`dl_status_show()`：把 kernel state 轉成文字

原始碼：

```c
static int dl_status_show(struct seq_file *m, void *unused)
{
	mutex_lock(&dl_state_lock);
	seq_printf(m, "trigger_count=%u\n", dl_trigger_count);
	seq_printf(m, "emit_debug=%u\n", dl_emit_debug);
	seq_printf(m, "last_message=%s\n", dl_last_message);
	mutex_unlock(&dl_state_lock);

	return 0;
}
```

這是 `cat /sys/kernel/debug/driver_lab_debugfs/status` 最後會呼叫的輸出 function。

`seq_printf()` 不是直接印到 terminal。它把文字寫到 `seq_file` 這個 kernel 輸出 helper，最後由 read path 交給 userspace。

白話講：

```text
cat status
  -> kernel 走 seq_file read path
  -> dl_status_show()
  -> seq_printf() 產生文字
  -> userspace 看到 trigger_count/emit_debug/last_message
```

為什麼要 lock？

因為 `dl_trigger_write()` 可能同時更新這些 state。讀取時拿 lock，避免看到一半新、一半舊的狀態。

## 四、`dl_status_open()`：把 status read path 接到 show callback

原始碼：

```c
static int dl_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, dl_status_show, inode->i_private);
}
```

`single_open()` 是 `seq_file` 的簡化用法，適合這種「一次產生一小段文字」的 debugfs file。

這裡最重要的是第二個參數：

```c
dl_status_show
```

它告訴 `seq_file`：真正要產生內容時，請呼叫 `dl_status_show()`。

官方 seq_file 文件也提醒，用 `single_open()` 時，file operations 的 release 應搭配 `single_release()`，本檔確實這樣做。

## 五、`dl_trigger_write()`：userspace 寫 trigger 後更新 state

原始碼主線：

```c
copy_len = min(count, sizeof(local) - 1);
if (copy_from_user(local, buf, copy_len))
	return -EFAULT;

local[copy_len] = '\0';
trimmed = strim(local);

if (mutex_lock_interruptible(&dl_state_lock))
	return -ERESTARTSYS;

strscpy(dl_last_message, trimmed[0] ? trimmed : "(empty)",
		sizeof(dl_last_message));
dl_trigger_count++;

pr_info("trigger #%u payload=\"%s\"\n",
		dl_trigger_count, dl_last_message);

if (dl_emit_debug)
	pr_debug("dynamic-debug path count=%u len=%zu\n",
			 dl_trigger_count, strlen(dl_last_message));

mutex_unlock(&dl_state_lock);
```

這條路徑從 userspace 開始：

```sh
printf '%s' 'smoke-one' | sudo tee /sys/kernel/debug/driver_lab_debugfs/trigger
```

進入 kernel 後：

1. 限制 payload 長度最多 63 bytes。
2. `copy_from_user()` 把 userspace bytes 複製到 kernel stack buffer。
3. 自己補 NUL terminator。
4. `strim()` 去掉前後空白/換行。
5. 拿 lock。
6. 更新 `dl_last_message` 與 `dl_trigger_count`。
7. 印 `pr_info()`。
8. 如果 `dl_emit_debug != 0`，呼叫 `pr_debug()`。
9. unlock，回傳原始 `count`。

白話講：

```text
trigger 是一個「測試按鈕」
寫入 payload 會更新 driver state
並留下 dmesg / dynamic debug 可觀測訊號
```

常見誤解：

- userspace 寫進來的是 bytes，不保證是 C string。
- `copy_from_user()` 不是普通 `memcpy()`。
- `ret = count` 代表這次 write 對 userspace 宣告已消費原本寫入長度，即使內部只保留截斷後的最後訊息。

## 六、`dl_status_fops`：status file 的 callback table

原始碼：

```c
static const struct file_operations dl_status_fops = {
	.owner = THIS_MODULE,
	.open = dl_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
```

這張表告訴 VFS：

| 操作 | callback |
|---|---|
| open status | `dl_status_open()` |
| read status | `seq_read()` |
| seek status | `seq_lseek()` |
| close status | `single_release()` |

所以 `cat status` 的實際路徑是：

```text
open status
  -> dl_status_open()
  -> single_open(..., dl_status_show, ...)
read status
  -> seq_read()
  -> dl_status_show()
close status
  -> single_release()
```

## 七、`dl_trigger_fops`：trigger file 的 callback table

原始碼：

```c
static const struct file_operations dl_trigger_fops = {
	.owner = THIS_MODULE,
	.write = dl_trigger_write,
	.llseek = noop_llseek,
};
```

這張表只提供 write path。因為 `trigger` 是測試觸發入口，不是狀態輸出檔。

白話講：

```text
status 用來讀
trigger 用來寫
兩個 debugfs file 對應不同 fops
```

## 八、init：建立 debugfs 目錄與檔案

原始碼主線：

```c
dl_root = debugfs_create_dir(DL_DEBUGFS_DIR_NAME, NULL);
...
entry = debugfs_create_file("status", 0444, dl_root, NULL, &dl_status_fops);
...
entry = debugfs_create_file("trigger", 0200, dl_root, NULL, &dl_trigger_fops);
...
debugfs_create_u32("trigger_count", 0444, dl_root, &dl_trigger_count);
debugfs_create_u32("emit_debug", 0644, dl_root, &dl_emit_debug);
```

載入 module 後會建立：

```text
/sys/kernel/debug/driver_lab_debugfs/status
/sys/kernel/debug/driver_lab_debugfs/trigger
/sys/kernel/debug/driver_lab_debugfs/trigger_count
/sys/kernel/debug/driver_lab_debugfs/emit_debug
```

權限：

| file | mode | 意義 |
|---|---|---|
| `status` | `0444` | 可讀 |
| `trigger` | `0200` | owner 可寫 |
| `trigger_count` | `0444` | 可讀 |
| `emit_debug` | `0644` | owner 可寫，其他可讀 |

`debugfs_create_u32()` 是 debugfs 提供的簡化 helper，可以直接把 `u32` kernel variable 導出成 debugfs file。官方 debugfs 文件也說明這類 helper 適合單一整數值。

## 九、init 失敗路徑

原始碼：

```c
if (IS_ERR_OR_NULL(entry)) {
	ret = entry ? PTR_ERR(entry) : -ENOMEM;
	goto err_remove_debugfs;
}
...
err_remove_debugfs:
	debugfs_remove(dl_root);
	return ret;
```

如果建立 `status` 或 `trigger` 失敗，就移除已建立的 debugfs root。

第一輪要抓的是：

```text
成功建立一部分 debugfs tree 後
後面失敗必須清掉前面已建立的部分
```

## 十、exit：移除 debugfs 目錄樹

原始碼：

```c
static void __exit driver_lab_debugfs_logging_exit(void)
{
	debugfs_remove(dl_root);
	pr_info("debugfs directory removed\n");
}
```

`debugfs_remove(dl_root)` 會移除這個 debugfs 目錄樹。本 lab 不逐一 remove `status` / `trigger` / `trigger_count` / `emit_debug`。

卸載後，test 會檢查：

```text
/sys/kernel/debug/driver_lab_debugfs 不存在
```

這確認 cleanup 真的反映到 filesystem surface。

## dynamic debug 在這份檔案中的位置

原始碼：

```c
if (dl_emit_debug)
	pr_debug("dynamic-debug path count=%u len=%zu\n",
			 dl_trigger_count, strlen(dl_last_message));
```

`dl_emit_debug` 是 driver 內部開關，`pr_debug()` 是否真的出現在 log，還受 kernel dynamic debug 設定影響。

test 裡會做：

```sh
echo 'module driver_lab_debugfs_logging +p' | sudo tee /proc/dynamic_debug/control
```

這是要求 dynamic debug 打開這個 module 的 `pr_debug()` callsite。

## source 和命令的對照

| userspace 命令 | driver path |
|---|---|
| `cat .../status` | `dl_status_open()` -> `seq_read()` -> `dl_status_show()` |
| `printf ... | tee .../trigger` | `dl_trigger_write()` |
| `cat .../trigger_count` | debugfs `u32` helper 直接讀 `dl_trigger_count` |
| `cat/echo .../emit_debug` | debugfs `u32` helper 直接讀寫 `dl_emit_debug` |
| `echo 'module ... +p' > /proc/dynamic_debug/control` | kernel dynamic debug 控制 `pr_debug()` |
| `rmmod driver_lab_debugfs_logging` | `driver_lab_debugfs_logging_exit()` -> `debugfs_remove(dl_root)` |

## 常見卡點

- debugfs 不是穩定產品 ABI；這是 debug/learning interface。
- `/sys/kernel/debug` 可能尚未 mount，要先跑 `scripts/mount-debugfs.sh`。
- 寫 `trigger` 要 root 權限或 sudo。
- `pr_debug()` 不一定預設出現在 `dmesg`，通常要 dynamic debug 開啟。
- `emit_debug=1` 只代表程式會呼叫 `pr_debug()`，不等於 dynamic debug 一定讓它輸出。
- `trigger` payload 超過 63 bytes 會被截斷保存。
- `debugfs_create_u32()` 直接導出 kernel variable，適合 debug，不適合穩定 ABI。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab01 建立的 debugfs root 是什麼？ | `/sys/kernel/debug/driver_lab_debugfs`。 |
| `cat status` 最終由哪個 function 產生內容？ | `dl_status_show()`。 |
| 寫 `trigger` 會進哪個 callback？ | `dl_trigger_write()`。 |
| `trigger_count` 是怎麼導出的？ | `debugfs_create_u32("trigger_count", ..., &dl_trigger_count)`。 |
| 為什麼 `status` fops 要用 `single_release()`？ | 因為 open path 使用 `single_open()`。 |
| `emit_debug=1` 是否保證 `pr_debug()` 一定出現在 log？ | 不保證，還要 dynamic debug / kernel debug print 設定允許。 |
| module 卸載時怎麼清 debugfs？ | `debugfs_remove(dl_root)`。 |

## 查證來源

- Linux kernel documentation `DebugFS`：debugfs 不是穩定 ABI、`debugfs_create_file()`、`debugfs_create_u32()`、`debugfs_remove()`。<https://docs.kernel.org/filesystems/debugfs.html>
- Linux kernel documentation `The seq_file Interface`：`single_open()` 與 `single_release()` 搭配。<https://docs.kernel.org/filesystems/seq_file.html>
- Linux kernel documentation `Dynamic debug`：`/proc/dynamic_debug/control`、module query、`+p`。<https://docs.kernel.org/admin-guide/dynamic-debug-howto.html>
- Linux kernel documentation `Message logging with printk`：`pr_debug()` / `pr_info()` logging 背景。<https://docs.kernel.org/core-api/printk-basics.html>
