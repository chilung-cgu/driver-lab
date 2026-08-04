#!/usr/bin/env python3
from __future__ import annotations

import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/quality.yml"

MAPPING = {
    "docs/guides/lab-04-walkthrough.md": "docs/guides/lab-04-study-order.md",
    "docs/guides/learning-roadmap.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/00-to-01-debugfs-bridge.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/01-to-03-user-kernel-abi-bridge.md": "docs/onboarding/kernel-interfaces.md",
    "docs/onboarding/03-to-05-concurrency-pci-bridge.md": "docs/concepts/concurrency-primer.md",
    "docs/onboarding/05-to-07-pci-irq-dma-bridge.md": "docs/concepts/pcie-primer.md",
    "docs/onboarding/07-to-09-runtime-validation-bridge.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/beginner-glossary.md": "docs/reference/glossary.md",
    "docs/onboarding/beginner-primer.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/check-kernel-env-explained.md": "docs/onboarding/linux-environment.md",
    "docs/onboarding/kernel-api-parameter-roles.md": "docs/onboarding/kernel-interfaces.md",
    "docs/onboarding/kernel-filesystem-surfaces.md": "docs/onboarding/kernel-interfaces.md",
    "docs/onboarding/lab-file-roles.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/lab-transition-map.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/learning-dashboard.md": "docs/onboarding/START-HERE.md",
    "docs/onboarding/linux-host-setup.md": "docs/onboarding/linux-environment.md",
    "docs/onboarding/reading-map.md": "docs/onboarding/START-HERE.md",
    "docs/reference/code-reading-guide.md": "docs/reference/debugging.md",
    "docs/reference/common-failures.md": "docs/reference/debugging.md",
    "docs/reference/companion-docs-index.md": "docs/reference/companion-docs.md",
    "docs/reference/companion-docs-rollout-plan.md": "docs/reference/companion-docs.md",
    "docs/reference/debugging-playbook.md": "docs/reference/debugging.md",
}

LINK_RE = re.compile(r"(?<!!)\[([^\]]*)\]\(([^)]+)\)")

CHECKER = r'''#!/usr/bin/env python3
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
'''


def split_destination(raw: str) -> tuple[str, str, str]:
    destination = raw.strip()
    title = ""
    if destination.startswith("<") and ">" in destination:
        end = destination.index(">")
        core = destination[1:end]
        title = destination[end + 1 :]
    else:
        pieces = destination.split(maxsplit=1)
        core = pieces[0] if pieces else ""
        title = f" {pieces[1]}" if len(pieces) == 2 else ""
    path, marker, anchor = core.partition("#")
    return path, anchor if marker else "", title


def repo_relative(source: Path, link_path: str) -> str | None:
    if not link_path or link_path.startswith(("http://", "https://", "mailto:", "tel:", "data:", "/")):
        return None
    target = (source.parent / link_path).resolve()
    try:
        return target.relative_to(ROOT.resolve()).as_posix()
    except ValueError:
        return None


def relative_link(source: Path, target_repo_path: str) -> str:
    value = os.path.relpath(ROOT / target_repo_path, start=source.parent)
    return Path(value).as_posix()


def rewrite_markdown_links(source: Path, text: str) -> tuple[str, int]:
    changes = 0

    def replace(match: re.Match[str]) -> str:
        nonlocal changes
        label, raw = match.group(1), match.group(2)
        link_path, anchor, title = split_destination(raw)
        resolved = repo_relative(source, link_path)
        old_key = resolved if resolved in MAPPING else None
        if old_key is None:
            # Some generated docs contain stale basename-only references whose
            # original relative path no longer resolves.  Basenames are unique.
            candidates = [key for key in MAPPING if Path(key).name == Path(link_path).name]
            if len(candidates) == 1:
                old_key = candidates[0]
        if old_key is None:
            return match.group(0)
        new_path = relative_link(source, MAPPING[old_key])
        # Old anchors generally do not have a stable one-to-one replacement.
        # Link to the canonical document entry instead of inventing an anchor.
        changes += 1
        return f"[{label}]({new_path}{title})"

    return LINK_RE.sub(replace, text), changes


def rewrite_plain_references(text: str) -> tuple[str, int]:
    changes = 0
    for old, new in MAPPING.items():
        old_name = Path(old).name
        replacements = (
            (f"`{old}`", f"`{new}`"),
            (f"`{old_name}`", f"`{new}`"),
            (old, new),
        )
        for before, after in replacements:
            count = text.count(before)
            if count:
                text = text.replace(before, after)
                changes += count
    return text, changes


def update_workflow() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    step = (
        "      - name: Check all Markdown links and superseded references\n"
        "        run: python3 scripts/check_docs_links_and_references.py\n\n"
    )
    if "scripts/check_docs_links_and_references.py" not in text:
        target = (
            "      - name: Check canonical documentation architecture\n"
            "        run: python3 scripts/check_docs_architecture.py\n\n"
        )
        if target not in text:
            raise RuntimeError("documentation architecture workflow step not found")
        text = text.replace(target, target + step, 1)
    WORKFLOW.write_text(text, encoding="utf-8")


def main() -> None:
    markdown_files = sorted(ROOT.rglob("*.md"))
    link_changes = 0
    plain_changes = 0
    changed_files = 0

    for source in markdown_files:
        text = source.read_text(encoding="utf-8")
        rewritten, changed_links = rewrite_markdown_links(source, text)
        rewritten, changed_plain = rewrite_plain_references(rewritten)
        if rewritten != text:
            source.write_text(rewritten, encoding="utf-8")
            changed_files += 1
        link_changes += changed_links
        plain_changes += changed_plain

    checker = ROOT / "scripts/check_docs_links_and_references.py"
    checker.write_text(CHECKER.rstrip() + "\n", encoding="utf-8")
    update_workflow()

    report = ROOT / "docs/pedagogy/link-audit-2026-08.md"
    report.write_text(
        "# Markdown link audit — 2026-08\n\n"
        "## 結論\n\n"
        "本次audit掃描repository中所有剩餘Markdown文件，重新導向已被集中化取代的文件參照，"
        "並新增永久CI，阻止broken local links、missing anchors與superseded filenames回歸。\n\n"
        "## 執行範圍\n\n"
        f"- 掃描Markdown files：{len(markdown_files)}\n"
        f"- 自動重寫link destinations：{link_changes}\n"
        f"- 自動重寫plain／code-span references：{plain_changes}\n"
        f"- 內容有變動的Markdown files：{changed_files}\n"
        "- Superseded document set：22份，由canonical documentation architecture管理。\n\n"
        "## 驗證\n\n"
        "`scripts/check_docs_links_and_references.py`會檢查所有local Markdown targets與anchors，"
        "並掃描普通文字中的舊檔名。外部HTTP links不在此checker的可用性保證範圍。\n\n"
        "此audit只證明文件導航完整，不證明教材technical semantics或runtime。\n",
        encoding="utf-8",
    )
    print(
        "documentation link audit staged: "
        f"files={len(markdown_files)} link_changes={link_changes} "
        f"plain_changes={plain_changes} changed_files={changed_files}"
    )


if __name__ == "__main__":
    main()
