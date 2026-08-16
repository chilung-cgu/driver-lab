#!/usr/bin/env python3
"""Validate all local Markdown links, anchors, and superseded references.

This checker covers every remaining Markdown file, including READMEs and source
companions. It checks documentation integrity only, not technical semantics.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORT = "docs/pedagogy/link-audit-2026-08.md"

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

LINK_RE = re.compile(r"(?<!!)\[([^\]]*)\]\(([^)]+)\)")
HEADING_RE = re.compile(r"^(#{1,6})\s+(.+)$")
EXTERNAL_PREFIXES = ("http://", "https://", "mailto:", "tel:", "data:")


def split_destination(raw: str) -> tuple[str, str]:
    destination = raw.strip()
    if destination.startswith("<") and ">" in destination:
        end = destination.index(">")
        destination = destination[1:end]
    else:
        # Repository paths do not intentionally contain spaces.  Ignore an
        # optional Markdown title after the first whitespace.
        destination = destination.split(maxsplit=1)[0] if destination else ""
    path, marker, anchor = destination.partition("#")
    return path, anchor if marker else ""


def slugify(text: str) -> str:
    text = re.sub(r"<[^>]+>", "", text)
    text = text.replace("`", "").strip().lower()
    chars: list[str] = []
    for char in text:
        if char.isalnum() or char in {" ", "-", "_"}:
            chars.append(char)
    slug = "".join(chars).replace(" ", "-")
    while "--" in slug:
        slug = slug.replace("--", "-")
    return slug.strip("-")


def anchors(path: Path) -> set[str]:
    seen: dict[str, int] = {}
    result: set[str] = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        match = HEADING_RE.match(line)
        if not match:
            continue
        base = slugify(match.group(2))
        if not base:
            continue
        count = seen.get(base, 0)
        value = base if count == 0 else f"{base}-{count}"
        seen[base] = count + 1
        result.add(value)
    return result


def main() -> int:
    errors: list[str] = []
    link_count = 0
    markdown_files = sorted(ROOT.rglob("*.md"))
    anchor_cache: dict[Path, set[str]] = {}
    superseded_names = {Path(path).name for path in SUPERSEDED}

    for source in markdown_files:
        relative_source = source.relative_to(ROOT).as_posix()
        text = source.read_text(encoding="utf-8")

        if relative_source != REPORT:
            for obsolete in SUPERSEDED:
                if obsolete in text:
                    errors.append(
                        f"{relative_source}: references superseded path {obsolete}"
                    )
            for name in superseded_names:
                if name in text:
                    errors.append(
                        f"{relative_source}: references superseded filename {name}"
                    )

        for match in LINK_RE.finditer(text):
            raw = match.group(2)
            path_part, anchor = split_destination(raw)
            if not path_part and not anchor:
                continue
            if path_part.startswith(EXTERNAL_PREFIXES):
                continue
            link_count += 1

            if path_part.startswith("/"):
                errors.append(
                    f"{relative_source}: absolute local link is not allowed: {raw}"
                )
                continue

            target = source if not path_part else (source.parent / path_part).resolve()
            try:
                target.relative_to(ROOT.resolve())
            except ValueError:
                errors.append(f"{relative_source}: link escapes repo root: {raw}")
                continue

            if not target.exists():
                errors.append(f"{relative_source}: missing local link target: {raw}")
                continue

            if anchor and target.is_file() and target.suffix.lower() == ".md":
                available = anchor_cache.setdefault(target, anchors(target))
                if anchor not in available:
                    errors.append(
                        f"{relative_source}: missing anchor {anchor!r} in {raw}"
                    )

    if errors:
        print("Markdown link/reference audit failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(
        "Markdown link/reference audit passed: "
        f"files={len(markdown_files)} local_links={link_count} "
        f"superseded_names={len(SUPERSEDED)}"
    )
    print("note: link integrity does not prove document technical correctness")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
