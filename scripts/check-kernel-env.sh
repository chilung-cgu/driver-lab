#!/bin/sh
set -eu

# KDIR 是目前執行中的 kernel 所對應的 build 目錄。
# 外掛 module 透過 kbuild 建置時，會用到這個位置。

info() {
    printf '%s\n' "$*"
}

warn() {
    printf 'WARN: %s\n' "$*" >&2
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

if [ "$(uname -s)" != "Linux" ]; then
    fail "這個腳本必須在 Linux 主機上執行。"
fi

KDIR="/lib/modules/$(uname -r)/build"

info "Kernel version (uname -r): $(uname -r)"
info "Kernel build tree (KDIR): $KDIR"

[ -d "$KDIR" ] || fail "找不到 kernel build tree: $KDIR"

for tool in make gcc git; do
    if command -v "$tool" >/dev/null 2>&1; then
        info "OK: found $tool at $(command -v "$tool")"
    else
        warn "missing tool: $tool"
    fi
done

if grep -qs ' /sys/kernel/debug ' /proc/mounts; then
    info "OK: debugfs is mounted"
else
    warn "debugfs 尚未掛載，可執行 ./scripts/mount-debugfs.sh"
fi

if command -v mokutil >/dev/null 2>&1; then
    info "Secure Boot state:"
    mokutil --sb-state || true
else
    warn "mokutil 不存在，若 module 載入失敗，請額外檢查 Secure Boot / module signature 設定。"
fi

if [ -r /proc/sys/kernel/tainted ]; then
    taint_value=$(cat /proc/sys/kernel/tainted)
    info "Current taint value: $taint_value"
    if [ "$taint_value" = "0" ]; then
        info "Taint summary: kernel is currently clean/untainted"
    else
        warn "kernel taint is non-zero; see docs/onboarding/linux-environment.md before debugging strange failures."
    fi
fi

info "Environment check completed."
