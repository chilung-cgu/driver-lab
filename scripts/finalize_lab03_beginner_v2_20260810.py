#!/usr/bin/env python3
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "labs/03-ioctl-poll-mmap/README.md"
original = subprocess.check_output(
    ["git", "show", "HEAD:labs/03-ioctl-poll-mmap/README.md"],
    cwd=ROOT,
    text=True,
)
original_headings = [line.strip() for line in original.splitlines() if line.startswith("## ")]
text = PATH.read_text(encoding="utf-8")


def expected(*keywords: str, default: str) -> str:
    for heading in original_headings:
        if all(keyword.lower() in heading.lower() for keyword in keywords):
            return heading
    return default


def insert_before(anchor: str, block: str) -> None:
    global text
    if block.splitlines()[0] in text:
        return
    position = text.find(anchor)
    if position < 0:
        raise RuntimeError(f"anchor not found: {anchor}")
    text = text[:position] + block.rstrip() + "\n\n" + text[position:]


lab_heading = expected("定位", default="## Lab 定位")
insert_before(
    "## 先講結論",
    f'''{lab_heading}

這個 Lab 位在 userspace / kernel boundary 的中段：前面的 Labs 已讓你看過基本 char-device callback，這裡第一次把 blocking read、poll、structured ioctl 與 read-only mmap 放進同一個 state machine。學習目標不是堆疊更多 API，而是追蹤同一份 state 被多個 actors、不同 execution contexts 與不同 lifetime 同時觀察時，哪些 contract 必須一致。

完成本 Lab 後，讀者應能畫出 writer、blocking reader、poll waiter、mmap reader 與 module teardown 的關係；能說明 wakeup 為何不是 reservation、kernel mutex 為何不能自動保護 arbitrary userspace loads，以及 permission、snapshot consistency 與 VMA lifetime 為何是三個不同責任。''',
)

problem_heading = expected("解決", "問題", default="## 這個 Lab 要解決什麼問題")
terms_heading = expected("名詞", default="## 名詞先說清楚")
model_heading = expected("心智", default="## 心智模型")
contract_heading = expected("implementation", "contract", default="## Current implementation contract")
source_heading = expected("source", "map", default="## Source map")
detail_heading = expected("從簡單到精確", default="## 從簡單到精確")

anchor = "## 先建立一個具體情境"
insert_before(
    anchor,
    f'''{problem_heading}

第一個問題是「被喚醒」與「條件仍成立」不是同一件事。兩個 readers 可以被同一次 write 喚醒，但第一個 reader 可能先取得 mutex 並消費唯一 record；第二個 reader 因此必須在拿鎖後重新檢查 predicate。

第二個問題是 `poll()`只回報某一時刻的 readiness。Poll return 與後續 `read()`之間仍有 race window，所以 `POLLIN`不等於 record 已預約給這個 thread；nonblocking `read()`仍可能正確回 `-EAGAIN`。

第三個問題是 `mmap()`建立後，ordinary userspace loads 不再逐次進入 driver callback。Kernel mutex仍能協調 kernel writers，卻不能被 arbitrary userspace reader自動取得；driver因此需要 version / sequence publication、read-only permission與獨立的 VMA lifetime policy。''',
)
insert_before(
    anchor,
    f'''{terms_heading}

| 名詞 | 白話意思 | 本 Lab 中不代表什麼 |
|---|---|---|
| **Predicate** | 現在是否真的可以繼續執行的條件，例如 `buffer_len > 0` | 被 wake 的次數 |
| **Wait queue** | 條件不成立時讓 task sleep，狀態可能改變時再喚醒 | Message queue 或 payload storage |
| **Readiness** | 此刻值得嘗試 I/O 的提示 | Record reservation |
| **VMA** | Process virtual address range 的 kernel 管理物件 | File descriptor 本身 |
| **Publication** | 讓另一個 observer 能辨認一組欄位何時形成完整版本 | General mutual exclusion |
| **Torn snapshot** | 每個欄位都讀成功，但組合跨越兩次更新 | 一定是單一欄位 atomicity 失敗 |

第一次閱讀先掌握 predicate、readiness、VMA、sequence與 snapshot。Full Linux memory model、architecture-specific barriers與 production hot-unplug revocation可以第二輪再看。''',
)
insert_before(
    anchor,
    f'''{model_heading}

把 `read()`想成每次到服務櫃台：承辦人先鎖住資料櫃，影印同一版本，再交出副本。把 `mmap()`想成管理者先開一扇只能觀看特定看板的窗；之後每看一眼不會再次叫承辦人來上鎖，所以看板必須用「更新中／版本號」讓讀者辨認完整畫面。

比喻不能延伸成「application拿到整棟大樓的鑰匙」。Kernel仍用 VMA與 page table限制映射範圍、read/write/execute permission與fault handling；真正不同的是後續每次 ordinary load不再進 `.read`、`.ioctl`或 `.mmap` callback。''',
)
insert_before(
    anchor,
    f'''{contract_heading}

Current branch 的 teaching contract 是 single global record、兩個 wait queues、一把 mutex與一頁 read-only shared snapshot。Writer在更新 logical state後才 wake waiters；blocking reader在取得mutex後 recheck；poll只回當下mask；mmap拒絕 writable / executable mapping並清除 MAYWRITE / MAYEXEC；snapshot使用odd/even sequence讓userspace retry。

這些是 source 中目前可查的行為，不等於 production UAPI。Current Lab未實作multiple devices、per-open queues、VMA open/close ref accounting、hot-unplug revocation、device DMA into the mapping或完整 ABI version negotiation。''',
)
insert_before(
    anchor,
    f'''{source_heading}

| 想回答的問題 | Current source入口 |
|---|---|
| Shared state與wait queues在哪裡建立？ | `driver_lab_ioctl_poll_mmap.c` 的module init與global state |
| Message如何原子地成為一筆record？ | `dl_publish_message_locked()`與`dl_read()` |
| Poll如何登記waiter並回mask？ | `dl_poll()` |
| Snapshot writer如何發布odd/even sequence？ | `dl_sync_shared_page_locked()` |
| Userspace如何retry並接受stable copy？ | `runtime/src/driver_lab_runtime.c` |
| Mapping如何限制size、offset與permission？ | `dl_mmap()` |
| Observable regressions如何被測試？ | `labs/03-ioctl-poll-mmap/test.sh` |

閱讀時把kernel writer與userspace reader並排，不要只看其中一側的barrier或sequence操作。''',
)

if "## 四條 UAPI 路徑各自做什麼？" in text and detail_heading not in text:
    text = text.replace(
        "## 四條 UAPI 路徑各自做什麼？",
        detail_heading + "\n\n下面依資料、等待、控制與mapping四條路徑逐步說明。每一節都先問driver何時重新取得控制權，再問state、ordering與lifetime由誰保證。",
        1,
    )

tools_heading = expected("工具", "分工", default="## 工具分工")
insert_before(
    "## Current source 閱讀順序",
    f'''{tools_heading}

| 機制 | 在本 Lab 的責任 | 不能替代什麼 |
|---|---|---|
| `dl_lock` mutex | 讓kernel callbacks以一致規則修改global state與snapshot | Arbitrary userspace mmap load、VMA lifetime |
| Wait queue | Sleep與wake blocking readers / poll waiters | Predicate、record ownership |
| `poll_wait()` | 把waiter登記到正確wait queue | Readiness reservation |
| Odd/even sequence | 讓mmap reader偵測copy期間是否跨越更新 | Writer mutual exclusion、permission |
| VMA flags | 限制mapping範圍與未來可升級權限 | Multi-field consistency |
| Tests | 檢查特定observable contracts與cleanup | 所有interleavings、formal memory-model proof |''',
)

misunderstanding_heading = expected("常見", "誤解", default="## 常見誤解")
insert_before(
    "## 適用邊界與尚未驗證",
    f'''{misunderstanding_heading}

「有 mutex 就不會讀到混合版本」只對參與同一 locking protocol 的 contexts成立；userspace ordinary load不會拿kernel mutex。「Mapping是read-only就一定一致」也不成立，因為permission只限制writer身份，不阻止reader跨越一次kernel更新。

「`poll()`回ready就能無條件read」忽略poll與read之間的時間差；「`close(fd)`後mapping也消失」則混淆file descriptor與VMA lifetime。這些誤解看似不同，根源都是把notification、ownership、consistency與lifetime當成同一個contract。''',
)

# Preserve the exact cross-repo heading used by the existing branch checker.
for heading in original_headings:
    if "pcie-study" in heading.lower():
        text = text.replace("## 與 `pcie-study` 的對應", heading)

missing = [heading for heading in original_headings if heading not in text]
if missing:
    raise SystemExit("rewritten Lab03 is missing original required headings: " + ", ".join(missing))

PATH.write_text(text, encoding="utf-8")
