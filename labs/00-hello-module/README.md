# 00 — External kernel module 的最小生命週期

> **定位**：Lab00 不是在學印 Hello，而是建立所有後續 lab 共用的最小閉環：用 running kernel 對應的 build tree 產生 `.ko`，載入時進 init，失敗時由 init 自行 unwind，卸載時進 exit，最後確認沒有殘留 module。
>
> **完成標準**：能畫出 resource/data flow，指出 current source 的入口、live resource、failure unwind、test evidence 與尚未驗證範圍。

## 先講結論

Lab00 不是在學印 Hello，而是建立所有後續 lab 共用的最小閉環：用 running kernel 對應的 build tree 產生 `.ko`，載入時進 init，失敗時由 init 自行 unwind，卸載時進 exit，最後確認沒有殘留 module。

```text
先確認 environment gate
→ 讀 current source 的入口與 resource
→ 跑正常路徑
→ 驗 error/unwind/teardown
→ 保存可重現 evidence
```

## 不確定處與驗證狀態

- **已對照 current source**：本 README 以 pedagogy branch 的 `.c/.h/.sh` 與 test 為準。
- **目前 evidence**：Current source 與 test 已可進行 static/compile/smoke 驗證；實際 load 仍需 Linux、matching headers、module policy 與足夠權限。
- **仍待驗證**：未附 target kernel/QEMU/repo SHA 與完整 log 的 module 行為，不稱 runtime-verified。
- **架構／device-specific**：遇到 kernel config、QEMU model、real hardware protocol 時回官方文件與實測。

## 這一關要解決什麼問題

初學者常把 kernel module 當成有 `main()` 的 userspace program，或以為 init 失敗後 kernel 會自動呼叫 exit 清理。這會讓後續 resource unwind 全部建立在錯誤模型上。

## 名詞先說清楚

| 名詞 | 本章中的意思 | 不代表什麼 |
|---|---|---|
| **external module** | 在 kernel source tree 外由 kbuild 編譯的 `.ko` | 不保證可載入任意 kernel |
| **init callback** | module load 時由 kernel 呼叫的入口 | 不是永久 main loop |
| **vermagic** | module 與 target kernel build 特徵的相容資訊 | 不是唯一 load policy |
| **unwind** | 失敗時撤銷本次已成功取得的 resource | 不是無條件呼叫 exit |

## 心智模型

把 module load 想成開店：init 依序取得營業所需資源；只有全部成功才正式營業。若中途失敗，施工中的 init 必須自己撤掉已完成步驟。exit 只處理成功載入後的正常卸載。

> **比喻的邊界**：心智模型只幫助理解角色與順序；精確 semantics 仍以 current source、Linux API 與 device protocol 為準。

## Resource 與 data flow

```text
check running kernel/build tree
→ make 產生 .ko
→ modinfo 檢查 metadata/vermagic
→ insmod 解析 parameters 並呼叫 init
→ lsmod/sysfs/dmesg 觀察
→ rmmod 呼叫 exit
→ 確認 module 與 build artifact 已清理
```

## 從簡單到精確

### Current source map

- `driver_lab_hello.c`：`driver_lab_hello_init()`、`driver_lab_hello_exit()`、module parameters。
- `Makefile`：external-module kbuild 入口。
- `test.sh`：build、valid/invalid parameter、load/unload 與 run-specific log gate。
- `quality.sh`：本 lab 的 static gate。

### 第一次讀 source 的順序

1. 找 init/probe/open 或 userspace entry。
2. 列出每一步取得的 resource，以及何時開始 live。
3. 找正常資料／事件路徑。
4. 找每個 failure label 與 teardown。
5. 對照 `test.sh` 的 observable evidence，不先追 generated companion 的固定行號。

## 最小正確範式

```c
static int __init driver_lab_hello_init(void)
{
    if (repeat < 1)
        return -EINVAL;
    return 0;
}

static void __exit driver_lab_hello_exit(void)
{
}
```
Init 回 0 才表示 module 成功 resident；回負 errno 時，exit 不會替這次失敗收尾。

## 看似合理但錯誤的寫法

```c
resource = allocate_something();
if (later_step_fails())
    return -EINVAL;   /* 漏掉 resource cleanup */
```
這段看似只是回錯誤，實際上把已取得 resource 留在 failed load path。

## 如何執行與觀察

```sh
cd labs/00-hello-module
./test.sh
```

手動觀察時可搭配：

```sh
modinfo ./driver_lab_hello.ko
sudo insmod ./driver_lab_hello.ko who=linux repeat=2
lsmod | grep '^driver_lab_hello '
cat /sys/module/driver_lab_hello/parameters/repeat
sudo rmmod driver_lab_hello
```

### 能證明／不能證明

成功 evidence 包含 `.ko`、`modinfo`、module 出現在 `lsmod`/sysfs、本次 init/exit log、invalid parameter load 被拒絕，以及卸載後 module 消失。它不能證明 IRQ、DMA、hot-unplug 或長時間併發安全。

## Debug order

1. 確認目前確實在 Linux，而不是 macOS。
2. 比較 `uname -r/-m` 與 `/lib/modules/$(uname -r)/build`。
3. 查看 `modinfo` 的 architecture/vermagic/license。
4. 讀本次新增的 kernel loader log，而不是只看 shell 的 `Invalid module format`。
5. 檢查 module signing、Secure Boot/lockdown 與權限。

## 工具分工

| 工具／機制 | 解決什麼 | 不解決什麼 |
|---|---|---|
| `make/kbuild` | 產生 target-kernel module | 不證明可成功 load |
| `modinfo` | 查看 metadata/vermagic/parameters | 不執行 init |
| `insmod/rmmod` | 觸發 load/unload lifecycle | 不自動驗證 resource 無洩漏 |
| `dmesg/journalctl -k` | 觀察 kernel loader/init/exit | 不是穩定程式 ABI |

## 與 pcie-study 的對應

這一關建立 probe/remove 前的共同地基：每個後續 PCI resource 都要有 acquire、failure unwind 與 teardown。對應 `pcie-study` P3-01 與 P2-08。

## 常見誤解

### 誤解：module init 就是 main

Init 是 loader callback；成功後 module resident，但不靠 init 持續執行。

### 誤解：init 失敗會呼叫 exit

失敗 init 必須自行撤銷已取得資源。

### 誤解：有 `.ko` 就一定能載入

還受 architecture、vermagic、signature、policy 與 dependency 影響。

## 適用邊界與尚未驗證

- Lab00 沒有 open fd、IRQ、work、timer 或 DMA producer，因此 exit 很簡單。
- `__init`/`__exit` 是 section/lifetime annotation，不是 runtime one-shot lock。
- 實際 module policy 依 kernel config、distribution 與 Secure Boot。

## 第一次閱讀先記住

1. Init 成功才有正常 exit；failed init 自己 unwind。
2. Compile、load、observable behavior 是三層證據。
3. 後續每取得一個 resource，都要立即想清楚對應 cleanup。

## Self-check

1. Init 回負 errno 時，exit 是否會自動執行？
2. `.ko` compile success 能證明什麼、不能證明什麼？
3. 遇到 `Invalid module format` 應依什麼順序查？
4. module parameter 在何時被解析，誰負責進一步 range validation？
5. Lab00 為後續 PCI driver 建立哪個最重要習慣？

<details>
<summary>參考答案</summary>

1. 不會；init/error labels 必須只釋放本次已成功取得的 resources，再回負 errno。
2. 能證明 source 在指定 headers/toolchain 下可產生 module；不能證明 target kernel 接受、init 成功或 runtime 正確。
3. 先查 running kernel/architecture/build tree，再查 modinfo vermagic、dmesg loader error、signature/lockdown 與 policy。
4. 基本型別 parsing 在 module load/init 前後由 parameter infrastructure 處理；device/lab-specific range 仍由 init 驗證。
5. 把 lifecycle 寫成 acquire → publish → quiesce → release，並讓每個 failure point 有對稱 unwind。

</details>

## 來源與查證

- Linux external modules: <https://docs.kernel.org/kbuild/modules.html>
- Kernel parameters: <https://docs.kernel.org/core-api/kernel-parameters.html>
- Module signing: <https://docs.kernel.org/admin-guide/module-signing.html>
- Current source: `labs/00-hello-module/driver_lab_hello.c`
