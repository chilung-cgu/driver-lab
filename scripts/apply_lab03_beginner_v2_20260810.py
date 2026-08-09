#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
README = r'''# 03 — ioctl、poll、blocking read 與 read-only mmap snapshot

> **這一關先學什麼**：同一個 char device 可以提供不同用途的介面，但它們都在操作同一份 driver state。本 Lab 用 `read/write` 傳資料、用 `ioctl` 下控制命令、用 `poll` 等事件、用 `mmap` 觀看共享 snapshot。
>
> **讀者假設**：你已知道 process、file descriptor、mutex 與 virtual memory 的課本概念；不要求先懂 wait queue、VMA、memory barrier 或 sequence counter。

## 先講結論

這個 Lab 最重要的不是背四組 API，而是理解一件事：**同一份狀態若能從不同路徑存取，每一條路徑都必須遵守同一套「何時可讀、誰能修改、如何判斷版本完整、資源活多久」的規則。**

四條路徑的分工如下：

| 路徑 | Application 看起來像什麼 | Driver 每次是否重新取得控制權 | 本 Lab 的用途 |
|---|---|---|---|
| `read/write` | 傳送或取得一筆 message | 是，每次都進 callback | Data path |
| `ioctl` | 送一個有欄位的控制命令 | 是，每次都進 callback | Control path |
| `poll` | 等待 fd 變成可讀或有事件 | Driver 先登記 waiter，醒來後重新判斷 | Event path |
| `mmap` | 把一頁映射成 userspace pointer | 只有建立 mapping 時進 `.mmap()`；之後一般 load 不再進 callback | Read-only status snapshot |

這四條路徑各自解決不同問題，不能互相代替。`poll()`通知「值得再檢查」，不會替 reader 保留一筆資料；`mutex`可以協調 kernel 裡的 writers/readers，卻不會被任意 userspace `mmap` load 自動取得；read-only mapping 可以阻止 userspace 改寫頁面，卻不能單獨保證多個欄位來自同一個版本。

## 不確定處與驗證狀態

本 README 已對照這個 branch 的 current source：

- `driver_lab_ioctl_poll_mmap.c`：kernel callbacks、wait queues、mutex、shared page 與 VMA flags。
- `runtime/include/driver_lab_uapi.h`：fixed-width ioctl / shared-page layout。
- `runtime/src/driver_lab_runtime.c`：userspace snapshot retry loop。
- `test.sh`：two-reader、empty poll、read-only mmap、`mprotect()` rejection 與 cleanup regressions。

目前能確認的是 source contract 與 static/test intent。尚未附上指定 target kernel 的完整 runtime log，也尚未完成 KCSAN、惡意 client、mapping-after-close、module-unload / VMA lifetime 與 hot-unplug stress。因此本章不宣稱 production-ready，也不把「測試通過一次」寫成所有 weak-memory interleaving 都已證明。

## 先建立一個具體情境

Driver 內有一筆全域 message 與幾個狀態欄位：

```text
buffer         ：目前的message內容
buffer_len     ：message長度；0表示沒有可讀record
event_count    ：累計事件數
event_pending  ：是否有尚待處理的event
shared page    ：提供給mmap reader看的唯讀snapshot
```

Application A 可以用 `write()` 或 ioctl 發布新 message；Application B 可能在 blocking `read()` 等資料；Application C 可能用 `poll()` 等 readiness；Application D 則持續從 mmap page 讀 status。這些 applications 不一定同時執行，也不一定依相同速度讀取，所以 driver 不能靠「通常很快」維持正確性。

正常發布一筆 message 的高層流程是：

```text
Writer把userspace input複製到local kernel buffer
→ 取得dl_lock
→ 更新kernel-owned message state
→ 更新shared snapshot（seq odd → fields → seq even）
→ 釋放dl_lock
→ wake blocking readers與poll waiters
```

注意 wake 放在 state 更新之後。否則 waiter 可能先醒來，看見舊狀態，再次睡下而錯過真正的更新。

## 四條 UAPI 路徑各自做什麼？

### 1. `write()`：把一筆 message 交給 driver

`write()` 的 userspace buffer 不是 kernel 可以任意解參考的 pointer。Current source 先用 `simple_write_to_buffer()`把 input 複製到 local buffer，這一步可能 page fault 或 sleep；完成後才取得 `dl_lock`，把 local copy 發布成 global message。

這個順序避免在持有 shared-state mutex 時執行不必要的 usercopy。取得 mutex 後，driver 同時更新 `dl_buffer`、`dl_buffer_len`、event state 與 mmap snapshot，讓不同 UAPI paths 看見同一個邏輯版本。最後才 wake waiters。

這個 Lab 把 message 視為**一筆 record**，不是任意 byte stream。若 userspace 的 read destination 太小，driver 回 `-EMSGSIZE` 並保留整筆 record，而不是交出半筆後讓多個 readers 的狀態變得難以定義。

### 2. Blocking `read()`：等待 predicate，而不是等待一次 wakeup

Blocking reader 真正等待的條件是：

```text
dl_buffer_len > 0
```

這個條件稱為 predicate。Wait queue 只負責讓 task 在條件不成立時睡眠，以及在狀態可能改變時把它喚醒；wait queue 本身不保存 message，也不保證醒來的 reader 一定得到資料。

看兩個 readers 的時間線：

```text
1. Reader A與Reader B都因buffer_len == 0而sleep。
2. Writer發布一筆message，並wake兩人。
3. A先取得dl_lock，copy並消費message，把buffer_len改回0。
4. B之後才取得dl_lock。
```

若 B 把「我被 wake」當成「資料已保留給我」，它會讀到空資料，甚至錯誤回 EOF。Current source 因此在 B 真正取得 mutex 後再次檢查 `dl_buffer_len`：條件已不成立就釋放 lock，blocking mode 回去繼續等，nonblocking mode 則回 `-EAGAIN`。

> **先記住**：Wakeup 的意思是「狀態可能改變，請重新檢查」，不是 reservation，也不是 payload。

### 3. `poll()`：提供 readiness hint，不替後續 `read()` 預約資料

`poll()` / `epoll()`讓 application 同時等待多個 fds。Driver 的 `.poll` callback 做兩件事。第一，它用 `poll_wait()`把目前 task 登記到相關 wait queues，讓未來的 state change 可以喚醒它。第二，它在 `dl_lock`保護下檢查當下 state；若 message 可讀就回 `POLLIN`，若 control event pending 就回 `POLLPRI`。

即使 `poll()`剛回報 readable，application 真正呼叫 `read()`前仍有時間差。另一個 process 可能先消費 record，或 control path 可能清除狀態。因此 nonblocking `read()`在 poll 之後仍可能合法地回 `-EAGAIN`；application 必須把它當成 race-aware API contract，而不是 driver 壞掉。

```text
poll ready
≠ record已預約給這個thread
≠ 下一次read保證成功
```

### 4. `ioctl()`：用固定格式處理 control request

Ioctl 適合傳送有明確欄位的控制命令，例如 set message、get status、trigger event 或 clear buffer。Current UAPI 使用 fixed-width integer types，不把 kernel pointer、native `unsigned long` 或 internal struct 直接暴露給 userspace。這能減少 32/64-bit layout、padding 與版本演進問題。

Driver 仍必須先 `copy_from_user()` / `copy_to_user()`，再驗證字串長度、command 與欄位語意。Usercopy 只負責安全搬 bytes；它不會自動證明 command 被授權、version 相容或 length 合理。

## `mmap()`為什麼特別容易讓人誤解？

### 先修正一句過度簡化的說法

把核心意思說成「`mmap()`繞過作業系統保護機制」方向接近，但範圍太大。`mmap()`沒有關掉 page-table protection，也沒有讓 application 任意碰整個 kernel memory。Kernel 仍決定映射哪一頁、範圍多大、能不能寫或執行，並處理 VMA 與 page fault。

更精確的說法是：**mapping 建立後，後續每一次 ordinary load 不再進入 driver callback。** 因此 driver 不能像 `read()`那樣，在每一次存取前取得 `dl_lock`、建立 snapshot、檢查 device state，然後才把結果交出去。

### `read()`像服務櫃台，`mmap()`像唯讀看板

用 `read()`時，可以想成每次都到服務櫃台：承辦人先鎖住資料櫃，把同一版本影印完，再把影本交給你。用 `mmap()`時，kernel 先替 application 開一扇只能觀看特定看板的窗；之後每看一眼不會再次呼叫承辦人。

這個比喻有兩個重要邊界。第一，窗不是「整棟大樓的鑰匙」；VMA / page-table permission 仍限制範圍與讀寫權限。第二，看板是 read-only 也可能讀到更新一半的畫面；permission 解決誰能寫，consistency 解決多個欄位是否屬於同一版本。

### 具體的 torn snapshot 時間線

先假設 shared page 是：

```text
event_count = 7
buffer_len  = 5
buffer      = "hello"
```

Userspace 可能先讀到 count 7；此時 kernel writer 在 `dl_lock`內把整頁更新成 count 8 與 `"world"`；userspace 隨後才讀 length 與 buffer。最後它得到「count 7 + world」。每個 load 都完成了，但這個欄位組合不屬於任何一個完整版本。

Kernel writers 仍應使用 mutex，因為它們彼此需要 mutual exclusion；問題在於 userspace load 不會取得那把 mutex，所以 mutex **不能單獨**把多欄位 snapshot 發布給 mmap reader。

## Odd/even sequence 如何讓 reader 偵測一致版本？

Lab03 在 shared page 放一個 `seq`：

```text
偶數：writer目前不在更新，畫面可能穩定
奇數：writer正在更新，reader必須重試
```

### Writer 的步驟

Writer 已在 `dl_lock`內，因此同一時間只有一個 kernel writer 修改 snapshot。接著它把 `seq` 改成奇數，先發布「更新中」狀態，再寫入 magic、version、event fields、length 與 buffer。所有 fields 發布後，最後把 `seq` 改成下一個偶數。

Odd/even sequence 不取代 mutex。Mutex 防止 writers 彼此交錯；sequence 讓不會取得 mutex 的 reader 判斷自己是否跨越一次更新。

### Reader 的步驟

Userspace runtime 先 acquire-load begin seq；若為奇數就立即重試。它接著真正從 mapping 複製整份 snapshot，在適當 ordering 後再讀 end seq。只有 begin 等於 end、兩者都是偶數，而且 snapshot 內 seq 也一致時才接受。

若 writer 在複製期間更新，seq 會變成奇數或下一個偶數，前後比較就不相同；reader 丟掉本次內容重讀。這提供的是**一致性偵測與 retry**，不是 general transaction，也不保證 reader 永遠在有限次數內成功；writer 持續高頻更新時，runtime 最後可能回 `EAGAIN`。

## Mapping permission 與 lifetime

### 為什麼不只檢查最初的 writable request？

Current `.mmap()`拒絕 writable / executable request，並清除 `VM_MAYWRITE` / `VM_MAYEXEC`。只拒絕最初的 writable mapping 還不夠；若保留 MAYWRITE，userspace 之後可能嘗試用 `mprotect()`升級權限。這個 Lab 的 page 是 kernel-published metadata，所以 policy 是從一開始到 mapping lifetime 都維持 read-only。

### 為什麼 fd 關掉後仍要想 VMA？

`mmap()`成功後，fd 與 VMA 是不同 object / lifetime。Application 可以關閉原 fd，mapping 仍可能存在，直到 `munmap()`或 process exit。這表示 backing page 的 lifetime 不能只綁在某一次 ioctl、原始 mmap syscall，甚至不能只假設 fd 還開著。

Current Lab 尚未實作 production-grade VMA open/close reference accounting、hot-unplug revocation 或 generation-based stale mapping policy。讀者應把這一點視為已知邊界，而不是從 demo 推論「module remove 後 pointer 會自動安全失效」。

## Current source 閱讀順序

第一次閱讀，不要從所有 API 細節同時開始。先看 module init，找出 char device、wait queues、mutex 與 shared page 何時建立；再看 `dl_publish_message_locked()`，理解一筆 logical state update 包含哪些 fields。接著追 `dl_read()` 的 blocking predicate、拿鎖後 recheck、record consume 與 usercopy。

然後看 `dl_poll()`，確認它只登記 waiter 並回當下 readiness mask。再把 `dl_sync_shared_page_locked()`與 runtime reader 放在一起，將 writer / reader sequence 步驟一一對上。最後才看 `dl_mmap()`的 size、offset、permission、VMA flags 與 page insertion，並用 `test.sh`問每個 observable evidence 能證明什麼、不能證明什麼。

## 最小正確範式

下面是概念骨架，不是可以不看 context 就貼進 production driver 的完整 API：

```c
/* Kernel writer: caller already holds the writer mutex. */
WRITE_ONCE(shared->seq, odd);
smp_wmb();
update_all_snapshot_fields(shared);
smp_wmb();
WRITE_ONCE(shared->seq, even);
```

```c
/* Userspace reader: retry until one stable version was copied. */
begin = atomic_load_acquire(&mapped->seq);
if (begin & 1)
    retry;
copy_every_field(snapshot, mapped);
order_copy_before_final_seq_load();
end = atomic_load_acquire(&mapped->seq);
if (begin != end || (end & 1))
    retry;
```

正確性取決於 writer 與 reader 使用配對的 publication protocol、欄位 layout 不被任意修改、permission 與 lifetime 也符合 UAPI；不是只要看到兩個 barriers 就自動成立。

## 看似合理但錯誤的寫法

### 醒來後不 recheck

錯誤直覺是「writer wake 我，所以 record 一定還在」。兩個 readers 被同一次 write 喚醒時，第一個可能已消費它。正確做法是在真正取得保護 state 的 mutex 後再次檢查 predicate；條件失效就繼續等或回 `-EAGAIN`。

### `poll()` ready 後把 `-EAGAIN`當成不可能

Poll 與 read 之間存在 race window，readiness 沒有 reservation 語意。正確 application 仍使用 nonblocking read contract，並在 `-EAGAIN`時回到 event loop。

### 只用 kernel mutex，就宣稱 mmap snapshot 一致

Kernel mutex 只約束參與該 locking protocol 的 kernel contexts。Arbitrary userspace loads 不會取得它。正確設計另加 publication / version protocol，讓 reader 能偵測 update overlap。

### Read-only 就等於 consistent

Read-only 只阻止 userspace 寫入；writer 仍可能在 reader 逐欄 load 時更新頁面。正確設計同時處理 permission 與 consistency。

### 假設 `close(fd)`會自動取消 mapping

Fd 與 VMA 是不同 object / lifetime。正確設計必須追蹤 backing memory、VMA refs 與 remove/reset後的 stale-view policy。

## 如何執行與保存 evidence

```sh
cd labs/03-ioctl-poll-mmap
./test.sh
```

不要只記錄最後一行「PASS」。至少保存 target kernel、repository SHA、完整 command、return code、本次新增的 kernel log 與 cleanup 結果。Two-reader test 能檢查拿鎖後 recheck 的 observable regression；permission test 能檢查 writable mapping / `mprotect()`被拒；snapshot test 能檢查 runtime 是否遵守 seq acceptance rule。

這些測試仍不能窮舉所有 CPU ordering、signal、malicious ioctl、mapping-after-close、unload race 或長時間 starvation。需要另外以 target architecture、KCSAN / sanitizers、stress 與 fault injection 擴大證據。

## Debug 順序

遇到失敗時，先分層，不要直接猜 barrier。`read()`卡住時先印 predicate 與 wake 前後 state，再看 wait queue；第二個 reader 錯回空資料時，看取得 `dl_lock`後是否 recheck；`poll()`結果不符時，看 `.poll`當下回的 mask，不把 wake 次數當 readiness。

Mmap permission 錯誤要看 requested prot、VMA flags、`VM_MAYWRITE`與 `mprotect()`結果。Snapshot內容混合時，同時記 begin/end seq、copied seq 與 writer publication順序。Unloading 或 stale mapping 問題則先畫 fd、VMA、page、module與device各自 lifetime，再找誰過早 free。

## 與 `pcie-study` 的對應

P1-06 說明 syscall callback、poll 與 mmap 為何有不同同步機會；P1-08～P1-10 分別解釋 mutex、race 與 publication ordering；P1-14 把 VFS、UAPI、VMA、close/remove lifetime 串成完整 boundary。

P2 之後會再加入 MMIO、IRQ、DMA ownership、IOMMU 與 hot-unplug；那些問題不能由本 Lab 的 sequence page 類比全部取代。

## 適用邊界與尚未驗證

這是一個 single-device、single global record、read-only one-page snapshot 的教學模型。它沒有 multiple devices、per-open queues、VMA close refcount、device DMA into the mapped page、hot-unplug revocation、production security policy或完整 ABI version negotiation。QEMU / Linux demo 中成立的 protocol，也不能直接推廣成所有 accelerator UAPI。

## 第一次閱讀先記住

1. Wait queue 負責 sleep / wake；predicate 才決定現在能不能繼續。
2. 多個 readers 被同時喚醒後，拿到 mutex 的順序會改變 state，所以每人都要 recheck。
3. `poll()`回報 readiness，不會預約 record。
4. `mmap()`不是取消 OS protection；它取消的是後續每次 load 都進 driver callback 的機會。
5. Kernel mutex 與 sequence publication 分工不同：前者協調 writers，後者讓 mmap reader偵測完整版本。
6. Read-only、snapshot consistency與 VMA lifetime 是三個不同責任。

## Self-check

1. 為什麼兩個 blocking readers 被同一次 write 喚醒後，第二個仍可能沒有資料？
2. `poll()`剛回 `POLLIN`後，nonblocking `read()`為什麼仍可能回 `-EAGAIN`？
3. Kernel writer 已持有 mutex，userspace mmap reader為什麼還可能看到混合版本？
4. Odd/even sequence reader 接受 snapshot 的完整條件是什麼？
5. Read-only mapping 解決了什麼，又沒有解決什麼？
6. 為什麼 `close(fd)`不能當成 backing page 可立即釋放的充分證據？

<details>
<summary>參考答案</summary>

1. 第一個 reader 可能先拿鎖並消費 global record；wakeup沒有 reservation 語意，第二個拿鎖後必須重查 predicate。
2. Poll 與 read 之間有時間差，其他 consumer 或 control path 可能改變 state；read 必須保留 race-aware contract。
3. Userspace ordinary load 不會取得 kernel mutex，可能在 writer更新多個欄位途中跨版本讀取。
4. Begin seq 必須是偶數；複製後 end 必須與 begin 相同且仍為偶數，snapshot內的 seq 也要與被接受版本一致。
5. 它阻止 userspace 修改 kernel-owned page，但不保證多欄位一致，也不解決 backing page / VMA lifetime。
6. Mapping 可在 fd close 後繼續存在；必須依 VMA、page ref與remove policy證明所有 readers都不再存取。

</details>

## 來源與查證

- Linux wait queues / poll 基礎：<https://docs.kernel.org/driver-api/basics.html>
- Linux memory barriers：<https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
- Linux MM / VMA API：<https://docs.kernel.org/core-api/mm-api.html>
- Linux `mmap(2)`語意：<https://man7.org/linux/man-pages/man2/mmap.2.html>
- Current kernel source：`driver_lab_ioctl_poll_mmap.c`
- Current userspace reader：`runtime/src/driver_lab_runtime.c`
'''

(ROOT / "labs/03-ioctl-poll-mmap/README.md").write_text(README, encoding="utf-8")

manifest = ROOT / "docs/pedagogy/beginner-explained-v2-docs.txt"
manifest.parent.mkdir(parents=True, exist_ok=True)
manifest.write_text(
    "# Learner-facing documents that completed the second section-by-section beginner explanation pass.\n"
    "# This list is separate from technical review and runtime verification.\n"
    "labs/03-ioctl-poll-mmap/README.md\n",
    encoding="utf-8",
)

CHECKER = r'''#!/usr/bin/env python3
"""Guard the Lab03 beginner explanation against compressed-summary regression."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "labs/03-ioctl-poll-mmap/README.md"
text = PATH.read_text(encoding="utf-8")
errors: list[str] = []
for marker in (
    "mmap()`不是取消 OS protection",
    "後續每一次 ordinary load 不再進入 driver callback",
    "具體的 torn snapshot 時間線",
    "Odd/even sequence 不取代 mutex",
    "fd 與 VMA 是不同 object / lifetime",
    "不是「整棟大樓的鑰匙」",
):
    if marker not in text:
        errors.append(f"missing Lab03 explanation marker: {marker}")

section_re = re.compile(r"^###\s+(.+)$", re.MULTILINE)
matches = list(section_re.finditer(text))
for index, match in enumerate(matches):
    end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
    body = text[match.end():end]
    without_code = re.sub(r"```.*?```", " ", body, flags=re.DOTALL)
    bullets = list(
        re.finditer(r"^\s*(?:[-*]|\d+\.)\s+", without_code, flags=re.MULTILINE)
    )
    if len(bullets) < 3:
        continue
    prefix = without_code[:bullets[0].start()]
    prefix = re.sub(r"[`*_#>|]", " ", prefix)
    prefix = re.sub(r"\s+", " ", prefix).strip()
    if len(prefix) < 90 or len(re.findall(r"[。！？]", prefix)) < 2:
        errors.append(
            f"{match.group(1)}: list appears before two complete explanatory sentences"
        )

if errors:
    print("beginner explanation check failed:", file=sys.stderr)
    for error in errors:
        print(f"- {error}", file=sys.stderr)
    raise SystemExit(1)
print("beginner explanation check passed for Lab03 README")
print("note: regression guard only; not technical or runtime proof")
'''
checker = ROOT / "scripts/check_beginner_explanation_quality.py"
checker.write_text(CHECKER, encoding="utf-8")
checker.chmod(0o755)

workflow = ROOT / ".github/workflows/quality.yml"
if workflow.is_file():
    text = workflow.read_text(encoding="utf-8")
    if "check_beginner_explanation_quality.py" not in text:
        lines = text.splitlines()
        command = "      - run: python3 scripts/check_beginner_explanation_quality.py"
        insert_at = next(
            (index + 1 for index, line in enumerate(lines) if "check_pedagogy_structure.py" in line),
            len(lines),
        )
        lines.insert(insert_at, command)
        workflow.write_text("\n".join(lines) + "\n", encoding="utf-8")
