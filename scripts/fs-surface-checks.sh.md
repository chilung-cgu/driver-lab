# `fs-surface-checks.sh` 詳解

## 結論

這份檔案是一組被 lab smoke tests source 進去使用的 filesystem surface 檢查 helper。它不是獨立執行的主程式；它的角色是把 driver 教學中提到的 `/dev`、`/sys`、`/proc`、debugfs、PCI sysfs 觀測點，變成測試真的會驗的條件。

最重要的學習價值是：你不只要知道 driver code 呼叫了某個 kernel API，還要能在 Linux filesystem 裡看到對應結果。

## 不確定處 / 查證範圍

這份講義根據本 repo 的 helper source、各 lab `test.sh` 使用方式，以及 Linux filesystem surface 的一般行為解釋。它不保證不同 distro 的 udev rule、權限、group ownership 或 `/sys` symlink 顯示完全相同；本 helper 驗的是 repo lab 需要的最小可觀測面。

## 先理解這份檔案在 repo 的位置

路徑：

```text
scripts/fs-surface-checks.sh
```

它主要被 lab smoke tests 這樣使用：

```sh
FS_SUDO=sudo
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
fs_expect_char_device "/dev/driver_lab_ctl0" \
    "/sys/class/driver_lab_ctl/driver_lab_ctl0" \
    "driver_lab_ctl"
```

它和 [`../docs/onboarding/kernel-filesystem-surfaces.md`](../docs/onboarding/kernel-interfaces.md) 是一組搭配：

- onboarding 文件解釋「為什麼會出現這些路徑」。
- 這支 helper 讓 smoke test 實際檢查「路徑有沒有出現、型別是否正確、報表是否列出」。

## 這份檔案要解決什麼問題

driver lab 很容易只驗「CLI command 有沒有成功」，但忽略 kernel 對外暴露的 surface 是否符合預期。例如：

- `/dev/driver_lab_char0` 有沒有存在？
- 它是不是 char device，而不是普通檔案？
- `/sys/class/.../.../dev` 有沒有暴露 `major:minor`？
- `/proc/devices` 是否列出這個 char driver name？
- PCI EDU 裝置是否真的在 guest 的 `/sys/bus/pci/devices` 裡？
- module unload 後，原本的 `/dev` 或 debugfs entry 是否消失？

這些不是額外裝飾，而是 driver bring-up 的核心觀測點。

## 它怎麼被呼叫

這份檔案沒有 shebang，因為它不是拿來直接執行：

```sh
. "$ROOT_DIR/scripts/fs-surface-checks.sh"
```

`.` 是 POSIX shell 的 source 語法。source 之後，裡面的 function 會存在於呼叫端 shell，`test.sh` 就可以直接呼叫。

### 為什麼不能當成普通 executable

如果你直接執行：

```sh
./scripts/fs-surface-checks.sh
```

它只會定義 function，沒有主流程可跑。這不是 bug；它本來就是 helper library。

## 讀 source 的主線

主線可以分成五組：

1. `fs_sudo`：統一處理是否需要 sudo。
2. `fs_ok`：統一成功輸出格式。
3. path 檢查：存在、消失、debugfs、optional path。
4. char device surface 檢查：`/dev`、`/sys/class`、`/proc/devices`。
5. PCI/IRQ surface 檢查：PCI device、driver bind、`/proc/interrupts`。

## 一、`fs_sudo`：把權限策略交給呼叫端

原始碼片段：

```sh
fs_sudo() {
	if [ -n "${FS_SUDO:-}" ]; then
		$FS_SUDO "$@"
	else
		"$@"
	fi
}
```

### 這段在做什麼

如果呼叫端設定 `FS_SUDO=sudo`，helper 會用 `sudo` 執行傳入的命令。若 `FS_SUDO` 是空字串或未設定，就直接執行原命令。

### 白話講

不同 lab、不同環境對 `/dev`、`/sys`、`debugfs` 的讀取權限不一定相同。這個 helper 不自己判斷「現在要不要 sudo」，而是讓 `test.sh` 在進入共用 helper 前先決定：

```sh
FS_SUDO=
if [ "$(id -u)" -ne 0 ]; then
    FS_SUDO=sudo
fi
```

這樣共用檢查邏輯不用到處重複權限判斷。

### 常見誤解

`$FS_SUDO "$@"` 看起來像少了引號。這裡是刻意允許 `FS_SUDO` 代表一個 command name，例如 `sudo`。本 repo 的使用情境是 `FS_SUDO=sudo` 或空字串，不是任意 shell fragment。

## 二、`fs_expect_path`：存在性檢查的基本積木

原始碼片段：

```sh
fs_expect_path() {
	path=$1
	description=$2

	if ! fs_sudo test -e "$path"; then
		printf 'ERROR: missing %s: %s\n' "$description" "$path" >&2
		return 1
	fi

	fs_ok "$description exists: $path"
}
```

### 這段在做什麼

它檢查某個路徑是否存在。失敗時印錯誤並 `return 1`；成功時印 `OK`。

### 為什麼只檢查存在還不夠

存在性是第一層檢查，不是全部。例如 `/dev/driver_lab_char0` 存在，還要再確認它是不是 char device；`/sys/class/.../dev` 存在，還要確認內容像 `major:minor`。

所以你會看到後面的 helper 把 `fs_expect_path` 當積木疊起來。

## 三、失敗傳遞：為什麼呼叫後面要接 `|| return 1`

原始碼片段：

```sh
fs_expect_path "$dev_node" "device node" || return 1
```

### 這段在做什麼

如果 `fs_expect_path` 失敗，外層 function 立刻 `return 1`。

### 為什麼這點重要

在 shell function 裡，不能只靠最外層 `set -e` 來保證失敗一定正確傳出。尤其當 helper 被 source 到不同 script、不同 function、不同條件判斷中時，明確寫 `|| return 1` 比隱含依賴更可靠。

這個 repo 的 smoke test 目標是「一旦 surface 缺失，就讓測試真的失敗」，所以 helper 需要明確傳遞錯誤。

## 四、`fs_expect_absent`：驗 cleanup 是否真的發生

原始碼片段：

```sh
fs_expect_absent() {
	path=$1
	description=$2

	if fs_sudo test -e "$path"; then
		printf 'ERROR: expected %s to be removed, but still exists: %s\n' \
			"$description" "$path" >&2
		return 1
	fi

	fs_ok "$description removed: $path"
}
```

### 這段在做什麼

它和 `fs_expect_path` 相反：期待路徑不存在。

### 對 driver 學習的意義

driver 不只要 init/probe 建立資源，也要 exit/remove 清掉資源。若 module unload 後 `/dev`、`/sys` 或 debugfs entry 還留著，就代表 cleanup path 有問題，下一次測試也可能被舊狀態污染。

## 五、`fs_expect_char_device`：一次驗三個 char device 觀測點

原始碼片段：

```sh
fs_expect_char_device() {
	dev_node=$1
	sysfs_device=$2
	proc_name=$3

	fs_expect_path "$dev_node" "device node" || return 1
	if ! fs_sudo test -c "$dev_node"; then
		printf 'ERROR: path exists but is not a char device: %s\n' "$dev_node" >&2
		return 1
	fi
	fs_ok "device node is a char device: $dev_node"

	fs_expect_path "$sysfs_device" "sysfs class device" || return 1
	fs_expect_path "$sysfs_device/dev" "sysfs major:minor file" || return 1
	if ! fs_sudo cat "$sysfs_device/dev" | grep '^[0-9][0-9]*:[0-9][0-9]*$' >/dev/null; then
		printf 'ERROR: sysfs dev file does not contain major:minor: %s\n' \
			"$sysfs_device/dev" >&2
		return 1
	fi
	fs_ok "sysfs dev file exposes major:minor: $sysfs_device/dev"

	if ! awk -v name="$proc_name" '$2 == name { found = 1 } END { exit !found }' \
		/proc/devices; then
		printf 'ERROR: /proc/devices does not list %s\n' "$proc_name" >&2
		return 1
	fi
	fs_ok "/proc/devices lists $proc_name"
}
```

### 這段在做什麼

它檢查三個 surface：

| 檢查 | 代表意義 |
|---|---|
| `/dev/...` 存在且 `test -c` 成立 | userspace 看到 char device node。 |
| `/sys/class/.../.../dev` 存在且內容是 `major:minor` | kernel device model 暴露 device number。 |
| `/proc/devices` 第二欄等於 driver name | char major registration 在 kernel 報表中可見。 |

### 為什麼 `/proc/devices` 用 `awk '$2 == name'`

`/proc/devices` 的格式通常像：

```text
Character devices:
 240 driver_lab_ctl
```

這裡要比對第二欄的完整名稱，而不是用單純 `grep driver_lab_ctl`。完整欄位比對可以避免名字只是部分匹配時誤判成功。

### 對應到哪些 lab

這個 helper 是 Lab02、Lab03、Lab04、Lab09 的共通觀測點。只要 lab 建立 char device node，就可以用它驗「入口存在且型別正確」。

## 六、debugfs 與 optional path

原始碼片段：

```sh
fs_expect_debugfs_file() {
	path=$1
	fs_expect_path "$path" "debugfs entry" || return 1
}

fs_note_optional_path() {
	path=$1
	description=$2

	if fs_sudo test -e "$path"; then
		fs_ok "$description exists: $path"
	else
		printf 'INFO: optional %s not present: %s\n' "$description" "$path"
	fi
}
```

### 這段在做什麼

`fs_expect_debugfs_file` 是 debugfs 必要入口檢查。`fs_note_optional_path` 則是可選入口，只印資訊，不讓測試失敗。

### 白話講

不是所有 kernel 功能都該變成 hard requirement。例如 dynamic debug 是否可用，可能跟 kernel config 或 distro 設定有關。對這種情況，測試可以提醒你「這裡沒有」，但不應該因此判定 lab driver 壞掉。

## 七、PCI EDU device 檢查

原始碼片段：

```sh
fs_expect_pci_device_id() {
	vendor=$1
	device=$2

	for pci_dev in /sys/bus/pci/devices/*; do
		[ -r "$pci_dev/vendor" ] || continue
		[ -r "$pci_dev/device" ] || continue
		if [ "$(cat "$pci_dev/vendor")" = "$vendor" ] &&
			[ "$(cat "$pci_dev/device")" = "$device" ]; then
			fs_ok "PCI device $vendor:$device exists: $pci_dev"
			return 0
		fi
	done

	printf 'ERROR: PCI device %s:%s not found in /sys/bus/pci/devices\n' \
		"$vendor" "$device" >&2
	return 1
}
```

### 這段在做什麼

它掃描 `/sys/bus/pci/devices/*/vendor` 與 `device`，尋找指定 PCI vendor/device ID。

### 對 Lab05-07 的意義

QEMU EDU driver 的第一個前提不是 driver code，而是 guest 裡真的看得到 EDU device。若這個 helper 找不到 `0x1234:0x11e8`，你應該先檢查 QEMU 啟動參數與 guest PCI bus，而不是先改 `probe()`。

## 八、PCI driver bind 檢查

原始碼片段：

```sh
fs_expect_pci_driver_bound() {
	driver=$1
	vendor=$2
	device=$3
	driver_dir=/sys/bus/pci/drivers/$driver

	fs_expect_path "$driver_dir" "PCI driver sysfs directory" || return 1

	for pci_dev in "$driver_dir"/*; do
		[ -r "$pci_dev/vendor" ] || continue
		[ -r "$pci_dev/device" ] || continue
		if [ "$(cat "$pci_dev/vendor")" = "$vendor" ] &&
			[ "$(cat "$pci_dev/device")" = "$device" ]; then
			fs_ok "PCI driver $driver is bound to $vendor:$device"
			return 0
		fi
	done

	printf 'ERROR: PCI driver %s is not bound to %s:%s\n' "$driver" "$vendor" "$device" >&2
	return 1
}
```

### 這段在做什麼

它先確認 `/sys/bus/pci/drivers/<driver>` 存在，再檢查 driver 目錄底下有沒有指定 vendor/device 的 device symlink。

### 白話講

「guest 看得到 PCI device」和「你的 driver 已經 bind 到它」是兩件事：

- `fs_expect_pci_device_id`：裝置存在。
- `fs_expect_pci_driver_bound`：你的 driver 接手了裝置。

Lab05-07 若卡在 `probe()` 沒進來，這兩層檢查可以幫你把問題切開。

## 九、`/proc/interrupts` 檢查

原始碼片段：

```sh
fs_expect_proc_interrupt() {
	name=$1

	if ! grep -q "$name" /proc/interrupts; then
		printf 'ERROR: /proc/interrupts does not list %s\n' "$name" >&2
		return 1
	fi
	fs_ok "/proc/interrupts lists $name"
}
```

### 這段在做什麼

它檢查 `/proc/interrupts` 裡是否出現某個名稱。

### 限制

這只能證明 kernel interrupt report 裡有該名稱，不代表：

- IRQ 計數一定會增加。
- handler 一定清掉 device interrupt。
- DMA completion 一定正確。

所以它是 smoke test 的輔助觀測，不是完整 IRQ correctness proof。

## 這份檔案和其他檔案的對照

| 檔案 | 關係 |
|---|---|
| [`../docs/onboarding/kernel-filesystem-surfaces.md`](../docs/onboarding/kernel-interfaces.md) | 解釋 `/dev`、`/sys`、`/proc`、debugfs 的學習模型。 |
| [`../labs/02-char-device/test.sh`](../labs/02-char-device/test.sh) | 使用 char device surface 檢查。 |
| [`../labs/03-ioctl-poll-mmap/test.sh`](../labs/03-ioctl-poll-mmap/test.sh) | 使用 char device surface 檢查 Lab03 control device。 |
| [`../labs/05-pci-edu-mmio/test.sh`](../labs/05-pci-edu-mmio/test.sh) | 使用 PCI device / driver bind 檢查。 |
| [`../labs/09-stress-and-fault-injection/stress-03-reload.sh`](../labs/09-stress-and-fault-injection/stress-03-reload.sh) | 在反覆 load/unload 過程中檢查 `/dev`、sysfs、`/proc/devices`。 |

## 常見卡點

### helper 裡沒有 `set -e`

這是正常的。它是被 source 的 library，不應該偷偷改呼叫端 shell option。每個 function 用 `return 1` 表達失敗，呼叫端決定整體流程。

### `sudo cat ... | grep ...` 看起來怪

`fs_sudo cat "$sysfs_device/dev"` 只把 `cat` 用 sudo 跑，後面的 `grep` 在目前使用者下跑。這裡 grep 只處理 pipe 過來的文字，不需要 sudo。

### `/dev` 不存在，但 `/sys/class` 存在

這通常代表 device model 入口建立了，但 devtmpfs/udev 或權限層有問題。先看 `dmesg`、`cat /sys/class/.../.../dev`、`/proc/devices`，再追 `/dev` node。

### PCI device 找不到

優先檢查 QEMU 是否有 `-device edu`，以及 guest 內 `lspci -nn` 是否看得到 `1234:11e8`。

## 讀完後你應該能回答

1. 為什麼這份檔案要被 source，而不是直接執行？
2. `fs_expect_char_device` 同時驗哪三個 Linux surface？
3. 為什麼 helper function 內部常見 `|| return 1`？
4. `fs_expect_pci_device_id` 和 `fs_expect_pci_driver_bound` 的差別是什麼？
5. 為什麼 `/proc/interrupts` 檢查不能證明 IRQ handler 完全正確？
