#!/usr/bin/env python3
"""Check minimum structure for pedagogy-migrated driver-lab documents.

Passing this script proves only that required teaching sections exist. It does
not prove semantic correctness, successful module runtime, or hardware safety.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST = REPO_ROOT / "docs/pedagogy/migrated-docs.txt"
EXPECTED_DOCUMENTS = 11

REQUIRED_SECTIONS = (
    "## 先講結論",
    "## 不確定處與驗證狀態",
    "## 這一關要解決什麼問題",
    "## 名詞先說清楚",
    "## 心智模型",
    "## Resource 與 data flow",
    "## 從簡單到精確",
    "## 最小正確範式",
    "## 看似合理但錯誤的寫法",
    "## 如何執行與觀察",
    "## Debug order",
    "## 工具分工",
    "## 與 pcie-study 的對應",
    "## 常見誤解",
    "## 適用邊界與尚未驗證",
    "## 第一次閱讀先記住",
    "## Self-check",
    "## 來源與查證",
)

FORBIDDEN_PLACEHOLDERS = ("TODO", "TBD", "FIXME", "待補", "待寫")
PRIMARY_SOURCE_MARKERS = (
    "https://docs.kernel.org/",
    "https://www.qemu.org/",
)


def load_manifest() -> list[str]:
    if not MANIFEST.is_file():
        raise FileNotFoundError(f"manifest not found: {MANIFEST}")

    paths: list[str] = []
    for raw_line in MANIFEST.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        paths.append(line)
    return paths


def check_document(relative_path: str) -> list[str]:
    errors: list[str] = []
    document = REPO_ROOT / relative_path
    if not document.is_file():
        return [f"{relative_path}: file does not exist"]

    content = document.read_text(encoding="utf-8")

    for section in REQUIRED_SECTIONS:
        if section not in content:
            errors.append(f"{relative_path}: missing section {section!r}")

    for placeholder in FORBIDDEN_PLACEHOLDERS:
        if placeholder in content:
            errors.append(f"{relative_path}: unresolved placeholder {placeholder!r}")

    if "<details>" not in content or "<summary>參考答案</summary>" not in content:
        errors.append(f"{relative_path}: missing page-local folded reference answers")

    if not any(marker in content for marker in PRIMARY_SOURCE_MARKERS):
        errors.append(f"{relative_path}: missing Linux/QEMU primary documentation link")

    if "Current source" not in content and "current source" not in content:
        errors.append(f"{relative_path}: must identify current source")

    if len(content) < 3_000:
        errors.append(
            f"{relative_path}: unexpectedly short for a migrated teaching document "
            f"({len(content)} characters)"
        )

    return errors


def main() -> int:
    try:
        paths = load_manifest()
    except FileNotFoundError as exc:
        print(f"pedagogy check failed: {exc}", file=sys.stderr)
        return 1

    errors: list[str] = []
    if not paths:
        errors.append("migration manifest has no paths")
    if len(paths) != EXPECTED_DOCUMENTS:
        errors.append(
            f"expected {EXPECTED_DOCUMENTS} migrated documents, found {len(paths)}"
        )
    if len(paths) != len(set(paths)):
        errors.append("migration manifest contains duplicate paths")
    if paths != sorted(paths):
        errors.append("migration manifest paths must be sorted")

    for relative_path in paths:
        errors.extend(check_document(relative_path))

    if errors:
        print("pedagogy structure check failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(f"pedagogy structure check passed for {len(paths)} documents")
    print("note: this checks structure only, not semantic or runtime correctness")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
