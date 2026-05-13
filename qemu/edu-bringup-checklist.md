# QEMU EDU Bring-up Checklist

## 目標

把 `QEMU edu` 裝置帶進 `Linux guest`，讓後續 `05-07` 可以真的開始做。

## 最小條件

- host 上有 `qemu-system-x86_64`
- host 是 `Linux` 或 `macOS`
- 你有一個可開機的 Linux guest image
- 你能登入 guest shell

## 啟動前檢查

1. host 上確認 `qemu-system-x86_64` 存在
2. 準備 guest image，例如 `ubuntu.qcow2`
3. 知道你現在是：
   - 在 host 啟動 QEMU
   - 在 guest 內 build / load driver

## 啟動範例

```sh
QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 ./qemu/launch-edu-vm.sh
```

如果你在 `macOS` 上跑，這支腳本會優先選 `hvf`。
如果你在 `Linux` 上跑，這支腳本會優先選 `kvm`。

## guest 內第一件事

```sh
lspci -nn | grep 1234:11e8
```

如果看得到：

```text
1234:11e8
```

代表 `edu` 裝置已經出現在 guest 內。

## 接著要做什麼

1. 把 `driver-lab` 帶進 guest
2. 在 guest 內補齊 build 工具與 kernel headers
3. 讀 [`../docs/guides/linux-guest-05-to-07-walkthrough.md`](../docs/guides/linux-guest-05-to-07-walkthrough.md)
4. 在 guest 內依序完成 `05`、`06`、`07`

## 先不要急著 debug 的東西

- 一開始先不要碰 MSI-X
- 先不要追效能
- 先把 `probe`、BAR map、基本 register read 做通
