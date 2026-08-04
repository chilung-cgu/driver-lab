# 閱讀地圖：現在先看什麼，做到什麼才能前進

> 這份文件只負責「順序與能力gate」。文件總索引看 [`../README.md`](../README.md)，
> 每關進度看 [`learning-dashboard.md`](learning-dashboard.md)。

## 先講結論

不要一次讀完整個repo，也不要先打開所有companion文件。每一關固定走：

```text
1. 先讀README的結論、問題、名詞、心智模型
2. 畫resource / data flow
3. 讀current source與test
4. 實際build / run
5. 保存observable evidence
6. 做一個失敗或邊界實驗
7. 闔上文件回答Self-check
8. 再看companion與延伸
```

目前 `review/pedagogy-pass-2026-08` 已完整改寫PCIe primer與Lab05～07 README；Lab00～04等文件仍以
accuracy audit為技術基線，後續分批遷移。

若source、README、companion不一致，依序相信：

```text
官方文件 / target runtime
→ current source + current test
→ reviewed README / guide
→ generated companion
```

## 先分清你在哪一台機器

```text
Host：啟動QEMU、存放image、編輯code
Guest：真正執行Linux kernel、lspci、build/load .ko、跑test
```

- macOS可當host/editor，不能load Linux `.ko`。
- Labs05～07必須在看得到QEMU EDU `1234:11e8` 的Linux guest。
- ARM host跑x86_64 guest通常用TCG，不假設KVM/HVF跨ISA加速。

---

# Route A：起步與Linux module（Lab00～02）

## 先讀

1. [`learning-dashboard.md`](learning-dashboard.md)
2. [`beginner-primer.md`](beginner-primer.md)
3. [`lab-file-roles.md`](lab-file-roles.md)
4. [`linux-host-setup.md`](linux-host-setup.md)
5. [`check-kernel-env-explained.md`](check-kernel-env-explained.md)

## Gate A

必須能實作與口述：

- Lab00 build/load/unload/dmesg；
- Lab01 debugfs、seq_file、logging；
- Lab02 `/dev`、VFS、`file_operations.read/write`；
- init每取得一個resource，失敗時如何unwind；
- exit為何只處理成功init後仍存活的resource。

搭配：

- [`00-to-01-debugfs-bridge.md`](00-to-01-debugfs-bridge.md)
- [`01-to-03-user-kernel-abi-bridge.md`](01-to-03-user-kernel-abi-bridge.md)
- [`kernel-filesystem-surfaces.md`](kernel-filesystem-surfaces.md)
- [`kernel-api-parameter-roles.md`](kernel-api-parameter-roles.md)

---

# Route B：UAPI與共享狀態（Lab03）

先讀 [`01-to-03-user-kernel-abi-bridge.md`](01-to-03-user-kernel-abi-bridge.md)，再讀Lab03 README、source、test。

## 先建立的圖

```text
userspace syscall
→ VFS
→ file_operations callback
→ shared driver state
→ waitqueue / mmap snapshot
```

## Gate B

能說明：

- `read/write/ioctl/poll/mmap`各自從哪個syscall進入；
- blocking read的predicate與wake-up角色；
- wake只讓wait/poll重新評估，不保證成功返回 `revents == 0`；
- 多reader取得mutex後為何仍要重檢condition；
- mmap page的permission、lifetime與snapshot consistency；
- kernel mutex不能被任意userspace mmap load取得，所以需要sequence publication protocol。

建議實驗：two blocking readers + one message、read-only mapping、嘗試 `mprotect(PROT_WRITE)`、concurrent snapshot。

---

# Route C：Concurrency與lifetime（Lab04）

入口：[`../guides/lab-04-study-order.md`](../guides/lab-04-study-order.md)

## 先建立的圖

```text
userspace ioctl path
+ kernel worker thread
→ 同時read-modify-write dl_counter
→ lost update / data race
```

## Gate C

不要只說「unsafe加mutex就好」，要能回答：

- 哪些execution paths共享state；
- `counter++`如何拆成load/modify/store；
- `READ_ONCE()`為何不是lock；
- mutex保護哪個invariant；
- init為何先初始化state，再啟動kthread；
- exit為何用 `kthread_stop()`同步退出後才能free。

Lab04 test是probabilistic teaching gate，不是data-race absence proof。進一步用KCSAN/lockdep與repeated reload。

---

# Route D：進PCI前的環境與共同模型

## 先讀

1. [`../concepts/pcie-primer.md`](../concepts/pcie-primer.md)
2. [`03-to-05-concurrency-pci-bridge.md`](03-to-05-concurrency-pci-bridge.md)
3. [`../guides/lab-05-study-order.md`](../guides/lab-05-study-order.md)
4. [`../guides/qemu-edu-first-pass.md`](../guides/qemu-edu-first-pass.md)
5. [`../../qemu/README.md`](../../qemu/README.md)
6. 跨ISA時看 [`../../qemu/arm-host-x86-guest.md`](../../qemu/arm-host-x86-guest.md)
7. [`../guides/linux-guest-05-to-07-walkthrough.md`](../guides/linux-guest-05-to-07-walkthrough.md)

## Environment gate

在guest：

```sh
uname -m
uname -r
lspci -Dnn | grep '1234:11e8'
test -e "/lib/modules/$(uname -r)/build"
```

`lspci`看不到EDU時，問題在driver bind前；先修QEMU/guest，不先改ID table或probe。

## Gate D

能畫出：

```text
enumeration
→ match / bind
→ probe
→ resource setup
→ operation
→ quiesce
→ remove / error unwind
```

並能分清：

- raw BAR、PCI resource、`__iomem` mapping；
- MMIO、IRQ、DMA三條通道；
- ordering、posted arrival、operation completion、payload correctness；
- current source / smoke test / production claim的證據層級。

---

# Route E：Lab05 — PCI/BAR/MMIO

入口：[`../../labs/05-pci-edu-mmio/README.md`](../../labs/05-pci-edu-mmio/README.md)

## 第一輪只抓主線

```text
EDU enumeration
→ ID match / probe
→ validate BAR type / length
→ claim + map
→ ident
→ liveness write/read-back/compare
→ remove
```

## Gate E

能回答：

- `pci_request_region()`與`pci_iomap()`各做什麼；
- 為什麼mapping前驗type/length；
- 為什麼不用raw BAR或普通pointer；
- normal accessor、posted write與read-back arrival的差別；
- liveness pass能證明與不能證明什麼；
- error unwind如何只撤銷已取得resource。

---

# Route F：Lab06 — IRQ

入口：[`../../labs/06-pci-edu-irq/README.md`](../../labs/06-pci-edu-irq/README.md)

## 第一輪主線

```text
allocate vector
→ clear stale source
→ request handler
→ trigger
→ read/filter status
→ ACK + read-back
→ complete waiter
→ quiesce + synchronize + free
```

## Gate F

能說明：

- vector index、Linux IRQ number與handler registration；
- shared INTx何時回 `IRQ_NONE`；
- request前為何清pending；
- ACK為何只清owned bit並read-back；
- hard IRQ為何不能sleep；
- MSI/MSI-X與BME的關係；
- read-back、`complete()`、`synchronize_irq()`各解什麼；
- remove為何先停source，再同步handler，最後unmap。

---

# Route G：Lab07 — DMA

入口：[`../../labs/07-pci-edu-dma/README.md`](../../labs/07-pci-edu-dma/README.md)

## 第一輪主線

```text
truthful DMA mask
→ coherent CPU pointer + DMA address
→ late BME
→ RAM→EDU
→ EDU→RAM
→ IRQ + START clear
→ dma_rmb()
→ memcmp()
→ prove quiesce before free
```

## Gate G

能回答：

- CPU pointer、DMA address、EDU-local `0x40000`如何分域；
- 28-bit mask為何不是越大越好；
- coherent免除什麼、未免除什麼；
- 本single-buffer path為何不用cargo-cult `dma_wmb(); wmb();`；
- 真正descriptor ring的fields→`dma_wmb()`→OWN→doorbell；
- IRQ、START clear、`dma_rmb()`、`memcmp()`各驗哪一層；
- timeout後為何不能直接free mapping；
- clear BME與function reset的限制。

---

# Route H：Lab08～09 — Runtime、stress與證據

先讀：

1. [`07-to-09-runtime-validation-bridge.md`](07-to-09-runtime-validation-bridge.md)
2. [`../../runtime/README.md`](../../runtime/README.md)
3. Lab08 README/source/test
4. Lab09 README/stress scripts

## Gate H

- Lab08是userspace wrapper，不是新的 `.ko`；
- 能處理partial I/O、errno、poll error bits、fd/mapping lifetime；
- Lab09目前主要是Lab03 reload/parallel stress，不等於完整KUnit/kselftest/fault framework；
- 能為Lab06/07設計deterministic timeout/reset與late-event測試；
- Log包含kernel/QEMU、兩repo SHA、command與sanitizer/IOMMU狀態。

---

# Walkthrough、Checklist、Companion差異

- **README / Primary guide**：第一次理解概念與contract。
- **Walkthrough**：第一次實際操作，解釋因果。
- **Checklist**：跑過後速查，不取代理解。
- **Companion `.c.md/.sh.md`**：貼source旁讀，可能落後current source。
- **Accuracy audit**：記錄已知錯誤模型與runtime gap。
- **Pedagogy standard/template**：維持教學結構，不自動證明技術正確。

## Workflow / Meta

以下不是driver主線，可晚看：

- [`../workflow/ai-agent-git-checkpoint-policy.md`](../workflow/ai-agent-git-checkpoint-policy.md)
- `.githooks/`
- [`../../scripts/install-git-hooks.sh`](../../scripts/install-git-hooks.sh)
