# driver-lab

> 給 0 driver 經驗工程師的 Linux Host Driver 實作練習場，目標是為 PCIe AI 加速卡 Host Driver 工作做準備。

## 這個專案解決什麼問題

這不是一份只講理論的筆記，而是一個可逐步落地的學習專案。它把學習內容拆成：

- 可在 `Linux host` 上反覆 build / load / unload 的最小 lab
- 可觀測的 debug 路徑：`dmesg`、`debugfs`、dynamic debug、後續可擴充 ftrace
- 可擴充的 user-space runtime 練習
- 從一般 kernel module，逐步過渡到 `PCIe + MMIO + IRQ + DMA`

## 如果你是 0 經驗，先怎麼看

先走這條路，不要直接打開所有檔案：

1. 先看 [新手前導](docs/beginner-primer.md)
2. 再看 [Linux Host 建置與風險檢查](docs/linux-host-setup.md)
3. 然後只做 [`labs/00-hello-module`](labs/00-hello-module)
4. `00` 穩定後，再進 [`labs/01-debugfs-logging`](labs/01-debugfs-logging)
5. 等你能解釋 `01` 的觀測路徑，再進 [`labs/02-char-device`](labs/02-char-device)

你現在看不懂大部分內容，是正常的。這個專案的正確打開方式不是「先把全部讀懂」，而是「先跑通最小閉環，再回頭理解 code」。

## 推薦執行模型

- `macOS`：編輯器、筆記、Git
- `Linux 主機`：真正 build / load / 測試 kernel module
- `QEMU`：第 10-12 週開始拿來練 `edu` PCI 教學裝置

> [!WARNING]
> 不要把 Docker container 當成主要 kernel module 練習環境。容器共享 kernel，而 Docker Desktop on Mac 會把容器跑在自己的 Linux VM 裡，這不適合作為你的主要 driver lab 主場。

## 目前已落地的內容

- `00-hello-module`：完整最小閉環
- `01-debugfs-logging`：完整 debugfs + dynamic debug 入門
- `02-char-device`：完整簡單 char device
- `03-ioctl-poll-mmap`：已實作 `ioctl + poll + mmap + runtime + smoke test`
- `04-locking-and-races`：已實作 race 重現、safe-mode 對照、CLI 與 smoke test
- `05-pci-edu-mmio`：已實作 PCI probe + BAR map + ident/liveness self-test
- `06-pci-edu-irq`：已實作 IRQ request + raise + acknowledge self-test
- `07-pci-edu-dma`：已實作 coherent DMA + IRQ completion + round-trip compare
- `08-runtime-library`：已跟著 `03` 補到第一版可用狀態
- `09-stress-and-fault-injection`：已補第一批 `03` 專用 stress 腳本
- `runtime/`：user-space runtime 起始骨架
- `docs/`：環境、路線圖、程式閱讀、debug playbook、AI 加速卡對映文件

## 成熟度說明

這個 repo 目前不是「全部 labs 都完成」的狀態。

- `00-02`：可以直接拿去 Linux host 練習
- `03`：可直接拿去 Linux host 練習
- `04`：可直接拿去 Linux host 練習
- `05-07`：已有第一版 driver code 與 Linux guest smoke test，但尚未在這台 macOS 主機上實機驗證
- `08-09`：已有第一版可執行內容
- `runtime/`：目前已覆蓋 `02-char-device` 與 `03-ioctl-poll-mmap`

## 建議的第一天流程

1. 先看 [`docs/beginner-primer.md`](docs/beginner-primer.md)
2. 再看 [`docs/linux-host-setup.md`](docs/linux-host-setup.md)
3. 再看 [`docs/check-kernel-env-explained.md`](docs/check-kernel-env-explained.md)
4. 這時候才在 Linux 主機執行 [`scripts/check-kernel-env.sh`](scripts/check-kernel-env.sh)
5. 完成 [`labs/00-hello-module/README.md`](labs/00-hello-module/README.md)
6. 完成 [`labs/01-debugfs-logging/README.md`](labs/01-debugfs-logging/README.md)
7. 完成 [`labs/02-char-device/README.md`](labs/02-char-device/README.md)

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

## 文檔索引

- [新手前導](docs/beginner-primer.md)
- [16 週學習路線](docs/learning-roadmap.md)
- [Linux Host 建置與風險檢查](docs/linux-host-setup.md)
- [第一次環境檢查輸出怎麼看](docs/check-kernel-env-explained.md)
- [新手術語表](docs/beginner-glossary.md)
- [常見失敗圖鑑](docs/common-failures.md)
- [併發與同步白話前導](docs/concurrency-primer.md)
- [04 Locking and Races 導讀](docs/lab-04-walkthrough.md)
- [PCIe / MMIO / IRQ / DMA 白話前導](docs/pcie-primer.md)
- [QEMU EDU 新手起手式](docs/qemu-edu-first-pass.md)
- [官方來源索引](docs/source-index.md)
- [程式閱讀指南](docs/code-reading-guide.md)
- [Debug / 測試 Playbook](docs/debugging-playbook.md)
- [AI 加速卡 Host Driver 架構對映](docs/accelerator-driver-architecture.md)
- [AI Agent + Git Checkpoint 規則範本](docs/ai-agent-git-checkpoint-policy.md)

## 品質檢查

在 Linux host 執行：

```sh
./scripts/quality.sh
```

或針對單一 lab：

```sh
cd labs/00-hello-module
./quality.sh
```

## Git Hook 範本

這個 repo 也附了一組可直接啟用的範本：

```sh
./scripts/install-git-hooks.sh
```

啟用後會：

- 用 `pre-commit` 擋 `.DS_Store` 並執行 `scripts/quality.sh`
- 用 `commit-msg` 要求 Conventional Commits

## 使用原則

- 先做 `00`、`01`、`02`，不要一開始跳到 PCIe
- 先讓每個 lab 可觀測、可重複、可清理
- 先接受「第一次先跑通、第二次再看懂 code」這個節奏
- 每次新增功能時，同步補：
  - `README`
  - `test.sh`
  - `quality.sh`
  - `debug-checklist.md`
  - `postmortem.md`
