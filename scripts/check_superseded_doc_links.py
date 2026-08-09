#!/usr/bin/env python3
"""Reject Markdown links that point to consolidated-away documentation.

The repository's general Markdown link checker already verifies that local link
and anchor targets exist. This complementary checker covers a different failure
mode: an old learner-facing path being reintroduced, including as an absolute
GitHub URL that a local-only link checker would normally skip.

Passing this script proves only link-architecture consistency. It does not prove
the linked document is technically correct or pedagogically sufficient.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path, PurePosixPath
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parents[1]

SUPERSEDED = frozenset(
    {
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
    }
)

# Exclude images: this checker is about learner navigation links, while the
# repository's existing link checker handles local image/file existence.
LINK_RE = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
GITHUB_REPO_PREFIXES = (
    "/chilung-cgu/driver-lab/blob/",
    "/chilung-cgu/driver-lab/tree/",
)


def strip_wrapping(destination: str) -> str:
    destination = destination.strip()
    if destination.startswith("<") and destination.endswith(">"):
        return destination[1:-1].strip()
    return destination


def github_repo_path(destination: str) -> str | None:
    parsed = urlparse(destination)
    if parsed.netloc.lower() not in {"github.com", "www.github.com"}:
        return None

    decoded_path = unquote(parsed.path)
    for prefix in GITHUB_REPO_PREFIXES:
        if decoded_path.startswith(prefix):
            remainder = decoded_path[len(prefix) :]
            # Remainder is <ref>/<repo-relative-path>. Branch names may contain
            # slashes, so search for the first known docs root rather than
            # assuming the ref occupies exactly one path component.
            marker = "/docs/"
            marker_index = remainder.find(marker)
            if marker_index >= 0:
                return remainder[marker_index + 1 :]
            return None
    return None


def local_repo_path(markdown_file: Path, destination: str) -> str | None:
    parsed = urlparse(destination)
    if parsed.scheme or parsed.netloc:
        return None

    path_part = unquote(destination.split("#", 1)[0].split("?", 1)[0]).strip()
    if not path_part:
        return None

    target = (markdown_file.parent / path_part).resolve()
    try:
        return target.relative_to(ROOT).as_posix()
    except ValueError:
        return None


def normalized_destination(markdown_file: Path, destination: str) -> str | None:
    destination = strip_wrapping(destination)
    if not destination or destination.startswith(("#", "mailto:", "tel:", "data:")):
        return None

    github_path = github_repo_path(destination)
    if github_path is not None:
        return PurePosixPath(github_path).as_posix()

    return local_repo_path(markdown_file, destination)


def main() -> int:
    errors: list[str] = []
    markdown_count = 0
    link_count = 0

    for markdown_file in sorted(ROOT.rglob("*.md")):
        markdown_count += 1
        content = markdown_file.read_text(encoding="utf-8", errors="replace")
        for match in LINK_RE.finditer(content):
            link_count += 1
            raw_destination = match.group(1)
            repo_path = normalized_destination(markdown_file, raw_destination)
            if repo_path in SUPERSEDED:
                relative_source = markdown_file.relative_to(ROOT).as_posix()
                errors.append(
                    f"{relative_source}: link {raw_destination!r} targets "
                    f"superseded document {repo_path!r}"
                )

    for relative in sorted(SUPERSEDED):
        if (ROOT / relative).exists():
            errors.append(f"superseded document unexpectedly exists: {relative}")

    if errors:
        print("superseded-document link check failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(
        "superseded-document link check passed: "
        f"markdown_files={markdown_count} links={link_count} "
        f"superseded_paths={len(SUPERSEDED)}"
    )
    print("note: general local-link and anchor validity is checked separately")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
