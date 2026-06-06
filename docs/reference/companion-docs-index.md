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

## 目前範圍

目前覆蓋 Lab00 與 Lab03 主線依賴。`quality.sh`、共用 filesystem helper、其他 labs 尚未加入長篇 companion docs；它們仍可從既有 README、debug checklist 和 source 註解閱讀。
