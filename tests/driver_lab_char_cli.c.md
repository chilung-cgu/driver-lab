# `driver_lab_char_cli.c` 詳解

## 結論

`tests/driver_lab_char_cli.c` 是 Lab02/Lab03 的 userspace 教學 CLI。它的功能不是展示完整 CLI framework，而是把每個 subcommand 明確對到 driver 的一條路徑：

```text
write/read      -> data path
ioctl-write     -> control path: set message
status          -> control path: get status
trigger         -> event path: generate event
clear           -> control path: clear state
poll            -> event path: wait
mmap-read       -> shared memory path
```

這支 CLI 是你從 userspace trace 到 kernel driver 的橋：

```text
shell command
  -> driver_lab_char_cli.c
  -> runtime/src/driver_lab_runtime.c
  -> syscall
  -> /dev/driver_lab_ctl0
  -> Lab03 driver callback
```

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- CLI source：[`driver_lab_char_cli.c`](driver_lab_char_cli.c)。
- runtime API：[`../runtime/include/driver_lab_runtime.h.md`](../runtime/include/driver_lab_runtime.h.md)、[`../runtime/src/driver_lab_runtime.c.md`](../runtime/src/driver_lab_runtime.c.md)。
- Lab03 driver：[`../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)。
- Lab03 smoke test：[`../labs/03-ioctl-poll-mmap/test.sh.md`](../labs/03-ioctl-poll-mmap/test.sh.md)。
- `mmap()` 失敗值與 `poll()` return/revents 語意以 Linux man-pages 為準。

這份 CLI 沒有使用 `getopt()`、subcommand table 或大型 CLI framework；以下只按目前 source 解釋。

## 先理解這份檔案在 repo 的位置

這支 CLI 由 [`../runtime/Makefile`](../runtime/Makefile) 建出：

```make
$(CLI): src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
	$(CC) $(CFLAGS) -o $@ src/driver_lab_runtime.c ../tests/driver_lab_char_cli.c
```

所以它不是單獨編譯；它會和 runtime source 一起 link。

使用方式範例：

```sh
../tests/driver_lab_char_cli /dev/driver_lab_ctl0 status
../tests/driver_lab_char_cli /dev/driver_lab_ctl0 mmap-read
../tests/driver_lab_char_cli /dev/driver_lab_ctl0 poll 3000
```

命令形狀固定是：

```text
driver_lab_char_cli <device> <subcommand> [subcommand args]
```

## 這份檔案要解決什麼問題？

如果每次測 driver 都直接寫 C 程式或手動 syscall，學習會很慢。這支 CLI 把 Lab03 的幾條路徑變成可從 shell 呼叫的 subcommand，讓 `test.sh` 和你手動操作都能重複使用。

白話講：

```text
CLI 不是 driver 邏輯
它是把「我要測哪條路徑」翻譯成 runtime API call
```

## 讀 source 的主線

第一次請照這個順序讀：

1. `usage()`：先看支援哪些 subcommand。
2. `main()` 的變數：看它需要哪些 runtime/UAPI 型別。
3. `argc < 3`：看命令基本形狀。
4. `poll` 特例：看為什麼加 `O_NONBLOCK`。
5. `dl_runtime_open_flags()`：所有 subcommand 都先打開同一個 device。
6. 每個 `strcmp(argv[2], "...")` 分支：看 subcommand 對應哪個 runtime helper。
7. 結尾 `dl_runtime_close()`：看 fd lifecycle。

## 一、include 區

原始碼：

```c
#include "driver_lab_runtime.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
```

重點：

| header | 用途 |
|---|---|
| `driver_lab_runtime.h` | runtime API 與 UAPI struct |
| `<fcntl.h>` | `O_RDWR`、`O_NONBLOCK` |
| `<stdio.h>` | `fprintf()`、`printf()`、`perror()` |
| `<stdlib.h>` | `atoi()` |
| `<string.h>` | `strcmp()`、`strlen()` |
| `<sys/mman.h>` | `MAP_FAILED` |

CLI 沒有直接 include `driver_lab_uapi.h`，因為 runtime header 已經 include 它。

## 二、`usage()`：subcommand 對照表

原始碼：

```c
static void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s <device> write <message>\n", prog);
	fprintf(stderr, "  %s <device> read\n", prog);
	fprintf(stderr, "  %s <device> ioctl-write <message>\n", prog);
	fprintf(stderr, "  %s <device> status\n", prog);
	fprintf(stderr, "  %s <device> trigger\n", prog);
	fprintf(stderr, "  %s <device> clear\n", prog);
	fprintf(stderr, "  %s <device> poll <timeout-ms>\n", prog);
	fprintf(stderr, "  %s <device> mmap-read\n", prog);
}
```

這就是 CLI 支援的所有操作。它同時也是 Lab03 學習地圖：

| subcommand | runtime helper | driver path |
|---|---|---|
| `write` | `dl_runtime_write()` | `.write` |
| `read` | `dl_runtime_read()` | `.read` |
| `ioctl-write` | `dl_runtime_ioctl_set_message()` | ioctl set message |
| `status` | `dl_runtime_ioctl_get_status()` | ioctl get status |
| `trigger` | `dl_runtime_ioctl_trigger_event()` | ioctl trigger |
| `clear` | `dl_runtime_ioctl_clear_buffer()` | ioctl clear |
| `poll` | `dl_runtime_poll_readable()` | `.poll` |
| `mmap-read` | `dl_runtime_mmap_shared()` | `.mmap` |

## 三、`main()` 變數：CLI 需要哪些狀態

原始碼：

```c
struct dl_runtime_handle handle = { .fd = -1 };
char buffer[256];
struct dl_ioctl_status status;
struct dl_shared_page *shared;
short revents = 0;
ssize_t ret;
int timeout_ms = -1;
int open_flags = O_RDWR;
```

逐一看：

| 變數 | 用途 |
|---|---|
| `handle` | runtime fd wrapper，初始為 invalid fd |
| `buffer` | `read` subcommand 的 userspace buffer |
| `status` | `status` subcommand 接 ioctl 回傳 |
| `shared` | `mmap-read` subcommand 讀 shared page |
| `revents` | `poll` subcommand 接 poll event mask |
| `ret` | syscall/runtime helper 回傳值 |
| `timeout_ms` | `poll` timeout |
| `open_flags` | open device flags |

`handle = { .fd = -1 }` 很重要。這讓 runtime 一開始知道 fd 尚未有效，錯誤路徑也比較容易處理。

## 四、參數數量與 `poll` open flag

原始碼：

```c
if (argc < 3) {
	usage(argv[0]);
	return 1;
}

if (strcmp(argv[2], "poll") == 0)
	open_flags |= O_NONBLOCK;
```

CLI 至少需要：

```text
argv[0] = program name
argv[1] = device path
argv[2] = subcommand
```

`poll` subcommand 會加 `O_NONBLOCK`。這不是讓 `poll()` 本身不等待；`poll()` 是否等待主要由 timeout 決定。這裡加 `O_NONBLOCK` 是讓同一個 fd 如果後續有 read-like 行為，不會因 driver blocking read 語意卡住，也讓 event test 更明確。

## 五、所有 subcommand 都先 open 同一個 device

原始碼：

```c
if (dl_runtime_open_flags(&handle, argv[1], open_flags) != 0) {
	perror("dl_runtime_open");
	return 1;
}
```

`argv[1]` 通常是：

```text
/dev/driver_lab_ctl0
```

差異不是 device path，而是後面呼叫哪個 runtime helper。

白話講：

```text
同一個 /dev node
根據 subcommand 走不同 syscall path
```

## 六、`write <message>`：data path 寫入

原始碼：

```c
ret = dl_runtime_write(&handle, argv[3], strlen(argv[3]));
if (ret < 0) {
	perror("dl_runtime_write");
	dl_runtime_close(&handle);
	return 1;
}

printf("wrote %zd bytes\n", ret);
```

這條路徑：

```text
CLI write
  -> dl_runtime_write()
  -> write()
  -> driver dl_write()
```

`strlen(argv[3])` 表示 CLI 傳的是不含 NUL terminator 的 bytes。driver 端會自己補 `'\0'` 到 kernel buffer。

## 七、`ioctl-write <message>`：control path 設定 message

原始碼：

```c
if (dl_runtime_ioctl_set_message(&handle, argv[3]) != 0) {
	perror("dl_runtime_ioctl_set_message");
	dl_runtime_close(&handle);
	return 1;
}

printf("ioctl message updated\n");
```

這條路徑：

```text
CLI ioctl-write
  -> dl_runtime_ioctl_set_message()
  -> ioctl(fd, DL_IOC_SET_MESSAGE, &msg)
  -> driver dl_unlocked_ioctl()
  -> dl_publish_message_locked()
```

和 `write` 的差異：

- `write` 是 data path。
- `ioctl-write` 是 control path。
- 兩者最後都會更新 driver message/event/shared page。

## 八、`read`：data path 讀回 message

原始碼：

```c
ret = dl_runtime_read(&handle, buffer, sizeof(buffer) - 1);
if (ret < 0) {
	perror("dl_runtime_read");
	dl_runtime_close(&handle);
	return 1;
}

buffer[ret] = '\0';
printf("read %zd bytes: %s\n", ret, buffer);
```

重點是：

```c
buffer[ret] = '\0';
```

`read()` 只回傳 bytes，不保證 C string 結尾。CLI 為了用 `%s` 印出來，所以自己補 NUL。

Lab03 driver 的 read 是消費型語意：完整讀完後會清 buffer 和 pending event。

## 九、`status`：control path 查狀態

原始碼：

```c
if (dl_runtime_ioctl_get_status(&handle, &status) != 0) {
	perror("dl_runtime_ioctl_get_status");
	dl_runtime_close(&handle);
	return 1;
}

printf("buffer_len=%u event_count=%u event_pending=%u mmap_size=%u\n",
	   status.buffer_len, status.event_count,
	   status.event_pending, status.mmap_size);
```

這條路徑不讀 message body，而是查 driver state summary。

輸出範例：

```text
buffer_len=11 event_count=1 event_pending=1 mmap_size=4096
```

`test.sh` 用 `grep 'buffer_len='` 確認這條路徑有回應。

## 十、`trigger`：產生 event

原始碼：

```c
if (dl_runtime_ioctl_trigger_event(&handle) != 0) {
	perror("dl_runtime_ioctl_trigger_event");
	dl_runtime_close(&handle);
	return 1;
}

printf("event triggered\n");
```

這條路徑主要用來喚醒 `poll`：

```text
poll 先在背景等
trigger 送 DL_IOC_TRIGGER_EVENT
driver 設 event_pending
driver wake_up_interruptible(&dl_event_wq)
poll 回來
```

它不設定 message，只設定 event state。

## 十一、`clear`：清 driver state

原始碼：

```c
if (dl_runtime_ioctl_clear_buffer(&handle) != 0) {
	perror("dl_runtime_ioctl_clear_buffer");
	dl_runtime_close(&handle);
	return 1;
}

printf("buffer cleared\n");
```

這條路徑會讓 driver 清掉 buffer 和 pending event。smoke test 末段會呼叫它，讓狀態回乾淨。

## 十二、`poll <timeout-ms>`：等待 data/event

原始碼：

```c
timeout_ms = atoi(argv[3]);
ret = dl_runtime_poll_readable(&handle, timeout_ms, &revents);
if (ret < 0) {
	perror("dl_runtime_poll_readable");
	dl_runtime_close(&handle);
	return 1;
}

printf("poll ret=%zd revents=0x%x\n", ret, (unsigned int)revents);
```

`poll()` 的 return value 和 `revents` 要分開看：

| 值 | 意義 |
|---|---|
| `ret` | 有幾個 fd 發生事件；`0` 表示 timeout |
| `revents` | 這個 fd 實際發生哪些事件 |

Lab03 smoke test 只 grep：

```text
poll ret=1
```

代表一個 fd 發生事件。

## 十三、`mmap-read`：讀 shared page snapshot

原始碼：

```c
shared = dl_runtime_mmap_shared(&handle, DL_MMAP_BYTES);
if (shared == MAP_FAILED) {
	perror("dl_runtime_mmap_shared");
	dl_runtime_close(&handle);
	return 1;
}

printf("magic=0x%x version=%u event_count=%u event_pending=%u buffer_len=%u buffer=%s\n",
	   shared->magic, shared->version, shared->event_count,
	   shared->event_pending, shared->buffer_len, shared->buffer);

if (dl_runtime_munmap_shared(shared, DL_MMAP_BYTES) != 0) {
	...
}
```

幾個重點：

- `mmap()` 失敗要比對 `MAP_FAILED`，不是 `NULL`。
- `shared` 是 userspace pointer，但指向 driver 映射出來的 shared page。
- CLI 用 `struct dl_shared_page` layout 讀欄位。
- 讀完要 `munmap()`。

白話講：

```text
mmap-read 不走 driver 的 read callback
它直接看 driver 維護的一頁 snapshot
```

## 十四、錯誤路徑與 close

每個分支失敗時大多會：

```c
perror("...");
dl_runtime_close(&handle);
return 1;
```

最後成功路徑也會：

```c
if (dl_runtime_close(&handle) != 0) {
	perror("dl_runtime_close");
	return 1;
}
```

這是 userspace resource lifecycle：

```text
open device
  -> 做一個 subcommand
  -> close device
```

## 這份檔案和 test.sh 的對照

| `test.sh` 命令 | CLI 分支 | 測到什麼 |
|---|---|---|
| `ioctl-write hello-ioctl` | `ioctl-write` | control path set message |
| `status | grep buffer_len=` | `status` | control path get status |
| `read | grep hello-ioctl` | `read` | data path read |
| `mmap-read | grep magic=0x` | `mmap-read` | shared page mapping |
| `poll 3000` + `trigger` | `poll` / `trigger` | event path wakeup |
| `clear` | `clear` | state cleanup |

## 常見卡點

- `argv[1]` 是 device path，`argv[2]` 才是 subcommand。
- `read()` 補 `buffer[ret] = '\0'` 是 CLI 自己做的。
- `poll ret=1` 不是 event mask；event mask 是 `revents`。
- `mmap-read` 不會呼叫 driver `.read`。
- `atoi()` 沒有嚴格錯誤處理；這是教學 CLI，不是產品級參數解析。
- 每個錯誤分支都要 close fd，否則反覆測試時容易留下錯誤狀態。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 CLI 的命令形狀是什麼？ | `<device> <subcommand> [args]`。 |
| 哪個 subcommand 走 data path write？ | `write <message>`。 |
| 哪個 subcommand 走 ioctl set message？ | `ioctl-write <message>`。 |
| `status` 印出的欄位來自哪個 struct？ | `struct dl_ioctl_status`。 |
| `poll ret=1` 代表什麼？ | 一個 fd 有事件，不代表 event mask 是 1。 |
| `mmap-read` 為什麼能印 magic？ | 它把 mmap 回傳 pointer 當成 `struct dl_shared_page *` 解讀。 |
| CLI 為什麼要 include `driver_lab_runtime.h`？ | 因為它透過 runtime API 操作 driver，而不是直接散寫 syscall。 |

## 查證來源

- Linux man-pages `poll(2)`：`poll()` return value 與 `revents`。<https://man7.org/linux/man-pages/man2/poll.2.html>
- Linux man-pages `mmap(2)`：`MAP_FAILED` 與 `munmap()` userspace 語意。<https://man7.org/linux/man-pages/man2/mmap.2.html>
