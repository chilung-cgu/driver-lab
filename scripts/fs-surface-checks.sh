# POSIX shell helper for lab smoke tests.
#
# 這個檔案不是主程式，而是一組可以被 test.sh / stress script 共用的
#「檔案系統入口檢查工具」。
#
# 用法：
# 1. 先在呼叫端設定 `FS_SUDO=sudo` 或 `FS_SUDO=`。
# 2. 再用 `. "$ROOT_DIR/scripts/fs-surface-checks.sh"` 把它 source 進來。
# 3. 之後就可以直接呼叫底下這些 function。
#
# 這些 helper 的目的不是取代 driver 行為驗證，而是把文件裡提到的
# `/dev`、`/sys`、`/proc`、debugfs、PCI sysfs 入口，變成 smoke test 真的會檢查的項目。

# fs_sudo: 依照呼叫端是否需要 sudo，幫後面的命令加上或不加上 sudo。
# 參數：
#   "$@" = 要執行的原始命令與參數
# 第一輪理解：
#   - 如果 FS_SUDO 有值，就代表這些檢查需要用 sudo 跑。
#   - 如果 FS_SUDO 是空字串，就直接執行原始命令。
#   - 這樣 test.sh 就不用到處重複寫 `if [ "$(id -u)" -ne 0 ] ...`。
fs_sudo() {
	if [ -n "${FS_SUDO:-}" ]; then
		$FS_SUDO "$@"
	else
		"$@"
	fi
}

# fs_ok: 印出一行固定格式的成功訊息。
# 參數：
#   $1 = 要顯示的成功描述
# 第一輪理解：
#   - 這不是驗證邏輯，只是把成功訊息統一格式化。
#   - smoke test 跑完時，你可以很快看到哪些 filesystem surface 已經確認過。
fs_ok() {
	printf 'OK: %s\n' "$1"
}

# fs_expect_path: 檢查某個路徑是否存在。
# 參數：
#   $1 = 要檢查的路徑
#   $2 = 對這個路徑的描述文字，例如 "device node" 或 "debugfs entry"
# 第一輪理解：
#   - 這是最基本的檢查，先回答「文件說會出現的東西，有沒有真的出現」。
#   - 成功時只證明路徑存在，還不保證它是正確型別，所以某些情況會再搭配
#     `test -c`、`cat .../dev`、`/proc/devices` 等第二層檢查。
#   - 失敗時會直接印錯誤並 return 1，讓 test.sh fail-fast。
fs_expect_path() {
	path=$1
	description=$2

	if ! fs_sudo test -e "$path"; then
		printf 'ERROR: missing %s: %s\n' "$description" "$path" >&2
		return 1
	fi

	fs_ok "$description exists: $path"
}

# fs_expect_absent: 檢查某個路徑是否已經消失。
# 參數：
#   $1 = 要檢查的路徑
#   $2 = 對這個路徑的描述文字
# 第一輪理解：
#   - 用來驗證 cleanup / unload / remove 是否真的把入口移除了。
#   - 這一點很重要，因為教學 driver 很常是「建立入口」和「清掉入口」都要學。
#   - 如果卸載後路徑還在，代表 cleanup 不完整，這會誤導後續實驗。
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

# fs_expect_char_device: 一次檢查 char device 的三個常用觀測點。
# 參數：
#   $1 = /dev node 路徑，例如 /dev/driver_lab_char0
#   $2 = sysfs class device 路徑，例如 /sys/class/driver_lab_char/driver_lab_char0
#   $3 = /proc/devices 裡應該出現的名稱，例如 driver_lab_char
# 第一輪理解：
#   - 這個 helper 是 `02/03/04` 的共通檢查。
#   - 它把「userspace 可操作入口」、「sysfs device model 入口」、
#     「major/minor 註冊名稱」三件事一起驗。
#   - 先確認 /dev 存在，再確認它真的是 char device，最後看 sysfs 與 /proc/devices。
fs_expect_char_device() {
	dev_node=$1
	sysfs_device=$2
	proc_name=$3

	fs_expect_path "$dev_node" "device node"
	if ! fs_sudo test -c "$dev_node"; then
		printf 'ERROR: path exists but is not a char device: %s\n' "$dev_node" >&2
		return 1
	fi
	fs_ok "device node is a char device: $dev_node"

	fs_expect_path "$sysfs_device" "sysfs class device"
	fs_expect_path "$sysfs_device/dev" "sysfs major:minor file"
	fs_sudo cat "$sysfs_device/dev" | grep '^[0-9][0-9]*:[0-9][0-9]*$' >/dev/null
	fs_ok "sysfs dev file exposes major:minor: $sysfs_device/dev"

	if ! grep -q "$proc_name" /proc/devices; then
		printf 'ERROR: /proc/devices does not list %s\n' "$proc_name" >&2
		return 1
	fi
	fs_ok "/proc/devices lists $proc_name"
}

# fs_expect_debugfs_file: 檢查 debugfs 檔案是否存在。
# 參數：
#   $1 = debugfs 檔案路徑
# 第一輪理解：
#   - `01` 的教學重點是 debugfs entries 是否真的被建立。
#   - 這裡不特別檢查檔案內容，因為不同狀態下內容可能會變；先確認 surface 存在最重要。
fs_expect_debugfs_file() {
	path=$1
	fs_expect_path "$path" "debugfs entry"
}

# fs_note_optional_path: 記錄「可選」路徑是否存在。
# 參數：
#   $1 = 路徑
#   $2 = 描述文字
# 第一輪理解：
#   - 有些 kernel 功能是可選的，例如 dynamic debug。
#   - 這個 helper 不會讓測試失敗；有就印 OK，沒有就印 INFO。
#   - 適合用來表達「這份教學希望你看到，但缺了不代表 lab 本身壞掉」。
fs_note_optional_path() {
	path=$1
	description=$2

	if fs_sudo test -e "$path"; then
		fs_ok "$description exists: $path"
	else
		printf 'INFO: optional %s not present: %s\n' "$description" "$path"
	fi
}

# fs_expect_pci_device_id: 在 /sys/bus/pci/devices 中尋找指定 vendor/device。
# 參數：
#   $1 = vendor id，例如 0x1234
#   $2 = device id，例如 0x11e8
# 第一輪理解：
#   - 這是 `05-07` 的前置檢查。
#   - 它回答的問題不是「driver 有沒有對」，而是「guest 裡有沒有真的看到 QEMU EDU」。
#   - 如果這裡找不到，優先修 QEMU/guest bring-up，而不是怪 driver code。
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

# fs_expect_pci_driver_bound: 檢查某個 PCI driver 是否真的 bind 到指定裝置。
# 參數：
#   $1 = driver 名稱
#   $2 = vendor id
#   $3 = device id
# 第一輪理解：
#   - 這是確認 `probe()` 已經真的進來的強一點版本。
#   - 不只看 PCI device 是否存在，還要看 driver 是否已接管那顆 device。
#   - 對 `05-07` 很有用，因為「有 EDU」和「driver 已 bind」是兩件不同的事。
fs_expect_pci_driver_bound() {
	driver=$1
	vendor=$2
	device=$3
	driver_dir=/sys/bus/pci/drivers/$driver

	fs_expect_path "$driver_dir" "PCI driver sysfs directory"

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

# fs_expect_proc_interrupt: 檢查 /proc/interrupts 裡是否有某個名字。
# 參數：
#   $1 = 要搜尋的名稱
# 第一輪理解：
#   - 這是 IRQ/DMA 教學裡的輔助觀測點。
#   - 它不保證 IRQ 計數正確，只保證 kernel 的中斷報表裡有這個名稱。
#   - 適合用來驗證「driver 已經把 IRQ 相關資源掛起來」。
fs_expect_proc_interrupt() {
	name=$1

	if ! grep -q "$name" /proc/interrupts; then
		printf 'ERROR: /proc/interrupts does not list %s\n' "$name" >&2
		return 1
	fi
	fs_ok "/proc/interrupts lists $name"
}
