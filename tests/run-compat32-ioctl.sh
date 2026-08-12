#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
ROOT_DIR=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)
BINARY=$(mktemp)

cleanup() {
    rm -f "$BINARY"
}

trap cleanup EXIT INT TERM

if [ "$(uname -s)" != "Linux" ]; then
    printf 'ERROR: compat ioctl regression 必須在 Linux x86_64 guest 上執行。\n' >&2
    exit 1
fi
if [ "$(uname -m)" != "x86_64" ]; then
    printf 'ERROR: expected x86_64 guest, got %s.\n' "$(uname -m)" >&2
    exit 1
fi
if [ "$#" -ne 2 ]; then
    printf 'Usage: %s <lab03|lab04> <device>\n' "$0" >&2
    exit 1
fi

case "$1" in
    lab03|lab04)
        ;;
    *)
        printf 'ERROR: unknown lab: %s\n' "$1" >&2
        exit 1
        ;;
esac

if ! cc -m32 -Wall -Wextra -Werror -std=c11 -o "$BINARY" \
    "$ROOT_DIR/tests/driver_lab_compat32_ioctl.c"; then
    printf '%s\n' \
        'ERROR: cc -m32 failed; on Ubuntu x86_64 install gcc-multilib libc6-dev-i386.' \
        >&2
    exit 1
fi

elf_header=$(od -An -tx1 -N 5 "$BINARY" | tr -d '[:space:]')
if [ "$elf_header" != "7f454c4601" ]; then
    printf 'ERROR: expected ELF32 binary, got ELF header %s.\n' "$elf_header" >&2
    exit 1
fi

printf 'ELF32 helper verified.\n'
"$BINARY" "$1" "$2"
printf '32-bit compat ioctl regression passed for %s.\n' "$1"
