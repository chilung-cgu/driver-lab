#!/bin/sh
set -eu

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: 這個腳本必須在 Linux 主機上執行。\n' >&2
    exit 1
fi

QEMU_BIN=${QEMU_BIN:-qemu-system-x86_64}
QEMU_IMAGE=${QEMU_IMAGE:-}
SSH_PORT=${SSH_PORT:-2222}
MEMORY_MB=${MEMORY_MB:-2048}
SMP_CPUS=${SMP_CPUS:-2}

if [ -z "$QEMU_IMAGE" ]; then
    printf 'ERROR: 請先設定 QEMU_IMAGE 指向你的 guest image。\n' >&2
    printf '例如：QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 %s\n' "$0" >&2
    exit 1
fi

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    printf 'ERROR: 找不到 %s\n' "$QEMU_BIN" >&2
    exit 1
fi

exec "$QEMU_BIN" \
    -m "$MEMORY_MB" \
    -smp "$SMP_CPUS" \
    -drive "file=$QEMU_IMAGE,if=virtio,format=qcow2" \
    -netdev "user,id=n1,hostfwd=tcp::${SSH_PORT}-:22" \
    -device virtio-net-pci,netdev=n1 \
    -device edu \
    -nographic
