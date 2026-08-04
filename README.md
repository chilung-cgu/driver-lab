# driver-lab — 從 kernel module 到 PCIe MMIO / IRQ / DMA

> 一套可 build、load、觀察、故障排查與反覆驗證的 Linux host-driver labs。概念教材：[`chilung-cgu/pcie-study`](https://github.com/chilung-cgu/pcie-study)。

## 先講結論

十個 Lab 已使用同一套 beginner-first 結構：

```text
結論與驗證狀態
→ 問題與名詞
→ 心智模型
→ resource/data flow
→ current source
→ 正反範式
→ test evidence / debug order
→ limits / Self-check / sources
```

目前分支：

```text
main
  └─ review/accuracy-audit-2026-08
       └─ review/pedagogy-pass-2026-08
```

- Accuracy audit 修正 source、tests 與高風險技術語意。
- Pedagogy pass 保留 audit contract，改善全部 Lab README、核心 concepts、導航與 docs 結構。

**唯一新手入口：[`docs/onboarding/START-HERE.md`](docs/onboarding/START-HERE.md)**

## 不確定處與驗證狀態

- CI 已覆蓋 shell/Markdown/static style、userspace runtime build、Labs00～07 external-module compile、pedagogy structure 與 docs graph。
- 真正 `insmod/rmmod`、MMIO、IRQ、DMA、timeout/reset、sanitizer、IOMMU/SWIOTLB 仍需指定 Linux/QEMU guest logs。
- QEMU EDU 不是 production accelerator；不涵蓋 vendor firmware、PHY/link、完整 AER/PM/hotplug/reset、multi-queue MSI-X、pinned memory 與 security-reviewed UAPI。

## 學習路線

| Lab | 核心概念 | 第一層 evidence | 重要邊界 |
|---|---|---|---|
| 00 | module lifecycle | init/exit、parameters | failed init 自己 unwind |
| 01 | debugfs/logging | trigger/status/log | debugfs 非 stable UAPI |
| 02 | cdev/read-write | `/dev`/sysfs/proc/readback | 不是 multi-client queue |
| 03 | ioctl/poll/mmap | predicates、read-only snapshot | wake 只要求 recheck |
| 04 | race/mutex/kthread | unsafe/safe、stop | probabilistic test 非 proof |
| 05 | PCI/BAR/MMIO | enumeration/bind/liveness | read-back 非任意 command completion |
| 06 | IRQ | vector/status/ACK/complete | 先停 source 再 sync handler |
| 07 | coherent DMA | mask/transfers/idle/compare | 未 quiesce 不可 free |
| 08 | userspace runtime | unit/CLI/device UAPI | partial I/O、handle lifetime |
| 09 | stress/fault scaffold | reload/parallel oracle | 不是完整 fault framework |

## Host / guest

```text
macOS or Linux host
  └─ QEMU + guest image/network/storage
       └─ Linux guest
            ├─ matching kernel build tree
            ├─ QEMU EDU 1234:11e8
            └─ build/load/test Labs05～07
```

Labs00～04 可在合適 Linux host/guest；Labs05～07 需要 Linux PCI hierarchy 中的 EDU。Cross-ISA 通常使用 TCG。

## 快速開始

```sh
./scripts/check-kernel-env.sh
(cd labs/00-hello-module && ./test.sh)
```

依 [`START-HERE`](docs/onboarding/START-HERE.md) 逐關前進。進 PCI 前先讀 [`PCIe primer`](docs/concepts/pcie-primer.md)。

## Static/build gates

```sh
./scripts/quality.sh .
python3 scripts/check_pedagogy_structure.py
python3 scripts/check_docs_architecture.py
make -C runtime clean all
```

這些是必要 gate，不是 runtime proof。

## Runtime gates

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

EDU guest：

```sh
lspci -Dnn | grep '1234:11e8'
for lab in \
  labs/05-pci-edu-mmio \
  labs/06-pci-edu-irq \
  labs/07-pci-edu-dma; do
  (cd "$lab" && ./test.sh)
done
```

Runtime report 至少記錄 kernel/QEMU/two-repo SHA、IOMMU/sanitizer state、commands、stdout/stderr/dmesg。

## Docs architecture

- [`Docs index`](docs/README.md)
- [`START-HERE`](docs/onboarding/START-HERE.md)
- [`Linux/QEMU environment`](docs/onboarding/linux-environment.md)
- [`Kernel interfaces`](docs/onboarding/kernel-interfaces.md)
- [`Concurrency primer`](docs/concepts/concurrency-primer.md)
- [`PCIe primer`](docs/concepts/pcie-primer.md)
- [`Accelerator architecture`](docs/concepts/accelerator-driver-architecture.md)
- [`Debugging`](docs/reference/debugging.md)
- [`Companion policy`](docs/reference/companion-docs.md)

重複的 onboarding bridge、roadmap、debugging 與 companion rollout 文件已整合到上述 canonical docs；全 repo local links 由 CI 驗證。

## 正確合併順序

1. 完成並合併 `driver-lab` accuracy audit。
2. `pcie-study` audit 鎖定 immutable merged driver SHA 後合併。
3. Rebase/retarget 兩個 pedagogy PR 到新 main 並重跑 CI/runtime。
4. 先合併 `driver-lab` pedagogy，再更新/合併 `pcie-study` pedagogy。
5. 最後重新生成並人工 review companion/NotebookLM artifacts。
