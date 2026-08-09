# 06 — QEMU EDU IRQ：讓 device 主動通知 CPU

> **定位**：在 Lab05 已驗證的 PCI/BAR/MMIO 路徑上，加上一條 interrupt notification path。
>
> **先備知識**：完成或理解 Lab05，先讀
> [`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)。
>
> **完成標準**：能說明 vector allocation、Linux IRQ number、handler registration、device status/ACK、
> shared INTx、MSI/MSI-X、BME，以及 remove 前為何要先quiesce source再同步in-flight handler。

## 先講結論

IRQ（Interrupt Request，中斷請求）讓 device 主動告訴 CPU：「有事件要處理」。但收到 IRQ 只代表
notification path 發生，不會自動證明資料正確，也不會自動清除 device 的 interrupt source。

Lab06流程：

```text
先完成Lab05 BAR/MMIO validation
→ allocate一條PCI IRQ vector
→ 清除device殘留pending status
→ request_irq()安裝handler
→ 若選到MSI/MSI-X，啟用Bus Master Enable
→ 寫EDU raise register
→ handler讀status、辨識、ACK、read-back
→ complete()喚醒等待者
→ remove前先清source/BME、同步handler，再free
```

最重要的分層：

```text
Device interrupt source  ≠ Linux IRQ handler registration
ACK posted-write arrival ≠ handler已退出
IRQ arrived              ≠ payload correctness
```

## 不確定處與驗證狀態

- **已由官方文件查證**：PCI IRQ vector allocation、generic IRQ handler lifecycle、shared IRQ回傳值、
  MSI/MSI-X為device-originated Memory Write，以及posted MMIO ACK/read-back的通用contract。
- **已對照 Current source**：`driver_lab_edu_irq.c` 驗BAR/identity、清pending、配置一vector、辨識/ACK
  EDU bit、read-back、completion與quiesce。
- **Compile/static 狀態**：audit branch建立external-module compile與script checks。
- **待 runtime / fault-injection 驗證**：需在guest實跑legacy INTx、MSI/MSI-X、repeated event、timeout、
  repeated unload，以及lockdep/KASAN與late-handler測試。
- **Device-specific**：EDU status/raise/ACK offsets與bit定義只適用QEMU EDU。

## 這一關要解決什麼問題

初學者常把「中斷」想成一個單一物件：

```text
Device發中斷 → handler執行
```

實際上至少有四層：

1. Device內部某個event source變pending；
2. PCI function以INTx/MSI/MSI-X把event送到host；
3. Linux IRQ subsystem把Linux IRQ number dispatch到handler；
4. Handler讀device status，判斷是否真的屬於自己，並依device規格ACK/mask。

如果在 `request_irq()` 前device source已經pending，handler可能在probe尚未完成時立刻被呼叫。
如果handler只回 `IRQ_HANDLED` 卻沒ACK source，legacy level-like interrupt可能持續assert，形成interrupt storm。
如果remove先unmap BAR，再等待handler，late handler可能讀已失效的MMIO。

本關就是建立完整notification lifecycle。

## 名詞先說清楚

| 名詞 | 本關中的意思 | 不代表什麼 |
|---|---|---|
| **Interrupt source** | Device內部造成IRQ的event/status bit | 不等於Linux IRQ number |
| **Vector** | PCI/MSI-X/MSI/legacy資源抽象中的一條中斷路徑 | 不一定等於固定硬體pin |
| **Linux IRQ number** | Kernel用來註冊/dispatch handler的編號 | 不等於PCI vector index或BDF |
| **Handler** | Hard IRQ context中執行的callback | 不能呼叫可能睡眠的API |
| **INTx** | Legacy、通常level-triggered且可shared的interrupt路徑 | 不等於MSI message |
| **MSI/MSI-X** | Device向平台指定address發出Memory Write Request | 不使用實體中斷線 |
| **`IRQ_NONE`** | Shared handler判定事件不是自己的 | 不是「handler失敗」 |
| **ACK** | 依device protocol清除已處理source | 不等於free handler |
| **Read-back** | 確認prior posted ACK到達相應point | 不等於等待handler退出 |
| **Completion object** | Kernel同步工具，讓handler喚醒sleepable waiter | 不等於device completion queue |
| **BME** | PCI Command中的Bus Master Enable，允許device-originated memory transaction | 不會自動產生IRQ或配置handler |
| **`synchronize_irq()`** | 等指定IRQ上目前in-flight handler完成 | 不會停止device產生新source |
| **Quiesce** | 先阻止/清除device source，再同步software path | 不只是free_irq() |

## 心智模型

### 門鈴、總機與值班人員

```text
Device status bit = 門外有人按鈴的原因
INTx/MSI/MSI-X   = 門鈴訊號送進大樓的方式
Linux IRQ number = 總機內部使用的分機號
IRQ handler      = 接電話的值班人員
ACK               = 處理後解除門鈴來源
```

Shared INTx像多間公司共用一條總機線。每個handler都可能被叫到，所以必須先查自己的status；不是自己的
事件就回 `IRQ_NONE`。

> **比喻的邊界**：實際interrupt controller、MSI routing、affinity、masking與PCI ordering更複雜。
> 這個比喻只用來區分device source、transport、Linux dispatch與handler protocol。

## 先備 gate

在Linux guest：

```sh
lspci -Dnn | grep '1234:11e8'
test -e "/lib/modules/$(uname -r)/build"
```

並確認：

- Lab05 identity/liveness path可理解或已通過；
- EDU未被其他driver擁有；
- 同名module尚未載入；
- guest kernel允許取得至少一條IRQ vector。

## Resource 與 data flow

### Setup

```text
pci_enable_device
→ validate/request/map BAR0
→ validate EDU identity
→ pci_alloc_irq_vectors(1, 1, PCI_IRQ_ALL_TYPES)
→ pci_irq_vector(pdev, 0)
→ clear/ACK stale pending source + read-back
→ request_irq()
→ 若實際模式為MSI/MSI-X，pci_set_master()
```

### Self-test data flow

```text
probe reinit_completion()
→ iowrite32(TEST_BIT, RAISE_REG)
→ device產生INTx/MSI/MSI-X
→ Linux dispatch handler
→ handler ioread32(STATUS)
→ 若不是本device bit：IRQ_NONE
→ ACK owned bit
→ read-back STATUS
→ complete(&irq_done)
→ probe waiter醒來並確認source清除
```

### Error unwind

依失敗點撤銷：

```text
request_irq失敗：free vectors → iounmap → release BAR → disable
self-test失敗：quiesce source/BME/handler → free vector → unmap/release/disable
```

### Teardown

```text
ACK / clear device source
→ read-back prior ACK
→ 若MSI/MSI-X曾啟用BME，pci_clear_master()
→ synchronize_irq()
→ free_irq()
→ pci_free_irq_vectors()
→ iounmap / release BAR
→ pci_disable_device()
```

順序重點是：handler仍可能使用 `dl` state與BAR0，所以它必須在這些resource被free/unmap之前退出。

## 從簡單到精確

### 1. Vector allocation不保證選到哪種模式

```c
ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
irq = pci_irq_vector(pdev, 0);
```

實際可能選MSI-X、MSI或legacy INTx，取決於device/platform/kernel policy與能力。Driver不硬編Linux IRQ number。

### 2. Legacy shared IRQ需要unique non-NULL `dev_id`

```c
flags = (pdev->msi_enabled || pdev->msix_enabled) ? 0 : IRQF_SHARED;
request_irq(irq, handler, flags, KBUILD_MODNAME, dl);
```

對shared IRQ，`dev_id`用來區分handler instance，並在 `free_irq(irq, dl)` 時解除正確registration。
Current source使用stable per-device state pointer。

### 3. 為什麼request前先清pending

EDU可能保留舊status。若source在 `request_irq()` 時已assert：

```text
handler剛註冊就立刻執行
→ probe尚未建立預期的self-test state
→ 測試結果與真實event混在一起
```

所以先：

```text
read STATUS
→ ACK stale bits
→ read-back STATUS
```

Read-back確保posted ACK推進，也有助legacy INTx source deassert。

### 4. Handler為何先filter再ACK

```c
status = ioread32(bar0 + STATUS_REG);
handled = status & TEST_IRQ_MASK;
if (!handled)
    return IRQ_NONE;
```

Shared INTx可能呼叫不相關handler；即使MSI/MSI-X通常不shared，filter status仍是確認device event與錯誤診斷的重要protocol。

ACK只寫 `handled` bit，避免把同一status register中的其他合法event一起清掉。

### 5. Hard IRQ context限制

Handler應：

- 不睡眠；
- 不做無界迴圈；
- 不做高頻大量 `dev_info()`；
- 只保存最小state、ACK/mask、喚醒/排程後續工作；
- 較重或可睡工作交給threaded IRQ/workqueue等合適機制。

Current source用ratelimited debug log與 `complete()` 喚醒probe中的sleepable wait。

### 6. ACK後為什麼要read-back

```c
iowrite32(handled, bar0 + ACK_REG);
(void)ioread32(bar0 + STATUS_REG);
```

PCI write可能posted。Handler返回前若ACK尚未到device，legacy INTx source可能仍assert，造成立即重進。
Read-back建立prior ACK arrival point；它不等待其他CPU handler，也不等任意device工作完成。

### 7. BME與MSI/MSI-X

MSI/MSI-X是device發出的Memory Write Request，因此function需要被允許bus-master transaction。
Current source在handler/state ready後才於message-signaled模式啟用BME，並在teardown先clear。

Legacy INTx是不同傳輸機制；本lab不為純legacy路徑無條件打開BME。

BME只是一項授權，不會：

- allocate vector；
- install handler；
- trigger event；
- 保證delivery；
- 證明in-flight event已停止。

### 8. Completion object與device completion不要混淆

```c
complete(&dl->irq_done);
```

這是Linux同步物件，表示handler已觀察並處理本次event，讓probe的 `wait_for_completion_timeout()` 醒來。
它不是PCIe Completion TLP，也不是device completion queue entry。

### 9. 為什麼timeout必須bounded

若IRQ未到，probe不能永遠卡住：

```c
wait_for_completion_timeout(..., timeout)
```

Timeout是failure evidence，不代表可直接free所有resource。Driver仍需quiesce device source、同步可能late到的handler，
再cleanup。

## 最小正確範式

### Handler

```c
static irqreturn_t handler(int irq, void *opaque)
{
    struct device_state *dev = opaque;
    u32 status = ioread32(dev->regs + STATUS);
    u32 handled = status & OWNED_MASK;

    if (!handled)
        return IRQ_NONE;

    iowrite32(handled, dev->regs + ACK);
    (void)ioread32(dev->regs + STATUS);
    complete(&dev->done);
    return IRQ_HANDLED;
}
```

### Teardown

```text
stop/ACK device source
→ ensure posted control write arrived
→ synchronize_irq()
→ free_irq()
→ free dependencies
```

這兩段共同建立完整lifecycle，不能只看handler happy path。

## 看似合理但錯誤的寫法

### 錯誤 1：request_irq前不清pending

可能在probe state尚未ready時立即進handler，或把舊event誤認為self-test成功。

### 錯誤 2：shared handler無條件回IRQ_HANDLED

```c
return IRQ_HANDLED;
```

若status不是自己的，會污染shared IRQ accounting並掩蓋spurious/other-device事件。應回 `IRQ_NONE`。

### 錯誤 3：ACK整個status值，不分owned bit

可能清掉其他interrupt source，造成event loss。只ACK此handler真正處理的bits。

### 錯誤 4：先iounmap BAR，再free/synchronize IRQ

Late handler會access已unmapped MMIO與freed state。先stop source並同步handler，最後才unmap。

### 錯誤 5：收到一次IRQ就宣稱無loss/高效能

Probe-time self-test只證明一個event path；不證明高rate、affinity、coalescing、concurrency或no-loss。

## 如何執行與觀察

```sh
cd labs/06-pci-edu-irq
./test.sh
```

### Current test 檢查

- 不卸載非本次載入的module；
- 不清空全系統 `dmesg`；
- 驗EDU enumeration、bind與 `/proc/interrupts`；
- 要求probe、request handler、status/ACK、self-test、remove；
- timeout、source未清、kernel warning/sanitizer report視為失敗。

### 成功證據

```text
request_irq ok: vector=... mode=...
irq status=... acknowledged
self-test passed count=1
device removed
06-pci-edu-irq smoke test passed
```

Exact IRQ number與mode依平台，不應寫死。

### 這個 test 不能證明

- sustained rate/no-loss；
- CPU affinity/NUMA；
- multiple vectors/queues；
- concurrent remove/reset；
- 所有legacy/MSI/MSI-X模式；
- payload correctness；
- production interrupt mitigation/coalescing。

## Debug order

```text
1. Lab05 BAR/identity仍正常
2. pci_alloc_irq_vectors回傳與選到的mode
3. Linux IRQ number / request_irq return
4. request前stale status是否清除
5. MSI/MSI-X時BME是否ready後啟用
6. RAISE write是否到device
7. handler是否被dispatch
8. status是否含owned bit
9. ACK bit與read-back後source是否clear
10. completion timeout / late handler
11. remove時是否先quiesce再unmap
```

## 工具分工

| 工具／API | 解決什麼 | 不解決什麼 |
|---|---|---|
| `pci_alloc_irq_vectors()` | 取得可用PCI interrupt vector | 安裝handler/觸發event |
| `pci_irq_vector()` | 取得Linux IRQ number | 固定選定MSI或INTx |
| `request_irq()` | 註冊CPU-side handler | 清device pending source |
| device status | 判定event來源 | ACK / software synchronization |
| ACK MMIO | 清device source | 等handler退出 |
| read-back | prior posted ACK arrival | handler synchronization |
| `complete()` | 喚醒sleepable waiter | device payload correctness |
| `synchronize_irq()` | 等in-flight handler結束 | 阻止新device event |
| `free_irq()` | 移除registration | 自動停止device source |
| BME | 允許device-originated memory transaction | 配置vector/handler |

## 與 pcie-study 的對應

- P1-11：hard IRQ context與deferred work。
- P1-12：interrupt storm、polling與NAPI邊界。
- P2-07：MMIO ACK與posted read-back。
- P2-10：INTx/MSI/MSI-X差異。
- P2-11：vector allocation、handler與teardown。
- P2-18：quiesce、reset與resource lifetime。

## 常見誤解

### 誤解 1：Vector index就是Linux IRQ number

- **為什麼錯**：`pci_irq_vector(pdev, index)`才取得kernel IRQ number，數值由平台配置。
- **正確說法**：index是driver向PCI API取第幾條vector的索引。

### 誤解 2：MSI是PCIe Message TLP

- **為什麼錯**：MSI/MSI-X使用Memory Write Request送到配置的message address。
- **正確說法**：它是message-signaled interrupt，但transaction type是Memory Write。

### 誤解 3：ACK寫出後source一定立刻清

- **為什麼錯**：PCI MMIO write可能posted。
- **正確說法**：必要時讀同device safe status形成arrival point，再依device語意確認clear。

### 誤解 4：`free_irq()`前不需要先停device

- **為什麼錯**：Device仍可產生source，造成late/spurious/fallback behavior。
- **正確說法**：先mask/ACK/stop producer，再同步與free。

### 誤解 5：IRQ到了就是工作完成

- **為什麼錯**：Event可能是error、unrelated source或notification早於payload validation。
- **正確說法**：讀matching status/ownership，必要時再驗payload。

## 適用邊界與尚未驗證

- Current QEMU EDU target使用status `0x24`、raise `0x60`、ACK `0x64`；真實device不可套用。
- Current source一次只分配一vector，不涵蓋MSI-X table、多queue affinity與per-CPU/NUMA design。
- Hard IRQ log、locking與PREEMPT_RT語意在production/RT kernel需再設計。
- `synchronize_irq()`與`free_irq()`有各自同步語意；此lab明示dependency，不代表所有driver都需完全相同排列。
- 仍需runtime強制legacy/MSI、repeated events、timeout、unload race、KASAN/lockdep與fault injection。

## 第一次閱讀先記住

1. **Device source、PCI transport、Linux IRQ number、handler是不同層。**
2. **Request handler前先處理stale pending source。**
3. **Shared handler不是自己的event要回 `IRQ_NONE`。**
4. **ACK只清owned bits，posted ACK必要時read-back。**
5. **Hard IRQ只做短且不睡眠的工作。**
6. **Teardown先stop source，再同步handler，最後free/unmap。**
7. **一次self-test IRQ不等於production no-loss。**

## Self-check

1. 為什麼 `request_irq()` 前要先清EDU pending status？
2. Shared INTx handler何時回 `IRQ_NONE`？
3. 為什麼ACK只寫handled bit，並在需要時read-back？
4. MSI/MSI-X為什麼與BME有關？BME又不能替代哪些步驟？
5. `complete()`、read-back與 `synchronize_irq()` 各解什麼問題？
6. Remove為什麼必須先quiesce source，再free IRQ與unmap BAR？
7. Probe-time self-test能證明與不能證明什麼？

<details>
<summary>參考答案</summary>

1. Source若已assert，handler可能在probe其餘state尚未ready時立刻執行，且舊event會污染self-test。
2. 讀status後發現沒有本device/handler所擁有的bit時回 `IRQ_NONE`，讓shared IRQ正確accounting。
3. 只ACKowned bit可保留其他source；read-back讓prior posted ACK到達相應point並有助level-like source deassert。
4. MSI/MSI-X是device-originated Memory Write，需要bus-master授權。BME不會allocate vector、request handler、
   trigger event或保證delivery。
5. `complete()`喚醒waiter；read-back處理posted ACK arrival；`synchronize_irq()`等待in-flight handler退出。
6. Handler仍會使用state/MMIO。先阻止新event並清source，再等所有handler完成，才能安全free/unmap。
7. 證明一個trigger能在timeout內進handler、辨識、ACK並喚醒；不證明高rate/no-loss、affinity、多vector、
   concurrent reset/remove或payload correctness。

</details>

## 來源與查證

- Current source：`driver_lab_edu_irq.c`
- Current test：`test.sh`
- PCIe primer：[`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)
- Linux MSI guide: <https://docs.kernel.org/PCI/msi-howto.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- PCI APIs: <https://docs.kernel.org/driver-api/pci/pci.html>
- Device I/O / posted write: <https://docs.kernel.org/driver-api/device-io.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
