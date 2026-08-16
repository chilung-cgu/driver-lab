#!/bin/sh

# Source this helper after setting DMESG_GATE_SUDO to either sudo or an empty
# string. It writes one marker and inspects only messages that follow it.

DMESG_GATE_LOG=
DMESG_GATE_ALL=
DMESG_GATE_MARKER=
DMESG_GATE_STARTED=0

dmesg_gate_sudo() {
    if [ -n "${DMESG_GATE_SUDO:-}" ]; then
        "$DMESG_GATE_SUDO" "$@"
    else
        "$@"
    fi
}

dmesg_gate_cleanup() {
    if [ -n "${DMESG_GATE_LOG:-}" ]; then
        rm -f "$DMESG_GATE_LOG" || true
    fi
    if [ -n "${DMESG_GATE_ALL:-}" ]; then
        rm -f "$DMESG_GATE_ALL" || true
    fi
    DMESG_GATE_LOG=
    DMESG_GATE_ALL=
    DMESG_GATE_MARKER=
    DMESG_GATE_STARTED=0
}

dmesg_gate_begin() {
    dmesg_gate_label=$1

    dmesg_gate_cleanup
    DMESG_GATE_LOG=$(mktemp) || return 1
    DMESG_GATE_ALL=$(mktemp) || {
        rm -f "$DMESG_GATE_LOG"
        DMESG_GATE_LOG=
        return 1
    }
    DMESG_GATE_MARKER="driver-lab-lab09: ${dmesg_gate_label} marker pid=$$ epoch=$(date +%s)"

    if ! printf '%s\n' "$DMESG_GATE_MARKER" |
        dmesg_gate_sudo tee /dev/kmsg >/dev/null; then
        printf 'ERROR: could not write kernel-log marker.\n' >&2
        dmesg_gate_cleanup
        return 1
    fi

    DMESG_GATE_STARTED=1
}

dmesg_gate_finish() {
    if [ "$DMESG_GATE_STARTED" -ne 1 ]; then
        printf 'ERROR: kernel-log gate was not started.\n' >&2
        return 1
    fi

    if ! dmesg_gate_sudo dmesg >"$DMESG_GATE_ALL"; then
        printf 'ERROR: could not read kernel log for this test run.\n' >&2
        return 1
    fi
    if ! grep -Fq "$DMESG_GATE_MARKER" "$DMESG_GATE_ALL"; then
        printf 'ERROR: kernel-log marker was lost; cannot isolate this test run.\n' >&2
        return 1
    fi
    if ! awk -v marker="$DMESG_GATE_MARKER" '
        index($0, marker) { capture = 1; next }
        capture { print }
    ' "$DMESG_GATE_ALL" >"$DMESG_GATE_LOG"; then
        printf 'ERROR: could not isolate this test run in the kernel log.\n' >&2
        return 1
    fi

    printf 'INFO: kernel-log messages after marker follow.\n'
    if ! cat "$DMESG_GATE_LOG"; then
        printf 'ERROR: could not display this test run kernel log.\n' >&2
        return 1
    fi
    if grep -Eq 'BUG:|WARNING:|KASAN:|KCSAN:|Oops:|use-after-free|general protection fault' \
        "$DMESG_GATE_LOG"; then
        printf 'ERROR: kernel warning or sanitizer report in this test run.\n' >&2
        return 1
    fi
    return 0
}

dmesg_gate_check_and_cleanup() {
    dmesg_gate_status=0

    dmesg_gate_finish || dmesg_gate_status=$?
    dmesg_gate_cleanup
    return "$dmesg_gate_status"
}
