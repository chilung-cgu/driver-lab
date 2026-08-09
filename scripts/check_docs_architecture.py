#!/usr/bin/env python3
"""Validate the canonical learner-facing documentation graph.

This checker prevents the documentation tree from drifting back toward many
competing roadmaps, bridge files, and debugging indexes. It checks only file
architecture and minimum structure; it does not prove technical correctness.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/pedagogy/canonical-docs.txt"

EXPECTED_CANONICAL = (
    "README.md",
    "docs/README.md",
    "docs/PEDAGOGY-PASS-2026-08.md",
    "docs/TEACHING-QUALITY-STANDARD.md",
    "docs/concepts/accelerator-driver-architecture.md",
    "docs/concepts/concurrency-primer.md",
    "docs/concepts/pcie-primer.md",
    "docs/guides/lab-04-study-order.md",
    "docs/guides/lab-05-study-order.md",
    "docs/guides/linux-guest-05-to-07-checklist.md",
    "docs/guides/linux-guest-05-to-07-walkthrough.md",
    "docs/guides/qemu-edu-first-pass.md",
    "docs/onboarding/START-HERE.md",
    "docs/onboarding/kernel-interfaces.md",
    "docs/onboarding/linux-environment.md",
    "docs/reference/accuracy-audit-2026-08.md",
    "docs/reference/companion-docs.md",
    "docs/reference/debugging.md",
    "docs/reference/glossary.md",
    "docs/reference/source-index.md",
)

SUPERSEDED = (
    "docs/guides/lab-04-walkthrough.md",
    "docs/guides/learning-roadmap.md",
    "docs/onboarding/00-to-01-debugfs-bridge.md",
    "docs/onboarding/01-to-03-user-kernel-abi-bridge.md",
    "docs/onboarding/03-to-05-concurrency-pci-bridge.md",
    "docs/onboarding/05-to-07-pci-irq-dma-bridge.md",
    "docs/onboarding/07-to-09-runtime-validation-bridge.md",
    "docs/onboarding/beginner-glossary.md",
    "docs/onboarding/beginner-primer.md",
    "docs/onboarding/check-kernel-env-explained.md",
    "docs/onboarding/kernel-api-parameter-roles.md",
    "docs/onboarding/kernel-filesystem-surfaces.md",
    "docs/onboarding/lab-file-roles.md",
    "docs/onboarding/lab-transition-map.md",
    "docs/onboarding/learning-dashboard.md",
    "docs/onboarding/linux-host-setup.md",
    "docs/onboarding/reading-map.md",
    "docs/reference/code-reading-guide.md",
    "docs/reference/common-failures.md",
    "docs/reference/companion-docs-index.md",
    "docs/reference/companion-docs-rollout-plan.md",
    "docs/reference/debugging-playbook.md",
)

REQUIRED_ENTRY_SECTIONS: dict[str, tuple[str, ...]] = {
    "README.md": (
        "## 先講結論",
        "## 不確定處與驗證狀態",
        "## 學習路線",
        "## Runtime gates",
        "## Docs architecture",
    ),
    "docs/README.md": (
        "## 先講結論",
        "## 新手入口",
        "## 核心 concepts",
        "## Reference",
        "## Authority order",
    ),
    "docs/onboarding/START-HERE.md": (
        "## 先講結論",
        "## 不確定處與驗證狀態",
        "## 十關學習路線與前進 gate",
        "## Evidence 分級",
        "## 完成一關的最低標準",
    ),
    "docs/onboarding/linux-environment.md": (
        "## 先講結論",
        "## 第一個 gate",
        "## QEMU EDU gate",
        "## Self-check",
        "## 來源與查證",
    ),
    "docs/onboarding/kernel-interfaces.md": (
        "## 先講結論",
        "## Filesystem surfaces 分工",
        "## API 參數固定分類",
        "## Resource 與 lifetime 讀法",
        "## Self-check",
    ),
    "docs/concepts/concurrency-primer.md": (
        "## 先講結論",
        "## 名詞先說清楚",
        "## 工具分工",
        "## Lab 對應",
        "## Self-check",
    ),
    "docs/concepts/accelerator-driver-architecture.md": (
        "## 先講結論",
        "## 典型元件與責任",
        "## Labs 對映",
        "## Resource / state machine 表",
        "## Self-check",
    ),
    "docs/reference/debugging.md": (
        "## 先講結論",
        "## 分層排查表",
        "## Evidence 原則",
        "## Bug diary 模板",
        "## Self-check",
    ),
    "docs/reference/companion-docs.md": (
        "## 先講結論",
        "## Authority order",
        "## Reviewed companion 的最低條件",
        "## 目前策略",
        "## 來源與查證",
    ),
    "docs/reference/glossary.md": (
        "## 先講結論",
        "## Module / build / logging",
        "## Execution context / concurrency / lifetime",
        "## PCIe / BAR / MMIO",
        "## DMA / IOMMU / queue",
        "## 來源與查證",
    ),
}

FORBIDDEN_PLACEHOLDERS = ("TODO", "TBD", "FIXME", "待補", "待寫")


def load_manifest() -> list[str]:
    paths: list[str] = []
    for raw in MANIFEST.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        paths.append(line)
    return paths


def main() -> int:
    errors: list[str] = []

    if not MANIFEST.is_file():
        errors.append(f"missing canonical manifest: {MANIFEST.relative_to(ROOT)}")
        paths: list[str] = []
    else:
        paths = load_manifest()

    expected_set = set(EXPECTED_CANONICAL)
    path_set = set(paths)
    if len(paths) != len(path_set):
        errors.append("canonical manifest contains duplicate paths")
    if path_set != expected_set:
        errors.append(
            "canonical manifest must contain exactly the expected path set; "
            f"expected={len(expected_set)} found={len(path_set)}"
        )
        for item in sorted(expected_set - path_set):
            errors.append(f"manifest missing canonical path: {item}")
        for item in sorted(path_set - expected_set):
            errors.append(f"manifest has unexpected path: {item}")

    for relative in EXPECTED_CANONICAL:
        path = ROOT / relative
        if not path.is_file():
            errors.append(f"missing canonical document: {relative}")
            continue
        content = path.read_text(encoding="utf-8")
        for placeholder in FORBIDDEN_PLACEHOLDERS:
            if placeholder in content:
                errors.append(f"{relative}: unresolved placeholder {placeholder!r}")
        if relative in REQUIRED_ENTRY_SECTIONS:
            for section in REQUIRED_ENTRY_SECTIONS[relative]:
                if section not in content:
                    errors.append(f"{relative}: missing section {section!r}")
            if len(content) < 2_000:
                errors.append(
                    f"{relative}: unexpectedly short canonical entry ({len(content)} chars)"
                )

    for relative in SUPERSEDED:
        if (ROOT / relative).exists():
            errors.append(f"superseded document still exists: {relative}")

    if errors:
        print("documentation architecture check failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(
        "documentation architecture check passed: "
        f"canonical={len(EXPECTED_CANONICAL)} superseded_absent={len(SUPERSEDED)}"
    )
    print("note: this checks document architecture, not semantic/runtime correctness")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
