#!/bin/sh
set -eu

# 使用 repo 內建 hooks，讓每個 clone 都共用同一套最小保護機制。

REPO_ROOT=$(git rev-parse --show-toplevel)
HOOKS_PATH="$REPO_ROOT/.githooks"

if [ ! -d "$HOOKS_PATH" ]; then
    printf 'ERROR: missing hooks directory: %s\n' "$HOOKS_PATH" >&2
    exit 1
fi

chmod +x "$HOOKS_PATH"/pre-commit "$HOOKS_PATH"/commit-msg
git config core.hooksPath "$HOOKS_PATH"

printf 'Configured core.hooksPath=%s\n' "$HOOKS_PATH"
