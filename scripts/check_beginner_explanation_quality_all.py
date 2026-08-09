#!/usr/bin/env python3
"""Regression guard for beginner-explained v2 canonical learner documents."""
from pathlib import Path
import hashlib
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/pedagogy/beginner-explained-v2-docs.txt"
GUIDE = "## 初學者導讀：先把整條流程看懂"
H3 = re.compile(r"^###\s+(.+)$", re.MULTILINE)
BULLET = re.compile(r"^\s*(?:[-*]|\d+[.)])\s+", re.MULTILINE)


def prose(value: str) -> str:
    value = re.sub(r"```.*?```", " ", value, flags=re.DOTALL)
    value = re.sub(r"^\s*\|.*\|\s*$", " ", value, flags=re.MULTILINE)
    value = re.sub(
        r"^\s*(?:[-*]|\d+[.)])\s+.*$", " ", value, flags=re.MULTILINE
    )
    value = re.sub(r"[`*_>#]", " ", value)
    return re.sub(r"\s+", " ", value).strip()


def main() -> int:
    paths = [
        line.strip()
        for line in MANIFEST.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.startswith("#")
    ]
    errors: list[str] = []
    if len(paths) != 11:
        errors.append(f"expected 11 canonical learner docs, found {len(paths)}")
    if paths != sorted(paths) or len(paths) != len(set(paths)):
        errors.append("manifest must be sorted and unique")

    guide_hashes: dict[str, str] = {}
    for relative in paths:
        path = ROOT / relative
        if not path.is_file():
            errors.append(f"{relative}: missing")
            continue
        text = path.read_text(encoding="utf-8")
        if GUIDE not in text:
            errors.append(f"{relative}: missing beginner guide")
            continue
        start = text.index(GUIDE)
        end = text.find("\n## ", start + len(GUIDE))
        section = text[start:] if end < 0 else text[start:end]
        for marker in (
            "### 具體情境",
            "### 先看流程",
            "### 讀這章時不要混在一起",
        ):
            if marker not in section:
                errors.append(f"{relative}: guide missing {marker}")
        digest = hashlib.sha256(section.encode()).hexdigest()
        if digest in guide_hashes:
            errors.append(f"{relative}: guide duplicates {guide_hashes[digest]}")
        guide_hashes[digest] = relative

        matches = list(H3.finditer(text))
        for index, match in enumerate(matches):
            title = match.group(1).strip()
            if title in {
                "具體情境",
                "先看流程",
                "讀這章時不要混在一起",
            }:
                continue
            if any(word in title.lower() for word in ("來源", "source", "參考答案")):
                continue
            finish = matches[index + 1].start() if index + 1 < len(matches) else len(text)
            body = text[match.end():finish]
            no_code = re.sub(r"```.*?```", " ", body, flags=re.DOTALL)
            bullets = len(BULLET.findall(no_code))
            narrative = prose(body)
            sentence_count = len(re.findall(r"[。！？]", narrative))
            if bullets >= 3 and (len(narrative) < 170 or sentence_count < 2):
                errors.append(
                    f"{relative}: {title}: compressed list lacks prose setup"
                )
            if len(narrative) < 75 and sentence_count < 1 and no_code.strip():
                errors.append(f"{relative}: {title}: subsection is still too compressed")

    lab03 = (ROOT / "labs/03-ioctl-poll-mmap/README.md").read_text(
        encoding="utf-8"
    )
    for marker in (
        "不是取消 OS protection",
        "ordinary load 不再進入 driver callback",
        "torn snapshot",
        "Odd/even sequence 不取代 mutex",
        "fd 與 VMA 是不同 object / lifetime",
    ):
        if marker not in lab03:
            errors.append(f"Lab03 missing marker: {marker}")

    if errors:
        print("driver-lab beginner v2 all-doc check failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print(
        f"beginner explanation v2 check passed for {len(paths)} canonical learner docs"
    )
    print("note: structural regression guard only; not technical or runtime proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
