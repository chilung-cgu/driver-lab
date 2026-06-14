#!/usr/bin/env python3
"""Generate a clangd compile database for both kernel and userspace sources."""

from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path


def find_compiler() -> str:
    for candidate in ("gcc-13", "gcc", "cc", "clang-18", "clang"):
        path = shutil.which(candidate)
        if path:
            return path
    raise FileNotFoundError("找不到可用的 C compiler（gcc/cc/clang）。")


def userspace_entries(root_dir: Path, compiler: str) -> list[dict[str, str]]:
    common_cflags = ["-Wall", "-Wextra", "-Werror"]
    runtime_include = f"-I{root_dir / 'runtime' / 'include'}"

    def entry(file_path: Path, args: list[str]) -> dict[str, str]:
        return {
            "directory": str(root_dir),
            "file": str(file_path),
            "command": " ".join([compiler, *args, "-c", str(file_path)]),
        }

    return [
        entry(
            root_dir / "runtime" / "src" / "driver_lab_runtime.c",
            [*common_cflags, "-std=c11", runtime_include],
        ),
        entry(
            root_dir / "tests" / "driver_lab_char_cli.c",
            [*common_cflags, "-std=c11", runtime_include],
        ),
        entry(
            root_dir / "tests" / "driver_lab_race_cli.c",
            [*common_cflags, "-pthread"],
        ),
    ]


def kernel_entries(root_dir: Path) -> list[dict[str, str]]:
    entries: list[dict[str, str]] = []

    for cmd_path in sorted(root_dir.glob("labs/**/.*.o.cmd")):
        name = cmd_path.name
        if name.endswith(".mod.o.cmd") or name == "..module-common.o.cmd":
            continue

        savedcmd = None
        source = None
        for line in cmd_path.read_text(encoding="utf-8").splitlines():
            if line.startswith("savedcmd_") and " := " in line:
                savedcmd = line.split(" := ", 1)[1].strip()
            elif line.startswith("source_") and " := " in line:
                source = line.split(" := ", 1)[1].strip()

            if savedcmd and source:
                break

        if not savedcmd or not source:
            continue

        source_path = (cmd_path.parent / source).resolve()
        entries.append(
            {
                "directory": str(cmd_path.parent.resolve()),
                "file": str(source_path),
                "command": savedcmd,
            }
        )

    return entries


def main() -> int:
    root_dir = Path(__file__).resolve().parent.parent
    output_path = root_dir / "compile_commands.json"

    compiler = find_compiler()
    merged_entries = kernel_entries(root_dir) + userspace_entries(root_dir, compiler)
    deduped_by_file: dict[str, dict[str, str]] = {}
    for item in merged_entries:
        deduped_by_file[item["file"]] = item

    final_entries = sorted(deduped_by_file.values(), key=lambda item: item["file"])
    output_path.write_text(
        json.dumps(final_entries, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    print(f"✅ 成功生成 {output_path}")
    print("已合併 kernel module 與 userspace runtime/tests 的編譯參數。")
    print("提醒: kernel 來源檔仍需要先在各 lab 目錄執行過 make，這支腳本才抓得到完整 .cmd。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
