#!/bin/sh
set -eu

# 有些 lab 會在 /sys/kernel/debug 匯出檔案，因此要先掛載 debugfs。

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

if grep -qs ' /sys/kernel/debug ' /proc/mounts; then
    printf 'debugfs is already mounted.\n'
    exit 0
fi

if [ "$(id -u)" -eq 0 ]; then
    mount -t debugfs none /sys/kernel/debug
else
    sudo mount -t debugfs none /sys/kernel/debug
fi

printf 'Mounted debugfs at /sys/kernel/debug.\n'
