# 07 — QEMU EDU DMA：讓 device 搬資料，再安全回收資源

> **定位**：在 Lab05 MMIO 與 Lab06 IRQ 基礎上，加入 coherent DMA round-trip，完成第一個
> PCI host-driver teaching loop。
>
> **先備知識**：先理解 Lab05/06，以及
> [`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md) 的DMA段落。
>
> **完成標準**：能分清 CPU pointer、DMA address與EDU-local address；能解釋mask、coherent mapping、
> BME、MMIO command、IRQ、command idle、`dma_rmb()`、payload compare與quiesce-before-free。

## 先講結論

DMA（Direct Memory Access）讓device直接讀寫host memory；CPU不必自己逐byte `memcpy()`。但driver必須
提供device可用的DMA address，並管理address能力、ownership、ordering、completion與lifetime。

Lab07做兩次搬運：

```text
CPU填TX coherent buffer
→ EDU DMA：host RAM → EDU local RAM
→ EDU DMA：EDU local RAM → host RX coherent buffer
→ IRQ + command START bit clear
→ dma_rmb()
→ CPU memcmp(TX, RX)
```

四個證據不能混在一起：

```text
IRQ          ：notification path發生
START clear  ：QEMU EDU-specific engine idle
_dma_rmb()   ：completion後，device writes排在CPU reads前
memcmp       ：address、direction、length與payload結果正確
```

最危險的錯誤是timeout後直接free mapping。若device仍可能使用DMA address，free後記憶體被重用會造成
DMA use-after-free與任意資料毀損。Current source在無法證明quiesce且reset失敗時，寧可保留mapping。

## 不確定處與驗證狀態

- **已由官方文件查證**：DMA mask、`dma_alloc_coherent()`的CPU/DMA雙address、coherent仍需ordering、
  MMIO accessor、IRQ與PCI reset API的通用contract。
- **已對照 Current source**：`driver_lab_edu_dma.c` 使用28-bit預設（可選32-bit fixture）mask、
  單一coherent allocation切TX/RX、late BME、兩方向transfer、IRQ + command-clear、
  `dma_rmb()`與fail-safe quiesce。
- **Compile/static 狀態**：audit branch有external-module compile與script gate。
- **已於 2026-08-16 runtime 驗證**：Lab05–07 smoke 與 forced-SWIOTLB streaming 已在隔離 QEMU EDU guest 實跑；
  IRQ/command timeout、reset failure、repeated load/unload、KASAN/lockdep 與 fault injection 尚未驗證。
- **Device-specific**：EDU 28-bit mask、local RAM offset、command/status/IRQ bits與reset behavior不代表真實硬體。
- **Endianness boundary**：Current target是x86_64 little-endian guest；跨endian target需重新核對QEMU EDU model與accessor。

## 這一關要解決什麼問題

初學者常看到：

```c
void *buffer = kmalloc(...);
device_reg = (u64)buffer;
```

並以為把CPU pointer寫給device就能DMA。這是錯的，因為：

- CPU pointer只在kernel virtual address space有意義；
- Physical page layout可能不連續；
- IOMMU可能把device address映射到不同IOVA；
- Platform可能使用bounce buffer；
- Device只能表示有限address bits；
- Mapping還有direction、ownership與lifetime。

即使address正確，也還要回答：

```text
CPU何時把buffer交給device？
Device何時真的完成？
CPU何時可以讀回？
Timeout / remove時，何時才可以free？
```

## 名詞先說清楚

| 名詞 | 本關中的意思 | 不代表什麼 |
|---|---|---|
| **CPU virtual address** | Kernel CPU code可dereference的pointer | Device不能直接使用 |
| **DMA address (`dma_addr_t`)** | DMA API回傳、device放進register/descriptor的address | 不保證等於physical address |
| **EDU-local address** | EDU內部RAM aperture使用的device內部offset | 不是host DMA mapping |
| **DMA mask** | Hardware能表示的DMA address bits | 不是設越大越安全 |
| **Coherent allocation** | CPU/device能互見、免一般per-transfer cache maintenance的mapping | 不免除ordering/completion/lifetime |
| **BME** | Bus Master Enable，允許device發出memory transaction | 不建立mapping、不啟動engine |
| **Direction** | 資料是host→device或device→host | 不等於source/destination欄位可隨意交換 |
| **Completion evidence** | IRQ、status、OWN、CQ、engine idle等device protocol證據 | 不自動保證payload正確 |
| **`dma_rmb()`** | 完成後排序device writes與CPU reads | 不主動等待device完成 |
| **Quiesce** | 阻止新DMA並證明/等待in-flight使用結束 | 不只是clear BME |
| **DMA UAF** | Mapping free後device仍使用舊address | 可能破壞其他subsystem記憶體 |

## 心智模型

### CPU地址、物流地址與倉庫內編號

```text
CPU pointer      = 員工在辦公室內使用的座位編號
DMA address      = 物流公司看得懂的配送地址
EDU local 0x40000= EDU自己倉庫裡的貨架編號
```

三者可能數值不同，不能互相cast。

搬運前，driver必須：

```text
確認物流車能表示這個address範圍（DMA mask）
→ 取得CPU與device各自的address view
→ 準備IRQ/completion state
→ 最後才允許device bus mastering
```

搬運後，不能只聽到門鈴（IRQ）就拆掉倉庫；要確認engine idle、同步software path，再free mapping。

> **比喻的邊界**：真實DMA還有scatter-gather、IOMMU permissions、cache lines、descriptor rings、
> multi-queue、ATS/PASID等；本lab只教最小coherent single-buffer path。

## 先備 gate

在能看見EDU的Linux guest：

```sh
uname -m
uname -r
lspci -Dnn | grep '1234:11e8'
test -e "/lib/modules/$(uname -r)/build"
```

並先確認：

- Lab05 BAR/identity path正確；
- Lab06 IRQ status/ACK path正確；
- EDU未被其他driver擁有；
- 同名module未載入；
- Guest是本repo目前承諾的x86_64 little-endian target。

## Resource 與 data flow

### Setup

```text
pci_enable_device()
→ validate/request/map BAR0
→ verify EDU identity
→ dl_edu_dma_configure_mask()（28 預設；32 僅限配對 fixture）
→ dma_alloc_coherent(TX + RX)
→ allocate IRQ vector / request handler
→ clear stale pending source
→ pci_set_master()（最後，準備開始device-originated traffic）
```

### Coherent allocation的兩個回傳值

```c
void *dma_buf;
dma_addr_t dma_handle;

dma_buf = dma_alloc_coherent(dev, total_bytes, &dma_handle, GFP_KERNEL);
```

- `dma_buf`：CPU pointer；
- `dma_handle`：device DMA address；
- TX = base；RX = base + 256 bytes；
- Current source確認整個TX/RX range都落在設定好的mask（28 預設或 32 fixture）內，
  避免截斷或錯誤假設。

### RAM → EDU

```text
CPU填TX pattern
→ program SRC = tx_dma
→ program DST = EDU local 0x40000
→ program COUNT
→ normal iowrite32(START | IRQ)
→ wait IRQ
→ wait START bit clear
```

### EDU → RAM

```text
program SRC = EDU local 0x40000
→ program DST = rx_dma
→ START | FROM_DEVICE | IRQ
→ wait IRQ
→ wait START bit clear
→ dma_rmb()
→ CPU讀RX
→ memcmp(TX, RX)
```

### Error / teardown

```text
clear BME，阻止新的device-originated traffic
→ 若command仍in-flight，bounded wait START clear
→ 若仍active，嘗試pci_reset_function()
→ ACK pending source
→ synchronize/free IRQ
→ 只有在quiescence可證明時才free coherent mapping
→ unmap/release BAR
→ disable device
```

若reset失敗且無法證明idle：

```text
保留coherent mapping
→ 避免device寫入已被重用的記憶體
→ 需要reboot/platform recovery回收
```

這是安全優先的teaching fail-safe，不是production recovery設計。

## 從簡單到精確

### 1. DMA mask是真實hardware限制

```c
ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(28));
if (ret)
    fail;
```

Mask告訴DMA subsystem：device最多能產生哪些address bits。若device只支援28-bit，宣稱64-bit可能讓driver
拿到device無法表示的address，造成截斷與錯誤DMA。

QEMU EDU current teaching model使用28-bit限制；記錄QEMU version，因model behavior可能變動。

Current source透過唯讀module parameter `dma_address_bits` 支援 28（預設）或 32
兩種值，其他值在probe時回傳 `-EINVAL`：

```c
static unsigned int dma_address_bits = DL_EDU_DMA_ADDRESS_BITS;
module_param(dma_address_bits, uint, 0444);

static int dl_edu_dma_configure_mask(struct dl_edu_dma_dev *dl)
{
	if (dma_address_bits != DL_EDU_DMA_ADDRESS_BITS &&
	    dma_address_bits != DL_EDU_DMA_ADDRESS_BITS_MAX)
		return -EINVAL;
	dl->dma_mask = DMA_BIT_MASK(dma_address_bits);
	return dma_set_mask_and_coherent(&dl->pdev->dev, dl->dma_mask);
}
```

32-bit不是「driver變強」：EDU的DMA register仍只有32-bit，且host必須以
`-device edu,dma_mask=0xffffffff` 啟動，module才應以 `dma_address_bits=32` 載入。

### 2. CPU pointer與DMA address分離

```c
dl->dma_buf = dma_alloc_coherent(..., &dl->dma_handle, ...);
```

絕對不要：

```c
dma_addr = virt_to_phys(cpu_ptr);
dma_addr = (dma_addr_t)cpu_ptr;
```

DMA API可能建立IOMMU mapping、bounce與platform offset；driver只使用回傳的 `dma_addr_t`。

### 3. Host DMA address與EDU local address分域

RAM→EDU：

```text
source      = tx_dma（host DMA address）
destination = 0x40000（EDU內部RAM offset）
```

EDU→RAM：

```text
source      = 0x40000
destination = rx_dma
```

`0x40000`不是host physical/DMA address。Direction寫反可能仍有IRQ或command變化，所以最後必須compare payload。

### 4. 為什麼BME最後才開

BME授權device發出DMA與MSI/MSI-X等memory transaction。若mapping、handler、state尚未ready就打開，
misbehaving device可能提早存取host memory或送event。

Current source在：

```text
BAR/identity ready
→ DMA mapping ready
→ IRQ handler ready
→ 最後pci_set_master()
```

這縮短device有bus-master權限但driver尚未準備好的時間。

### 5. RAM→device為什麼沒有固定 `dma_wmb()`

本lab不是descriptor ownership ring。它是：

```text
CPU寫一塊coherent TX buffer
→ normal iowrite32()寫MMIO START command
```

Default mapping的normal accessor已提供prior coherent/normal memory writes→MMIO trigger ordering。
因此Current source刻意不加沒有對應ownership publication的cargo-cult：

```c
dma_wmb();
wmb();
iowrite32(START, ...);
```

真正descriptor ring則不同：

```text
填descriptor fields
→ dma_wmb()
→ publish OWN/VALID
→ normal doorbell
```

Barrier必須對應實際protocol，不是看到DMA就固定套公式。

### 6. Device→RAM為什麼在completion後做 `dma_rmb()`

Current path先：

```text
IRQ arrived
→ START bit clear
```

建立QEMU EDU-specific completion/idle evidence；接著：

```c
dma_rmb();
```

才讓CPU消費device-written RX data。`dma_rmb()`不是wait；放在completion之前不能讓device加速完成。

### 7. IRQ、command clear與compare各自必要

- IRQ可遺失/錯配或只是notification；
- START clear是engine state，但不驗資料內容；
- compare能抓source/destination/direction/count/data錯誤，但前提是CPU在正確completion/order後讀取。

所以三者一起使用，而不是選一個。

### 8. Handler為什麼只ACK DMA bit

EDU status可能包含其他event bit。Handler：

```text
read status
→ handled = status & DMA_IRQ_MASK
→ ACK only handled
→ read-back status
→ complete waiter
```

這避免丟掉unrelated source，並確保legacy level-like ACK推進。

### 9. Clear BME不等於所有DMA已停止

`pci_clear_master()`阻止新的bus-master transaction，但不能普遍證明已發出的transaction、device internal command或
queue state已完成。Current source還會bounded wait command clear，必要時function reset。

### 10. `pci_reset_function()`也不是萬能production recovery

Helper處理PCI function reset與相關PCI config state，但不重建device-specific firmware、queue、software ownership或
application protocol。Current EDU lab把它當teaching fallback；真實device要有自己的abort/reset/reinit state machine。

## 最小正確範式

### Address views

```c
void *cpu_addr;
dma_addr_t dma_addr;

cpu_addr = dma_alloc_coherent(dev, size, &dma_addr, GFP_KERNEL);
if (!cpu_addr)
    return -ENOMEM;

/* CPU uses cpu_addr; device registers use dma_addr. */
```

### Submit / complete / consume

```text
CPU準備buffer
→ normal MMIO command
→ wait matching device completion
→ dma_rmb()（device→CPU coherent data path需要時）
→ CPU驗資料
```

### Safe teardown

```text
reject new work
→ clear BME / stop device
→ prove command idle or reset
→ ACK source
→ synchronize IRQ
→ free mapping only when quiescence is proven
```

## 看似合理但錯誤的寫法

### 錯誤 1：把CPU pointer寫進DMA register

```c
iowrite32((u32)(uintptr_t)dma_buf, DMA_SRC);
```

CPU virtual pointer不是device address；IOMMU/platform下可能完全無意義。使用DMA API回傳的 `dma_handle`。

### 錯誤 2：Mask一律設64-bit

```c
dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
```

若hardware只有28-bit，這是虛假宣告，可能拿到不可表示address。Mask必須符合device能力。

### 錯誤 3：收到IRQ立刻讀RX，不確認command/status

IRQ可能不匹配或只是notification。依device protocol確認START clear/ownership/CQ，再做ordering與consume。

### 錯誤 4：Timeout後直接free

```c
if (timeout)
    dma_free_coherent(...);
```

Device可能仍持有address，造成DMA UAF。先quiesce/reset；無法證明時不可重用該memory。

### 錯誤 5：`dma_rmb()`放在等待前當作wait

Barrier只排序access，不讓hardware完成。先observe completion，再barrier，再讀data。

### 錯誤 6：所有doorbell前固定加兩個barrier

沒有descriptor OWN publication時可能多餘，也可能掩蓋真正缺少的read-back/completion。依accessor/mapping/protocol選擇。

## 如何執行與觀察

```sh
cd labs/07-pci-edu-dma
./test.sh
```

### Current test 檢查

- 不卸載非本次載入的module；
- 不清空global `dmesg`；
- 驗EDU enumeration、bind與 `/proc/interrupts`；
- 要求兩方向transfer完成；
- 要求 `round-trip compare passed`；
- timeout、warning、sanitizer、unproven quiescence視為失敗。

### 成功證據

```text
dma mask configured to 28 bits
coherent buffer allocated
ram-to-edu transfer finished
edu-to-ram transfer finished
round-trip compare passed
device removed
07-pci-edu-dma smoke test passed
```

### 這個 test 不能證明

- streaming/SG DMA；
- descriptor ring/OWN/phase；
- multi-queue/MSI-X/NUMA；
- user-pinned memory/IOMMU security；
- hot-unplug/AER/PM；
- deterministic timeout/reset failure；
- real hardware firmware/PHY/performance；

### 選用：forced-SWIOTLB streaming probe（`test-swiotlb.sh`）

`test.sh` 只走 coherent allocation 路徑。`test-swiotlb.sh` 是預設不跑的
streaming TX 驗證：在獨立、無 IOMMU、`swiotlb=force` 的 guest 中，以
`dma_map_single()` 映射整頁、由 EDU 搬前 256 bytes、unmap 後再讀回比較，並要求
kernel 的 `swiotlb_bounced` trace 顯示 `FORCE`。

```sh
cd labs/07-pci-edu-dma
./test-swiotlb.sh                    # 預設 28-bit EDU（guest RAM <= 256 MiB）
EDU_DMA_ADDRESS_BITS=32 ./test-swiotlb.sh   # 32-bit 專用 fixture
```

- module parameter `dma_address_bits` 只接受 28（預設）或 32；32 必須搭配 host
  launcher 的 `EDU_DMA_ADDRESS_BITS=32`（`-device edu,dma_mask=0xffffffff`）。
- boot log 檢查相容兩種訊息：舊 `PCI-DMA: Using software bounce buffering for IO (SWIOTLB)`
  與 kernel 6.8+ 的 `software IO TLB: `。
- `swiotlb_bounced` 的 `dev_addr` 是 bounce 前的原始位址；要看 bounce 後實際
  mapped address，以 driver log 的 `streaming TX map established: ... dma=0x...` 為準。
- 32-bit fixture 會以 sysfs 回讀 module parameter，並要求 driver log 的 mapped DMA
  address 高於 `0x0fffffff`；低位址結果不接受為 32-bit 證據。
- 256 MiB guest 編譯 module 若 OOM，先在 guest 建 1 GiB swapfile；不改變 fixture 語意。
- 所有條件都依 architecture/QEMU version 核對；換版本需重新驗證。

## Debug order

```text
1. Lab05 BAR/identity正常
2. Lab06 IRQ/status/ACK正常
3. dma_set_mask_and_coherent return
4. dma_alloc_coherent CPU pointer / DMA handle
5. 整個range是否落在設定好的mask（28 或 32）內
6. BME是否在mapping/handler ready後啟用
7. RAM→EDU source/destination/count/direction
8. IRQ status與ACK
9. START bit是否清除
10. EDU→RAM source/destination/count/direction
11. dma_rmb位置
12. memcmp差異
13. timeout時clear master / wait / reset / retained mapping
14. remove後有無late IRQ、warning、UAF或resource殘留
```

## 工具分工

| 工具／API | 解決什麼 | 不解決什麼 |
|---|---|---|
| DMA mask | 宣告device address能力 | 建立mapping/啟動DMA |
| `dma_alloc_coherent()` | CPU pointer + DMA address、coherent view | ownership order/completion |
| normal `iowrite32()` | prior memory→MMIO command ordering | posted arrival/engine completion |
| `dma_wmb/rmb` | coherent shared-memory ownership ordering | MMIO、wait、resource lifetime |
| IRQ | notification | payload correctness/engine idle |
| START clear | EDU-specific idle evidence | 地址/資料內容正確 |
| `memcmp()` | round-trip payload correctness | teardown安全 |
| `pci_clear_master()` | 阻止新的bus-master transaction | 普遍證明in-flight已結束 |
| `pci_reset_function()` | 嘗試function reset | 重建vendor firmware/queue/software state |
| `synchronize_irq()` | 等in-flight handler退出 | 停DMA engine |
| Retain mapping | 避免unproven-quiesce DMA UAF | 正常可回收的production recovery |

## 與 pcie-study 的對應

- P1-10：coherence、ordering、barrier、completion分層。
- P2-07：normal MMIO accessor與posted read-back。
- P2-10/11：MSI/MSI-X、IRQ status/ACK/teardown。
- P2-12：CPU pointer、physical、DMA address。
- P2-13：coherent vs streaming DMA。
- P2-14：descriptor ownership、doorbell與completion。
- P2-18：reset、quiesce與lifetime。

本Lab刻意是single-buffer EDU command，不是descriptor ring；不要把P2-14的OWN範式硬套成不存在的protocol。

## 常見誤解

### 誤解 1：DMA address就是physical address

- **為什麼錯**：IOMMU、bounce與platform translation可能改變device view。
- **正確說法**：只使用DMA API回傳的 `dma_addr_t`。

### 誤解 2：Coherent代表不用任何barrier

- **為什麼錯**：coherent解cache visibility，不保證不同control fields的ownership order。
- **正確說法**：本single-buffer path用normal accessor；descriptor ring仍在OWN publication前用DMA barrier。

### 誤解 3：IRQ到達等於DMA完成且資料正確

- **為什麼錯**：IRQ是notification，仍要matching status/idle與payload compare。
- **正確說法**：completion與correctness分開。

### 誤解 4：Clear BME後可立即free

- **為什麼錯**：它不普遍證明已發出transaction或internal engine已停止。
- **正確說法**：還要bounded wait、device-specific idle/reset與software synchronization。

### 誤解 5：Memory leak一定比任何情況都糟

- **為什麼錯**：在無法證明DMA quiesce時，free可能造成跨subsystem memory corruption。
- **正確說法**：暫時保留mapping是fail-safe；production需可恢復的stop/reset/reinit。

## 適用邊界與尚未驗證

- 本lab只承諾current x86_64 little-endian QEMU EDU target；跨endian/architecture需重驗。
- EDU local `0x40000`、28-bit mask與command bits是model-specific。
- Coherent single-buffer不能代表streaming/SG、descriptor ring、multi-queue或user memory。
- Function reset能否真正停所有real-hardware DMA取決於device；不能泛化。
- Current fail-safe retain mapping避免UAF，但需要reboot/platform recovery，並非可接受的長期production behavior。
- 尚需deterministic timeout/reset fault、IOMMU on/off、SWIOTLB、KASAN/lockdep與repeated teardown logs。

## 第一次閱讀先記住

1. **CPU pointer與DMA address是兩個view；device只用DMA API回傳address。**
2. **DMA mask要如實描述hardware能力。**
3. **EDU local address不是host DMA address。**
4. **BME最後才開，且不等於mapping/engine/completion。**
5. **IRQ、idle、`dma_rmb()`、compare各驗一層。**
6. **本lab不是descriptor ring，不固定堆cargo-cult barrier。**
7. **未證明quiesce時不能free mapping。**

## Self-check

1. `dma_alloc_coherent()`回傳的CPU pointer與DMA handle分別給誰使用？
2. 為什麼EDU預設使用28-bit DMA mask，何時才允許32-bit？不能直接宣稱64-bit？
3. RAM→EDU與EDU→RAM時，host DMA address與 `0x40000` 各放在哪個欄位？
4. 為什麼本single-buffer start path沒有固定加入 `dma_wmb(); wmb();`？
5. IRQ、START clear、`dma_rmb()`與 `memcmp()`各證明什麼？
6. 為什麼clear BME後仍不能立即free coherent mapping？
7. Reset失敗且無法證明idle時，為什麼Current source選擇保留mapping？

<details>
<summary>參考答案</summary>

1. CPU用virtual pointer dereference；EDU register用DMA handle。兩者可能因IOMMU/platform translation而不同。
2. Mask是device可表示address bits的真實能力；虛假設64-bit可能拿到EDU無法表示且會被截斷的address。
3. RAM→EDU：SRC=tx_dma、DST=0x40000；EDU→RAM：SRC=0x40000、DST=rx_dma。
4. 沒有獨立descriptor OWN publication；default mapping的normal `iowrite32()`已排序prior coherent writes與MMIO start。
   真正ring才在fields與OWN間用 `dma_wmb()`。
5. IRQ=notification；START clear=EDU engine idle；`dma_rmb()`=completion後device writes→CPU reads ordering；
   `memcmp()`=payload/address/direction/count correctness。
6. Clear BME只阻止新的bus-master transaction，不普遍證明已發出的transaction、command或engine已完成。
   還需idle/reset與software synchronization。
7. Free後memory可能被重用，而device仍使用舊DMA address，造成DMA UAF/corruption。保留是較安全fail-safe，
   但production仍需device-specific recovery。

</details>

## 來源與查證

- Current source：`driver_lab_edu_dma.c`
- Current test：`test.sh`
- PCIe primer：[`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- Memory barriers / DMA barriers: <https://docs.kernel.org/core-api/wrappers/memory-barriers.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- PCI APIs / reset: <https://docs.kernel.org/driver-api/pci/pci.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
