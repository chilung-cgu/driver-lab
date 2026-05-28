# POSIX shell helper for lab smoke tests.
# Source this file from test.sh after setting FS_SUDO to sudo or empty.

fs_sudo() {
	if [ -n "${FS_SUDO:-}" ]; then
		$FS_SUDO "$@"
	else
		"$@"
	fi
}

fs_ok() {
	printf 'OK: %s\n' "$1"
}

fs_expect_path() {
	path=$1
	description=$2

	if ! fs_sudo test -e "$path"; then
		printf 'ERROR: missing %s: %s\n' "$description" "$path" >&2
		return 1
	fi

	fs_ok "$description exists: $path"
}

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

fs_expect_debugfs_file() {
	path=$1
	fs_expect_path "$path" "debugfs entry"
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

fs_expect_proc_interrupt() {
	name=$1

	if ! grep -q "$name" /proc/interrupts; then
		printf 'ERROR: /proc/interrupts does not list %s\n' "$name" >&2
		return 1
	fi
	fs_ok "/proc/interrupts lists $name"
}
