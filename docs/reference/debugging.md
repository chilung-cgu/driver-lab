# Driver debugging — 從 symptom 到可重現 regression

> **定位**：集中取代分散的 code-reading guide、common failures 與 debugging playbook。遇到問題先定位最早失敗的 layer，再選工具。

## 先講結論

Debug 不是隨機試 API，而是：

```text
symptom
→ 分層與假設
→ 最小實驗
→ run-specific evidence
→ 根因
→ 最小修正
→ regression / fault test
```

越早失敗的 gate 越有資訊。`lspci` 看不到 device 時，不要先改 probe；probe 未進時，不要先查 DMA compare；IRQ 未發生時，不要只改 `dma_rmb()`。

## 不確定處與驗證狀態

- Tool availability、overhead、false positive/negative 依 kernel config/architecture。
- QEMU 能控制部分 failure，但不涵蓋 real board/firmware/link faults。
- Sanitizer/stress 沒報告不等於 bug 不存在。

## 讀 source 的固定順序

1. **入口**：module init、PCI probe、file_operations、IRQ handler、worker、remove。
2. **state**：global/per-device/per-open/per-queue fields。
3. **resource acquire**：alloc/register/request/map/enable/start。
4. **normal flow**：誰寫什麼、誰觀察什麼、completion evidence。
5. **failure unwind**：每個成功步驟的對應撤銷。
6. **teardown**：unpublish → stop producer → synchronize → free。
7. **test oracle**：它如何知道真的成功/失敗？

每個 function 都寫下：caller、context、inputs/outputs、shared state、return convention、lifetime。

## 分層排查表

| Symptom | 第一個 layer | 首要 evidence |
|---|---|---|
| module build fail | toolchain/headers/source | compiler error、KDIR、uname |
| `insmod` fail | module loader/policy/init | modinfo、dmesg、signature/vermagic |
| `/dev` 不見 | cdev/device model/udev | init log、sysfs、`/proc/devices` |
| callback 沒進 | VFS/UAPI/path/permission | strace/CLI errno、dynamic debug |
| poll 不醒 | predicate/wakeup/concurrency | state before wake、poll mask、waiter |
| mmap snapshot 不穩 | publication/VMA/lifetime | seq begin/end、permissions、page size |
| `lspci` 無 EDU | QEMU/guest enumeration | launch args、lspci/sysfs |
| probe 不進 | ID/binding/policy | lspci -k、driver sysfs、probe return |
| BAR/MMIO fail | resource/mapping/register protocol | flags/len/request/iomap/width/readback |
| IRQ timeout | source/vector/BME/ACK/handler | vector mode、status、raise、ACK、count |
| DMA timeout | BME/address/domain/direction/command | DMA handle、register values、status/idle |
| compare fail | address/count/direction/ordering/data | TX/RX dump、status、completion sequence |
| unload warning/UAF | quiesce/lifetime | producer state、synchronize、sanitizer |

## Evidence 原則

- 不清全域 `dmesg`；記錄前後 cursor/line/time，只擷取本次新增訊息。
- Test 只卸載自己載入的 module。
- 不用 broad `|| true` 吞掉 crash/I/O error。
- 保存 exact command、stdout/stderr、dmesg、sysfs/IRQ/resource state。
- 一個 `passed` 字串不夠；IRQ/DMA 同時看 status/ACK/idle/payload/cleanup。

## 常用工具與邊界

| 工具 | 適合回答 | 不能單獨證明 |
|---|---|---|
| `dmesg` / journal | loader/callback/error path | timing/race absence |
| `lspci -Dnnvvk` | enumeration/resources/capability/binding | device firmware健康 |
| sysfs/proc/debugfs | current registration/state | hidden concurrent transition |
| dynamic debug | 選擇性 callsite log | high-rate performance |
| ftrace/tracepoints/perf | call/timing/scheduling | device wire protocol |
| lockdep | lock ordering/context misuse | data race/logic race全部 |
| KASAN | memory bounds/UAF class | 所有 timing/firmware bug |
| KCSAN | sampled data race evidence | 所有 interleaving/logic race |
| IOMMU fault logs | illegal DMA address/access | payload correctness |
| PCIe analyzer | TLP/link evidence | software ownership/lifetime全部 |

## Bug diary 模板

```text
Title / date
kernel, config, architecture
QEMU/device/firmware version
pcie-study SHA / driver-lab SHA
sanitizer/IOMMU state

Symptom:
Expected:
Exact command sequence:
Run-specific stdout/stderr/dmesg:
Resource/IRQ state before and after:

Hypothesis 1:
Experiment:
Evidence:
Result:

Root cause:
Fix and why the contract now holds:
Regression/fault test:
Remaining limits:
```

## 常見失敗的真正第一步

### `Invalid module format`

查 running kernel/arch/build tree → modinfo/vermagic → loader dmesg → signature/symbol policy。

### `/dev` 沒建立

查 init return → dev_t/cdev/class/device → sysfs → devtmpfs/udev/permission。Node 問題與 callback 問題分開。

### Poll/read 卡住

查 predicate 是否在保護下改變、wake 是否在 publish 後、waiter 是否在拿鎖後 recheck。

### PCI probe 沒進

查 enumeration、ID、existing driver、binding/policy、probe return。BDF 不 hard-code。

### IRQ 沒來

查 device source/status、vector allocation/mode、BME（MSI/MSI-X）、request、trigger write、ACK/mask。IRQ number本身通常不是第一根因。

### DMA 壞掉

先分 CPU pointer、DMA address、device-local address；再查 mask、direction/count、BME、completion/idle、ordering、payload compare。Timeout 後不盲 free。

## Fix review checklist

- 修正的是根因還是只延長 timeout？
- Contract 是否在所有 access path 一致使用？
- Error path 是否只撤銷已成功步驟？
- Remove/reset 是否先 stop/quiesce/synchronize？
- Test 在舊 bug 存在時真的會 fail 嗎？
- 是否新增了 run-specific evidence 與 remaining limits？

## Self-check

1. 為什麼「最早失敗的 gate」比最後症狀更有價值？
2. 為什麼不能 `dmesg -C`？
3. Sanitizer 無報告能否證明無 bug？
4. DMA compare fail 應先加 barrier，還是先分 address/domain/completion？
5. 一份可信 bug diary 最少要保存什麼？

<details>
<summary>參考答案</summary>

1. 後續失敗常是上游 gate 未成立的連鎖症狀；修最早 failure 可縮小假設空間。
2. Kernel log 是共享系統資源，清除會破壞其他 evidence；應用 cursor/time/line 隔離本次新增內容。
3. 不能；instrumentation、sample、執行路徑與 bug class 有限，只能增加 evidence。
4. 先分清 CPU/DMA/device-local address、mask/direction/count、operation completion與 payload；只有缺 ordering contract 時才選正確 barrier。
5. Environment/version/SHA、exact commands、expected/observed、run-specific logs/state、hypothesis/experiment/evidence、root cause、fix、regression與limits。

</details>

## 來源與查證

- Kernel testing overview: <https://docs.kernel.org/dev-tools/testing-overview.html>
- KASAN: <https://docs.kernel.org/dev-tools/kasan.html>
- KCSAN: <https://docs.kernel.org/dev-tools/kcsan.html>
- Lockdep: <https://docs.kernel.org/locking/lockdep-design.html>
- PCI driver guide: <https://docs.kernel.org/PCI/pci.html>
