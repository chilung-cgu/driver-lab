# Accelerator host-driver architecture — 從 labs 到真實產品

> **定位**：把十個 labs 對映到 AI/HPC accelerator 的 host-side software stack，並誠實標出仍缺少的 production work。

## 先講結論

典型 accelerator software 不是單一 `.ko`，而是一組跨層 state machines：

```text
application / framework
→ userspace runtime / compiler integration
→ UAPI / queue / memory management
→ kernel PCI driver
→ BAR/MMIO control + IRQ/CQ + DMA/IOMMU
→ device firmware / engines
→ reset / recovery / telemetry
```

`driver-lab` 已覆蓋 Linux host-driver 的共通骨架，但沒有 vendor 真卡、firmware boot、production queue、security-reviewed UAPI 或完整 reset/error recovery。正確說法是「已建立可重現 baseline」，不是「只差 vendor registers」。

## 不確定處與驗證狀態

- 不同 accelerator 的 split 可能偏 kernel、userspace、firmware 或 subsystem framework。
- Queue/register/firmware/reset protocol 完全 device-specific。
- QEMU EDU 的單 buffer、單 vector、probe-time self-test 不能代表 production throughput/latency。
- 職缺內容會變，投遞時要回官方 JD 重查。

## 典型元件與責任

| 層 | 常見責任 | 主要 failure/lifetime |
|---|---|---|
| application/framework | graph/model/work submission | cancellation、process death |
| userspace runtime | device discovery、context、buffer/queue API、poll/event | fd/mapping ownership、ABI version |
| UAPI | ioctl/mmap/poll、handles、memory pin/map | hostile input、compat、security |
| PCI kernel driver | probe/remove、BAR、IRQ、DMA/IOMMU | hot-unplug、reset、AER、PM |
| queue engine | descriptor/CQ/doorbell、scheduling | wrap、ownership、timeout、backpressure |
| memory manager | coherent/streaming/SG/pinned memory/IOVA | unmap-before-idle、isolation |
| firmware | boot、command protocol、health | hang、version mismatch、recovery |
| observability | logs、tracepoints、counters、crash dump | perturbation、privacy、volume |

## Labs 對映

| Lab | 已建立的能力 | Production 還要補 |
|---|---|---|
| 00 | module lifecycle、failure unwind | PCI bind、PM、hotplug/error recovery |
| 01 | debugfs/log observation | tracepoints、health/telemetry policy |
| 02 | cdev/read-write boundary | versioned/security-reviewed UAPI |
| 03 | ioctl/poll/mmap、snapshot | handles、pinning、multi-process lifetime |
| 04 | race/mutex/kthread stop | per-queue locking、RCU/refcount、cancel/reset races |
| 05 | PCI bind、BAR/MMIO | full register protocol、power/firmware bring-up |
| 06 | one vector、status/ACK | MSI-X multi-queue、affinity/coalescing |
| 07 | coherent round-trip | descriptor rings、streaming/SG、IOMMU、performance |
| 08 | userspace wrapper/CLI | production runtime、context/session/API compatibility |
| 09 | reload/parallel scaffold | sanitizer/fault matrix、CI hardware farm |

## 一筆 command 的完整故事

```text
runtime validates request
→ allocate/reserve queue slot and memory mapping
→ fill descriptor/payload
→ publish ownership with correct ordering
→ ring MMIO doorbell
→ device/firmware executes
→ DMA writes completion/payload
→ MSI-X or polling exposes CQ entry
→ driver/runtime reclaims ownership
→ validate status/length/sequence
→ release resources only after all users stop
```

每一步都可能有 timeout、process exit、reset、hot-unplug 或 stale completion；產品設計需要 generation/tag/cancellation/recovery，而不只 happy path。

## Resource / state machine 表

面試或 design review 時至少能畫：

| Resource/state | 建立 | 開始被誰使用 | 停止條件 | 釋放 |
|---|---|---|---|---|
| PCI function/BAR | probe | control paths | remove/reset reject new work | unmap/release/disable |
| IRQ vectors | probe/queue setup | device + handlers | source masked/ACK、handlers synchronized | free IRQ/vectors |
| DMA mapping | buffer/queue setup | CPU/device ownership protocol | completion/abort/reset proves idle | unmap/free/unpin |
| queue/context | open/ioctl/runtime | process, workers, IRQ/CQ | cancel/drain/refcount zero | destroy state |
| firmware state | probe/reset | all commands | health/stop/reset protocol | reinitialize or device unavailable |

## 作品如何誠實描述

可以說：

> 我以 current Linux/QEMU source 建立了可重現的 host-driver baseline：module/UAPI/concurrency、PCI BAR/MMIO、IRQ、coherent DMA、userspace runtime與 stress scaffold。作品明確區分 static、compile、QEMU runtime 與 real-hardware gap；下一步是 descriptor ring、streaming/SG、MSI-X multi-queue、IOMMU 及 device-specific reset/firmware recovery。

不要說：

- 已完成 production accelerator driver；
- QEMU IRQ/DMA 等同真卡 bring-up；
- 只剩下 register map；
- compile/smoke pass 證明 race-free。

## 高價值下一步

1. 實作 coherent descriptor ring：OWN/phase/index、`dma_wmb/rmb`、wrap/full/empty。
2. 加 streaming/SG payload 與 IOMMU on/off/SWIOTLB tests。
3. Lab06 擴成多 vector/per-queue state/affinity。
4. 可控制地注入 IRQ timeout、command timeout、reset success/failure。
5. 讀一支 upstream accelerator/NVMe/network driver，畫 resource/lifecycle 表。
6. 保存 target kernel/QEMU/device SHA、commands、logs 與 bug diary。

## Self-check

1. Runtime、UAPI、kernel driver、firmware 各自解什麼？
2. 一個 IRQ/CQ 到達為什麼不等於 payload 一定正確？
3. Descriptor ring 比 Lab07 single-buffer path 多哪些 contract？
4. Process exit/reset/remove 為什麼是 queue/mapping lifetime 問題？
5. 如何客觀描述 QEMU EDU 作品而不誇大？

<details>
<summary>參考答案</summary>

1. Runtime 管 application-facing context/handles；UAPI 定義跨 boundary contract；kernel driver 管 PCI/resources/isolation；firmware 管 device-specific engines/protocol。
2. Notification 只證明 event path；錯誤 address、length、direction、stale tag 或 corruption 仍可能存在，需 status/sequence/compare。
3. Slot reservation、多 producer、OWN/phase publication/reclaim、wrap/full/empty、CQ/doorbell、backpressure、timeout/cancel與 lifetime。
4. 非同步 device/IRQ/worker 可能仍持有 queue或 DMA address；free/unmap 前要拒絕新 work、drain/abort/reset並同步 refs。
5. 說清楚 target、已實跑層級、可重現 evidence、已建立的通用骨架，以及 real hardware/production gaps。

</details>

## 來源與查證

- PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
- DMA API HOWTO: <https://docs.kernel.org/core-api/dma-api-howto.html>
- Generic IRQ: <https://docs.kernel.org/core-api/genericirq.html>
- VFIO/IOMMUFD: <https://docs.kernel.org/driver-api/vfio.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
