# `driver_lab_race_cli.c` 詳解

## 結論

`tests/driver_lab_race_cli.c` 是 Lab04 的 userspace race reproduction tool。它不是一般產品 CLI，而是用 pthreads 同時對 `/dev/driver_lab_race0` 送 ioctl，讓 kernel driver 裡的 shared counter 更容易踩出 lost update。

它提供五個 subcommand：

```text
status
reset
safe-mode <0|1>
inc <count>
race <threads> <loops>
```

核心是：

```text
race 8 50
  -> 建 8 條 pthread
  -> 每條 thread 送 50 次 DL_RACE_IOC_INC_COUNTER
  -> 最後 GET_STATUS
  -> 印 expected_at_least / observed / safe_mode
```

這份 CLI 是 Lab04 讀懂「問題如何被重現」的另一半；只看 kernel driver 會少掉 userspace 施壓模型。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`driver_lab_race_cli.c`](driver_lab_race_cli.c) 本身。
- Lab04 UAPI：[`../labs/04-locking-and-races/driver_lab_race_uapi.h.md`](../labs/04-locking-and-races/driver_lab_race_uapi.h.md)。
- Lab04 driver：[`../labs/04-locking-and-races/driver_lab_race.c.md`](../labs/04-locking-and-races/driver_lab_race.c.md)。
- Lab04 test：[`../labs/04-locking-and-races/test.sh.md`](../labs/04-locking-and-races/test.sh.md)。
- Linux man-pages：`open(2)`、`ioctl(2)`、`pthread_create(3)`、`pthread_join(3)`。

這裡不展開 pthread scheduling 的所有細節；第一輪只解釋它如何建立多條 userspace execution paths，同時打同一個 driver ioctl。

## 先理解這份檔案在 repo 的位置

Lab04 測試會在 runtime 中暫時 build 這支 CLI：

```sh
cc -Wall -Wextra -Werror -pthread \
  -o "$ROOT_DIR/tests/driver_lab_race_cli" \
  "$ROOT_DIR/tests/driver_lab_race_cli.c"
```

然後用它打：

```text
/dev/driver_lab_race0
```

相關檔案：

| 檔案 | 角色 |
|---|---|
| [`driver_lab_race_cli.c`](driver_lab_race_cli.c) | userspace 施壓工具 |
| [`../labs/04-locking-and-races/driver_lab_race_uapi.h.md`](../labs/04-locking-and-races/driver_lab_race_uapi.h.md) | ioctl command 與 status ABI |
| [`../labs/04-locking-and-races/driver_lab_race.c.md`](../labs/04-locking-and-races/driver_lab_race.c.md) | kernel ioctl handler |
| [`../labs/04-locking-and-races/test.sh.md`](../labs/04-locking-and-races/test.sh.md) | 自動跑 unsafe/safe 對照 |

## 這份檔案要解決什麼問題？

如果只用單執行緒呼叫：

```sh
driver_lab_race_cli /dev/driver_lab_race0 inc 400
```

你不一定容易看到 race。Lab04 需要的是多條 userspace path 同時進 driver：

```text
thread 0 -> ioctl INC
thread 1 -> ioctl INC
thread 2 -> ioctl INC
...
```

所以這份 CLI 用 pthreads 建立壓力，讓 `dl_race_increment_unlocked()` 更容易出現 lost update。

## 一、include 與 UAPI

原始碼：

```c
#include "../labs/04-locking-and-races/driver_lab_race_uapi.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>
```

重點：

| Header | 用途 |
|---|---|
| `driver_lab_race_uapi.h` | `DL_RACE_IOC_*` 與 `struct dl_race_status`。 |
| `<fcntl.h>` | `open()` 的 `O_RDWR`。 |
| `<pthread.h>` | `pthread_create()` / `pthread_join()`。 |
| `<sys/ioctl.h>` | userspace `ioctl()` declaration。 |
| `<unistd.h>` | `close()`。 |

這支 CLI 和 kernel driver 共享同一份 UAPI header，避免 ioctl number 或 status struct 各寫一份造成不一致。

## 二、`struct worker_args`

原始碼：

```c
struct worker_args {
	int fd;
	int loops;
};
```

這個 struct 傳給每條 pthread：

| 欄位 | 意義 |
|---|---|
| `fd` | 已開啟的 `/dev/driver_lab_race0` file descriptor。 |
| `loops` | 這條 thread 要送幾次 `DL_RACE_IOC_INC_COUNTER`。 |

注意：多條 pthread 共用同一個 `fd`。這正是 Lab04 想要的壓力模型：同一個 opened device，被多條 userspace threads 同時操作。

## 三、usage

原始碼：

```c
static void usage(const char *prog)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  %s <device> status\n", prog);
	fprintf(stderr, "  %s <device> reset\n", prog);
	fprintf(stderr, "  %s <device> safe-mode <0|1>\n", prog);
	fprintf(stderr, "  %s <device> inc <count>\n", prog);
	fprintf(stderr, "  %s <device> race <threads> <loops>\n", prog);
}
```

這是錯誤輸入時的提醒。Lab04 的自動測試主要用：

```sh
safe-mode 0
reset
race 8 50
safe-mode 1
reset
race 8 50
```

## 四、worker thread：反覆送 increment ioctl

原始碼：

```c
static void *worker_thread(void *opaque)
{
	struct worker_args *args = opaque;
	int i;

	for (i = 0; i < args->loops; ++i) {
		if (ioctl(args->fd, DL_RACE_IOC_INC_COUNTER) != 0) {
			perror("DL_RACE_IOC_INC_COUNTER");
			return (void *)1;
		}
	}

	return NULL;
}
```

這是 `race` subcommand 的核心。

每條 pthread 都跑同一個 function，反覆呼叫：

```c
ioctl(fd, DL_RACE_IOC_INC_COUNTER)
```

kernel 端對應：

```text
dl_race_ioctl()
  -> DL_RACE_IOC_INC_COUNTER
  -> dl_race_increment()
```

unsafe mode 下，這會走 `dl_race_increment_unlocked()`；safe mode 下，會走 mutex-protected path。

## 五、main：基本 argument 與 open

原始碼：

```c
if (argc < 3) {
	usage(argv[0]);
	return 1;
}

fd = open(argv[1], O_RDWR);
if (fd < 0) {
	perror("open");
	return 1;
}
```

CLI 呼叫形狀：

```sh
driver_lab_race_cli /dev/driver_lab_race0 status
```

`argv[1]` 是 device path，`argv[2]` 是 subcommand。

如果 `open()` 失敗，先查：

```sh
ls -l /dev/driver_lab_race0
lsmod | grep '^driver_lab_race'
sudo dmesg | tail -n 50
```

## 六、`status`

原始碼：

```c
if (strcmp(argv[2], "status") == 0) {
	if (ioctl(fd, DL_RACE_IOC_GET_STATUS, &status) != 0) {
		perror("DL_RACE_IOC_GET_STATUS");
		close(fd);
		return 1;
	}

	printf("counter=%u safe_mode=%u worker_running=%u\n",
		   status.counter, status.safe_mode, status.worker_running);
}
```

方向：

```text
kernel -> userspace
```

driver 會填 `struct dl_race_status`，CLI 印出：

```text
counter=123 safe_mode=1 worker_running=1
```

## 七、`reset`

原始碼：

```c
} else if (strcmp(argv[2], "reset") == 0) {
	if (ioctl(fd, DL_RACE_IOC_RESET_COUNTER) != 0) {
		perror("DL_RACE_IOC_RESET_COUNTER");
		close(fd);
		return 1;
	}
}
```

這會要求 driver 把 `dl_counter` 歸零。每次 race 實驗前都要 reset，否則上一輪數字會干擾觀察。

## 八、`safe-mode <0|1>`

原始碼：

```c
} else if (strcmp(argv[2], "safe-mode") == 0) {
	unsigned int value;
	...
	value = (unsigned int)strtoul(argv[3], NULL, 10);
	if (ioctl(fd, DL_RACE_IOC_SET_SAFE_MODE, &value) != 0) {
		perror("DL_RACE_IOC_SET_SAFE_MODE");
		close(fd);
		return 1;
	}
}
```

方向：

```text
userspace -> kernel
```

`value = 0`：

```text
driver 走 unsafe increment
```

`value = 1`：

```text
driver 走 mutex-protected increment
```

注意：這裡沒有嚴格限制只能輸入 `0` 或 `1`；driver 端會把非零值當 true。README 仍建議用 `0|1`。

## 九、`inc <count>`

原始碼：

```c
} else if (strcmp(argv[2], "inc") == 0) {
	count = atoi(argv[3]);
	for (i = 0; i < count; ++i) {
		if (ioctl(fd, DL_RACE_IOC_INC_COUNTER) != 0) {
			perror("DL_RACE_IOC_INC_COUNTER");
			close(fd);
			return 1;
		}
	}
}
```

這是單執行緒版本，適合先確認 ioctl path 是通的。

但它不是 Lab04 的主要 race reproduction。真正用來製造競爭的是 `race <threads> <loops>`。

## 十、`race <threads> <loops>`

原始碼主線：

```c
threads = atoi(argv[3]);
loops = atoi(argv[4]);
...
ids = calloc((size_t)threads, sizeof(*ids));
...
args.fd = fd;
args.loops = loops;

for (i = 0; i < threads; ++i)
	pthread_create(&ids[i], NULL, worker_thread, &args);

for (i = 0; i < threads; ++i)
	pthread_join(ids[i], NULL);
```

這段做：

```text
建立 N 條 pthread
每條 thread 跑 worker_thread()
每條送 loops 次 INC_COUNTER ioctl
main thread 等全部 pthread 結束
```

為什麼 `pthread_join()` 重要？

因為如果不 join，main thread 可能在 worker 還沒跑完前就去讀 status，結果沒有意義。

## 十一、印 `expected_at_least` / `observed`

原始碼：

```c
if (ioctl(fd, DL_RACE_IOC_GET_STATUS, &status) != 0) {
	...
}

printf("expected_at_least=%d observed=%u safe_mode=%u\n",
	   threads * loops, status.counter, status.safe_mode);
```

`expected_at_least` 只計算 userspace 自己送出的 increment：

```text
threads * loops
```

但 kernel background worker 也會加 counter，所以 safe mode 下 `observed` 可能大於 `expected_at_least`。這不是錯。

unsafe mode 下，因為 lost update，`observed` 常常比期待值低很多。

## CLI subcommand 對照 driver path

| CLI subcommand | ioctl | driver path |
|---|---|---|
| `status` | `DL_RACE_IOC_GET_STATUS` | fill `struct dl_race_status` + `copy_to_user()` |
| `reset` | `DL_RACE_IOC_RESET_COUNTER` | lock -> `dl_counter = 0` |
| `safe-mode 0|1` | `DL_RACE_IOC_SET_SAFE_MODE` | `copy_from_user()` -> set `dl_safe_mode` |
| `inc <count>` | `DL_RACE_IOC_INC_COUNTER` repeatedly | `dl_race_increment()` |
| `race <threads> <loops>` | `DL_RACE_IOC_INC_COUNTER` from many pthreads | unsafe/safe contrast |

## 常見卡點

- `race` 的 `threads` / `loops` 必須大於 0。
- `atoi()` / `strtoul()` 這裡是教學 CLI 的簡化解析，不是嚴格產品級 input parser。
- 多條 pthread 共用同一個 `fd` 是刻意設計，用來對同一個 driver state 施壓。
- `expected_at_least` 不是精確期待值，因為 worker 也會加 counter。
- `pthread_create()` 失敗時，目前已建立的 threads 沒有額外 join cleanup；這是教學 CLI 的簡化，測試規模很小。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 CLI 為什麼需要 `-pthread`？ | `race` subcommand 會用 pthread 建多條 userspace threads。 |
| `race 8 50` 代表什麼？ | 8 條 pthread，每條送 50 次 increment ioctl。 |
| `expected_at_least` 怎麼算？ | `threads * loops`。 |
| 為什麼 `observed` 可能大於 `expected_at_least`？ | kernel background worker 也會 increment counter。 |
| 哪個 subcommand 切 safe/unsafe？ | `safe-mode <0|1>`。 |
| 哪個 subcommand 讀回 struct status？ | `status` 或 `race` 結尾都會用 `DL_RACE_IOC_GET_STATUS`。 |

## 查證來源

- Linux man-pages `open(2)`：開啟 `/dev/driver_lab_race0` 取得 file descriptor。<https://man7.org/linux/man-pages/man2/open.2.html>
- Linux man-pages `ioctl(2)`：對 special file descriptor 下 device-specific control operation。<https://man7.org/linux/man-pages/man2/ioctl.2.html>
- Linux man-pages `pthread_create(3)` / `pthread_join(3)`：建立並等待 pthread。<https://www.man7.org/linux/man-pages/man3/pthread_create.3.html>、<https://www.man7.org/linux/man-pages/man3/pthread_join.3.html>
