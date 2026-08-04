#!/usr/bin/env python3
"""Rewrite the remaining learner-facing Lab READMEs with one shared structure."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/pedagogy/migrated-docs.txt"


@dataclass(frozen=True)
class Lab:
    path: str
    title: str
    conclusion: str
    status: str
    problem: str
    terms: tuple[tuple[str, str, str], ...]
    model: str
    flow: str
    source_map: str
    pattern: str
    wrong: str
    run: str
    evidence: str
    debug: tuple[str, ...]
    tools: tuple[tuple[str, str, str], ...]
    pcie: str
    misconceptions: tuple[tuple[str, str], ...]
    limits: tuple[str, ...]
    takeaways: tuple[str, ...]
    questions: tuple[str, ...]
    answers: tuple[str, ...]
    sources: tuple[str, ...]


def lab(**kwargs: object) -> Lab:
    return Lab(**kwargs)  # type: ignore[arg-type]


LABS = (
    lab(
        path="labs/00-hello-module/README.md",
        title="00 — External kernel module 的最小生命週期",
        conclusion=(
            "Lab00 不是在學印 Hello，而是建立所有後續 lab 共用的最小閉環："
            "用 running kernel 對應的 build tree 產生 `.ko`，載入時進 init，"
            "失敗時由 init 自行 unwind，卸載時進 exit，最後確認沒有殘留 module。"
        ),
        status=(
            "Current source 與 test 已可進行 static/compile/smoke 驗證；"
            "實際 load 仍需 Linux、matching headers、module policy 與足夠權限。"
        ),
        problem=(
            "初學者常把 kernel module 當成有 `main()` 的 userspace program，"
            "或以為 init 失敗後 kernel 會自動呼叫 exit 清理。這會讓後續 resource unwind 全部建立在錯誤模型上。"
        ),
        terms=(
            ("external module", "在 kernel source tree 外由 kbuild 編譯的 `.ko`", "不保證可載入任意 kernel"),
            ("init callback", "module load 時由 kernel 呼叫的入口", "不是永久 main loop"),
            ("vermagic", "module 與 target kernel build 特徵的相容資訊", "不是唯一 load policy"),
            ("unwind", "失敗時撤銷本次已成功取得的 resource", "不是無條件呼叫 exit"),
        ),
        model=(
            "把 module load 想成開店：init 依序取得營業所需資源；只有全部成功才正式營業。"
            "若中途失敗，施工中的 init 必須自己撤掉已完成步驟。exit 只處理成功載入後的正常卸載。"
        ),
        flow=(
            "check running kernel/build tree\n"
            "→ make 產生 .ko\n"
            "→ modinfo 檢查 metadata/vermagic\n"
            "→ insmod 解析 parameters 並呼叫 init\n"
            "→ lsmod/sysfs/dmesg 觀察\n"
            "→ rmmod 呼叫 exit\n"
            "→ 確認 module 與 build artifact 已清理"
        ),
        source_map=(
            "- `driver_lab_hello.c`：`driver_lab_hello_init()`、`driver_lab_hello_exit()`、module parameters。\n"
            "- `Makefile`：external-module kbuild 入口。\n"
            "- `test.sh`：build、valid/invalid parameter、load/unload 與 run-specific log gate。\n"
            "- `quality.sh`：本 lab 的 static gate。"
        ),
        pattern=(
            "```c\n"
            "static int __init driver_lab_hello_init(void)\n"
            "{\n"
            "    if (repeat < 1)\n"
            "        return -EINVAL;\n"
            "    return 0;\n"
            "}\n\n"
            "static void __exit driver_lab_hello_exit(void)\n"
            "{\n"
            "}\n"
            "```\n"
            "Init 回 0 才表示 module 成功 resident；回負 errno 時，exit 不會替這次失敗收尾。"
        ),
        wrong=(
            "```c\n"
            "resource = allocate_something();\n"
            "if (later_step_fails())\n"
            "    return -EINVAL;   /* 漏掉 resource cleanup */\n"
            "```\n"
            "這段看似只是回錯誤，實際上把已取得 resource 留在 failed load path。"
        ),
        run=(
            "```sh\n"
            "cd labs/00-hello-module\n"
            "./test.sh\n"
            "```\n\n"
            "手動觀察時可搭配：\n\n"
            "```sh\n"
            "modinfo ./driver_lab_hello.ko\n"
            "sudo insmod ./driver_lab_hello.ko who=linux repeat=2\n"
            "lsmod | grep '^driver_lab_hello '\n"
            "cat /sys/module/driver_lab_hello/parameters/repeat\n"
            "sudo rmmod driver_lab_hello\n"
            "```"
        ),
        evidence=(
            "成功 evidence 包含 `.ko`、`modinfo`、module 出現在 `lsmod`/sysfs、"
            "本次 init/exit log、invalid parameter load 被拒絕，以及卸載後 module 消失。"
            "它不能證明 IRQ、DMA、hot-unplug 或長時間併發安全。"
        ),
        debug=(
            "確認目前確實在 Linux，而不是 macOS。",
            "比較 `uname -r/-m` 與 `/lib/modules/$(uname -r)/build`。",
            "查看 `modinfo` 的 architecture/vermagic/license。",
            "讀本次新增的 kernel loader log，而不是只看 shell 的 `Invalid module format`。",
            "檢查 module signing、Secure Boot/lockdown 與權限。",
        ),
        tools=(
            ("make/kbuild", "產生 target-kernel module", "不證明可成功 load"),
            ("modinfo", "查看 metadata/vermagic/parameters", "不執行 init"),
            ("insmod/rmmod", "觸發 load/unload lifecycle", "不自動驗證 resource 無洩漏"),
            ("dmesg/journalctl -k", "觀察 kernel loader/init/exit", "不是穩定程式 ABI"),
        ),
        pcie=(
            "這一關建立 probe/remove 前的共同地基：每個後續 PCI resource 都要有 acquire、"
            "failure unwind 與 teardown。對應 `pcie-study` P3-01 與 P2-08。"
        ),
        misconceptions=(
            ("module init 就是 main", "Init 是 loader callback；成功後 module resident，但不靠 init 持續執行。"),
            ("init 失敗會呼叫 exit", "失敗 init 必須自行撤銷已取得資源。"),
            ("有 `.ko` 就一定能載入", "還受 architecture、vermagic、signature、policy 與 dependency 影響。"),
        ),
        limits=(
            "Lab00 沒有 open fd、IRQ、work、timer 或 DMA producer，因此 exit 很簡單。",
            "`__init`/`__exit` 是 section/lifetime annotation，不是 runtime one-shot lock。",
            "實際 module policy 依 kernel config、distribution 與 Secure Boot。",
        ),
        takeaways=(
            "Init 成功才有正常 exit；failed init 自己 unwind。",
            "Compile、load、observable behavior 是三層證據。",
            "後續每取得一個 resource，都要立即想清楚對應 cleanup。",
        ),
        questions=(
            "Init 回負 errno 時，exit 是否會自動執行？",
            "`.ko` compile success 能證明什麼、不能證明什麼？",
            "遇到 `Invalid module format` 應依什麼順序查？",
            "module parameter 在何時被解析，誰負責進一步 range validation？",
            "Lab00 為後續 PCI driver 建立哪個最重要習慣？",
        ),
        answers=(
            "不會；init/error labels 必須只釋放本次已成功取得的 resources，再回負 errno。",
            "能證明 source 在指定 headers/toolchain 下可產生 module；不能證明 target kernel 接受、init 成功或 runtime 正確。",
            "先查 running kernel/architecture/build tree，再查 modinfo vermagic、dmesg loader error、signature/lockdown 與 policy。",
            "基本型別 parsing 在 module load/init 前後由 parameter infrastructure 處理；device/lab-specific range 仍由 init 驗證。",
            "把 lifecycle 寫成 acquire → publish → quiesce → release，並讓每個 failure point 有對稱 unwind。",
        ),
        sources=(
            "Linux external modules: <https://docs.kernel.org/kbuild/modules.html>",
            "Kernel parameters: <https://docs.kernel.org/core-api/kernel-parameters.html>",
            "Module signing: <https://docs.kernel.org/admin-guide/module-signing.html>",
            "Current source: `labs/00-hello-module/driver_lab_hello.c`",
        ),
    ),
    lab(
        path="labs/01-debugfs-logging/README.md",
        title="01 — debugfs、seq_file 與可控制的 logging",
        conclusion=(
            "Lab01 建立 driver 可觀測性：用 debugfs 導出狀態與測試入口，"
            "用 `pr_info()`/`pr_debug()` 與 dynamic debug 控制 log。"
            "Debugfs 是開發介面，不是穩定產品 UAPI；helper 與 callback 共用 state 時仍需相同 synchronization。"
        ),
        status=(
            "Current source 使用 atomic scalar knobs 與 mutex-protected message；"
            "smoke test 驗 entry、trigger、status、dynamic-debug optional path 與 cleanup。"
        ),
        problem=(
            "只有大量 printk 很難知道 driver 現在的 state，也會擾動 timing。"
            "但單純建立 debugfs 檔案也不夠：讀寫 callback、helper 與 unload 必須共享正確 lifetime/synchronization。"
        ),
        terms=(
            ("debugfs", "kernel 開發與除錯用 filesystem surface", "不承諾 stable UAPI"),
            ("seq_file", "安全產生可分段讀取文字輸出的 helper", "不自動鎖共享 state"),
            ("dynamic debug", "runtime 選擇性開啟 pr_debug/dev_dbg callsite", "不等於 device tracing protocol"),
            ("dentry", "debugfs 目錄/檔案在 VFS 中的 object", "不是普通 userspace fd"),
        ),
        model=(
            "把 `trigger` 想成測試按鈕，`status` 是儀表板，dynamic debug 是可單獨打開的詳細記錄。"
            "按鈕、儀表與內部 state 必須使用同一套 synchronization，卸載時先移除入口再釋放 state。"
        ),
        flow=(
            "module init\n"
            "→ 建立 debugfs directory/files\n"
            "→ userspace read/write 進 file_operations callbacks\n"
            "→ 同步更新 counter/message\n"
            "→ status/log 可觀察\n"
            "→ remove_recursive 後才結束 module lifetime"
        ),
        source_map=(
            "- `driver_lab_debugfs_logging.c`：`dl_status_show()`、`dl_trigger_write()`、debugfs init/exit。\n"
            "- `test.sh`：mount/debugfs、entry existence、trigger/state/log 與 unload。\n"
            "- `scripts/mount-debugfs.sh`：只處理 debugfs mount prerequisite。"
        ),
        pattern=(
            "```text\n"
            "write trigger\n"
            "→ callback validates/copies input\n"
            "→ update shared state under its synchronization\n"
            "→ read status obtains a consistent snapshot\n"
            "→ unload removes entries before backing state disappears\n"
            "```"
        ),
        wrong=(
            "錯誤做法是讓 `debugfs_create_atomic_t()` 操作一個 scalar，"
            "但另一條 callback 用 plain read/write 修改同一變數；兩條路徑沒有共享同一 synchronization contract。"
        ),
        run=(
            "```sh\n"
            "cd labs/01-debugfs-logging\n"
            "./test.sh\n"
            "```\n\n"
            "手動路徑：載入後讀 `status`，寫 `trigger`，再讀 `trigger_count`；"
            "若 `/proc/dynamic_debug/control` 存在，再只開啟本 module 的 `pr_debug()`。"
        ),
        evidence=(
            "Entry 存在、trigger 後 state 改變、log 出現、unload 後 directory 消失，"
            "可證明最小 observation path。不能證明 debugfs ABI 穩定、高併發正確或 logging 對性能無影響。"
        ),
        debug=(
            "確認 debugfs 已掛載且 kernel config 支援。",
            "確認 module init 成功及 root dentry 非 error pointer。",
            "分開查 entry permission、callback return 與 state synchronization。",
            "Dynamic debug 缺席時視為 optional capability，不先判 lab 失敗。",
            "卸載後仍有 entry 時，優先查 cleanup/lifetime。",
        ),
        tools=(
            ("debugfs", "導出 debug state/knob", "穩定產品 ABI"),
            ("seq_file", "格式化文字讀取", "共享 state 互斥"),
            ("dynamic debug", "選擇性啟用 debug callsite", "結構化 event/payload correctness"),
            ("tracepoints/ftrace", "低侵入時序觀察", "自動修正 race"),
        ),
        pcie=(
            "PCIe bring-up 會需要可讀的 BAR/IRQ/DMA state 與動態 log；"
            "這一關先學會『先設計 observation，再 debug』。對應 `pcie-study` P3-02、P2-19。"
        ),
        misconceptions=(
            ("debugfs 是正式 ABI", "它可隨 kernel/driver 改變，產品 UAPI 應使用穩定介面。"),
            ("seq_file 會自動同步", "它處理輸出流程，不知道你的 shared-state invariant。"),
            ("log 越多越容易除錯", "高頻 log 會淹沒訊息、改變 timing 並增加 latency。"),
        ),
        limits=(
            "Debugfs/dynamic debug 可被 config 或 permission 關閉。",
            "本 lab 只處理很小 state，未測高頻多 reader/writer。",
            "產品 driver 常需 tracepoints、devlink/debugfs policy 或 subsystem-specific telemetry。",
        ),
        takeaways=(
            "Debug surface 也有 resource、concurrency 與 cleanup。",
            "可觀測性不是無限制 printk。",
            "同一 state 的所有 access path 必須共享一致 synchronization。",
        ),
        questions=(
            "Debugfs 為什麼不應當 stable UAPI？",
            "seq_file 解決什麼，沒有解決什麼？",
            "寫 `trigger` 後如何證明 callback 真正執行？",
            "Dynamic debug control 不存在時該如何判斷？",
            "卸載時為什麼要先移除 entries？",
        ),
        answers=(
            "它是 kernel debug interface，layout/semantics 可隨版本與 driver 改變，沒有產品 ABI 相容承諾。",
            "它協助產生可分段讀取的文字；不提供 shared state lock、lifetime 或 snapshot consistency。",
            "同時觀察 callback-associated counter/message、run-specific kernel log 與 expected return，而不是只看 write command 成功。",
            "把它視為 kernel optional feature；仍驗基本 debugfs path，不虛構 pr_debug evidence。",
            "避免新 open/read/write 進入，並防止 VFS callback 使用已釋放 backing state。",
        ),
        sources=(
            "Debugfs: <https://docs.kernel.org/filesystems/debugfs.html>",
            "Seq_file: <https://docs.kernel.org/filesystems/seq_file.html>",
            "Dynamic debug: <https://docs.kernel.org/admin-guide/dynamic-debug-howto.html>",
            "Current source: `labs/01-debugfs-logging/driver_lab_debugfs_logging.c`",
        ),
    ),
    lab(
        path="labs/02-char-device/README.md",
        title="02 — Character device、VFS read/write 與 record semantics",
        conclusion=(
            "Lab02 把 `/dev`、cdev 與 `file_operations.read/write` 串起來。"
            "Current implementation 是一個 global text-like record；每次 open 有自己的 file/f_pos，"
            "但它不是 multi-client queue，也不是 production byte stream。"
        ),
        status=(
            "Current source/test 已覆蓋 dev_t、cdev/class/device lifetime、usercopy、"
            "read/write 與 filesystem surfaces；多 reader/queue semantics 不在本 lab 保證內。"
        ),
        problem=(
            "初學者容易把 `/dev/foo` 當普通檔案，或以為 `write()` 進 kernel 後一定完整成功。"
            "另一個常見誤解是 global buffer 加每個 open 的 f_pos 就自然成為可靠多 client stream。"
        ),
        terms=(
            ("dev_t", "major/minor 組合的 device number", "不是 file descriptor"),
            ("cdev", "把 dev_t 與 file_operations 註冊給 VFS", "不自動建立 `/dev` node"),
            ("file_operations", "VFS dispatch 到 driver callback 的 operation table", "不是 userspace callback"),
            ("f_pos", "每個 open file description 的 offset", "不等於 global buffer owner"),
        ),
        model=(
            "把 char device 想成一個共用公告欄：`/dev` 是入口，每次 open 是自己的閱讀狀態，"
            "但公告內容仍可能全域共用。要做 queue，還需 record ownership、per-reader cursor 與 backpressure。"
        ),
        flow=(
            "alloc dev_t\n"
            "→ cdev_add\n"
            "→ class_create/device_create 形成 sysfs + /dev surface\n"
            "→ open/read/write callbacks validate and usercopy\n"
            "→ device_destroy/class_destroy/cdev_del/unregister"
        ),
        source_map=(
            "- `driver_lab_char.c`：registration、global buffer、`dl_read()`/`dl_write()`。\n"
            "- `test.sh`：/dev、sysfs、/proc/devices、read/write/error/unload。\n"
            "- `runtime/include/driver_lab_uapi.h`：後續共用的 fixed-width UAPI types。"
        ),
        pattern=(
            "```text\n"
            "userspace write\n"
            "→ validate count\n"
            "→ copy_from_user before publishing state\n"
            "→ synchronized update\n"
            "→ read rechecks availability and copy_to_user\n"
            "→ return actual bytes or negative errno\n"
            "```"
        ),
        wrong=(
            "錯誤做法：把 userspace pointer 直接 dereference，或 copy_to_user 失敗後仍清掉 global record。"
            "這會造成 fault、安全問題或資料被無聲丟失。"
        ),
        run=(
            "```sh\n"
            "cd labs/02-char-device\n"
            "./test.sh\n"
            "```\n\n"
            "手動觀察 `/dev/driver_lab_char0`、`/sys/class/.../dev`、`/proc/devices`，"
            "再使用 `printf`/`cat` 或 current CLI 進行 read/write。"
        ),
        evidence=(
            "能證明 registration、node/surface、一次 read/write 與 cleanup。"
            "不能由此推論多 reader 公平、message queue、partial-record policy、poll/mmap 或 stable production ABI。"
        ),
        debug=(
            "先確認 module init 與 dev_t/cdev registration。",
            "確認 class/device_create 是否成功及 udev/node permission。",
            "分開查 callback 是否進入、count/offset policy、usercopy return。",
            "檢查 global buffer 與 per-open f_pos 是否符合預期 model。",
            "卸載後確認 /dev/sysfs/proc surfaces 都消失。",
        ),
        tools=(
            ("VFS/cdev", "syscall 到 callback dispatch", "message queue semantics"),
            ("copy_to/from_user", "受檢查跨 boundary copy", "不保證不 fault/sleep"),
            ("mutex", "global record mutual exclusion", "per-client ownership/ordering 的全部設計"),
            ("sysfs/proc/dev", "registration evidence", "callback correctness proof"),
        ),
        pcie=(
            "PCIe driver 的 control/data UAPI 也會經 VFS 或 subsystem interface；"
            "先學會 callback、bytes/errno、per-open/global state。對應 `pcie-study` P1-06、P1-14、P3-03。"
        ),
        misconceptions=(
            ("`/dev` 是磁碟檔案", "它是 VFS 入口，operation 由 driver callbacks 定義。"),
            ("write 成功就一定寫完整", "syscall 可回 partial bytes；本 lab 的 policy 要明確查看。"),
            ("global buffer 是 queue", "沒有 record ownership、capacity、cursor、公平與 backpressure。"),
        ),
        limits=(
            "本 lab 是全域單筆 record 教學模型。",
            "沒有 poll/ioctl/mmap；這些在 Lab03 加入。",
            "產品 UAPI 還需 versioning、permissions、compat、安全與 hot-unplug lifetime。",
        ),
        takeaways=(
            "`/dev` 只是入口，semantics 由 callbacks 定義。",
            "回傳 bytes/errno 是 ABI 的一部分。",
            "要分開 global device state 與 per-open state。",
        ),
        questions=(
            "cdev_add 與 device_create 分別建立什麼？",
            "為什麼不能直接 dereference userspace pointer？",
            "global buffer 與 per-open f_pos 為何不等於 queue？",
            "copy_to_user 失敗時為什麼不應先消費 record？",
            "Lab02 test pass 不能證明哪些 multi-client 性質？",
        ),
        answers=(
            "cdev_add 註冊 dev_t 到 file_operations；device_create 建立 device-model/sysfs surface，udev 才可能建立 `/dev` node。",
            "Userspace address 可能無效、fault 或變動；必須用 usercopy API 並處理未複製 bytes。",
            "f_pos 屬每次 open，但 backing record 全域共享；缺少 per-reader ownership/cursor/capacity/backpressure。",
            "否則 userspace 沒收到資料，driver 卻把唯一 record 清掉，造成 silent loss。",
            "不能證明公平、無 starvation、record framing、concurrent readers/writers 或 production ABI stability。",
        ),
        sources=(
            "Character devices/VFS APIs: <https://docs.kernel.org/core-api/kernel-api.html>",
            "Usercopy guidance: <https://docs.kernel.org/core-api/memory-allocation.html>",
            "Current source: `labs/02-char-device/driver_lab_char.c`",
        ),
    ),
    lab(
        path="labs/03-ioctl-poll-mmap/README.md",
        title="03 — ioctl、poll、blocking read 與 read-only mmap snapshot",
        conclusion=(
            "Lab03 在同一 char device 中分開 data、control、event 與 mapping 四條路徑。"
            "Blocking/poll wakeup 只要求重新檢查 predicate；多 reader 取得 mutex 後仍要 recheck。"
            "Kernel mutex 無法保護 arbitrary userspace mmap load，因此 snapshot 使用 read-only odd/even sequence publication。"
        ),
        status=(
            "Current source/test 已修正 multi-reader、actual PAGE_SIZE、read-only VMA、mprotect rejection、"
            "sequence snapshot 與 fixed-width UAPI；仍需 target runtime/KCSAN 與 VMA lifetime stress。"
        ),
        problem=(
            "把 wakeup 當成『條件一定成立』會讓第二個 reader 錯誤返回；"
            "把 kernel mutex 當成 userspace mmap reader 也會取得的鎖，則會接受 torn snapshot。"
        ),
        terms=(
            ("predicate", "wait/poll 每次醒來重新判斷的 readiness 條件", "wake 本身不等於 ready"),
            ("wait queue", "讓 task sleep 並在 event 後重新排程的 mechanism", "不儲存 payload"),
            ("VMA", "一段 userspace virtual memory mapping 的 kernel object", "不等於 backing page owner"),
            ("sequence publication", "odd=更新中、even=穩定的 snapshot protocol", "不提供 writer mutual exclusion"),
        ),
        model=(
            "把四條 path 想成同一設備的資料窗口、控制表單、門鈴與唯讀儀表板。"
            "門鈴只叫你回來看狀態；儀表板讀者用 sequence 確認前後是同一個完整畫面。"
        ),
        flow=(
            "writer copies input\n"
            "→ mutex protects kernel state\n"
            "→ seq odd\n"
            "→ update snapshot fields\n"
            "→ seq even\n"
            "→ wake waiters\n"
            "→ reader/poll rechecks predicate; mmap reader retries on odd/change"
        ),
        source_map=(
            "- `driver_lab_ioctl_poll_mmap.c`：read/write/ioctl/poll/mmap 與 `dl_sync_shared_page_locked()`。\n"
            "- `runtime/include/driver_lab_uapi.h`：fixed-width UAPI/snapshot layout。\n"
            "- `runtime/src/driver_lab_runtime.c`：userspace atomic snapshot reader。\n"
            "- `test.sh`：two-reader、empty poll、read-only mmap/mprotect 與 cleanup regressions。"
        ),
        pattern=(
            "```c\n"
            "WRITE_ONCE(shared->seq, odd);\n"
            "smp_wmb();              /* odd visible before fields */\n"
            "update_fields();\n"
            "smp_wmb();              /* fields visible before even */\n"
            "WRITE_ONCE(shared->seq, even);\n"
            "```\n"
            "Userspace 先讀 even seq、複製 snapshot、再讀 seq；前後不同或 odd 就 retry。"
        ),
        wrong=(
            "錯誤做法：wait_event 醒來後不在 mutex 下 recheck global record，"
            "或 mmap 成 writable，讓 userspace 可破壞 kernel-published metadata。"
        ),
        run=(
            "```sh\n"
            "cd labs/03-ioctl-poll-mmap\n"
            "./test.sh\n"
            "```\n\n"
            "測試會同時使用 kernel module、runtime/CLI 與多 process helpers；"
            "注意它驗的是本次 run 的 predicates、permissions、snapshot 與 cleanup。"
        ),
        evidence=(
            "Two-reader regression、poll timeout/readiness、mmap read-only、mprotect rejection、"
            "sequence snapshot 與卸載可提供具體 evidence。仍不能證明所有 weak-memory interleaving 或 malicious UAPI input。"
        ),
        debug=(
            "先分清是 read predicate、poll mask、ioctl state 還是 mmap snapshot 問題。",
            "確認 wakeup 前 state 已在 mutex 下發布。",
            "多 reader 問題要看拿鎖後的第二次 predicate check。",
            "mmap 問題要看 PAGE_SIZE、VMA flags、vm_insert_page 與 mapping lifetime。",
            "snapshot 問題保存 begin/end seq 與接受/重試條件。",
        ),
        tools=(
            ("mutex", "kernel writers/readers 的 shared-state invariant", "userspace mmap loads"),
            ("wait queue/wake", "sleep/wakeup 與重新檢查", "條件一定成立"),
            ("smp_wmb + sequence", "snapshot publication order/detection", "writer mutual exclusion"),
            ("VMA flags", "mapping permission/lifetime policy", "device DMA mapping"),
        ),
        pcie=(
            "這四條 path 會演進為 accelerator 的 data/control/event/mapping UAPI；"
            "但 PCIe 還加入 MMIO、IRQ、DMA ownership、IOMMU 與 hot-remove。對應 `pcie-study` P1-10、P1-14、P2-20、P3-04。"
        ),
        misconceptions=(
            ("wake 會讓 poll 成功回 revents=0", "wake 只促使重新評估；條件仍 false 時通常繼續睡。"),
            ("mutex 可保護 mmap reader", "任意 userspace load 不會取得 kernel mutex。"),
            ("頁面一定 4096 bytes", "PAGE_SIZE 依 architecture/kernel build。"),
        ),
        limits=(
            "Sequence protocol 只提供 snapshot consistency detection，不是 general shared-memory transaction。",
            "本 lab 沒有 hot-unplug、multiple devices、VMA close refcount 或 pinned user DMA。",
            "Weak-memory correctness 仍應在 target architecture/KCSAN/stress 下驗證。",
        ),
        takeaways=(
            "Wakeup 與 predicate 是兩件事。",
            "拿到 mutex 後仍要 recheck shared condition。",
            "Mmap reader 需要 publication protocol與 permission/lifetime。",
        ),
        questions=(
            "為什麼兩個 blocking reader 被同一 write 喚醒後，第二個仍要 recheck？",
            "Wakeup 能證明 poll 的什麼？",
            "Kernel mutex 為什麼不能讓 userspace mmap snapshot 一致？",
            "Odd/even sequence reader 的接受條件是什麼？",
            "Read-only VMA 為什麼還要清 VM_MAYWRITE？",
        ),
        answers=(
            "第一個 reader 可能先取得 mutex 並消費 global record；第二個拿鎖時 predicate 已改變，必須繼續等或回 EAGAIN。",
            "只證明 waiter 被要求重新排程/評估；不保證 readiness、成功 return 或 payload。",
            "Userspace load 不參與 kernel lock protocol，可能在 writer 更新中間讀到欄位組合；需要 sequence publication/retry。",
            "開始 seq 為 even，複製後 end 與 begin 相同且仍 even，snapshot 內 seq 也一致。",
            "避免 userspace 之後用 mprotect 把 mapping 升級成 writable，破壞 kernel-owned page。",
        ),
        sources=(
            "Poll/wait queues: <https://docs.kernel.org/driver-api/basics.html>",
            "Memory barriers: <https://docs.kernel.org/core-api/wrappers/memory-barriers.html>",
            "Memory mapping APIs: <https://docs.kernel.org/core-api/mm-api.html>",
            "Current source: `labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c`",
        ),
    ),
    lab(
        path="labs/04-locking-and-races/README.md",
        title="04 — Lost update、mutex、kthread 與停止同步",
        conclusion=(
            "Lab04 把 `counter++` 拆成 read/modify/write，讓你觀察 lost update，"
            "再以 mutex 對照。另一半重點是 lifecycle：shared state 必須在 kthread 啟動前初始化，"
            "exit 用 `kthread_stop()` 等 worker 真正返回後才能釋放資源。"
        ),
        status=(
            "Current source/CLI/test 可重現 unsafe/safe 差異並檢查 startup/stop；"
            "race demo 具機率性，仍需 repeated stress、KCSAN/lockdep 才能增加 evidence。"
        ),
        problem=(
            "只看到最終 counter 小於 expected，容易把問題簡化成『加 mutex 就好』。"
            "但如果 init 在 worker 啟動後清 state，或 exit 未等待 worker，仍會有 lost work/UAF。"
        ),
        terms=(
            ("lost update", "兩個 RMW 使用同一舊值而遺失其中一次更新", "不等於 CPU 不會做 arithmetic"),
            ("critical section", "必須一起維持 invariant 的 code/data 範圍", "不一定等於整個 ioctl"),
            ("kthread", "由 Linux scheduler 執行的 kernel task", "不是 hard IRQ"),
            ("stop synchronization", "要求停止並等待 worker 退出", "不只是寫一個 flag"),
        ),
        model=(
            "把 counter 想成共享帳本：兩人同時讀 10、各自算 11、最後都寫 11。"
            "Mutex 讓整個 RMW 只能一人進行；kthread_stop 則像關門後等最後一位員工離開再拆店。"
        ),
        flow=(
            "initialize shared state\n"
            "→ create device/UAPI resources\n"
            "→ start kthread last\n"
            "→ unsafe or mutex-protected RMW\n"
            "→ reject new work\n"
            "→ kthread_stop waits for return\n"
            "→ free state/resources"
        ),
        source_map=(
            "- `driver_lab_race.c`：unsafe/safe increment、kthread function、init/exit。\n"
            "- `driver_lab_race_uapi.h`：status/control ioctls。\n"
            "- `tests/driver_lab_race_cli.c`：parallel workers 與 expected/observed。\n"
            "- `test.sh`：startup, unsafe/safe, unload/reload gates。"
        ),
        pattern=(
            "```c\n"
            "mutex_lock(&counter_lock);\n"
            "counter++;\n"
            "mutex_unlock(&counter_lock);\n"
            "```\n"
            "正確範圍是整個 read-modify-write；只在 read 或 write 一側加 lock 仍可能 lost update。"
        ),
        wrong=(
            "錯誤做法：`kthread_run()` 後再把 counter/stop state 初始化，"
            "或 exit 只設 stop flag 就立即 destroy device/free memory。Worker 可能已更新被清零，或仍在使用 freed state。"
        ),
        run=(
            "```sh\n"
            "cd labs/04-locking-and-races\n"
            "./test.sh\n"
            "```\n\n"
            "CLI 可切 safe mode、reset、單次 increment 與多 thread race；"
            "同一組 workload 要比較 expected、observed 與 worker lifecycle。"
        ),
        evidence=(
            "Unsafe path 的 deficit 與 safe path 的 expected count 是教學 evidence；"
            "一次沒出現 deficit 不能證明無 data race。卸載無 warning/late worker 也只覆蓋本次 timing。"
        ),
        debug=(
            "先畫所有 execution paths：ioctl callers、kthread、module exit。",
            "把 counter++ 展開成 load/add/store，找共享 invariant。",
            "確認 shared state 在 worker start 前完成初始化。",
            "確認 exit 使用 kthread_stop 等待返回。",
            "使用 repeated stress、KCSAN/lockdep 補機率性 demo。",
        ),
        tools=(
            ("mutex", "RMW mutual exclusion", "IRQ/device completion"),
            ("READ_ONCE/WRITE_ONCE", "特定 access compiler/single-access discipline", "multi-step invariant"),
            ("kthread_stop", "stop request + join-like wait", "其他 timer/IRQ/DMA producer"),
            ("KCSAN/lockdep", "data-race/locking evidence", "邏輯 race 完整 proof"),
        ),
        pcie=(
            "Queue producer、IRQ handler、timeout、remove 也會共享 state；"
            "Lab04 的重點是先建立 concurrency/lifetime map，再進 Lab05～07。對應 `pcie-study` P1-08、P1-09、P3-05。"
        ),
        misconceptions=(
            ("READ_ONCE 能修 counter++", "它不是 atomic RMW 或 lock。"),
            ("測一次結果正確就沒有 race", "不同 interleaving 可能尚未發生。"),
            ("設 stop flag 就能 free", "必須等待 worker/callback 完全退出。"),
        ),
        limits=(
            "本 lab 是單一 teaching counter，不涵蓋 multi-field state machine。",
            "Mutex 適用 task context；hard IRQ 共享 state 需不同 lock/context design。",
            "PREEMPT_RT 與 target scheduling 會改變 timing，但不改變需明確 synchronization 的事實。",
        ),
        takeaways=(
            "Counter++ 是 RMW，不是單一不可分割動作。",
            "初始化要在 producer 啟動前。",
            "釋放前要 stop 並同步 in-flight worker。",
        ),
        questions=(
            "Lost update 如何由兩個 counter++ 交錯產生？",
            "為什麼 READ_ONCE/WRITE_ONCE 不能修正它？",
            "為什麼 state 要在 kthread_run 前初始化？",
            "kthread_stop 比只設 flag 多做什麼？",
            "Safe path 一次通過能證明無 race 嗎？",
        ),
        answers=(
            "兩邊先讀同一舊值，各自加一，再寫回同一結果，因此少一次更新。",
            "它們不把整個 read-modify-write 變原子，也不排除另一 writer 插入。",
            "Worker 可能一啟動就讀寫 state；後初始化會清掉更新或看到未準備資料。",
            "它發出 stop request 並等待 thread function 返回，建立 free 前的 lifetime boundary。",
            "不能；它只提供該次 workload/timing evidence，仍需 repeated stress與 sanitizer。",
        ),
        sources=(
            "Lock types: <https://docs.kernel.org/locking/locktypes.html>",
            "Kthread APIs: <https://docs.kernel.org/driver-api/basics.html>",
            "KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>",
            "Current source: `labs/04-locking-and-races/driver_lab_race.c`",
        ),
    ),
    lab(
        path="labs/08-runtime-library/README.md",
        title="08 — Userspace runtime、CLI 與 UAPI ownership",
        conclusion=(
            "Lab08 不是新的 `.ko`，而是把 Lab02/03 的 raw syscalls 封裝成 userspace runtime/CLI。"
            "正確性重點是 fd/mapping ownership、partial I/O、errno、poll error bits、"
            "mmap size/version 與 close/unmap exactly once。"
        ),
        status=(
            "Current runtime、unit test 與 CLI 可在 CI 以 `-Wall -Wextra -Werror` build；"
            "實際 device smoke 仍需要先載入 Lab02/03 module。"
        ),
        problem=(
            "Wrapper 很容易把 kernel 錯誤隱藏掉：例如假設 read/write 一次完整、"
            "close 失敗後重試舊 fd、poll 只看 POLLIN、或固定 mmap 4096。"
        ),
        terms=(
            ("runtime", "application 與 raw UAPI 之間的 userspace helper layer", "不改變 kernel ABI"),
            ("handle ownership", "哪個 object 負責 close/unmap", "複製 struct 不會自動轉移"),
            ("partial I/O", "成功處理少於 request bytes", "不必然是 fatal error"),
            ("errno", "userspace failure 原因的 thread-local convention", "不是 kernel internal stack trace"),
        ),
        model=(
            "把 runtime 想成翻譯器兼資源管理員：它把 raw ioctl/read/poll/mmap 包成一致 API，"
            "但不能改寫 kernel 的 bytes/errno/ownership contract。"
        ),
        flow=(
            "initialize invalid handle\n"
            "→ open exactly once\n"
            "→ wrapper validates arguments and preserves bytes/errno\n"
            "→ poll/map/read snapshot with returned sizes\n"
            "→ unmap/close exactly once and invalidate ownership"
        ),
        source_map=(
            "- `runtime/include/driver_lab_runtime.h`：public userspace API/handle。\n"
            "- `runtime/src/driver_lab_runtime.c`：open/close/read/write/ioctl/poll/mmap/snapshot wrappers。\n"
            "- `tests/driver_lab_runtime_unit.c`：invalid argument、ownership 與 return convention。\n"
            "- `tests/driver_lab_char_cli.c`：可操作 Lab02/03 的 CLI。"
        ),
        pattern=(
            "```text\n"
            "handle.fd = -1\n"
            "→ open publishes valid fd only on success\n"
            "→ close first invalidates ownership, then calls close(fd)\n"
            "→ caller never retries a stale/reused descriptor number\n"
            "```"
        ),
        wrong=(
            "錯誤做法：把一個已 open 的 handle 複製兩份，兩邊都 close；"
            "或 close 回錯後用同一舊整數重試，可能關到已被其他 thread 重用的新 fd。"
        ),
        run=(
            "```sh\n"
            "make -C runtime clean all\n"
            "make -C runtime test\n"
            "```\n\n"
            "要做 device smoke，再先載入 Lab02/03，使用 `tests/driver_lab_char_cli` 執行 write/read/ioctl/poll/mmap-read。"
        ),
        evidence=(
            "Compile/unit test 能證明 argument/ownership 的一部分 userspace contract；"
            "只有連到 current module 的 smoke 才能證明 UAPI integration。兩者都不代表 production thread safety。"
        ),
        debug=(
            "先區分 wrapper input validation、syscall errno、partial bytes 與 kernel log。",
            "檢查 handle 是否仍 owner，以及 fd 是否已 invalidate。",
            "Poll 同時看 return count 與 POLLERR/POLLHUP/POLLNVAL。",
            "Mmap size 取自 ioctl/status 與 sysconf page size，不硬編碼。",
            "Snapshot retry 問題保存 begin/end seq 與 errno。",
        ),
        tools=(
            ("wrapper", "一致 argument/ownership/error handling", "修正 kernel bug"),
            ("unit test", "無 device 的 userspace invariants", "UAPI runtime integration"),
            ("CLI smoke", "指定 module/UAPI 正常路徑", "concurrent/hostile workload proof"),
            ("errno + bytes", "精確回報 outcome", "自動重試 policy"),
        ),
        pcie=(
            "Accelerator driver 通常需要 userspace runtime 管理 queues、mappings、events 與 handles；"
            "這一關先把 ownership/error semantics 做對。對應 `pcie-study` P1-14、P3-09。"
        ),
        misconceptions=(
            ("Wrapper 可以忽略 partial I/O", "它必須保留或明確完成 retry policy。"),
            ("close 失敗就重試同一 fd", "Linux 可能已釋放 descriptor number；盲目重試危險。"),
            ("固定 4096 可跨平台", "page size 與 UAPI mapping size需 runtime取得。"),
        ),
        limits=(
            "Runtime 目前不是 thread-safe ownership framework。",
            "沒有 ABI negotiation、async queues、pinned memory 或 VFIO/IOMMUFD。",
            "Unit test 不載入 kernel module；integration evidence 必須分開記錄。",
        ),
        takeaways=(
            "Userspace wrapper 也有 resource lifetime。",
            "保留 bytes/errno，不用 convenience 隱藏 contract。",
            "Unit、compile、device smoke 是不同證據層。",
        ),
        questions=(
            "為什麼 open handle 不應直接複製？",
            "Close 前先把 fd 設為 -1 的理由是什麼？",
            "Partial I/O 應如何處理？",
            "Poll 為什麼不能只看 ret > 0？",
            "Unit test pass 能否證明 kernel UAPI integration？",
        ),
        answers=(
            "複製只複製 descriptor number，沒有複製 ownership；兩份 handle 可能 double close。",
            "即使 close 回錯，Linux 可能已釋放 descriptor；先 invalidate 避免危險重試或 double close。",
            "保留實際 bytes 給 caller，或由明確 retry loop 完成；不能默認等於 request count。",
            "還要檢查 revents 中的 POLLERR/POLLHUP/POLLNVAL 與實際 readiness bits。",
            "不能；unit test 只覆蓋 userspace logic，需 current module 的 smoke/runtime evidence。",
        ),
        sources=(
            "open/close/read/write/poll/mmap: <https://man7.org/linux/man-pages/>",
            "Userspace API guidance: <https://docs.kernel.org/userspace-api/index.html>",
            "Current source: `runtime/src/driver_lab_runtime.c`",
        ),
    ),
    lab(
        path="labs/09-stress-and-fault-injection/README.md",
        title="09 — Stress、fault injection 與可信的 regression oracle",
        conclusion=(
            "Lab09 的目標是把『正常跑過一次』提升為可重複驗證。"
            "Stress 放大 timing/resource 問題，sanitizer 增加特定 bug class 的可見性，"
            "fault injection 驗 error/teardown；每個 test 都必須有會可靠失敗的 oracle。"
        ),
        status=(
            "Current scaffold 主要覆蓋 Lab03 parallel/reload；"
            "Lab06 repeated IRQ、Lab07 timeout/reset/IOMMU/SWIOTLB 與完整 fault framework 尚未自動化。"
        ),
        problem=(
            "很多腳本看似穩定，其實用 `|| true` 吞掉 crash、清空全域 dmesg、"
            "或卸載不是本次載入的 module。這種 test pass 只表示腳本走到最後。"
        ),
        terms=(
            ("stress", "反覆/併行放大 timing 與 resource bug", "不覆蓋所有 interleaving"),
            ("fault injection", "可控制地觸發 allocation/timeout/error/remove path", "不是任意讓系統壞掉"),
            ("oracle", "能客觀判定 pass/fail 的 invariant/evidence", "不只是最後一行文字"),
            ("regression", "修正後持續防止同類 bug 回歸的 test", "不等於一次手動重跑"),
        ),
        model=(
            "把 test 想成安全檢查機：不只要正常物件通過，也要故障樣本確實被攔下。"
            "若你不知道 test 在 bug 存在時是否會 fail，就沒有可信 oracle。"
        ),
        flow=(
            "define invariant and ownership\n"
            "→ isolate run-specific logs/resources\n"
            "→ repeat and parallel workload\n"
            "→ inject one controlled fault\n"
            "→ require expected error/cleanup\n"
            "→ scan warnings/sanitizers\n"
            "→ preserve reproducible bug diary"
        ),
        source_map=(
            "- `stress-03-parallel.sh`：Lab03 parallel userspace workload。\n"
            "- `stress-03-reload.sh`：repeated module ownership/load/unload。\n"
            "- `test.sh`：目前 scaffold 的入口與 expected exit handling。\n"
            "- 各 Lab `test.sh`：應作為後續 fault cases 的最小 oracle。"
        ),
        pattern=(
            "```text\n"
            "test records whether it loaded the module\n"
            "→ only unloads what it owns\n"
            "→ captures log position before run\n"
            "→ validates only newly added lines\n"
            "→ accepts only documented success/expected timeout codes\n"
            "```"
        ),
        wrong=(
            "```sh\n"
            "some_command || true\n"
            "dmesg -C\n"
            "rmmod module || true\n"
            "echo passed\n"
            "```\n"
            "它會隱藏真正錯誤、破壞共享系統 log，並可能卸載別人的 state。"
        ),
        run=(
            "```sh\n"
            "cd labs/09-stress-and-fault-injection\n"
            "./test.sh\n"
            "STRESS_ITERATIONS=100 ./stress-03-reload.sh\n"
            "STRESS_WORKERS=8 ./stress-03-parallel.sh\n"
            "```"
        ),
        evidence=(
            "可靠 evidence 需要 exact command、iteration/workers、kernel config、兩 repo SHA、"
            "run-specific stdout/stderr/dmesg 與 cleanup state。沒有 warning 不等於沒有 bug。"
        ),
        debug=(
            "先確認 test 是否真的能在已知 bug/fault 下 fail。",
            "確認 module/resource ownership，不碰別人的 loaded state。",
            "只分析本次新增 logs，處理 ring-buffer wrap。",
            "分開 expected timeout/interrupt 與 unexpected crash/I/O error。",
            "將最小 reproducer 固化為 regression。",
        ),
        tools=(
            ("repeat/parallel", "放大 timing/resource race", "所有 failure mode"),
            ("KASAN/KCSAN/lockdep", "memory/data-race/locking evidence", "device protocol proof"),
            ("fault injection", "指定 error path/recovery", "real hardware 全部 fault"),
            ("bug diary", "保存可重現推理與修法", "自動測試本身"),
        ),
        pcie=(
            "PCIe/accelerator 最危險的 bug 常出現在 timeout、reset、remove、late IRQ/DMA；"
            "Lab09 是把 Lab06/07 從 happy path 推向可面試、可維護 evidence 的入口。對應 `pcie-study` P2-18、P2-19、P3-10。"
        ),
        misconceptions=(
            ("跑越久就一定找到 race", "只增加機率；沒有 oracle/fault coverage 仍可能永遠 pass。"),
            ("Sanitizer 沒報告就安全", "它只覆蓋特定 instrumentation 與執行路徑。"),
            ("可以先清 dmesg 方便測試", "全域 log 是共享資源，應以位置/時間隔離本次訊息。"),
        ),
        limits=(
            "目前主要 stress target 是 Lab03。",
            "真實 IRQ/DMA fault 需要 QEMU/device-specific hooks 或 real hardware。",
            "Stress 結果受 scheduler、CPU count、kernel config 與 machine load 影響。",
        ),
        takeaways=(
            "Test 必須能在壞掉時可靠 fail。",
            "不清全域 log、不卸載非本次擁有的 module。",
            "把 timeout/reset/remove 變成 first-class test。",
        ),
        questions=(
            "為什麼 broad `|| true` 會破壞 oracle？",
            "測試為什麼要追蹤 module ownership？",
            "不清 dmesg 如何隔離本次 logs？",
            "Stress 與 fault injection 各自增加什麼 evidence？",
            "下一個最有價值的 Lab07 fault case 是什麼？",
        ),
        answers=(
            "它把 expected 與 unexpected failure 都轉成成功，test 無法區分 crash、I/O error 或正常 timeout。",
            "避免卸載測試開始前已由其他人/流程載入的 module，破壞共享 state。",
            "記錄 run 前位置/時間，只取新增 lines；若 ring buffer wrap，test 應明確失敗或改用 journal cursor。",
            "Stress 放大 timing/interleaving；fault injection 確認指定 error/cleanup/recovery path。兩者皆非完整 proof。",
            "受控 IRQ/command timeout 後 reset 失敗，驗證 mapping 不被 free、無 DMA UAF，並保存 quiesce evidence。",
        ),
        sources=(
            "KASAN: <https://docs.kernel.org/dev-tools/kasan.html>",
            "KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>",
            "Fault injection: <https://docs.kernel.org/fault-injection/index.html>",
            "Current source: `labs/09-stress-and-fault-injection/`",
        ),
    ),
)

PILOT_PATHS = (
    "docs/concepts/pcie-primer.md",
    "labs/05-pci-edu-mmio/README.md",
    "labs/06-pci-edu-irq/README.md",
    "labs/07-pci-edu-dma/README.md",
)


def table(rows: tuple[tuple[str, str, str], ...]) -> str:
    result = ["| 名詞 | 本章中的意思 | 不代表什麼 |", "|---|---|---|"]
    result.extend(f"| **{a}** | {b} | {c} |" for a, b, c in rows)
    return "\n".join(result)


def tool_table(rows: tuple[tuple[str, str, str], ...]) -> str:
    result = ["| 工具／機制 | 解決什麼 | 不解決什麼 |", "|---|---|---|"]
    result.extend(f"| `{a}` | {b} | {c} |" for a, b, c in rows)
    return "\n".join(result)


def render(item: Lab) -> str:
    misconceptions = "\n\n".join(
        f"### 誤解：{name}\n\n{answer}" for name, answer in item.misconceptions
    )
    limits = "\n".join(f"- {line}" for line in item.limits)
    takeaways = "\n".join(f"{i}. {line}" for i, line in enumerate(item.takeaways, 1))
    questions = "\n".join(f"{i}. {line}" for i, line in enumerate(item.questions, 1))
    answers = "\n".join(f"{i}. {line}" for i, line in enumerate(item.answers, 1))
    debug = "\n".join(f"{i}. {line}" for i, line in enumerate(item.debug, 1))
    sources = "\n".join(f"- {line}" for line in item.sources)

    return f"""# {item.title}

> **定位**：{item.conclusion}
>
> **完成標準**：能畫出 resource/data flow，指出 current source 的入口、live resource、failure unwind、test evidence 與尚未驗證範圍。

## 先講結論

{item.conclusion}

```text
先確認 environment gate
→ 讀 current source 的入口與 resource
→ 跑正常路徑
→ 驗 error/unwind/teardown
→ 保存可重現 evidence
```

## 不確定處與驗證狀態

- **已對照 current source**：本 README 以 pedagogy branch 的 `.c/.h/.sh` 與 test 為準。
- **目前 evidence**：{item.status}
- **仍待驗證**：未附 target kernel/QEMU/repo SHA 與完整 log 的 module 行為，不稱 runtime-verified。
- **架構／device-specific**：遇到 kernel config、QEMU model、real hardware protocol 時回官方文件與實測。

## 這一關要解決什麼問題

{item.problem}

## 名詞先說清楚

{table(item.terms)}

## 心智模型

{item.model}

> **比喻的邊界**：心智模型只幫助理解角色與順序；精確 semantics 仍以 current source、Linux API 與 device protocol 為準。

## Resource 與 data flow

```text
{item.flow}
```

## 從簡單到精確

### Current source map

{item.source_map}

### 第一次讀 source 的順序

1. 找 init/probe/open 或 userspace entry。
2. 列出每一步取得的 resource，以及何時開始 live。
3. 找正常資料／事件路徑。
4. 找每個 failure label 與 teardown。
5. 對照 `test.sh` 的 observable evidence，不先追 generated companion 的固定行號。

## 最小正確範式

{item.pattern}

## 看似合理但錯誤的寫法

{item.wrong}

## 如何執行與觀察

{item.run}

### 能證明／不能證明

{item.evidence}

## Debug order

{debug}

## 工具分工

{tool_table(item.tools)}

## 與 pcie-study 的對應

{item.pcie}

## 常見誤解

{misconceptions}

## 適用邊界與尚未驗證

{limits}

## 第一次閱讀先記住

{takeaways}

## Self-check

{questions}

<details>
<summary>參考答案</summary>

{answers}

</details>

## 來源與查證

{sources}
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    changed: list[str] = []
    for item in LABS:
        path = ROOT / item.path
        if not path.is_file():
            print(f"missing lab README: {item.path}")
            return 1
        content = render(item)
        if path.read_text(encoding="utf-8") != content:
            changed.append(item.path)
            if args.write:
                path.write_text(content, encoding="utf-8")

    all_paths = sorted((*PILOT_PATHS, *(item.path for item in LABS)))
    manifest = "# One repo-relative path per pedagogy-reviewed teaching document.\n" + "\n".join(all_paths) + "\n"
    if args.write:
        MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        MANIFEST.write_text(manifest, encoding="utf-8")

    print(f"lab README rewrite: changed={len(changed)} tracked={len(all_paths)} write={args.write}")
    for path in changed:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
