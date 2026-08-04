#!/bin/sh
set -eu

QEMU_BIN=${QEMU_BIN:-qemu-system-x86_64}
QEMU_IMAGE=${QEMU_IMAGE:-}
QEMU_IMAGE_FORMAT=${QEMU_IMAGE_FORMAT:-qcow2}
QEMU_GUEST_ARCH=${QEMU_GUEST_ARCH:-}
QEMU_ACCEL=${QEMU_ACCEL:-}
QEMU_EXTRA_ARGS=${QEMU_EXTRA_ARGS:-}
SSH_PORT=${SSH_PORT:-2222}
MEMORY_MB=${MEMORY_MB:-2048}
SMP_CPUS=${SMP_CPUS:-2}
HOST_OS=$(uname -s)
HOST_ARCH=$(uname -m)

normalize_arch() {
    case "$1" in
        x86_64|amd64)
            printf 'x86_64\n'
            ;;
        aarch64|arm64)
            printf 'aarch64\n'
            ;;
        *)
            printf '%s\n' "$1"
            ;;
    esac
}

infer_guest_arch() {
    qemu_name=$(basename -- "$QEMU_BIN")

    case "$qemu_name" in
        qemu-system-x86_64)
            printf 'x86_64\n'
            ;;
        qemu-system-aarch64)
            printf 'aarch64\n'
            ;;
        *)
            printf 'unknown\n'
            ;;
    esac
}

supports_accel() {
    accel=$1

    "$QEMU_BIN" -accel help 2>/dev/null |
        grep -Eq "(^|[[:space:]])${accel}([[:space:]]|$)"
}

same_architecture() {
    [ "$(normalize_arch "$HOST_ARCH")" = \
      "$(normalize_arch "$QEMU_GUEST_ARCH")" ]
}

kvm_is_usable() {
    supports_accel kvm &&
        [ -c /dev/kvm ] &&
        [ -r /dev/kvm ] &&
        [ -w /dev/kvm ]
}

pick_default_accel() {
    if ! same_architecture; then
        printf 'tcg\n'
        return 0
    fi

    case "$HOST_OS" in
        Linux)
            if kvm_is_usable; then
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

is_positive_integer() {
    case "$1" in
        ''|*[!0-9]*|0)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
    printf 'ERROR: 找不到 QEMU binary: %s\n' "$QEMU_BIN" >&2
    exit 1
fi

if [ -z "$QEMU_IMAGE" ]; then
    printf 'ERROR: 請設定 QEMU_IMAGE 指向 guest image。\n' >&2
    printf '例如：QEMU_IMAGE=%s/vm/ubuntu.qcow2 %s\n' "\$HOME" "$0" >&2
    exit 1
fi

if [ ! -f "$QEMU_IMAGE" ]; then
    printf 'ERROR: QEMU_IMAGE 不存在或不是 regular file: %s\n' \
        "$QEMU_IMAGE" >&2
    exit 1
fi

case "$QEMU_IMAGE_FORMAT" in
    raw|qcow2)
        ;;
    *)
        printf 'ERROR: 目前只接受 QEMU_IMAGE_FORMAT=raw 或 qcow2，收到 %s\n' \
            "$QEMU_IMAGE_FORMAT" >&2
        exit 1
        ;;
esac

if ! is_positive_integer "$SSH_PORT" || [ "$SSH_PORT" -gt 65535 ]; then
    printf 'ERROR: SSH_PORT 必須是 1..65535 的整數。\n' >&2
    exit 1
fi
if ! is_positive_integer "$MEMORY_MB"; then
    printf 'ERROR: MEMORY_MB 必須是正整數。\n' >&2
    exit 1
fi
if ! is_positive_integer "$SMP_CPUS"; then
    printf 'ERROR: SMP_CPUS 必須是正整數。\n' >&2
    exit 1
fi

if [ -z "$QEMU_GUEST_ARCH" ]; then
    QEMU_GUEST_ARCH=$(infer_guest_arch)
fi

if [ "$QEMU_GUEST_ARCH" = unknown ]; then
    printf 'ERROR: 無法從 %s 推斷 guest architecture；請設定 QEMU_GUEST_ARCH。\n' \
        "$QEMU_BIN" >&2
    exit 1
fi

if [ -z "$QEMU_ACCEL" ]; then
    QEMU_ACCEL=$(pick_default_accel)
else
    if ! supports_accel "$QEMU_ACCEL"; then
        printf 'ERROR: %s 未編譯/列出 accel=%s\n' \
            "$QEMU_BIN" "$QEMU_ACCEL" >&2
        exit 1
    fi

    if ! same_architecture && [ "$QEMU_ACCEL" != tcg ]; then
        printf 'ERROR: host=%s、guest=%s 是 cross-architecture；請使用 accel=tcg。\n' \
            "$HOST_ARCH" "$QEMU_GUEST_ARCH" >&2
        exit 1
    fi

    if [ "$QEMU_ACCEL" = kvm ] && ! kvm_is_usable; then
        printf 'ERROR: KVM 未可用；確認同架構且目前使用者可讀寫 /dev/kvm。\n' >&2
        exit 1
    fi
fi

printf 'Launching QEMU: host=%s/%s guest=%s accel=%s image-format=%s\n' \
    "$HOST_OS" "$HOST_ARCH" "$QEMU_GUEST_ARCH" "$QEMU_ACCEL" \
    "$QEMU_IMAGE_FORMAT"

# QEMU_EXTRA_ARGS is an explicitly trusted local escape hatch. For complex
# quoting or untrusted input, use a local wrapper/array-capable shell instead.
# shellcheck disable=SC2086
exec "$QEMU_BIN" \
    -accel "$QEMU_ACCEL" \
    -m "$MEMORY_MB" \
    -smp "$SMP_CPUS" \
    -drive "file=$QEMU_IMAGE,if=virtio,format=$QEMU_IMAGE_FORMAT" \
    -netdev "user,id=n1,hostfwd=tcp::${SSH_PORT}-:22" \
    -device virtio-net-pci,netdev=n1 \
    -device edu \
    -nographic \
    $QEMU_EXTRA_ARGS
