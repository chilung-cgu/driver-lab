#!/bin/sh
set -eu

# 先做 shell 語法與靜態檢查、Markdown local-link 檢查，最後在有
# kernel tree 時對 kernel-facing C/H source 跑 checkpatch。
# Userspace runtime/tests 由 -Wall -Wextra -Werror 的 build gate 檢查；
# checkpatch 的 kernel-only規則（例如反對volatile）不適合直接套用。
# 這些都只是 static gates，不等於 module runtime test。

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

        # SC1007: repository scripts intentionally use the POSIX
        #   CDPATH= cd -- <dir>
        # idiom. New/edited scripts should prefer CDPATH='' for clarity, but do
        # not hide unrelated diagnostics solely to normalize older files.
        #
        # Scripts sourcing fs-surface-checks.sh intentionally set FS_SUDO for
        # the sourced helper. Because the source path is constructed from
        # ROOT_DIR, ShellCheck cannot follow it statically and reports SC1091
        # plus SC2034. Suppress those two only for the known cross-file pattern.
        if grep -q 'fs-surface-checks\.sh' "$file"; then
            shellcheck -e SC1007,SC1091,SC2034 "$file"
        else
            shellcheck -e SC1007 "$file"
        fi
    done
}

run_markdown_link_checks() {
    if ! command -v python3 >/dev/null 2>&1; then
        warn "python3 不存在，略過 Markdown 連結檢查。"
        return 0
    fi

    python3 - "$TARGET_DIR" "$ROOT_DIR" <<'PY'
import pathlib
import re
import sys

target_dir = pathlib.Path(sys.argv[1]).resolve()
root_dir = pathlib.Path(sys.argv[2]).resolve()

link_re = re.compile(r'(?<!!)\[[^\]]+\]\(([^)]+)\)')
heading_re = re.compile(r'^(#{1,6})\s+(.*)$')
skip_prefixes = ('http://', 'https://', 'mailto:', 'tel:', 'data:')


def slugify(text: str) -> str:
    text = text.strip().lower().replace('`', '')
    cleaned = []
    for ch in text:
        if ch.isalnum() or ch in {' ', '-', '_'}:
            cleaned.append(ch)
    slug = ''.join(cleaned).replace(' ', '-')
    while '--' in slug:
        slug = slug.replace('--', '-')
    return slug.strip('-')


def collect_anchors(path: pathlib.Path) -> set[str]:
    seen: dict[str, int] = {}
    anchors: set[str] = set()

    try:
        lines = path.read_text(encoding='utf-8').splitlines()
    except UnicodeDecodeError:
        lines = path.read_text().splitlines()

    for line in lines:
        match = heading_re.match(line)
        if not match:
            continue

        base = slugify(match.group(2))
        if not base:
            continue

        count = seen.get(base, 0)
        anchor = base if count == 0 else f"{base}-{count}"
        seen[base] = count + 1
        anchors.add(anchor)

    return anchors


errors: list[str] = []
anchor_cache: dict[pathlib.Path, set[str]] = {}

for markdown_file in sorted(target_dir.rglob('*.md')):
    try:
        content = markdown_file.read_text(encoding='utf-8')
    except UnicodeDecodeError:
        content = markdown_file.read_text()

    for match in link_re.finditer(content):
        raw_dest = match.group(1).strip()
        if not raw_dest:
            continue

        if raw_dest.startswith('<') and raw_dest.endswith('>'):
            raw_dest = raw_dest[1:-1].strip()

        if raw_dest.startswith(skip_prefixes):
            continue

        if raw_dest.startswith('/'):
            errors.append(f"{markdown_file}: absolute local link is not allowed: {raw_dest}")
            continue

        if raw_dest.startswith('#'):
            target_path = markdown_file
            anchor = raw_dest[1:]
        else:
            path_part, _, anchor = raw_dest.partition('#')
            target_path = (markdown_file.parent / path_part).resolve()

            if not target_path.exists():
                errors.append(f"{markdown_file}: missing link target: {raw_dest}")
                continue

            if root_dir not in target_path.parents and target_path != root_dir:
                errors.append(f"{markdown_file}: link escapes repo root: {raw_dest}")
                continue

        if anchor and target_path.suffix.lower() == '.md':
            anchors = anchor_cache.setdefault(target_path, collect_anchors(target_path))
            if anchor not in anchors:
                errors.append(f"{markdown_file}: missing anchor '{anchor}' in {raw_dest}")

if errors:
    for err in errors:
        print(f"ERROR: {err}", file=sys.stderr)
    sys.exit(1)
PY
}

run_superseded_doc_link_checks() {
    if ! command -v python3 >/dev/null 2>&1; then
        warn "python3 不存在，略過 superseded documentation link 檢查。"
        return 0
    fi

    python3 "$ROOT_DIR/scripts/check_superseded_doc_links.py"
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
        find "$TARGET_DIR" -type f \( -name '*.c' -o -name '*.h' \) \
            ! -name '*.mod.c' \
            ! -name '.module-common.c' \
            ! -path '*/.tmp_versions/*' | while IFS= read -r file; do
            case "$file" in
                */runtime/src/*|*/tests/*)
                    printf 'skip checkpatch for userspace source %s\n' "$file"
                    continue
                    ;;
            esac

            printf 'checkpatch %s\n' "$file"
            perl "$checkpatch" --no-tree -f "$file"
        done
    else
        warn "找不到 checkpatch.pl，略過 C 風格檢查。設定 KERNEL_TREE 可指定 kernel source tree。"
    fi
}

run_shell_syntax_checks
run_shellcheck
run_markdown_link_checks
run_superseded_doc_link_checks
run_checkpatch

printf 'quality checks completed for %s\n' "$TARGET_DIR"
