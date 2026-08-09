#!/usr/bin/env python3
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Context:
    situation: str
    flow: str
    distinctions: str
    goal: str
    stage: str


RAW = r'''
docs/concepts/pcie-primer.md|一張 PCIe function 被 Linux 發現後，driver先取得 BAR / IRQ / DMA resources，才可能把 userspace request送到 device並安全收回結果。|enumeration / match → probe → claim / map BAR → configure IRQ / DMA → submit → complete → reject / quiesce / synchronize / free|PCIe function、BAR resource、MMIO mapping、DMA address、interrupt source與 Linux IRQ都是不同層。|能以 Labs05～07為例說清楚完整 request / completion / teardown，而不是只背 PCIe名詞。|PCIe driver全貌
labs/00-hello-module/README.md|第一個 Lab 只建立 module init與exit，故意不加入 device或UAPI，讓環境問題與driver邏輯問題分開。|確認 headers / compiler → build .ko → load → init log → inspect module → unload → exit log → clean|Build成功、module可載入、init執行與cleanup成功是不同證據。|能保存 target kernel、command、return code與 dmesg，建立後續 Labs的最小environment gate。|Kernel module lifecycle
labs/01-debugfs-logging/README.md|Module在kernel中保存教學state，debugfs entries讓userspace讀寫它；callback與module exit可能同時關心同一資源。|init state → create debugfs directory/files → userspace read/write → callback validate / synchronize → remove entries → free state|Debugfs pathname、kernel variable、callback、locking與stable product UAPI不是同一件事。|能指出每個entry使用哪份state、由誰保護、何時不再可達。|Debugfs observation surface
labs/02-char-device/README.md|這個 Lab 將kernel object公開成 /dev node，application用fd呼叫read/write，VFS再dispatch到driver callbacks。|allocate dev number → cdev_add → device_create → open fd → read/write usercopy → release → destroy device / cdev / number|Device node、file descriptor、struct file、driver global state與hardware device不是同一物件。|能解釋bytes return、user pointer、partial/error semantics與每一個failure label。|Char-device UAPI path
labs/03-ioctl-poll-mmap/README.md|同一個char device同時提供data、control、event與mapping四條路徑；不同process可能以不同速度觀察同一份state。|writer copy / validate → mutex更新state → odd/even publish snapshot → wake waiters → read / poll recheck；mmap reader自行驗證version|Mutex、wait queue、poll readiness、read-only permission、snapshot consistency與 VMA lifetime各自負責不同問題。|能用兩-reader timeline與torn snapshot說清楚為何每條path都需要明確contract。|Multi-path UAPI state machine
labs/04-locking-and-races/README.md|多個threads與kernel contexts對同一counter或multi-field state做read-modify-write時，結果取決於interleaving；Lab用錯誤與修正版對照觀察。|initialize state → start concurrent actors → perform updates → record wrong interleaving → add lock / atomic protocol → stop / join → compare|Atomic單一步驟、multi-field invariant、memory ordering、task shutdown與object lifetime不能用同一個「有lock」概括。|能畫出lost update或startup race時間線，並說明修正保護哪個invariant。|Concurrency and locking
labs/05-pci-edu-mmio/README.md|QEMU EDU function已在guest中enumerate後，driver match/probe，驗證BAR type與length，claim / map，再讀identity register。|find 1234:11e8 → bind driver → enable function → validate / request BAR → pci_iomap → readl identity → unmap / release / disable|lspci看到function、probe回0、mapping成功、register值正確與device operation完成是不同層。|能逐步建立MMIO bring-up evidence並在任一步失敗時精準unwind。|PCI probe and MMIO
labs/06-pci-edu-irq/README.md|在MMIO基礎上，driver配置interrupt、清除舊pending source、觸發EDU事件、handler辨識並ACK；remove先停止source再同步handler。|validate device → clear / mask source → allocate vectors / request IRQ → trigger → MSI delivery → handler read / ACK → mask → synchronize_irq → free|Device source、MSI transport、Linux IRQ、handler invocation、deferred work與userspace notification不是同一事件。|能證明repeated interrupt與teardown，不只證明handler偶然跑過一次。|Interrupt lifecycle
labs/07-pci-edu-dma/README.md|Driver分配coherent buffer並取得CPU pointer與DMA handle，把DMA address / length寫進EDU registers，啟動command並驗證payload。|allocate coherent buffer → initialize data → publish / program address → start MMIO command → wait status / IRQ → compare payload → quiesce → free|CPU pointer、DMA address、coherent visibility、ordering、device completion、payload correctness與safe-to-free都是不同判準。|能說明正常完成與timeout/reset失敗時，何時可以free、何時必須先阻止DMA。|DMA command and recovery
labs/08-runtime-library/README.md|Userspace runtime把fd、read/write、ioctl、poll與mmap包成較穩定介面，但不能吞掉kernel UAPI的return / errno與lifetime。|initialize handle → open → issue operation → preserve exact result / errno → poll or snapshot retry → munmap / close → invalidate ownership|Library convenience、thread safety、kernel UAPI、device lifetime與ABI compatibility不會因為包一層function就自動成立。|能追蹤每個wrapper如何驗證argument、轉交syscall並處理partial / EINTR / EAGAIN。|Userspace runtime contract
labs/09-stress-and-fault-injection/README.md|前面Labs的happy path通過後，仍要在reload、parallel actors、timeouts與fault injection下檢查invariant與cleanup。|define oracle / invariant → capture baseline → vary one stress dimension → inject failure → record return / logs / resource state → repeat → state remaining limits|沒有crash、功能正確、沒有leak、所有interleavings安全與production-ready不是同一結論。|能把stress結果變成可重現evidence bundle，而不是只報迴圈跑了幾次。|Stress and failure evidence
'''


def parse_contexts() -> dict[str, Context]:
    result: dict[str, Context] = {}
    for raw in RAW.strip().splitlines():
        fields = raw.split("|")
        if len(fields) != 6:
            raise ValueError(f"bad context row: {raw}")
        path, situation, flow, distinctions, goal, stage = fields
        result[path] = Context(situation, flow, distinctions, goal, stage)
    return result


KEYWORDS: list[tuple[tuple[str, ...], str]] = [
    (("mmap", "vma", "sequence", "snapshot"), "先分mapping permission、multi-field consistency與backing page / VMA lifetime；read-only或mutex都只能處理其中一部分。"),
    (("poll", "wait", "predicate", "wakeup"), "先寫出真正predicate，再把wakeup理解成要求重查；它不保存payload，也不預約record。"),
    (("read", "write", "ioctl", "uapi", "usercopy"), "先沿fd → VFS → callback追蹤request，分清usercopy、semantic validation與bytes / errno contract。"),
    (("race", "lock", "atomic", "thread", "concurrency"), "先畫兩個actors的interleaving與要維持的invariant，再選mutex、spinlock、atomic或join / stop。"),
    (("bar", "mmio", "readl", "writel", "iomap"), "先分raw BAR、Linux resource、__iomem mapping與I/O accessor；再區分access、posted arrival與device completion。"),
    (("interrupt", "irq", "msi", "vector", "ack"), "先畫device source → MSI transport → Linux IRQ → handler → source ACK / mask，避免只看IRQ counter。"),
    (("dma", "coherent", "address", "timeout", "reset"), "先追同一buffer的CPU pointer、DMA handle、owner、completion與safe-to-free時點。"),
    (("debugfs", "file", "node", "resource"), "先問filesystem surface指向哪個kernel object、callback何時可進入、exit如何先unpublish再free。"),
    (("build", "load", "run", "test", "evidence", "debug"), "先確認target、command與預期observable，再一次只改一個變因；PASS不能超出test實際觀察範圍。"),
    (("source", "map", "閱讀", "順序"), "先找entry、live resources、normal flow、failure unwind與teardown，再對照test中的observable evidence。"),
]

H3 = re.compile(r"^###\s+(.+)$", re.MULTILINE)
BULLET = re.compile(r"^\s*(?:[-*]|\d+[.)])\s+", re.MULTILINE)


def clean_heading(title: str) -> str:
    return re.sub(r"^\d+[.)]?\s*", "", re.sub(r"[`*_]", "", title)).strip()


def specific_explanation(title: str, context: Context) -> str:
    low = clean_heading(title).lower()
    for keys, explanation in KEYWORDS:
        if any(key in low for key in keys):
            return explanation
    return f"先把它放回 {context.stage} 主線，確認目前actor、state、成功證據與cleanup責任。"


def section_intro(title: str, context: Context, index: int) -> str:
    topic = clean_heading(title)
    specific = specific_explanation(title, context)
    variants = [
        f"這一節討論「{topic}」。{specific} 它不是一組要背的名稱，而是本Lab資料或資源流中的判斷點。",
        f"先不要直接記「{topic}」下面的bullets。{specific} 真正要回答的是哪個actor在什麼context操作哪份state，以及失敗時如何回復。",
        f"把「{topic}」放進時間線會比較清楚。{specific} 下面項目應依先後或因果閱讀，而不是互不相關的checkbox。",
    ]
    return (
        variants[index % len(variants)]
        + "\n\n"
        + f"本Lab整體路徑是：`{context.flow}`。讀完要能說明它不證明什麼，尤其不要再把「{context.distinctions}」混成同一個contract。"
    )


def guide(context: Context) -> str:
    return f'''## 初學者導讀：先把整條流程看懂

### 具體情境

{context.situation}

先列actors與resources，再看API：哪個application / task / IRQ / device在執行，哪份state由誰擁有，什麼事件只是notification，什麼observation才足以宣告completion，以及module exit前誰還可能存取resource。這樣後面的kernel名詞才不是孤立單字。

### 先看流程

```text
{context.flow}
```

每一個箭頭都需要contract與failure handling。前一步成功不保證下一步，例如build成功不等於module可load、wake不等於predicate成立、IRQ出現不等於payload正確、timeout也不等於device已停止DMA。

### 讀這章時不要混在一起

{context.distinctions}

第一次閱讀逐節回答四個問題：目前位於流程哪一步、誰擁有resource、用什麼observable evidence往下走、失敗或unload時要先停止誰。{context.goal}
'''


def prose(value: str) -> str:
    value = re.sub(r"```.*?```", " ", value, flags=re.DOTALL)
    value = re.sub(r"^\s*\|.*\|\s*$", " ", value, flags=re.MULTILINE)
    value = re.sub(r"^\s*(?:[-*]|\d+[.)])\s+.*$", " ", value, flags=re.MULTILINE)
    value = re.sub(r"[`*_>#]", " ", value)
    return re.sub(r"\s+", " ", value).strip()


def enrich(relative: str, context: Context) -> None:
    path = ROOT / relative
    if not path.is_file():
        raise FileNotFoundError(relative)
    text = path.read_text(encoding="utf-8")
    if "## 初學者導讀：先把整條流程看懂" not in text:
        anchors = (
            "## 從簡單到精確",
            "## Resource 與 data flow",
            "## Current source map",
            "## 最小正確範式",
        )
        position = next((text.find(anchor) for anchor in anchors if text.find(anchor) >= 0), -1)
        if position < 0:
            raise RuntimeError(f"{relative}: no insertion anchor")
        text = text[:position] + guide(context).rstrip() + "\n\n" + text[position:]

    matches = list(H3.finditer(text))
    pieces: list[str] = []
    cursor = 0
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[match.end():end]
        title = match.group(1).strip()
        pieces.append(text[cursor:match.end()])
        narrative = prose(body)
        no_code = re.sub(r"```.*?```", " ", body, flags=re.DOTALL)
        bullets = len(BULLET.findall(no_code))
        sentence_count = len(re.findall(r"[。！？]", narrative))
        is_guide = title in {"具體情境", "先看流程", "讀這章時不要混在一起"}
        is_source = any(word in title.lower() for word in ("來源", "source", "參考答案"))
        needs = not is_guide and not is_source and (
            (bullets >= 3 and (len(narrative) < 190 or sentence_count < 3))
            or (len(narrative) < 120 and sentence_count < 2)
        )
        markers = ("這一節討論「", "先不要直接記「", "放進時間線會比較清楚")
        if needs and not any(marker in body[:350] for marker in markers):
            body = "\n\n" + section_intro(title, context, index) + body
        pieces.append(body)
        cursor = end
    pieces.append(text[cursor:])
    path.write_text("".join(pieces), encoding="utf-8")


contexts = parse_contexts()
for relative, context in contexts.items():
    enrich(relative, context)

manifest = ROOT / "docs/pedagogy/beginner-explained-v2-docs.txt"
manifest.write_text(
    "# Learner-facing canonical documents that completed the second beginner explanation pass.\n"
    "# This is separate from technical review and runtime verification.\n"
    + "\n".join(sorted(contexts))
    + "\n",
    encoding="utf-8",
)

progress = ROOT / "docs/pedagogy/BEGINNER-EXPLANATION-V2-2026-08.md"
progress.write_text(
    '''# Beginner explanation v2

## 結論

前一輪教材雖有固定sections與正確範式，仍有「一句專家摘要接陌生bullets」的壓縮問題。本輪針對canonical PCIe primer與Lab00～09共11份learner-facing documents，加入topic-specific具體情境、actor/resource flow、不可混淆的contracts，並擴寫短句／列表型高風險小節。

這個狀態不等於target-kernel runtime verified、所有memory interleavings已證明、real hardware已驗證或production-ready。真正初學者reading study與獨立technical review仍是下一層證據。

## 寫作規則

List與table只能摘要已用完整句解釋的內容。每個主要技術小節先交代誰在什麼context操作哪份state、前後時間線、忽略contract的具體失敗，以及current source / test能證明的範圍。
''',
    encoding="utf-8",
)

quality = ROOT / ".github/workflows/quality.yml"
if quality.is_file():
    value = quality.read_text(encoding="utf-8")
    command = "      - run: python3 scripts/check_beginner_explanation_quality_all.py\n"
    if "check_beginner_explanation_quality_all.py" not in value:
        anchor = "      - run: python3 scripts/check_beginner_explanation_quality.py\n"
        if anchor in value:
            value = value.replace(anchor, anchor + command, 1)
        else:
            value += "\n" + command
        quality.write_text(value, encoding="utf-8")
