# Companion Docs Index

這份索引列出「source 檔旁邊的詳解文件」。主 README 仍然是 lab 入口；companion docs 是當你正在 trace 某份 `.c`、`.h`、`.sh` 或 `Makefile` 時使用的旁讀層。

## 使用方式

1. 先從 lab README 確認目標與操作命令。
2. 開始看 source 時，打開同目錄的 `<source-file>.md`。
3. 如果讀到 runtime、UAPI、CLI 或 test 依賴，就沿 companion doc 裡的相對連結跳過去。

分段導入計畫見 [`companion-docs-rollout-plan.md`](companion-docs-rollout-plan.md)。

## Lab00 主線依賴

| Source | Companion doc | 角色 |
|---|---|---|
| [`../../labs/00-hello-module/driver_lab_hello.c`](../../labs/00-hello-module/driver_lab_hello.c) | [`../../labs/00-hello-module/driver_lab_hello.c.md`](../../labs/00-hello-module/driver_lab_hello.c.md) | 最小 kernel module 本體 |
| [`../../labs/00-hello-module/Makefile`](../../labs/00-hello-module/Makefile) | [`../../labs/00-hello-module/Makefile.md`](../../labs/00-hello-module/Makefile.md) | Lab00 kbuild 入口 |
| [`../../labs/00-hello-module/test.sh`](../../labs/00-hello-module/test.sh) | [`../../labs/00-hello-module/test.sh.md`](../../labs/00-hello-module/test.sh.md) | Lab00 smoke test |

## Lab01 主線依賴

| Source | Companion doc | 角色 |
|---|---|---|
| [`../../labs/01-debugfs-logging/driver_lab_debugfs_logging.c`](../../labs/01-debugfs-logging/driver_lab_debugfs_logging.c) | [`../../labs/01-debugfs-logging/driver_lab_debugfs_logging.c.md`](../../labs/01-debugfs-logging/driver_lab_debugfs_logging.c.md) | Lab01 debugfs/logging kernel module 本體 |
| [`../../labs/01-debugfs-logging/Makefile`](../../labs/01-debugfs-logging/Makefile) | [`../../labs/01-debugfs-logging/Makefile.md`](../../labs/01-debugfs-logging/Makefile.md) | Lab01 kbuild 入口 |
| [`../../labs/01-debugfs-logging/test.sh`](../../labs/01-debugfs-logging/test.sh) | [`../../labs/01-debugfs-logging/test.sh.md`](../../labs/01-debugfs-logging/test.sh.md) | Lab01 debugfs smoke test |

## Lab02 主線依賴

| Source | Companion doc | 角色 |
|---|---|---|
| [`../../labs/02-char-device/driver_lab_char.c`](../../labs/02-char-device/driver_lab_char.c) | [`../../labs/02-char-device/driver_lab_char.c.md`](../../labs/02-char-device/driver_lab_char.c.md) | Lab02 char device kernel module 本體 |
| [`../../labs/02-char-device/Makefile`](../../labs/02-char-device/Makefile) | [`../../labs/02-char-device/Makefile.md`](../../labs/02-char-device/Makefile.md) | Lab02 kbuild 入口 |
| [`../../labs/02-char-device/test.sh`](../../labs/02-char-device/test.sh) | [`../../labs/02-char-device/test.sh.md`](../../labs/02-char-device/test.sh.md) | Lab02 char device smoke test |

## Lab03 主線依賴

| Source | Companion doc | 角色 |
|---|---|---|
| [`../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c`](../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c) | [`../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../../labs/03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md) | Lab03 kernel driver 本體 |
| [`../../labs/03-ioctl-poll-mmap/test.sh`](../../labs/03-ioctl-poll-mmap/test.sh) | [`../../labs/03-ioctl-poll-mmap/test.sh.md`](../../labs/03-ioctl-poll-mmap/test.sh.md) | Lab03 smoke test |
| [`../../labs/03-ioctl-poll-mmap/Makefile`](../../labs/03-ioctl-poll-mmap/Makefile) | [`../../labs/03-ioctl-poll-mmap/Makefile.md`](../../labs/03-ioctl-poll-mmap/Makefile.md) | Lab03 kbuild 入口 |
| [`../../runtime/src/driver_lab_runtime.c`](../../runtime/src/driver_lab_runtime.c) | [`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md) | userspace runtime 實作 |
| [`../../runtime/include/driver_lab_runtime.h`](../../runtime/include/driver_lab_runtime.h) | [`../../runtime/include/driver_lab_runtime.h.md`](../../runtime/include/driver_lab_runtime.h.md) | userspace runtime public API |
| [`../../runtime/include/driver_lab_uapi.h`](../../runtime/include/driver_lab_uapi.h) | [`../../runtime/include/driver_lab_uapi.h.md`](../../runtime/include/driver_lab_uapi.h.md) | kernel/userspace 共用 ABI |
| [`../../tests/driver_lab_char_cli.c`](../../tests/driver_lab_char_cli.c) | [`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md) | userspace CLI |

## Lab04 主線依賴

| Source | Companion doc | 角色 |
|---|---|---|
| [`../../labs/04-locking-and-races/driver_lab_race.c`](../../labs/04-locking-and-races/driver_lab_race.c) | [`../../labs/04-locking-and-races/driver_lab_race.c.md`](../../labs/04-locking-and-races/driver_lab_race.c.md) | Lab04 race/safe-mode kernel driver 本體 |
| [`../../labs/04-locking-and-races/driver_lab_race_uapi.h`](../../labs/04-locking-and-races/driver_lab_race_uapi.h) | [`../../labs/04-locking-and-races/driver_lab_race_uapi.h.md`](../../labs/04-locking-and-races/driver_lab_race_uapi.h.md) | Lab04 kernel/userspace ioctl ABI |
| [`../../labs/04-locking-and-races/Makefile`](../../labs/04-locking-and-races/Makefile) | [`../../labs/04-locking-and-races/Makefile.md`](../../labs/04-locking-and-races/Makefile.md) | Lab04 kbuild 入口 |
| [`../../labs/04-locking-and-races/test.sh`](../../labs/04-locking-and-races/test.sh) | [`../../labs/04-locking-and-races/test.sh.md`](../../labs/04-locking-and-races/test.sh.md) | Lab04 unsafe/safe smoke test |
| [`../../tests/driver_lab_race_cli.c`](../../tests/driver_lab_race_cli.c) | [`../../tests/driver_lab_race_cli.c.md`](../../tests/driver_lab_race_cli.c.md) | Lab04 userspace pthread race CLI |

## Lab05 主線依賴

| Source | Companion doc | 角色 |
|---|---|---|
| [`../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c`](../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c) | [`../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c.md`](../../labs/05-pci-edu-mmio/driver_lab_edu_mmio.c.md) | Lab05 PCI EDU MMIO driver 本體 |
| [`../../labs/05-pci-edu-mmio/Makefile`](../../labs/05-pci-edu-mmio/Makefile) | [`../../labs/05-pci-edu-mmio/Makefile.md`](../../labs/05-pci-edu-mmio/Makefile.md) | Lab05 kbuild 入口 |
| [`../../labs/05-pci-edu-mmio/test.sh`](../../labs/05-pci-edu-mmio/test.sh) | [`../../labs/05-pci-edu-mmio/test.sh.md`](../../labs/05-pci-edu-mmio/test.sh.md) | Lab05 PCI/MMIO smoke test |

## Lab06 主線依賴

| Source | Companion doc | 角色 |
|---|---|---|
| [`../../labs/06-pci-edu-irq/driver_lab_edu_irq.c`](../../labs/06-pci-edu-irq/driver_lab_edu_irq.c) | [`../../labs/06-pci-edu-irq/driver_lab_edu_irq.c.md`](../../labs/06-pci-edu-irq/driver_lab_edu_irq.c.md) | Lab06 PCI EDU IRQ driver 本體 |
| [`../../labs/06-pci-edu-irq/Makefile`](../../labs/06-pci-edu-irq/Makefile) | [`../../labs/06-pci-edu-irq/Makefile.md`](../../labs/06-pci-edu-irq/Makefile.md) | Lab06 kbuild 入口 |
| [`../../labs/06-pci-edu-irq/test.sh`](../../labs/06-pci-edu-irq/test.sh) | [`../../labs/06-pci-edu-irq/test.sh.md`](../../labs/06-pci-edu-irq/test.sh.md) | Lab06 PCI/IRQ smoke test |

## Lab07 主線依賴

| Source | Companion doc | 角色 |
|---|---|---|
| [`../../labs/07-pci-edu-dma/driver_lab_edu_dma.c`](../../labs/07-pci-edu-dma/driver_lab_edu_dma.c) | [`../../labs/07-pci-edu-dma/driver_lab_edu_dma.c.md`](../../labs/07-pci-edu-dma/driver_lab_edu_dma.c.md) | Lab07 PCI EDU DMA driver 本體 |
| [`../../labs/07-pci-edu-dma/Makefile`](../../labs/07-pci-edu-dma/Makefile) | [`../../labs/07-pci-edu-dma/Makefile.md`](../../labs/07-pci-edu-dma/Makefile.md) | Lab07 kbuild 入口 |
| [`../../labs/07-pci-edu-dma/test.sh`](../../labs/07-pci-edu-dma/test.sh) | [`../../labs/07-pci-edu-dma/test.sh.md`](../../labs/07-pci-edu-dma/test.sh.md) | Lab07 PCI/DMA smoke test |

## 目前範圍

目前覆蓋 Lab00、Lab01、Lab02、Lab03、Lab04、Lab05、Lab06 與 Lab07 主線依賴。`quality.sh`、共用 filesystem helper、其他 labs 尚未加入長篇 companion docs；它們仍可從既有 README、debug checklist 和 source 註解閱讀。
