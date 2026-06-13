#!/bin/bash
# 這個腳本會動態提取當下 Linux Kernel 環境的編譯參數，生成給 Clangd 使用的 compile_commands.json

KERNEL_BUILD="/lib/modules/$(uname -r)/build"
GEN_TOOL="${KERNEL_BUILD}/scripts/clang-tools/gen_compile_commands.py"
WORKSPACE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ ! -f "$GEN_TOOL" ]; then
    echo "錯誤: 找不到 Linux Kernel 原廠的 gen_compile_commands.py 工具"
    echo "路徑: $GEN_TOOL"
    echo "請確認你有正確安裝 linux-headers-$(uname -r)"
    exit 1
fi

echo "正在捕捉全專案的編譯參數 (動態匹配 kernel header 路徑)..."
# 使用相對或絕對路徑均可捕捉全專案目錄下的 .cmd 隱藏檔
python3 "$GEN_TOOL" -d "$KERNEL_BUILD" -o "$WORKSPACE_DIR/compile_commands.json" "$WORKSPACE_DIR"

if [ $? -eq 0 ]; then
    echo "✅ 成功生成 $WORKSPACE_DIR/compile_commands.json"
    echo "請確保你在各個 lab 目錄下執行過 'make' 產生編譯中介檔 (.cmd)，資訊才會是最完整的！"
else
    echo "❌ 生成失敗"
fi