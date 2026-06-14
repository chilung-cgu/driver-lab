#!/bin/bash
# 這個 wrapper 會保留 kernel 官方 generator，並額外把 repo 內的 userspace source
# 合併進同一份 compile_commands.json，避免 clangd 把 tests/runtime 錯當成 kernel code。

set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "正在捕捉 kernel 與 userspace 的編譯參數..."
python3 "$SCRIPT_DIR/gen_compile_commands.py"
