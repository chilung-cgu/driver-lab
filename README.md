# driver-lab

> 給 `0 driver 經驗` 工程師的 Linux host driver 學習專案，目標是為 PCIe AI 加速卡 host driver 工作做準備。

## 這個專案在做什麼

這不是只講理論的筆記集，而是一套可反覆操作的學習路線。它把學習內容拆成：

- 可在 Linux 上反覆 `build / load / unload / 觀測` 的最小 lab
- 從 `module lifecycle`、`debugfs`、`char device`，一路走到 `PCIe + MMIO + IRQ + DMA`
- 早期就引入 user-space runtime 與 CLI，讓你建立「driver 不只是一個 `.ko`」的觀念
- 以 `QEMU edu` 當第一個完整 PCI 教學裝置，再把心智模型翻譯到 AI 加速卡 driver

## 如果你是完全新手，先照這個順序

1. 看 [閱讀地圖](docs/onboarding/reading-map.md)
2. 看 [0 基礎學習儀表板](docs/onboarding/learning-dashboard.md)
3. 看 [新手前導](docs/onboarding/beginner-primer.md)
4. 看 [Lab 檔案角色導讀](docs/onboarding/lab-file-roles.md)
5. 看 [Linux Host 建置與風險檢查](docs/onboarding/linux-host-setup.md)
6. 看 [第一次環境檢查輸出怎麼看](docs/onboarding/check-kernel-env-explained.md)
7. 然後只做 [`labs/00-hello-module`](labs/00-hello-module)

你現在看不懂大部分內容是正常的。這個 repo 的正確打開方式不是先把全部文件讀完，而是先跑通最小閉環，再回頭理解 code。

## 建議執行模型

- `macOS`：編輯器、Git、筆記、QEMU host
- `Linux 主機`：直接 build / load / 測試早期 labs
- `Linux guest`：執行 `05-07` 的 QEMU EDU driver build / load / smoke test

> [!WARNING]
> 你可以在 `macOS` 上跑 QEMU，但不能在 `macOS` 直接 build / load Linux kernel module。
> `05-07` 的實際 driver 驗證位置是 `Linux guest`。

## Lab 成熟度矩陣

| Lab | 目標 | 建議環境 | 目前驗證程度 | 下一份要讀的文件 |
|---|---|---|---|---|
| `00-hello-module` | 建立最小 `build / load / unload / dmesg` 閉環 | Linux host | 可直接練習 | [`labs/00-hello-module/README.md`](labs/00-hello-module/README.md) |
| `01-debugfs-logging` | 學會基本觀測與 debugfs | Linux host | 可直接練習 | [`labs/01-debugfs-logging/README.md`](labs/01-debugfs-logging/README.md) |
| `02-char-device` | 熟悉 user-kernel 邊界與 `read/write` | Linux host | 可直接練習 | [`labs/02-char-device/README.md`](labs/02-char-device/README.md) |
| `03-ioctl-poll-mmap` | 練 `ioctl / poll / mmap` 與 shared buffer | Linux host | driver、runtime、CLI、smoke test 已落地 | [`labs/03-ioctl-poll-mmap/README.md`](labs/03-ioctl-poll-mmap/README.md) |
| `04-locking-and-races` | 練 race 重現、對照與 cleanup 對稱性 | Linux host | driver、CLI、smoke test 已落地 | [`docs/concepts/concurrency-primer.md`](docs/concepts/concurrency-primer.md) |
| `05-pci-edu-mmio` | 練 PCI `probe`、BAR map、基本 MMIO | Linux guest | 已在遠端 Linux host 啟動 QEMU EDU guest 實測通過；macOS 不作 kernel module load 驗證 | [`docs/guides/qemu-edu-first-pass.md`](docs/guides/qemu-edu-first-pass.md) |
| `06-pci-edu-irq` | 練 IRQ request、raise、acknowledge | Linux guest | 已在遠端 Linux host 啟動 QEMU EDU guest 實測通過；macOS 不作 kernel module load 驗證 | [`docs/guides/linux-guest-05-to-07-walkthrough.md`](docs/guides/linux-guest-05-to-07-walkthrough.md) |
| `07-pci-edu-dma` | 練 coherent DMA 與 round-trip 驗證 | Linux guest | 已在遠端 Linux host 啟動 QEMU EDU guest 實測通過；macOS 不作 kernel module load 驗證 | [`docs/guides/linux-guest-05-to-07-walkthrough.md`](docs/guides/linux-guest-05-to-07-walkthrough.md) |
| `08-runtime-library` | 把 `02/03` 的 ABI 封裝成 runtime | Linux host | `build` 與 `02/03` 對應封裝已驗證；不是產品級 runtime | [`labs/08-runtime-library/README.md`](labs/08-runtime-library/README.md) |
| `09-stress-and-fault-injection` | 把「能跑」提升成「能重複驗證」 | Linux host | 已有 `03` 專用 stress 腳本；fault injection 尚未自動化 | [`labs/09-stress-and-fault-injection/README.md`](labs/09-stress-and-fault-injection/README.md) |

## 文件入口

如果你想先看 `docs/` 目錄的分類總覽，再進各份文件，可先看 [Docs Index](docs/README.md)。

### 起步必讀

- [閱讀地圖](docs/onboarding/reading-map.md)
- [新手前導](docs/onboarding/beginner-primer.md)
- [Lab 檔案角色導讀](docs/onboarding/lab-file-roles.md)
- [Linux Host 建置與風險檢查](docs/onboarding/linux-host-setup.md)
- [第一次環境檢查輸出怎麼看](docs/onboarding/check-kernel-env-explained.md)

### 章節過渡導讀

- [00 到 01：debugfs 過渡導讀](docs/onboarding/00-to-01-debugfs-bridge.md)
- [01 到 03：user-kernel ABI 過渡導讀](docs/onboarding/01-to-03-user-kernel-abi-bridge.md)
- [03 到 05：併發與 PCI 過渡導讀](docs/onboarding/03-to-05-concurrency-pci-bridge.md)
- [05 到 07：PCI、IRQ、DMA 過渡導讀](docs/onboarding/05-to-07-pci-irq-dma-bridge.md)
- [07 到 09：runtime 與驗證過渡導讀](docs/onboarding/07-to-09-runtime-validation-bridge.md)

### 概念前導

- [新手術語表](docs/onboarding/beginner-glossary.md)
- [併發與同步白話前導](docs/concepts/concurrency-primer.md)
- [PCIe / MMIO / IRQ / DMA 白話前導](docs/concepts/pcie-primer.md)
- [AI 加速卡 Host Driver 架構對映](docs/concepts/accelerator-driver-architecture.md)

### 操作 Runbook

- [16 週學習路線](docs/guides/learning-roadmap.md)
- [04 Locking and Races 導讀](docs/guides/lab-04-walkthrough.md)
- [QEMU EDU 新手起手式](docs/guides/qemu-edu-first-pass.md)
- [Linux Guest 操作手冊：05 到 07](docs/guides/linux-guest-05-to-07-walkthrough.md)
- [Linux Guest 快速檢查表：05 到 07](docs/guides/linux-guest-05-to-07-checklist.md)

### 除錯與參考

- [Source companion docs 索引](docs/reference/companion-docs-index.md)
- [常見失敗圖鑑](docs/reference/common-failures.md)
- [官方來源索引](docs/reference/source-index.md)
- [程式閱讀指南](docs/reference/code-reading-guide.md)
- [Debug / 測試 Playbook](docs/reference/debugging-playbook.md)
- [QEMU 說明](qemu/README.md)

### 專案 / Agent Workflow

- [AI Agent + Git Checkpoint 規則範本](docs/workflow/ai-agent-git-checkpoint-policy.md)
- [Git hooks 安裝腳本](scripts/install-git-hooks.sh)

## 建議的第一天流程

1. 完成 [閱讀地圖](docs/onboarding/reading-map.md) 指定的起步文件
2. 用 [0 基礎學習儀表板](docs/onboarding/learning-dashboard.md) 確認今天只追 `00`
3. 在 Linux 環境執行 [`scripts/check-kernel-env.sh`](scripts/check-kernel-env.sh)
4. 完成 [`labs/00-hello-module/README.md`](labs/00-hello-module/README.md)
5. 回答 `00` README 裡的「完成後你應該能回答」

## 建議的第一週流程

1. 反覆跑通 `00`，直到你能說明 `insmod`、`rmmod`、`module_init()`、`dmesg`
2. 完成 [`labs/01-debugfs-logging/README.md`](labs/01-debugfs-logging/README.md)，理解 debugfs 是 debug 介面
3. 完成 [`labs/02-char-device/README.md`](labs/02-char-device/README.md)，理解 `/dev/...` 與 `file_operations`
4. 能清楚解釋 `read/write`、`debugfs`、cleanup path 之後，再前進 `03` 與 `04`

## 專案結構

```text
driver-lab/
  README.md
  docs/
  labs/
    00-hello-module/
    01-debugfs-logging/
    02-char-device/
    03-ioctl-poll-mmap/
    04-locking-and-races/
    05-pci-edu-mmio/
    06-pci-edu-irq/
    07-pci-edu-dma/
    08-runtime-library/
    09-stress-and-fault-injection/
  runtime/
  tests/
  scripts/
  qemu/
  notes/
```

## 品質檢查

在 repo 根目錄執行：

```sh
./scripts/quality.sh .
```

或針對單一 lab：

```sh
cd labs/00-hello-module
./quality.sh
```

## Git hooks

這個 repo 附了一組 repo-local hook 範本：

```sh
./scripts/install-git-hooks.sh
```

啟用後會：

- 用 `pre-commit` 擋 `.DS_Store` 並執行 `scripts/quality.sh`
- 用 `commit-msg` 要求 Conventional Commits，且主旨預設使用繁體中文

## 使用原則

- 先做 `00-02`，不要一開始跳 PCIe
- 先讓每個 lab 可觀測、可重複、可清理
- 先接受「第一次先跑通、第二次再看懂 code」這個節奏
- 每次新增功能時，同步補 `README`、`test.sh`、`quality.sh`、`debug-checklist.md`
- 遇到實際故障或重要學習事故時，再用 `notes/postmortem-template.md` 建立復盤紀錄
