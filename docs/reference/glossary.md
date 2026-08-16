# Driver glossary — 初學者術語速查

> **定位**：這是一份按主題集中、可搜尋的速查表。第一次閱讀仍以 [`START-HERE`](../onboarding/START-HERE.md) 與各 Lab README 為主；遇到陌生詞再回來查，不需要一次背完。

## 先講結論

術語只有在說清楚「誰使用、在哪個 context、解決什麼、不代表什麼」時才有用。下表刻意使用短定義，避免再形成第二套長篇主教材。

## 不確定處與驗證狀態

- Linux API 與 implementation 會隨 kernel version/config/architecture 演進。
- PCIe/device-specific register、queue、reset、firmware semantics 仍需查 datasheet/QEMU source。
- 本表是 navigation/reference，不取代 official docs、current source 或 runtime evidence。

---

## Module / build / logging

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| kernel module | 可在 runtime 載入 Linux kernel 的程式碼 | userspace process |
| `.ko` | external kernel module 常見的 build artifact | 任意 kernel 都能載入 |
| kbuild | Linux kernel 的建置系統 | 普通單檔 `gcc` 命令 |
| `KDIR` | 專案常用的 target kernel build-tree 變數名 | kernel API 固定名稱 |
| build tree | matching headers/config/Makefile/symbol build infrastructure | 只有一個 include 目錄 |
| `module_init()` | 指定 module load 成功路徑的 init callback | 持續執行的 `main()` |
| `module_exit()` | 指定成功載入後卸載時的 cleanup callback | failed init 的自動 cleanup |
| `module_param()` | 宣告 module load/runtime parameter | 自動完成所有 range validation |
| `MODULE_LICENSE()` | module license metadata，會影響 taint/GPL symbols | 法律意見或 runtime safety proof |
| `modinfo` | 顯示 module metadata、parameters、vermagic | 執行 module |
| `insmod` | 直接載入指定 `.ko` | 自動解析所有 dependency |
| `modprobe` | 依 module database/config/dependency 載入 | source build tool |
| `rmmod` | 要求卸載已載入 module | 強制停止所有錯誤 producer |
| vermagic | module 與 target kernel build 特徵的相容資訊 | 唯一 load-policy gate |
| taint | kernel 記錄非標準/錯誤狀態的 flags | taint=0 就完全正確 |
| `pr_info()` | kernel info-level log | 穩定 ABI |
| `pr_debug()` | 可由 dynamic debug 控制的 debug callsite | 零成本 tracing |
| dynamic debug | runtime 選擇性開關 `pr_debug/dev_dbg` | 結構化 trace protocol |
| `dmesg` | 讀 kernel ring-buffer log 的常用工具 | 每次 test 的私有 log |

---

## Filesystem / VFS / UAPI

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| VFS | 將 file-descriptor operations 分派到 filesystem/driver callbacks 的層 | 單一 filesystem |
| `/dev` node | userspace 開啟 char/block device 的常見入口 | 一般磁碟檔案 semantics |
| devtmpfs / udev | 建立/管理 `/dev` nodes 與 policy 的機制 | cdev registration 本身 |
| sysfs | device model 與簡單 attributes 的 filesystem view | 大量 payload channel |
| procfs | process/system report 與部分 legacy interface | 每個 driver 的正式 UAPI |
| debugfs | kernel developer debug state/knobs | stable product ABI |
| `seq_file` | 協助產生可分段讀取的文字輸出 | shared-state lock |
| `dev_t` | major/minor 組成的 device number | file descriptor |
| major/minor | char/block driver family 與 instance number | PCI BDF |
| `struct cdev` | 將 dev_t range 註冊到 `file_operations` | 自動建立 `/dev` node |
| `struct file` | 一個 open file description 的 kernel object | global device state |
| `file_operations` | VFS 呼叫 driver read/write/ioctl/poll/mmap 的 callback table | userspace function pointer |
| per-open state | 每次 open 專屬的 `private_data`、offset、ownership | 全域 device state |
| `f_pos` | 每個 open file description 的 offset | global queue cursor |
| UAPI | userspace 可依賴的 binary/data-layout contract | internal C struct 可任意曝光 |
| ABI | binary calling/data-layout compatibility contract | source-level convenience API |
| ioctl | command-oriented control syscall | 大量高速 payload 的唯一方法 |
| poll | userspace 等待 fd readiness/events 的 API | event 本身的 payload |
| readiness predicate | poll/read 每次醒來重新判斷的條件 | wakeup 一定代表 true |
| wait queue | 讓 task sleep 並在狀態可能改變時 wake 的 mechanism | 保存 message data |
| mmap | 建立 userspace virtual mapping | 自動建立 DMA mapping/ordering |
| VMA | 一段 userspace mapping 的 kernel object | backing page 的唯一 owner |
| usercopy | `copy_to_user/copy_from_user` 等跨 boundary copy | 普通 pointer dereference |
| partial I/O | 成功處理少於 request 的 bytes | 必然 fatal error |
| errno | userspace failure 原因 convention | kernel stack trace |

---

## Execution context / concurrency / lifetime

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| task | Linux scheduler 可排程的 execution entity | 只等於 process |
| process / thread | 由共享 address space/files/signals 等資源關係描述的 tasks | 兩套完全不同 scheduler object |
| task context | 代表某個 schedulable task 執行、通常可依規則 sleep | 任何 kernel code 都可 sleep |
| hard IRQ context | interrupt top-half 執行環境，不可 blocking/sleep | 沒有 `current` 或 `task_struct` |
| workqueue | 由 kernel worker task 執行 deferred work | hard IRQ |
| kthread | driver/kernel 建立的 schedulable kernel task | userspace pthread |
| shared state | 多 execution paths 可存取或共同依賴 lifetime 的 object | 只指 global scalar |
| critical section | 必須一起維持 invariant 的 code/data 範圍 | 整個 callback 必須上鎖 |
| invariant | 多欄位/步驟必須共同成立的條件 | 單一變數值 |
| mutex | 可睡 task context 的 mutual-exclusion lock | hard IRQ lock、device completion |
| spinlock | busy-spin、適合短 atomic/IRQ-shared section 的 lock | 持鎖期間可 sleep |
| atomic operation | 對單一 object 的不可分割 operation | multi-field transaction |
| race condition | 結果依事件順序而可能違反邏輯 | 必然是 data race |
| data race | 未同步 concurrent conflicting accesses 到同一 location | 所有 timing bug |
| lost update | 多個 read-modify-write 使用同一舊值而遺失更新 | CPU arithmetic 錯誤 |
| deadlock | 執行者形成無法前進的等待循環 | 單純慢或 contention |
| starvation | 一個執行者長期得不到 CPU/resource | 單次 latency 高 |
| `READ_ONCE/WRITE_ONCE` | marked single-access/compiler discipline | general barrier、lock、atomic RMW |
| acquire/release | 透過同步變數建立單向 publication ordering | full barrier 或 mutual exclusion |
| memory barrier | 限制特定 memory accesses 的可見順序 | 等待 device 工作完成 |
| completion | kernel 的 event-wait primitive，或泛指 protocol completion evidence | device source ACK、payload correctness |
| in-flight | 已開始但尚未證明退出/完成的工作或使用者 | 只指 CPU 正在執行某行 |
| quiesce | 拒絕新工作、停止 producer、等待既有 users 退出 | 只清一個 flag |
| refcount / kref | object lifetime user accounting | ordering/locking 的全部設計 |
| `kthread_stop()` | 要求 kthread 停止並等待 function 返回 | 停止 IRQ/timer/DMA producer |
| `synchronize_irq()` | 等待已進入的 IRQ handler 完成 | 阻止 device 發出新 IRQ |

---

## Memory / address views

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| CPU virtual address | CPU/kernel code 使用、經 page tables 翻譯的地址 | DMA address |
| physical address | memory/resource system 的實體地址 view | device 一定可直接使用 |
| page | kernel memory management 的基本單位 | 固定永遠 4096 bytes |
| TLB | CPU address-translation cache | page table 本身 |
| TLB miss | translation cache 未命中、可能 hardware page walk | 必然 page fault/kernel entry |
| page fault | translation/permission 需要 OS 處理的 exception | 只代表 swap-in |
| `kmalloc` | 小/中型 kernel allocation，CPU virtual 連續且通常底層連續 | DMA mapping |
| `vmalloc` | CPU virtual 連續、backing pages 可分散 | 單一 physically contiguous segment |
| page allocator | 直接取得一頁或高階連續 pages | device address API |
| GFP flags | allocation context/reclaim/zone constraints | 成功保證 |
| `__iomem` | sparse/type annotation 的 I/O mapping token | 普通 RAM pointer |
| PCI resource | kernel 解析/配置/管理的 device address range | raw BAR bits 或 virtual mapping |
| IOVA | IOMMU domain 中 device 使用的 virtual address | host physical address |

---

## PCIe / BAR / MMIO

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| PCIe fabric | point-to-point links、ports、switches、endpoints 組成的 packet hierarchy | shared parallel bus |
| Root Complex | CPU/memory system 與 PCIe hierarchy 的根 | 單一固定 BDF |
| endpoint | hierarchy 末端提供功能的 device | 一定只有一個 function |
| PCI function | 可由 Configuration Space 枚舉的 logical function | 一整張實體卡 |
| BDF | domain:bus:device.function enumeration address | 永久 device identity |
| Configuration Space | function 的 identity、BAR、capability、command/status 描述區 | device BAR register window |
| capability | Configuration Space 中宣告 optional feature 的 structure | feature 已被 driver 啟用 |
| BAR | Base Address Register，描述 I/O/memory resource requirement/assignment | 可直接解參考的 pointer |
| raw BAR | Configuration register encoding，含 type/prefetch/64-bit bits | kernel resource address |
| prefetchable | 允許特定 speculative/combining semantics 的 BAR attribute | ordinary cached RAM |
| MMIO | CPU 透過 address window 存取 device registers/memory | normal memory semantics |
| I/O accessor | `readl/writel/ioread32/iowrite32` 等 architecture-aware API | device register protocol 本身 |
| relaxed accessor | 省略部分 normal-memory/lock ordering 的 MMIO accessor | 無任何 ordering 的 raw pointer |
| posted write | requester 不等待 Completion TLP 的 write | device 已收到或完成 command |
| read-back | same-device safe read 建立 prior posted-write arrival point | engine 已 idle/firmware 已完成 |
| doorbell | driver 寫入以通知 queue/work 可用的 register | descriptor publication ordering 本身 |
| TLP | Transaction Layer Packet，承載 Memory/Config/Completion 等 transaction | 每段 link ACK/flow-control packet |
| DLLP | Data Link Layer Packet，處理 per-link ACK/NAK/flow control | end-to-end driver command |
| LTSSM | Link Training and Status State Machine | Linux driver state machine |
| AER | Advanced Error Reporting capability/infrastructure | 自動完成 device recovery |
| DPC | Downstream Port Containment | 所有 fatal error 的完整修復 |

---

## IRQ

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| interrupt source | device 內部產生 event 的狀態/bit | Linux IRQ number |
| INTx | legacy level-like、可共享的 interrupt mechanism | message write |
| MSI | device 對配置 message address 發出 Memory Write Request | generic PCIe Message TLP |
| MSI-X | 具有 table/PBA、通常更多 vectors 的 message-signaled mechanism | 自動建立 per-queue software architecture |
| vector | PCI core 配置的 interrupt resource/index | 固定 CPU IRQ number |
| Linux IRQ number | kernel IRQ subsystem 分配給 handler 的 identifier | PCI vector index或BDF |
| handler | IRQ 發生時 kernel 呼叫的 callback | 可以睡的普通 function（hard IRQ） |
| `dev_id` | handler context 與 `free_irq()` matching token | 任意顯示名稱 |
| shared IRQ | 多 device handlers 共用 Linux IRQ，需 filter/`IRQ_NONE` | 所有 handler 都可回 HANDLED |
| ACK | 依 device protocol 清除/確認 handled source | 等待 deferred work 完成 |
| mask | 暫時阻止特定 device source delivery | 清除已 pending event |
| interrupt storm | event/IRQ 固定成本壓垮 CPU/latency | 只代表 log 很多 |
| coalescing | device 合併多事件再通知 | 零 latency cost |
| threaded IRQ | hard handler 配合 kernel thread 執行可睡工作 | 不需 mask/ACK/lifetime |

---

## DMA / IOMMU / queue

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| DMA | device 主動讀寫 host memory | CPU `memcpy` |
| `dma_addr_t` | DMA API 回傳、寫給 device 的 address type | CPU 可解參考 pointer |
| DMA mask | hardware 可表示的 DMA address bits | 設越大越安全 |
| bus mastering / BME | 允許 PCI function 發起 memory transactions | 建立 mapping、start engine、證明 idle |
| coherent DMA | CPU/device views 具 coherent mapping contract | 不需 ownership/ordering/completion |
| streaming DMA | 以 direction、map/sync/unmap 交接 transfer ownership | 只適合大 buffer |
| `dma_sync_*` | 重用 streaming mapping 時交接 CPU/device cache view | 等待 hardware 完成 |
| scatter-gather | 多個 memory segments 映成 device 可使用的 mapped segments | 原始 entries 數一定等於 mapped segments |
| descriptor | 描述 DMA/queue work 的 control structure | 自動具 atomic ownership |
| OWN / VALID / phase | CPU/device 交接 descriptor/completion 的 protocol bits | lock 或 wait-for-hardware API |
| `dma_wmb()` | coherent DMA control data 在 ownership publication 前的 write ordering | flush posted MMIO、wait device |
| `dma_rmb()` | 看到 device 歸還 ownership後，排序其 writes 與 CPU reads | 建立 completion 本身 |
| completion queue / CQ | device 發布完成 records 的 queue | payload 必然正確 |
| queue tail/head | producer/consumer progress indices | 無需 wrap/full/empty protocol |
| IOMMU | device-side address translation與permission enforcement | CPU MMU 或 performance-only switch |
| VFIO | 安全 device assignment/userspace access framework | 直接 mmap BAR 就完成 isolation |
| IOMMUFD | 較新的 userspace I/O address-space/control framework | 所有 kernel 都已採用同一路徑 |
| SWIOTLB | device-addressable bounce buffering mechanism | 永遠表示「4 GiB 以下」 |
| DMA UAF | device 在 memory 已 free/reused 後仍讀寫 | 普通 CPU-only UAF |

---

## Validation / career evidence

| 名詞 | 第一輪定義 | 不代表什麼 |
|---|---|---|
| static check | syntax/style/link/source consistency gate | runtime correctness |
| compile check | 指定 headers/toolchain 下可建置 | module 能 load 或 device 能工作 |
| smoke test | 指定環境的最小正常路徑 | race/error coverage |
| stress test | 重複/併行放大 timing/resource bug | 所有 interleaving proof |
| sanitizer | KASAN/KCSAN 等特定 bug-class instrumentation | 完全無 bug |
| fault injection | 可控制地觸發 allocation/timeout/error path | 真實硬體全部 failure modes |
| regression test | 防止已修根因回歸的可重現 oracle | 只印 pass 的腳本 |
| test oracle | 能客觀區分 pass/fail 的 invariant/evidence | broad `|| true` |
| bug diary | symptom→hypothesis→experiment→evidence→root cause→fix→regression | 流水帳 |
| portfolio evidence | 他人可依 SHA/command/log 重現的作品證據 | README 截圖或自評百分比 |
| transferable skill | 目標職務可直接以 evidence 支持的能力 | 只聽過相關名詞 |
| exposure | 接觸過但未必主導的經驗 | production ownership |
| gap | 目標需求但目前缺少可展示 evidence 的項目 | 永久弱點 |

## 如何查更精確的定義

1. 先回各 Lab README 或 canonical concept，看這個詞在目前 protocol 中的角色。
2. 再看 current source/test 的 function、object、return convention。
3. API/architecture semantics 回 official Linux/QEMU/device docs。
4. 最後用 target runtime evidence 確認，而不是由詞彙名稱猜行為。

## 來源與查證

- Linux driver APIs: <https://docs.kernel.org/driver-api/index.html>
- Memory barriers: <https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
- PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
