# PCIe host driver 白話前導：從裝置出現到安全 teardown

> **定位**：這是進入 Lab05～Lab07 前的共同地圖。先建立 Linux PCI host driver 的完整閉環，
> 再分別深入 MMIO、IRQ 與 DMA。
>
> **先備知識**：知道 Linux kernel module、pointer、function callback 與基本 concurrency 即可。
>
> **完成標準**：能畫出 enumeration → bind → probe → BAR/MMIO → IRQ → DMA → quiesce/remove；
> 能分清 resource、mapping、address、ordering、arrival、completion 與 correctness。

## 先講結論

Linux PCI host driver 的第一個完整閉環是：

```text
PCI core 列舉 device function
→ driver ID table match
→ probe() 取得並驗證資源
→ BAR/MMIO 控制裝置
→ IRQ 接收事件
→ DMA 搬資料
→ remove/error path 先 quiesce，再釋放依賴
```

Lab05、Lab06、Lab07 將這條路徑切成三個可觀察階段：

```text
Lab05：裝置存在、driver bind、BAR/MMIO 可用
Lab06：裝置能通知 CPU，handler 能辨識與 ACK
Lab07：device 能 DMA round-trip，driver 能驗完成與資料
```

最重要的分層是：

```text
MMIO ordering        ≠ PCI posted-write arrival
posted-write arrival ≠ device operation completion
operation completion ≠ payload correctness
coherence            ≠ ownership / lifetime
```

## 不確定處與驗證狀態

- **已由官方文件查證**：Linux PCI resource / driver lifecycle、I/O accessor、IRQ、DMA API 與 QEMU EDU
  register model 的通用 contract。
- **已對照 current source**：
  - `labs/05-pci-edu-mmio/driver_lab_edu_mmio.c`
  - `labs/06-pci-edu-irq/driver_lab_edu_irq.c`
  - `labs/07-pci-edu-dma/driver_lab_edu_dma.c`
- **Compile/static 狀態**：accuracy audit branch 已建立 external-module compile 與 static checks；
  pedagogy branch 加入 teaching structure check。
- **待 runtime 驗證**：尚需在指定 Linux/QEMU EDU guest 執行 Lab05～07、repeated load/unload、
  timeout/reset fault、KASAN/lockdep、IOMMU/SWIOTLB 等測試。
- **Device-specific**：QEMU EDU 是 teaching device；真實 accelerator/NVMe/NIC 的 queue、firmware、reset、
  hot-unplug、AER、PM 與 security contract 不同。

## 這一關要解決什麼問題

初學者常從一段 `probe()` code 開始讀，看到大量 API：

```text
pci_enable_device
pci_request_region
pci_iomap
pci_alloc_irq_vectors
request_irq
dma_alloc_coherent
pci_set_master
```

若只背呼叫順序，很容易產生三種問題：

1. 不知道每個 API 取得了什麼 resource；
2. 不知道 resource 從何時開始可能被其他 context / device 使用；
3. Error / remove 時只把 API 倒著呼叫，卻沒有先停 IRQ / DMA producer。

正確讀法是先問：

```text
誰呼叫這個 path？
哪個 resource 現在開始 live？
誰可能並行使用它？
什麼證據代表成功？
釋放前要先阻止誰繼續使用？
```

## 名詞先說清楚

| 名詞 | 本文件中的意思 | 不代表什麼 |
|---|---|---|
| **PCI function** | Configuration Space 與 BDF 所表示的一個邏輯 function | 不一定等於一張實體卡 |
| **Enumeration** | Firmware/Linux 掃描 hierarchy，建立 `struct pci_dev` | 不代表已有 driver bind |
| **Match / bind** | PCI core 依 ID/class/policy 將 function 交給 driver | 不代表 `probe()` 一定成功 |
| **`probe()`** | Driver core 呼叫的 resource setup / validation callback | 不是 userspace syscall callback |
| **Resource** | BAR range、mapping、IRQ vector、handler、DMA mapping 等有 lifetime 的物件 | 不只是 pointer |
| **BAR** | Function 宣告的 I/O / memory resource window | raw value 不是 kernel pointer |
| **MMIO** | CPU 透過 I/O mapping 存取 device register/window | 不是普通 RAM |
| **IRQ** | Device 通知 CPU 有事件的路徑 | 不自動證明資料正確 |
| **DMA** | Device 透過 DMA address 直接存取 host memory | Device 不能使用 CPU pointer |
| **Ownership** | 某時刻 CPU 或 device 被允許使用 descriptor/buffer | 不只是 cache visibility |
| **Quiesce** | 阻止新工作，並等待/證明既有 in-flight 使用者退出 | 不等於只 disable 一個 bit |
| **Error unwind** | `probe()` 失敗時，只撤銷已成功取得的 resource | 不是無條件呼叫全部 cleanup |
| **Teardown** | Remove/error/recovery 中依 dependency 停 producer、同步、釋放 | 不只是 setup API 倒序 |

## 心智模型

### 一間工廠的三條通道

```text
MMIO = 控制櫃台：下命令、讀狀態
IRQ  = 電鈴：工廠通知「有事件」
DMA  = 貨運通道：工廠直接搬 host memory 中的大量資料
```

Driver 的工作不只「會用三條通道」，還要管理它們的依賴：

```text
MMIO mapping 必須存在，IRQ handler 才能讀/ACK status
IRQ/DMA state 必須存在，device 才能被允許產生 event / bus-master traffic
Device producer 必須先停止，mapping/state 才能被 free
```

> **比喻的邊界**：真實 PCIe 還有 config transaction、TLP/DLLP、IOMMU、cache hierarchy、
> queue protocol 與 error recovery。本 primer 先教 software resource/lifetime，不取代 PCIe specification。

## Resource 與 data flow

### Setup：Lab05 → Lab07 逐步增加資源

```text
pci_enable_device()
→ validate BAR flags / length
→ pci_request_region()
→ pci_iomap()
→ verify device identity
→ allocate IRQ vector / request handler
→ configure DMA mask / allocate DMA mapping
→ pci_set_master()（在需要 device-originated traffic 且狀態已準備好後）
```

不是所有 lab 都取得全部資源：

- Lab05 到 MMIO 為止；
- Lab06 再加入 vector / handler，必要時啟用 BME；
- Lab07 再加入 coherent DMA mapping 與 bus mastering。

### Normal operation

```text
CPU 準備 memory / command state
→ normal MMIO accessor 寫 command / doorbell
→ device 執行
→ IRQ / status / ownership 表示事件或完成
→ CPU 讀結果
→ compare / checksum 驗內容
```

### Error unwind

若 `probe()` 在某一步失敗，只撤銷之前已成功的 resource。例如 mapping 失敗時：

```text
已成功：enable + request region
未成功：iomap、IRQ、DMA

所以撤銷：release region → disable device
```

不能 free 尚未取得的 handler / mapping，也不能漏掉已取得的 resource。

### Teardown

有 IRQ/DMA 後，dependency 比「API 反向順序」重要：

```text
拒絕新 submission
→ mask / ACK / stop device producer
→ 必要時 read-back 確認 control write 到達
→ 證明 DMA engine / queue idle 或執行可證明的 reset
→ synchronize IRQ / work / waiter
→ free handler / vector / DMA mapping
→ unmap / release BAR
→ disable function
```

## 從簡單到精確

### 1. 裝置先被列舉，driver 才有 match 對象

Linux PCI core 掃描 hierarchy，讀 Configuration Space，建立 `struct pci_dev`。Driver 以 ID table 宣告支援：

```c
static const struct pci_device_id ids[] = {
    { PCI_DEVICE(0x1234, 0x11e8) },
    { }
};
```

兩邊都 ready 且 policy允許時，driver core 才 bind 並呼叫 `probe()`。

因此：

```text
Guest 內 lspci 看不到 1234:11e8
→ 沒有可 match 的 pci_dev
→ 先查 QEMU / guest enumeration
→ 不要先改 probe() source
```

Driver先載入或device先出現都可收斂到 match/bind；這不等於所有實體 PCIe slot 支援任意 hotplug。

### 2. BAR 有三個 view

```text
raw BAR register
→ PCI core resource
→ __iomem mapping
```

Driver 應使用：

```c
pci_resource_flags(pdev, bar);
pci_resource_len(pdev, bar);
pci_request_region(pdev, bar, name);
regs = pci_iomap(pdev, bar, 0);
```

Raw BAR 含 encoding；resource 是 kernel 管理的 range；mapping 才是交給 I/O accessor 的 token。

### 3. MMIO 不是普通 pointer store

```c
u8 __iomem *regs;
value = ioread32(regs + STATUS);
iowrite32(command, regs + CONTROL);
```

Accessor 處理 architecture-specific I/O operation、width、endianness與 ordering。`u8 __iomem *` 讓
`+ 0x20` 清楚代表 0x20 bytes。

Normal accessor、posted arrival 與 device completion分開：

```text
iowrite32() 返回       ：CPU完成 accessor submission
same-device read-back  ：prior posted write 到達相應 point
DONE/IDLE/IRQ/CQ       ：device-specific operation completion
memcmp/checksum        ：payload correctness
```

### 4. IRQ 是通知路徑

Linux常見：legacy INTx、MSI、MSI-X。

```c
nvec = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
irq = pci_irq_vector(pdev, 0);
ret = request_irq(irq, handler, flags, name, dev);
```

Handler基本責任：

```text
讀 status
→ 判斷是否自己的事件
→ ACK / mask device source
→ 保存少量狀態 / 喚醒 waiter
→ 快速返回
```

Shared INTx不屬於自己時回 `IRQ_NONE`。Hard IRQ不能呼叫可能睡眠的 API，較重工作交給 threaded IRQ /
workqueue 等適合機制。

Teardown先停 source，再等待 in-flight handler，最後才 free state / unmap MMIO。

### 5. DMA address 不是 CPU pointer

```c
void *cpu_addr;
dma_addr_t dma_handle;

cpu_addr = dma_alloc_coherent(dev, size, &dma_handle, GFP_KERNEL);
```

- CPU dereference `cpu_addr`；
- Device register / descriptor 使用 `dma_handle`；
- IOMMU、bounce buffer 或 platform translation 可能讓兩者數值完全不同；
- 不使用 `virt_to_phys()` 或 pointer cast 代替 DMA API。

DMA mask是hardware address能力的真實宣告：

```c
ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(28));
```

不是「設越大越安全」。失敗就不能照原路繼續 DMA。

### 6. Coherent 不等於不用 ordering

Coherent mapping免除一般 per-transfer cache flush/invalidate，但 descriptor欄位與 OWN/VALID publication仍可需：

```c
desc->addr = dma_addr;
desc->len = len;
dma_wmb();
WRITE_ONCE(desc->owner, DEVICE_OWNS);
writel(tail, doorbell);
```

`dma_wmb()`排序 coherent memory，normal `writel()`處理 memory→MMIO ordering；`dma_wmb()`不會等待device，
也不會 flush PCI posted write。

### 7. Completion 與 payload correctness 必須分開

Lab07 round-trip 使用：

```text
IRQ arrived
→ EDU START bit clear
→ dma_rmb()
→ memcmp(TX, RX)
```

- IRQ：notification；
- START clear：EDU-specific engine idle；
- `dma_rmb()`：completion後的 device-write→CPU-read ordering；
- `memcmp()`：資料真的正確。

### 8. BME（Bus Master Enable）只是一項授權

`pci_set_master()` 允許 function 發出 device-originated memory transaction，例如 DMA，MSI/MSI-X 也屬
memory write。它不會：

- 建立 DMA mapping；
- 自動啟動 engine；
- 配置 vector / handler；
- 證明 in-flight DMA 已停止。

因此 current source 在需要時、且 handler/mapping/state ready 後才啟用，teardown則先阻止新 bus-master traffic，
再證明 engine idle或 reset。

## 最小正確範式

### 範式 A：最小 PCI/MMIO probe

```c
ret = pci_enable_device(pdev);
if (ret)
    return ret;

if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
    pci_resource_len(pdev, 0) < REQUIRED_BYTES) {
    ret = -ENODEV;
    goto err_disable;
}

ret = pci_request_region(pdev, 0, KBUILD_MODNAME);
if (ret)
    goto err_disable;

regs = pci_iomap(pdev, 0, 0);
if (!regs) {
    ret = -ENOMEM;
    goto err_release;
}
```

這建立 resource validation、ownership與mapping；還要驗 device identity與 register protocol。

### 範式 B：IRQ teardown dependency

```text
mask / ACK device source
→ read-back（若需完成 posted control write）
→ synchronize_irq()
→ free_irq()
→ free vectors
→ unmap MMIO
```

Read-back 與 `synchronize_irq()` 解不同問題。

### 範式 C：DMA lifecycle

```text
set truthful mask
→ allocate / map memory
→ install IRQ / completion state
→ enable bus mastering
→ submit
→ prove completion
→ verify payload
→ quiesce device / software users
→ free mapping
```

## 看似合理但錯誤的寫法

### 錯誤 1：`lspci` 看不到 EDU 就改 ID table

- **為什麼看起來合理**：以為 match ID 寫錯。
- **缺少的模型**：沒有 enumeration 就沒有 `pci_dev`，任何 ID table 都無法 bind。
- **修正**：先查 QEMU command、guest、machine topology與 `lspci`。

### 錯誤 2：raw BAR cast 成 pointer

```c
regs = (void __iomem *)raw_bar;
```

Raw BAR含encoding且未經resource translation/claim/mapping。使用PCI core APIs。

### 錯誤 3：收到 IRQ 就 free DMA buffer

IRQ只表示事件；仍要匹配status/command/queue，確認device不再使用address，並同步其他software users。

### 錯誤 4：remove只把setup API倒著呼叫

若device仍能發IRQ或DMA，先free state會造成late handler或DMA use-after-free。先quiesce producer與in-flight user。

## 如何執行與觀察

### Environment gate

在 Linux guest：

```sh
uname -m
uname -r
lspci -Dnn | grep '1234:11e8'
test -e "/lib/modules/$(uname -r)/build"
```

### Lab run

```sh
(cd labs/05-pci-edu-mmio && ./test.sh)
(cd labs/06-pci-edu-irq && ./test.sh)
(cd labs/07-pci-edu-dma && ./test.sh)
```

### 成功證據

- Lab05：probe、BAR mapping、identity、liveness、remove；
- Lab06：request handler、matching status、ACK、bounded completion、remove；
- Lab07：truthful mask、coherent mapping、兩方向 transfer、IRQ、command idle、round-trip compare、remove。

### 這些 smoke tests 不能證明

- 所有 architecture / QEMU version；
- sustained interrupt rate / no-loss；
- deterministic race absence；
- timeout/reset所有分支；
- real hardware signal integrity、firmware、AER、hotplug、PM；
- production security / UAPI / multi-queue correctness。

## Debug order

```text
1. Host / guest / kernel headers
2. PCI enumeration：lspci 是否看得到 function
3. Driver ownership：是否已有其他 driver bind
4. probe entry / return code
5. BAR type、length、claim、mapping、identity
6. MMIO offset、width、endianness、posted read-back
7. IRQ vector、mode、pending source、handler、ACK
8. DMA mask、CPU pointer vs dma_addr_t、direction、BME
9. completion / idle / payload compare
10. quiesce、late IRQ、in-flight DMA、resource leak/UAF
```

不要從第9層的payload錯誤直接隨機改第3層ID table；依dependency縮小範圍。

## 工具分工

| 工具／API | 解決什麼 | 不解決什麼 |
|---|---|---|
| `lspci` / sysfs | enumeration、BDF、resource與driver ownership觀測 | driver內部 race |
| `pci_request_region()` | BAR resource ownership | mapping / register protocol |
| `pci_iomap()` | I/O mapping | access width / endianness |
| normal I/O accessor | MMIO access與default ordering | posted arrival / device completion |
| safe read-back | prior posted write arrival | engine idle / payload correctness |
| IRQ status / handler | notification與source identification | DMA address correctness |
| DMA API | device address、mapping、coherency contract | wait-for-device / ring concurrency |
| `dma_wmb/rmb` | coherent ownership ordering | MMIO / posted completion |
| `synchronize_irq()` | 等in-flight handler退出 | 停device source |
| command idle / reset | device-specific quiesce evidence | 自動重建firmware/queue |
| `memcmp` / checksum | payload correctness | resource lifetime |

## 與 pcie-study 的對應

- P1-10：memory ordering、acquire/release、coherence vs ordering。
- P2-05/06：Configuration Space、BAR raw/resource/mapping。
- P2-07：MMIO accessor、relaxed access、posted read-back。
- P2-08/09：probe/remove、bring-up、dependency-driven teardown。
- P2-10/11：INTx/MSI/MSI-X、IRQ vector與handler lifecycle。
- P2-12～14：DMA address、coherent/streaming、ownership、doorbell與completion。
- P2-18：reset、surprise removal、PM與teardown邊界。

`pcie-study` 提供概念 contract；本 repo 的 current source / test 將它們變成可執行證據。

## 常見誤解

### 誤解 1：BDF 是裝置永久 ID

- **為什麼錯**：BDF 是此次 topology / enumeration 結果，QEMU參數或硬體拓撲改變可能不同。
- **正確說法**：以vendor/device/class/serial/subsystem等合適identity與sysfs查找，不硬編BDF。

### 誤解 2：BAR就是一段可直接dereference的記憶體

- **為什麼錯**：raw BAR、resource、mapping分層，MMIO有device semantics。
- **正確說法**：validate、claim、map，再用I/O accessor。

### 誤解 3：IRQ表示資料一定完成且正確

- **為什麼錯**：IRQ是notification，仍需matching status / ownership / payload validation。
- **正確說法**：completion與correctness分開驗。

### 誤解 4：coherent buffer不用barrier

- **為什麼錯**：coherent解cache visibility，不保證descriptor fields與OWN publication order。
- **正確說法**：依ownership protocol使用DMA barrier。

### 誤解 5：`pci_clear_master()`後可立即free

- **為什麼錯**：它阻止新bus-master traffic，不普遍證明已發出的transaction或engine已idle。
- **正確說法**：還要device-specific idle/reset與software synchronization。

### 誤解 6：QEMU Lab07通過就等於production driver完成

- **為什麼錯**：EDU是單device teaching path，缺multi-queue、streaming SG、hot-unplug、AER、PM、firmware recovery等。
- **正確說法**：它證明可重現的基礎閉環與debug evidence，不是production claim。

## 適用邊界與尚未驗證

- Current runtime target是能看到QEMU EDU `1234:11e8` 的Linux guest；macOS只能當host/editor，不能load `.ko`。
- Cross-ISA host/guest通常使用TCG，不假設KVM/HVF能加速不同ISA。
- QEMU EDU register behavior與endianness需依實際QEMU version/target查證；不泛化為真實PCIe規格。
- Real hardware需要vendor datasheet、firmware/queue/reset state machine、AER/DPC、PM、hot-unplug、IOMMU security、
  pinned user memory、multi-vector/NUMA與全面fault injection。
- Audit與pedagogy branch目前仍缺完整runtime logs；任何「已驗證」聲明必須附kernel、QEMU、兩repo SHA、
  command與完整evidence。

## 第一次閱讀先記住

1. **先有 enumeration，才有 match / bind / probe。**
2. **Raw BAR、resource、`__iomem` mapping 是三個 view。**
3. **MMIO、IRQ、DMA 是控制、通知、搬運三條不同通道。**
4. **CPU pointer 與 DMA address 不可混用。**
5. **Ordering、posted arrival、operation completion、payload correctness 分層處理。**
6. **Teardown先quiesce producer與in-flight users，再free dependencies。**
7. **Smoke pass是證據之一，不等於production correctness。**

## Self-check

1. Guest內 `lspci` 看不到EDU時，為什麼修改 `probe()` 不能讓它被呼叫？
2. Raw BAR、PCI resource、`__iomem` mapping各自是什麼？
3. `writel()`返回、same-device read-back、DONE IRQ、`memcmp()`各證明哪一層？
4. Device為什麼不能使用kernel CPU pointer做DMA？
5. Coherent DMA免除了什麼？仍未免除什麼？
6. IRQ/DMA teardown為什麼不能只把setup API倒序呼叫？
7. Lab07通過後，哪些production能力仍未被證明？

<details>
<summary>參考答案</summary>

1. `lspci`反映PCI enumeration；沒有 `struct pci_dev` 就沒有match target，driver core不會進probe。
   先修QEMU/guest topology或enumeration。
2. Raw BAR是Config Space encoding；resource是PCI core解析/轉換後管理的range；`__iomem` mapping是
   driver交給I/O accessor的mapping/token。
3. `writel`是accessor submission/order；read-back是prior posted arrival；DONE IRQ/status是device-specific
   completion；`memcmp`驗payload correctness。
4. CPU pointer只在CPU virtual address space有意義；device需要DMA API建立的 `dma_addr_t`，其中可能含
   IOMMU、bounce或platform translation。
5. Coherent免除一般per-transfer cache maintenance並提供互見；仍需mask/address separation、ownership、
   ordering、completion、concurrency與quiesce-before-free。
6. Device或handler可能仍在使用MMIO/state/mapping。先停止source、證明engine idle、同步in-flight users，
   才能安全free。
7. 尚未證明streaming/SG、多queue MSI-X、NUMA、hot-unplug、AER/PM、firmware/reset recovery、security、
   real-board PHY與全面stress/fault behavior。

</details>

## 來源與查證

- Current source：
  - `labs/05-pci-edu-mmio/driver_lab_edu_mmio.c`
  - `labs/06-pci-edu-irq/driver_lab_edu_irq.c`
  - `labs/07-pci-edu-dma/driver_lab_edu_dma.c`
- Current tests：各lab的 `test.sh`。
- Linux PCI guide: <https://docs.kernel.org/PCI/pci.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- Memory barriers: <https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- MSI guide: <https://docs.kernel.org/PCI/msi-howto.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
