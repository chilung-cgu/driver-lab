#!/bin/sh
set -eu

QEMU_BIN=${QEMU_BIN:-qemu-system-x86_64}
QEMU_IMAGE=${QEMU_IMAGE:-}
QEMU_ACCEL=${QEMU_ACCEL:-}
QEMU_EXTRA_ARGS=${QEMU_EXTRA_ARGS:-}
SSH_PORT=${SSH_PORT:-2222}
MEMORY_MB=${MEMORY_MB:-2048}
SMP_CPUS=${SMP_CPUS:-2}
HOST_OS=$(uname -s)

supports_accel() {
    accel=$1

    if "$QEMU_BIN" -accel help 2>/dev/null | grep -Eq "(^|[[:space:]])${accel}([[:space:]]|$)"; then
        return 0
    fi

    return 1
}

pick_default_accel() {
    case "$HOST_OS" in
        Linux)
            if supports_accel kvm; then
                printf 'kvm\n'
            else
                printf 'tcg\n'
            fi
            ;;
        Darwin)
            if supports_accel hvf; then
                printf 'hvf\n'
            else
                printf 'tcg\n'
            fi
            ;;
        *)
            printf 'tcg\n'
            ;;
    esac
}

if [ -z "$QEMU_IMAGE" ]; then
    printf 'ERROR: 請先設定 QEMU_IMAGE 指向你的 guest image。\n' >&2
    # shellcheck disable=SC2016
    printf '例如：QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 %s\n' "$0" >&2
    exit 1
fi

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    printf 'ERROR: 找不到 %s\n' "$QEMU_BIN" >&2
    exit 1
fi

if [ -z "$QEMU_ACCEL" ]; then
    QEMU_ACCEL=$(pick_default_accel)
elif ! supports_accel "$QEMU_ACCEL"; then
    printf 'ERROR: %s 不支援 accel=%s\n' "$QEMU_BIN" "$QEMU_ACCEL" >&2
    exit 1
fi

printf 'Launching QEMU on %s with accel=%s\n' "$HOST_OS" "$QEMU_ACCEL"

# shellcheck disable=SC2086
exec "$QEMU_BIN" \
    -accel "$QEMU_ACCEL" \
    -m "$MEMORY_MB" \
    -smp "$SMP_CPUS" \
    -drive "file=$QEMU_IMAGE,if=virtio,format=qcow2" \
    -netdev "user,id=n1,hostfwd=tcp::${SSH_PORT}-:22" \
    -device virtio-net-pci,netdev=n1 \
    -device edu \
    -nographic \
    $QEMU_EXTRA_ARGS
