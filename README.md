# driver-lab — 從 kernel module 到 PCIe MMIO / IRQ / DMA

> 一套可以實際build、load、觀察與除錯的 Linux host-driver labs。
> 概念教材：[`chilung-cgu/pcie-study`](https://github.com/chilung-cgu/pcie-study)。

## 先講結論

這個repo不是一支production PCIe driver，而是一條刻意拆小的學習路線：

```text
module lifecycle
→ debugfs / logging
→ char-device UAPI
→ ioctl / poll / mmap
→ concurrency / lifetime
→ PCI BAR / MMIO
→ IRQ
→ DMA
→ userspace runtime
→ stress / fault-injection scaffold
```

目前branch分層：

```text
main
  └─ review/accuracy-audit-2026-08
       └─ review/pedagogy-pass-2026-08
```

- **Accuracy audit**：修正source、tests與技術敘述。
- **Pedagogy pass**：建立在audit之上，把名詞、source flow、resource lifetime、test evidence與限制教清楚。

本branch的第一批pilot已完成：

1. [`PCIe host-driver beginner primer`](docs/concepts/pcie-primer.md)
2. [`Lab05 PCI/MMIO`](labs/05-pci-edu-mmio/README.md)
3. [`Lab06 IRQ`](labs/06-pci-edu-irq/README.md)
4. [`Lab07 DMA`](labs/07-pci-edu-dma/README.md)
5. 教材標準、模板、migration manifest與CI結構檢查

## 不確定處與驗證狀態

- Audit branch完成source/static review與CI建設；完整runtime仍需你的Linux/QEMU EDU guest。
- Pedagogy branch沒有改動Lab05～07 `.c` behavior；它改善文件與維護規則。
- Compile pass不等於MMIO/IRQ/DMA runtime正確；smoke pass也不等於race-free或production-ready。
- QEMU EDU只教Linux PCI software model，不涵蓋真實PHY/LTSSM、vendor firmware、AER、hotplug、PM、SR-IOV、
  security-reviewed UAPI或完整reset recovery。

詳細狀態：

- [`Accuracy Audit`](docs/reference/accuracy-audit-2026-08.md)
- [`Pedagogy Pass`](docs/PEDAGOGY-PASS-2026-08.md)
- [`Teaching Quality Standard`](docs/TEACHING-QUALITY-STANDARD.md)

## 完全初學者從哪裡開始

1. [`docs/onboarding/reading-map.md`](docs/onboarding/reading-map.md)
2. [`docs/onboarding/learning-dashboard.md`](docs/onboarding/learning-dashboard.md)
3. [`docs/onboarding/beginner-primer.md`](docs/onboarding/beginner-primer.md)
4. [`docs/onboarding/lab-file-roles.md`](docs/onboarding/lab-file-roles.md)
5. [`docs/onboarding/linux-host-setup.md`](docs/onboarding/linux-host-setup.md)
6. Lab00 → Lab01 → Lab02 → Lab03 → Lab04
7. [`docs/concepts/pcie-primer.md`](docs/concepts/pcie-primer.md)
8. Lab05 → Lab06 → Lab07
9. Lab08 userspace runtime → Lab09 stress scaffold

每一關都走：

```text
先讀結論與名詞
→ 畫resource/data flow
→ 讀current source
→ 執行test
→ 保存log/evidence
→ 闔上README回答Self-check
→ 做一個失敗/邊界實驗
```

## Lab matrix

| Lab | 核心概念 | 第一層成功證據 | 重要邊界 |
|---|---|---|---|
| 00 | Module lifecycle | init/exit、parameter、dmesg | init失敗不會呼叫exit，需自行unwind |
| 01 | debugfs / seq_file / logging | trigger/status/knob | debugfs不是stable product UAPI |
| 02 | cdev / read-write | `/dev`與buffer行為 | 不是multi-client queue |
| 03 | ioctl / poll / mmap | blocking/event/snapshot | wake只重評ready；mutex不能鎖userspace load |
| 04 | race / mutex / kthread | unsafe vs safe counter | probabilistic demo不等於data-race absence proof |
| 05 | PCI bind / BAR / MMIO | identity/liveness | read-back不等於任意command完成 |
| 06 | IRQ vector / status / ACK | one bounded event | 先quiesce source，再同步handler |
| 07 | Coherent DMA round-trip | two transfers + compare | CPU pointer≠DMA address；未quiesce不可free |
| 08 | Userspace runtime / CLI | wrapper/UAPI build | partial I/O、ABI與lifetime仍需處理 |
| 09 | Stress scaffold | reload/parallel tests | 不是完整KUnit/kselftest/fault framework |

## Host / guest 心智模型

```text
macOS或Linux host
  └─ 執行QEMU、持有guest image與network

Linux guest
  ├─ 看得到QEMU EDU 1234:11e8
  ├─ 有與uname -r匹配的kernel build tree
  ├─ build/load Labs05～07
  └─ 執行lspci、dmesg、test與stress
```

- Labs00～04可在合適Linux host或guest執行。
- Labs05～07需要Linux PCI hierarchy中存在EDU。
- macOS不能load Linux `.ko`，只能當editor/QEMU host。
- ARM host跑x86_64 guest通常用TCG，不假設KVM/HVF跨ISA加速。

## 核心讀法：每個API都問六件事

1. 誰呼叫？
2. 執行context可不可以sleep？
3. 取得/修改哪個resource或state？
4. 哪些其他path可能並行存取？
5. 哪個observable evidence證明成功？
6. Error/remove前要先停哪個producer或in-flight user？

這比背API列表更接近真實driver debug。

## MMIO / IRQ / DMA 的共同分層

| 問題 | 工具／證據 |
|---|---|
| BAR是否存在且夠大 | `pci_resource_flags/len` |
| Resource誰擁有 | `pci_request_region()` |
| 如何取得I/O mapping | `pci_iomap()` |
| 如何讀寫register | normal I/O accessor |
| Posted write是否到達 | same-device safe read-back |
| IRQ是否屬於自己 | device status + mask |
| Handler是否退出 | `synchronize_irq()` / `free_irq()` semantics |
| Device用哪個memory address | DMA API回傳 `dma_addr_t` |
| Coherent ownership order | `dma_wmb/rmb`（有對應protocol時） |
| Device operation是否完成 | OWN/CQ/status/IRQ/idle |
| Payload是否正確 | length/sequence/checksum/`memcmp()` |
| 能否free | producer停止 + quiesce + software synchronization |

## Static / build gate

```sh
./scripts/quality.sh .
python3 scripts/check_pedagogy_structure.py
./scripts/check-kernel-env.sh
make -C runtime clean all
```

GitHub Actions會：

- Shell syntax / ShellCheck；
- Markdown local links；
- pedagogy structure；
- runtime/CLI build；
- Labs00～07 external-module compile；
- whitespace。

這些是必要gate，但不是module runtime proof。

## Runtime gate

Labs00～04：

```sh
for lab in \
  labs/00-hello-module \
  labs/01-debugfs-logging \
  labs/02-char-device \
  labs/03-ioctl-poll-mmap \
  labs/04-locking-and-races; do
  (cd "$lab" && ./test.sh)
done
```

EDU guest內：

```sh
lspci -Dnn | grep '1234:11e8'

for lab in \
  labs/05-pci-edu-mmio \
  labs/06-pci-edu-irq \
  labs/07-pci-edu-dma; do
  (cd "$lab" && ./test.sh)
done
```

Runtime log至少記錄：kernel、QEMU、兩repo SHA、IOMMU/sanitizer狀態、完整command、stdout/stderr/dmesg。

## 高價值後續測試

- Lab03：two readers/one message、read-only mmap、concurrent snapshot。
- Lab04：KCSAN/lockdep與repeated reload。
- Lab06：強制legacy/MSI、repeated events、late-handler teardown。
- Lab07：IRQ timeout、command timeout、reset success/failure、IOMMU on/off或SWIOTLB。
- 全部：KASAN/lockdep repeated load/unload。

## 教材品質如何維持

- [`Teaching Quality Standard`](docs/TEACHING-QUALITY-STANDARD.md)
- [`Lab README Template`](docs/templates/LAB-README-TEMPLATE.md)
- [`Migration Manifest`](docs/pedagogy/migrated-docs.txt)
- `scripts/check_pedagogy_structure.py`

CI檢查已遷移文件是否有結論、驗證狀態、名詞、心智模型、resource/data flow、正反例、test evidence、
debug order、限制、Self-check與官方來源。

**CI不會自動判斷barrier、teardown或device protocol是否真的正確。** 仍需official docs、current source、
runtime/fault evidence與人工technical review。

## 正確branch與合併順序

1. 先完成並合併 `driver-lab` accuracy audit。
2. 再讓 `pcie-study` audit鎖定merged driver SHA並合併。
3. Pedagogy PR目前以audit branch為base；audit合併後rebase/retarget到main。
4. 先合併 `driver-lab` pedagogy，再更新 `pcie-study` cross-repo source reference。
5. 最後重新生成並人工review companion/NotebookLM artifacts。

## 快速入口

- [Docs index](docs/README.md)
- [Reading map](docs/onboarding/reading-map.md)
- [Learning dashboard](docs/onboarding/learning-dashboard.md)
- [PCIe beginner primer](docs/concepts/pcie-primer.md)
- [Lab05 MMIO](labs/05-pci-edu-mmio/README.md)
- [Lab06 IRQ](labs/06-pci-edu-irq/README.md)
- [Lab07 DMA](labs/07-pci-edu-dma/README.md)
- [Accuracy audit](docs/reference/accuracy-audit-2026-08.md)
- [Pedagogy pass](docs/PEDAGOGY-PASS-2026-08.md)
