# 05 — QEMU EDU PCI/MMIO：第一次讓 driver 控制 PCI device

> **定位**：這一關建立最小 PCI host-driver 閉環：device被列舉、driver bind、`probe()`取得BAR，
> 再用MMIO讀寫兩個register。
>
> **先備知識**：先讀 [`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)。
>
> **完成標準**：能解釋 enumeration、match/bind、BAR validation、resource claim、mapping、accessor、
> posted read-back，以及 `probe()` / error unwind / `remove()` 的resource lifetime。

## 先講結論

Lab05只做一件事：證明 Linux guest 真的看得到 QEMU EDU，而且 current driver 能安全取得 BAR0，
完成最小 register read/write/read-back。

```text
EDU被guest列舉
→ PCI ID match / bind
→ probe()
→ validate BAR0 type / length
→ claim resource
→ map成 __iomem
→ read identification
→ write/read liveness
→ remove時 unmap / release / disable
```

這一關**不做 IRQ，也不做 DMA**。先把「裝置存在、driver接手、register path可用」證明清楚，
再進 Lab06 / Lab07。

## 不確定處與驗證狀態

- **已由官方文件查證**：PCI probe/resource流程、`pci_request_region()`、`pci_iomap()`、I/O accessor與
  posted-write read-back的通用contract。
- **已對照 Current source**：`driver_lab_edu_mmio.c` 已驗BAR type/length、EDU signature與liveness。
- **Compile/static 狀態**：audit branch有external-module compile、ShellCheck、Markdown link與whitespace gate。
- **待 runtime / fault-injection 驗證**：仍需在你的Linux/QEMU guest執行 `test.sh`、保存完整log，並做
  repeated load/unload與錯誤注入。
- **Device-specific**：ident/liveness offsets與inverse行為只屬於QEMU EDU，不是所有PCIe device通用規格。

## 這一關要解決什麼問題

常見錯誤路徑是：

```text
看不到 probe log
→ 立刻修改 driver source
```

但 `probe()` 被呼叫之前，必須先成立：

1. QEMU把EDU放進guest PCI hierarchy；
2. Guest PCI core成功enumerate function；
3. `lspci`能看到 `1234:11e8`；
4. Function未被其他driver佔用；
5. Driver ID table match；
6. Driver core才呼叫 `probe()`。

如果 `lspci` 根本看不到EDU，問題在driver之前。這一關首先教你分辨「environment/enumeration問題」與
「probe/resource/MMIO問題」。

## 名詞先說清楚

| 名詞 | 本關中的意思 | 不代表什麼 |
|---|---|---|
| **Enumeration** | Guest PCI core找到function並建立 `struct pci_dev` | 不代表已有driver bind |
| **Match / bind** | ID table與function匹配，driver core把device交給driver | 不代表probe一定成功 |
| **`probe()`** | Driver core呼叫的setup callback | 不是userspace直接呼叫 |
| **BAR0** | EDU提供的第一個MMIO resource window | Raw BAR值不是kernel pointer |
| **Resource claim** | `pci_request_region()`宣告本driver擁有該range | 不等於建立mapping |
| **Mapping** | `pci_iomap()`建立可交給I/O accessor的`__iomem` token | 不等於普通RAM pointer |
| **MMIO accessor** | `ioread32()` / `iowrite32()`等device I/O API | 不知道device register protocol |
| **Identification register** | EDU offset `0x00`，用來確認映射到預期model | 不是全域PCI identity機制 |
| **Liveness register** | EDU offset `0x04`，讀回最近write的bitwise inverse | 不代表IRQ/DMA已可用 |
| **Posted write** | PCI write可先被CPU/bridge接受，稍後才到device | accessor返回不等於arrival |
| **Error unwind** | probe失敗時只撤銷已成功取得的resource | 不是無條件執行全部cleanup |

## 心智模型

### 先找到店面，再租櫃位，最後操作機器

```text
lspci看見EDU       = 確認商場裡真的有這間店
ID match / bind     = 店家與管理員確認由這個driver接手
pci_request_region  = 登記這個櫃位由我使用
pci_iomap            = 拿到可操作櫃台設備的控制介面
ioread/iowrite       = 透過正式按鈕讀寫設備
```

你不能只看到BAR address，就直接把它當成普通pointer；也不能在店面不存在時，靠改 `probe()` 讓它出現。

> **比喻的邊界**：真實 PCI hierarchy、host bridge translation、page-table/I/O mapping與PCI transaction
> 比商場比喻複雜；這裡只建立 enumeration、ownership、mapping 的順序。

## 先備 gate

在 **Linux guest** 內執行：

```sh
uname -m
uname -r
lspci -Dnn | grep '1234:11e8'
test -e "/lib/modules/$(uname -r)/build"
```

判讀：

- `uname`：確認你真的在要load module的guest，不是在macOS host；
- `lspci`失敗：先修QEMU/guest enumeration，不改driver；
- kernel build tree不存在：先準備與 `uname -r` 相符的headers/build tree；
- EDU已被其他driver bind：先釐清ownership，不由test擅自卸載別人的module。

## Resource 與 data flow

### Setup

```text
pci_enable_device()
→ validate BAR0 is IORESOURCE_MEM
→ validate BAR0 length covers 0x00 / 0x04 32-bit registers
→ pci_request_region()
→ pci_iomap()
→ ioread32(IDENT)
→ verify EDU signature
→ iowrite32(LIVENESS)
→ ioread32(LIVENESS)
→ compare inverse
```

### Resource 何時開始 live

| Resource | 取得後代表什麼 | Cleanup |
|---|---|---|
| Enabled function | PCI function可被driver使用 | `pci_disable_device()` |
| Claimed BAR0 | 其他driver不應同時使用該resource | `pci_release_region()` |
| `bar0` mapping | Driver可以透過accessor碰register | `pci_iounmap()` |

Lab05沒有IRQ/DMA producer，因此teardown比後面單純。

### Error unwind

```text
BAR validation失敗：disable device
request region失敗：disable device
iomap失敗：release region → disable device
identity/liveness失敗：iounmap → release region → disable device
```

每個label只撤銷已成功的步驟。

### Teardown

```text
pci_iounmap()
→ pci_release_region()
→ pci_disable_device()
```

因為本關沒有IRQ/DMA，unmap前只需確保沒有其他current software path仍在存取MMIO。Lab06/07則必須先
quiesce device producer與in-flight handler/DMA。

## 從簡單到精確

### 1. Match table是driver宣告，不是掃描命令

```c
static const struct pci_device_id dl_edu_mmio_ids[] = {
    { PCI_DEVICE(0x1234, 0x11e8) },
    { }
};
```

`MODULE_DEVICE_TABLE(pci, ...)`匯出modalias資訊；`module_pci_driver()`包裝register/unregister。
Driver core負責match並呼叫 `probe()`。

### 2. Per-device state集中管理lifetime

```c
struct dl_edu_mmio_dev {
    struct pci_dev *pdev;
    u8 __iomem *bar0;
    resource_size_t bar0_len;
    u32 ident;
    u32 liveness_written;
    u32 liveness_read;
};
```

`u8 __iomem *`讓 `bar0 + 0x04` 明確是4-byte offset。State由 `pci_set_drvdata()` / `pci_get_drvdata()`
在probe/remove間傳遞。

### 3. Mapping前先驗type與length

```c
if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM))
    return -ENODEV;

if (pci_resource_len(pdev, 0) < REQUIRED_BYTES)
    return -ENODEV;
```

這避免：

- 把I/O-port resource當MMIO；
- 讀寫超出BAR範圍；
- 因QEMU/model/topology錯誤仍盲目access。

### 4. Claim與map是兩件事

```text
pci_request_region()：resource ownership
pci_iomap()          ：I/O mapping
```

只有map沒有claim可能與其他owner衝突；只有claim沒有map則沒有可供accessor使用的address token。

### 5. Identification先驗「這是不是預期device model」

```c
ident = ioread32(bar0 + IDENT_REG);
if ((ident & SIGNATURE_MASK) != SIGNATURE)
    fail;
```

Vendor/device ID match仍可能因model/version、endianness、offset、wrong function等因素讀出不預期值。
Signature check是第二層防線。

### 6. Liveness write/read各證明什麼

```c
iowrite32(pattern, bar0 + LIVENESS_REG);
value = ioread32(bar0 + LIVENESS_REG);
expected = ~pattern;
```

- write：發出MMIO transaction；
- same-device read：形成prior posted write的read-back point；
- compare：驗證EDU liveness register的inverse語意。

它不能證明：

- 中斷可用；
- DMA可用；
- 所有register offsets正確；
- real hardware行為；
- long-running device command完成。

## 最小正確範式

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

value = ioread32(regs + IDENT_REG);
```

這個範式的核心不是API背誦，而是：

```text
validate → claim → map → accessor
```

## 看似合理但錯誤的寫法

### 錯誤 1：`lspci`看不到EDU，仍一直改ID table

- **為什麼看起來合理**：以為vendor/device ID寫錯。
- **缺少的contract**：沒有enumerated `pci_dev`就沒有match target。
- **修正**：先修QEMU command、guest machine與enumeration。

### 錯誤 2：直接把raw BAR cast成pointer

```c
regs = (void __iomem *)raw_bar;
```

Raw BAR含encoding，且尚未經PCI core resource translation、claim與mapping。

### 錯誤 3：用普通pointer / `volatile`讀寫

```c
*(volatile u32 *)(regs + 4) = pattern;
```

缺少architecture I/O、endianness、ordering與sparse contract。使用 `iowrite32()`。

### 錯誤 4：read-back後宣稱device所有工作完成

Liveness read只驗本register語意與prior write arrival；不代表其他engine或command完成。

## 如何執行與觀察

```sh
cd labs/05-pci-edu-mmio
./test.sh
```

### Current test 流程

1. 確認Linux、`lspci`與sysfs中的EDU；
2. 拒絕卸載非本次載入的同名module；
3. build module；
4. 保存測試前kernel log位置；
5. `insmod`並確認bind；
6. `rmmod`並確認driver sysfs entry消失；
7. 只分析本次新增log；
8. gate probe、BAR mapping、ident、liveness、remove；
9. 遇到BUG/WARNING/KASAN/KCSAN/Oops/UAF則失敗。

### 成功證據

```text
probe start
BAR0 mapped
ident=...
liveness check passed
device removed
05-pci-edu-mmio smoke test passed
```

BDF、BAR length與完整ident值依環境，不應寫死。

### 這個 test 不能證明

- IRQ / DMA path；
- repeated stress下沒有race；
- 所有QEMU / architecture；
- real hardware register protocol；
- hot-unplug、AER、PM、reset；
- production security或performance。

## Debug order

```text
1. 是否在正確Linux guest
2. kernel headers/build tree是否匹配
3. lspci是否看見1234:11e8
4. 是否已有其他driver bind
5. module register / probe是否進入
6. BAR type / length
7. request region conflict
8. iomap結果
9. identification signature
10. liveness offset / width / endianness / expected inverse
11. remove / error unwind有無warning或resource殘留
```

## 工具分工

| 工具／API | 解決什麼 | 不解決什麼 |
|---|---|---|
| `lspci` | enumeration、BDF與基本resource觀測 | probe內部錯誤 |
| sysfs driver link | bind / ownership | register功能正確 |
| `pci_resource_flags/len` | BAR type / range validation | claim / mapping |
| `pci_request_region()` | resource ownership | access token |
| `pci_iomap()` | 建立mapping | register protocol |
| `ioread32/iowrite32` | 32-bit MMIO access與default ordering | device command completion |
| same-device read-back | prior posted write arrival | IRQ/DMA/payload correctness |
| test.sh / dmesg | normal path evidence | race absence / production proof |

## 與 pcie-study 的對應

- P2-02：PCI topology / BDF。
- P2-05：Configuration Space與device identity。
- P2-06：BAR raw/resource/mapping。
- P2-07：MMIO accessor、posted read-back、completion分層。
- P2-08/09：probe/remove、resource dependency與error unwind。

本Lab把這些概念縮成一個最小可執行閉環。

## 常見誤解

### 誤解 1：Driver載入後就一定會進probe

- **為什麼錯**：還需device enumeration、ID match、unbound/policy允許。
- **正確說法**：先用 `lspci` / sysfs分辨enumeration與binding。

### 誤解 2：BAR address就是CPU virtual address

- **為什麼錯**：raw BAR、resource與`__iomem` mapping不同。
- **正確說法**：透過PCI core validate/claim/map。

### 誤解 3：`iowrite32()`返回就表示EDU已處理

- **為什麼錯**：PCI write可能posted。
- **正確說法**：需要arrival時read-back；operation completion另看device protocol。

### 誤解 4：Liveness pass代表整個PCIe driver完成

- **為什麼錯**：只證明最小MMIO path與特定register語意。
- **正確說法**：IRQ、DMA、error recovery、stress仍是後續gate。

## 適用邊界與尚未驗證

- EDU `1234:11e8`、offset `0x00/0x04`與inverse behavior是QEMU-specific。
- Current source使用 `ioread32/iowrite32`，runtime target需重新確認QEMU model/guest endianness。
- Test目前是smoke gate，仍需repeated load/unload、forced claim conflict、wrong identity與MMIO failure情境。
- 真實device可能有read-to-clear、write-only、split register、power/reset與surprise removal規則。
- 未附guest kernel、QEMU version、repo SHA與完整log前，只能稱source/compile-reviewed，不能稱runtime-verified。

## 第一次閱讀先記住

1. **`lspci`看不到device時，先修enumeration，不先改probe。**
2. **BAR要先validate、claim、map。**
3. **Raw BAR、resource、`__iomem` mapping是三個view。**
4. **MMIO使用accessor，不用普通pointer或`volatile`。**
5. **Write、read-back、device completion是不同層。**
6. **Lab05只驗最小MMIO閉環，不驗IRQ/DMA。**

## Self-check

1. `lspci`看不到EDU時，為什麼修改 `probe()` 不會讓它被呼叫？
2. `pci_request_region()`與`pci_iomap()`各做什麼？
3. 為什麼mapping前要驗BAR type與minimum length？
4. 為什麼base使用 `u8 __iomem *`？
5. Liveness write、read-back、inverse compare各能證明什麼？
6. Probe在identity check失敗時，應撤銷哪些resource？
7. Lab05 smoke pass仍不能證明哪些能力？

<details>
<summary>參考答案</summary>

1. 沒有PCI enumeration就沒有 `struct pci_dev` / match target，driver core不會呼叫probe；先修QEMU/guest。
2. Request region取得BAR resource ownership；iomap建立可交給I/O accessor的mapping。兩者不可互換。
3. 防止把I/O-port或過短resource當成可存取的MMIO，避免wrong-type與out-of-range access。
4. Pointer arithmetic以byte為單位，`bar0 + 0x04`明確表示4-byte offset，不會乘上`u32`大小。
5. Write提交MMIO；read-back使prior posted write到達相應point；compare驗EDU liveness特定語意。
   它們不驗IRQ/DMA或任意device command完成。
6. 此時已enable、claim、map，所以要iounmap、release region、disable device；未取得IRQ/DMA，不能free它們。
7. 尚未證明IRQ、DMA、stress/race、timeout/reset、hot-unplug/AER/PM、real hardware與production安全/效能。

</details>

## 來源與查證

- Current source：`driver_lab_edu_mmio.c`
- Current test：`test.sh`
- Debug guide：[`debug-checklist.md`](debug-checklist.md)
- PCIe primer：[`../../docs/concepts/pcie-primer.md`](../../docs/concepts/pcie-primer.md)
- Linux PCI guide: <https://docs.kernel.org/PCI/pci.html>
- Device I/O: <https://docs.kernel.org/driver-api/device-io.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
