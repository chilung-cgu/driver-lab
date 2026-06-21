# `test.sh` 詳解

## 結論

`labs/04-locking-and-races/test.sh` 是 Lab04 的 Linux smoke test。它不只驗證 module 可以載入，還會用 userspace pthread CLI 先跑 unsafe mode，再跑 safe mode，確認 mutex 修正後不應比 unsafe 更差。

測試主線：

```text
build driver_lab_race.ko
build tests/driver_lab_race_cli with -pthread
insmod
verify /dev + sysfs + /proc/devices
safe-mode 0 -> reset -> race 8 50
safe-mode 1 -> reset -> race 8 50
parse observed values
assert safe_observed >= unsafe_observed
rmmod
verify /dev/sysfs removed
make clean
```

這支 test 的定位是 smoke test，不是數學證明。race 結果受 timing 影響；它驗的是同一組壓力下 safe mode 不應表現得比 unsafe 更差。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab04 driver：[`driver_lab_race.c.md`](driver_lab_race.c.md)。
- userspace CLI：[`../../tests/driver_lab_race_cli.c.md`](../../tests/driver_lab_race_cli.c.md)。
- repo helper：[`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)。
- Linux man-pages：`sed(1)`、`tee(1)`、`pthread_create(3)`、`pthread_join(3)`。

這裡不聲稱 unsafe mode 每次一定會比 safe mode 小很多；race 本來就是 timing-dependent。test 只設計成可重複的基本對照。

## 一、shell 模式與 Linux guard

原始碼：

```sh
#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: test.sh 必須在 Linux 主機上執行。\n' >&2
    exit 1
fi
```

Lab04 必須在 Linux 上跑，因為它要：

- build/load kernel module。
- 建立 `/dev/driver_lab_race0`。
- 用 ioctl 打 kernel driver。
- 讀 `/sys/class/...`、`/proc/devices`、`dmesg`。

`set -eu` 讓未處理的命令失敗或未設定變數直接中止，避免錯誤被吞掉。

## 二、路徑與暫存 log

原始碼：

```sh
SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
CLI="$ROOT_DIR/tests/driver_lab_race_cli"
MODULE_NAME=driver_lab_race
SUDO=
UNSAFE_LOG=$(mktemp)
SAFE_LOG=$(mktemp)
```

| 變數 | 用途 |
|---|---|
| `SCRIPT_DIR` | Lab04 目錄。 |
| `ROOT_DIR` | repo 根目錄。 |
| `CLI` | 編出的 userspace race CLI 路徑。 |
| `MODULE_NAME` | `lsmod` / `rmmod` 使用。 |
| `UNSAFE_LOG` | 保存 unsafe mode 的 `race` output。 |
| `SAFE_LOG` | 保存 safe mode 的 `race` output。 |

`mktemp` 讓兩份 log 不會互相覆蓋，也方便 cleanup。

`CDPATH=''` 是為了避免使用者環境中的 `CDPATH` 影響 `cd` 的輸出或路徑解析；這個寫法也比 `CDPATH= cd ...` 更容易通過 shellcheck。

## 三、cleanup 與 trap

原始碼：

```sh
cleanup() {
    if lsmod | grep -q "^${MODULE_NAME} "; then
        $SUDO rmmod "$MODULE_NAME" || true
    fi
    rm -f "$UNSAFE_LOG" "$SAFE_LOG" "$CLI"
}

trap cleanup EXIT INT TERM
```

cleanup 做三件事：

1. 如果 module 還載著，嘗試 `rmmod`。
2. 刪掉 unsafe/safe log。
3. 刪掉臨時 build 出來的 race CLI。

這很重要，因為 Lab04 module 有 background kthread。測試中斷時如果不卸載，下一輪可能帶著舊 worker 狀態重跑。

## 四、sudo 與 filesystem helper

原始碼：

```sh
if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi
# shellcheck disable=SC2034
FS_SUDO=$SUDO
# shellcheck disable=SC1091
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

不是 root 時使用 `sudo`。

`FS_SUDO` 是傳給被 source 進來的 `fs-surface-checks.sh` 使用，所以在本檔看起來像沒有直接引用；`SC2034` 是針對這個跨檔案用法的 shellcheck 註記。

`fs-surface-checks.sh` 的路徑是 runtime 算出來的 repo 路徑，shellcheck 靜態分析不會跟進這個動態 source；`SC1091` 註記只是在說明這個限制，不改變執行行為。

source [`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh) 後，可以直接使用：

- `fs_expect_char_device`
- `fs_expect_absent`

## 五、build module 與 CLI

原始碼：

```sh
cd "$SCRIPT_DIR"
make
cc -Wall -Wextra -Werror -pthread -o "$CLI" "$ROOT_DIR/tests/driver_lab_race_cli.c"
```

第一行建 kernel module：

```text
driver_lab_race.ko
```

第二行建 userspace CLI：

```text
tests/driver_lab_race_cli
```

`-pthread` 是必要的，因為 CLI 的 `race` subcommand 會用 `pthread_create()` 建多條 userspace threads。

## 六、清掉舊 module、載入新 module、驗 filesystem surface

原始碼：

```sh
if lsmod | grep -q "^${MODULE_NAME} "; then
    $SUDO rmmod "$MODULE_NAME"
fi

$SUDO insmod "./${MODULE_NAME}.ko"
fs_expect_char_device /dev/driver_lab_race0 \
	/sys/class/driver_lab_race/driver_lab_race0 \
	driver_lab_race
```

`fs_expect_char_device` 會驗：

| 檢查 | 意義 |
|---|---|
| `/dev/driver_lab_race0` 存在且是 char device | userspace ioctl 入口已出現。 |
| `/sys/class/driver_lab_race/driver_lab_race0/dev` 存在且像 `major:minor` | device model surface 正常。 |
| `/proc/devices` 列出 `driver_lab_race` | major number 註冊正常。 |

如果這裡失敗，先看：

```sh
sudo dmesg | tail -n 50
ls -l /sys/class/driver_lab_race
ls -l /dev/driver_lab_race0
grep driver_lab_race /proc/devices
```

## 七、unsafe mode 實驗

原始碼：

```sh
$SUDO "$CLI" /dev/driver_lab_race0 safe-mode 0
$SUDO "$CLI" /dev/driver_lab_race0 reset
$SUDO "$CLI" /dev/driver_lab_race0 race 8 50 | tee "$UNSAFE_LOG"
```

逐行：

1. `safe-mode 0`：切到故意不加鎖的 increment path。
2. `reset`：把 counter 歸零。
3. `race 8 50`：建立 8 條 pthread，每條送 50 次 increment ioctl。

理想上 userspace 至少送出：

```text
8 * 50 = 400
```

所以 CLI 印：

```text
expected_at_least=400 observed=... safe_mode=0
```

unsafe mode 可能看到 `observed` 明顯小於 `expected_at_least`，這是 lost update 的表現。

## 八、safe mode 實驗

原始碼：

```sh
$SUDO "$CLI" /dev/driver_lab_race0 safe-mode 1
$SUDO "$CLI" /dev/driver_lab_race0 reset
$SUDO "$CLI" /dev/driver_lab_race0 race 8 50 | tee "$SAFE_LOG"
```

這次切到 mutex path。driver 的 increment 會走：

```text
mutex_lock(&dl_race_lock)
dl_counter++
mutex_unlock(&dl_race_lock)
```

safe mode 的 `observed` 通常會更接近或超過 `expected_at_least`。超過不是錯，因為 background worker 也會加 counter。

## 九、解析 observed 並比較

原始碼：

```sh
unsafe_observed=$(sed -n 's/.*observed=\([0-9][0-9]*\).*/\1/p' "$UNSAFE_LOG")
safe_observed=$(sed -n 's/.*observed=\([0-9][0-9]*\).*/\1/p' "$SAFE_LOG")

[ -n "$unsafe_observed" ]
[ -n "$safe_observed" ]

if [ "$safe_observed" -lt "$unsafe_observed" ]; then
    printf 'ERROR: safe mode should not perform worse than unsafe mode.\n' >&2
    exit 1
fi
```

`sed` 從 CLI output 抽出 `observed=` 數字。

這個判斷很保守：

```text
safe_observed 不應小於 unsafe_observed
```

它沒有要求：

```text
safe_observed == expected_at_least
```

原因是 background worker 會同時增加 counter，而且 race timing 不穩定。

## 十、rmmod、surface cleanup、make clean

原始碼：

```sh
$SUDO rmmod "$MODULE_NAME"
fs_expect_absent /dev/driver_lab_race0 "device node"
fs_expect_absent /sys/class/driver_lab_race/driver_lab_race0 "sysfs class device"
make clean
```

`rmmod` 會觸發：

```text
driver_lab_race_exit()
  -> kthread_stop()
  -> device_destroy()
  -> class_destroy()
  -> cdev_del()
  -> unregister_chrdev_region()
```

`fs_expect_absent` 確認 `/dev` 和 sysfs entry 消失，避免 cleanup path 留殘影。

## test 和 source 的對照

| test 片段 | 驗證的 source path |
|---|---|
| `insmod` | `driver_lab_race_init()` |
| `fs_expect_char_device` | char device registration + device_create 外部效果 |
| `safe-mode 0` | `DL_RACE_IOC_SET_SAFE_MODE` -> `dl_safe_mode = false` |
| `race 8 50` | pthreads repeatedly call `DL_RACE_IOC_INC_COUNTER` |
| `safe-mode 1` | `DL_RACE_IOC_SET_SAFE_MODE` -> `dl_safe_mode = true` |
| observed comparison | unsafe/safe mode behavior contrast |
| `rmmod` | `driver_lab_race_exit()` |
| `fs_expect_absent` | cleanup removes `/dev` and sysfs surface |

## 常見卡點

- unsafe 和 safe 差異不一定每次都一樣；race 是 timing-dependent。
- 如果差異太小，可以手動加大 `race <threads> <loops>` 觀察，但 smoke test 保持短時間可跑。
- `expected_at_least` 只計算 userspace increment，worker 也會加 counter。
- `safe_observed >= unsafe_observed` 是保守 smoke gate，不是完整 correctness proof。
- `driver_lab_race_cli` 是 test 暫時 build 出來的 artifact，cleanup 會刪掉。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| 這支 test 為什麼要 build CLI？ | race reproduction 需要 userspace pthreads 反覆送 ioctl。 |
| unsafe mode 跑哪個 driver path？ | `dl_race_increment_unlocked()`。 |
| safe mode 跑哪個 driver path？ | mutex 保護下的 `dl_race_increment_locked()`。 |
| 為什麼不要求 safe observed 等於 400？ | background worker 也會加 counter，而且 timing 會影響結果。 |
| test 的主要 gate 是什麼？ | `safe_observed` 不應小於 `unsafe_observed`。 |
| rmmod 後驗什麼？ | `/dev/driver_lab_race0` 與 `/sys/class/driver_lab_race/driver_lab_race0` 都消失。 |

## 查證來源

- Linux man-pages `pthread_create(3)` / `pthread_join(3)`：CLI 用 pthread 建立並等待 worker threads。<https://www.man7.org/linux/man-pages/man3/pthread_create.3.html>、<https://www.man7.org/linux/man-pages/man3/pthread_join.3.html>
- Linux man-pages `tee(1)`：把 CLI output 同時寫到 stdout 與 log file。<https://man7.org/linux/man-pages/man1/tee.1.html>
- Linux man-pages `sed(1)`：從 log 中抽取 `observed=` 數字。<https://man7.org/linux/man-pages/man1/sed.1.html>
