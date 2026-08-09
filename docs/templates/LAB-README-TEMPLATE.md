# NN — Lab 名稱：核心概念

> **定位**：這一關在完整 driver 路線中新增哪一層。
>
> **先備知識**：真正必要的前一關 / concept note。
>
> **完成標準**：能實作、觀察、解釋哪些行為。

## 先講結論

用 3～8 句與一個最小 flow 說明本關：

```text
入口
→ resource setup
→ normal operation
→ observable evidence
→ quiesce / teardown
```

## 不確定處與驗證狀態

- **已由官方文件查證**：……
- **已對照 current source**：……
- **Compile/static 狀態**：……
- **待 runtime / fault-injection 驗證**：……
- **Architecture / device-specific**：……

## 這一關要解決什麼問題

以具體症狀開始：

```text
症狀
→ 常見錯誤假設
→ 本關建立的正確模型
```

## 名詞先說清楚

| 名詞 | 本關中的意思 | 不代表什麼 |
|---|---|---|
| …… | …… | …… |

## 心智模型

圖、比喻或步驟。

> **比喻的邊界**：……

## 先備 gate

```sh
# 只列真正必要的環境檢查
```

說明每個檢查失敗時，問題在哪一層，為何不應先修改 driver source。

## Resource 與 data flow

### Setup

```text
API A
→ API B
→ resource 開始 live
```

### Normal path

```text
producer
→ shared state / MMIO / IRQ / DMA
→ consumer
```

### Error unwind

每個失敗點只撤銷已成功取得的 resource。

### Teardown

```text
reject new work
→ stop / mask producer
→ wait / synchronize in-flight users
→ free dependencies
```

## 從簡單到精確

### 1. 第一輪先懂的 source flow

### 2. 關鍵 structure / callback / register

### 3. Ordering、completion 或 lifetime contract

### 4. Exception / architecture / device boundary

## 最小正確範式

```c
/* 說明 context、observer、resource、contract。 */
```

## 看似合理但錯誤的寫法

```c
/* 錯誤範例 */
```

- **為什麼看起來合理**：……
- **缺少的 contract**：……
- **可能症狀**：……
- **修正**：……

## 如何執行與觀察

```sh
./test.sh
```

### 成功證據

- ……

### 這個 test 不能證明

- ……

## Debug order

```text
environment
→ enumeration / entry
→ resource
→ operation
→ completion
→ payload
→ teardown
```

## 工具分工

| 工具／API | 解決什麼 | 不解決什麼 |
|---|---|---|
| …… | …… | …… |

## 與 pcie-study 的對應

- Primary note：……
- 本 lab 如何具體化該 contract：……

## 常見誤解

### 誤解 1：……

- **為什麼錯**：……
- **正確說法**：……

## 適用邊界與尚未驗證

- ……

## 第一次閱讀先記住

1. ……
2. ……
3. ……

## Self-check

1. Context / resource 題。
2. Contract 題。
3. Evidence 題。
4. Failure / teardown 題。
5. Limit 題。

<details>
<summary>參考答案</summary>

1. 回答包含 context、contract、limit。
2. ……
3. ……
4. ……
5. ……

</details>

## 來源與查證

- Current source：`path/to/source.c`
- Current test：`path/to/test.sh`
- Official documentation：<https://docs.kernel.org/...>
- Target kernel / QEMU / guest version（runtime 後填）：……
