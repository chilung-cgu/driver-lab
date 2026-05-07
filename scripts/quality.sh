#!/bin/sh
set -eu

# 這個腳本刻意保持簡單：
# 先做語法檢查，再做可選的靜態檢查，最後在有 kernel tree 時跑 checkpatch。

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TARGET_DIR=${1:-$ROOT_DIR}
KERNEL_TREE=${KERNEL_TREE:-}

warn() {
    printf 'WARN: %s\n' "$*" >&2
}

run_shell_syntax_checks() {
    find "$TARGET_DIR" -type f -name '*.sh' | while IFS= read -r file; do
        printf 'sh -n %s\n' "$file"
        sh -n "$file"
    done
}

run_shellcheck() {
    if ! command -v shellcheck >/dev/null 2>&1; then
        warn "shellcheck 不存在，略過 shell 靜態檢查。"
        return 0
    fi

    find "$TARGET_DIR" -type f -name '*.sh' | while IFS= read -r file; do
        printf 'shellcheck %s\n' "$file"
        shellcheck "$file"
    done
}

locate_checkpatch() {
    if [ -n "$KERNEL_TREE" ] && [ -x "$KERNEL_TREE/scripts/checkpatch.pl" ]; then
        printf '%s\n' "$KERNEL_TREE/scripts/checkpatch.pl"
        return 0
    fi

    if [ "$(uname -s)" = "Linux" ] && [ -x "/lib/modules/$(uname -r)/build/scripts/checkpatch.pl" ]; then
        printf '%s\n' "/lib/modules/$(uname -r)/build/scripts/checkpatch.pl"
        return 0
    fi

    return 1
}

run_checkpatch() {
    checkpatch=

    if checkpatch=$(locate_checkpatch); then
        find "$TARGET_DIR" -type f -name '*.c' | while IFS= read -r file; do
            printf 'checkpatch %s\n' "$file"
            perl "$checkpatch" --no-tree -f "$file"
        done
    else
        warn "找不到 checkpatch.pl，略過 C 風格檢查。設定 KERNEL_TREE 可指定 kernel source tree。"
    fi
}

run_shell_syntax_checks
run_shellcheck
run_checkpatch

printf 'quality checks completed for %s\n' "$TARGET_DIR"
