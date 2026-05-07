# QEMU EDU Bring-up Checklist

## 目標

把 `QEMU edu` 裝置放進 Linux guest，讓後續 `05-07` 可以真的開始做。

## 最小條件

- Linux host 上有 `qemu-system-x86_64`
- 你有一個可開機的 Linux guest image
- 你能登入 guest

## 啟動前檢查

1. host 上確認 `qemu-system-x86_64` 存在
2. 準備 guest image，例如 `ubuntu.qcow2`
3. 確認你知道怎麼進 guest shell

## 啟動範例

```sh
QEMU_IMAGE=$HOME/vm/ubuntu.qcow2 ./qemu/launch-edu-vm.sh
```

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
2. 在 guest 內先完成 `00-03`
3. 再開始 `05-pci-edu-mmio`

## 先不要急著 debug 的東西

- 一開始先不要碰 MSI-X
- 先不要追效能
- 先把 `probe`、BAR map、基本 register read 做通
