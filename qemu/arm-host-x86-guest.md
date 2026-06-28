# ARM host 跑 x86_64 EDU guest

這份文件記錄在 `arm64/aarch64` host 上，用 `qemu-system-x86_64`
建立 Lab05 到 Lab07 可用 Linux guest 的最小流程。

重點不是把本機 VM image 放進 repo，而是保留可重建的步驟。`qcow2`、
cloud-init ISO、serial log、pid file 都應該留在 repo 外，例如
`$HOME/vm/lab05-x86/`。

## 第一輪先懂

- `qemu-system-x86_64` 表示要建立 `x86_64` guest，不表示 host 必須是 `x86_64`。
- ARM host 可以跑 x86_64 guest，但通常只能用 `tcg`，速度會比 x86 host 的 `kvm` 慢很多。
- Lab05 的驗證位置是 guest。host 負責啟 QEMU 與掛 `-device edu`，guest 負責 `make`、`insmod`、`test.sh`。
- 第一個成功訊號是 guest 裡看得到 `lspci -nn | grep 1234:11e8`。

## 不要放進 repo 的東西

| 檔案 | 原因 |
|---|---|
| `*.qcow2` | VM disk image 會變大，也包含本機狀態 |
| `cloud-init.iso` | 由本機 seed 產生，常含使用者或 SSH 設定 |
| `serial.log` | runtime log |
| `qemu.pid` | runtime pid |
| 含個人 SSH key 的 `user-data` | 只適合放在本機 VM 目錄 |

## Host 檢查

在 host 先確認 QEMU binary 存在：

```sh
uname -m
command -v qemu-system-x86_64
qemu-system-x86_64 --version
qemu-system-x86_64 -accel help
qemu-system-x86_64 -device help | grep -w edu
```

在 ARM host 上，`-accel help` 只看到 `tcg` 是正常的。

## 建立本機 VM 目錄

以下用 `$HOME/vm/lab05-x86` 當例子。這個目錄不要 commit。

```sh
mkdir -p "$HOME/vm/lab05-x86"
cd "$HOME/vm/lab05-x86"
```

準備一個 x86_64 Ubuntu cloud image。檔名可以不同，重點是 guest image
本身要是 `x86_64/amd64`。

```sh
# 範例：假設 base image 已放在 $HOME/vm/ubuntu-24.04-base.img
qemu-img create \
  -f qcow2 \
  -F qcow2 \
  -b "$HOME/vm/ubuntu-24.04-base.img" \
  "$HOME/vm/lab05-x86/ubuntu-lab05.qcow2" \
  20G
```

## 建立 cloud-init seed

先準備 `meta-data`：

```sh
cat > "$HOME/vm/lab05-x86/meta-data" <<'EOF'
instance-id: lab05-x86-01
local-hostname: lab05-x86
EOF
```

再準備 `user-data`。請把 `ssh-ed25519 ...` 換成你自己的 public key。

```sh
cat > "$HOME/vm/lab05-x86/user-data" <<'EOF'
#cloud-config
hostname: lab05-x86
fqdn: lab05-x86.local
users:
  - name: ubuntu
    groups: sudo
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    lock_passwd: false
    ssh_authorized_keys:
      - ssh-ed25519 REPLACE_WITH_YOUR_PUBLIC_KEY
ssh_pwauth: false
package_update: true
package_upgrade: false
packages:
  - build-essential
  - linux-headers-generic
  - pciutils
  - kmod
  - git
  - rsync
EOF
```

產生 seed ISO：

```sh
cloud-localds \
  "$HOME/vm/lab05-x86/cloud-init.iso" \
  "$HOME/vm/lab05-x86/user-data" \
  "$HOME/vm/lab05-x86/meta-data"
```

## 啟動 guest

可以直接使用 repo 的啟動腳本，並用環境變數指定 image 與 seed ISO：

```sh
QEMU_IMAGE="$HOME/vm/lab05-x86/ubuntu-lab05.qcow2" \
QEMU_ACCEL=tcg \
QEMU_EXTRA_ARGS="-machine q35 -cpu qemu64 -drive file=$HOME/vm/lab05-x86/cloud-init.iso,if=virtio,format=raw,readonly=on" \
./qemu/launch-edu-vm.sh
```

這個指令會在 foreground 執行 QEMU。若你需要背景執行，可以另外在本機 VM
目錄放自己的 wrapper script，加入 `-display none`、`-serial file:...`、
`-pidfile ...`、`-daemonize`。這類 wrapper 通常也不需要 commit。

## 登入 guest

QEMU 腳本預設把 host `2222` 轉到 guest `22`：

```sh
ssh -i ~/.ssh/id_ed25519 -p 2222 ubuntu@127.0.0.1
```

如果重建 VM 後出現 host key changed，代表 `127.0.0.1:2222` 背後換了一台
guest。確認不是連錯 VM 後，可以清掉舊 key：

```sh
ssh-keygen -f ~/.ssh/known_hosts -R '[127.0.0.1]:2222'
```

## Guest 內驗證

登入 guest 後先檢查架構、EDU device、工具與 kernel headers：

```sh
uname -m
lspci -nn | grep 1234:11e8
command -v gcc make lspci insmod rmmod git
ls -ld /lib/modules/$(uname -r)/build
cloud-init status --long
```

期望看到：

```text
x86_64
00:03.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
```

## 同步 repo 並跑 Lab05

在 host 同步目前 repo 到 guest：

```sh
rsync -a --exclude .git \
  -e 'ssh -i ~/.ssh/id_ed25519 -p 2222' \
  /home/ubuntu/driver-lab/ \
  ubuntu@127.0.0.1:/home/ubuntu/driver-lab/
```

在 guest 裡跑 smoke test：

```sh
cd ~/driver-lab
./labs/05-pci-edu-mmio/test.sh
```

成功時會看到類似：

```text
driver_lab_edu_mmio: probe start for 0000:00:03.0
driver_lab_edu_mmio 0000:00:03.0: BAR0 mapped, len=1048576 bytes
driver_lab_edu_mmio 0000:00:03.0: liveness check passed
05-pci-edu-mmio smoke test passed.
```

## 關機

優先從 guest 正常關機：

```sh
ssh -i ~/.ssh/id_ed25519 -p 2222 ubuntu@127.0.0.1 'sudo poweroff'
```

避免直接殺 QEMU，除非 guest 已經沒有反應。

## 常見卡點

| 現象 | 先看哪裡 |
|---|---|
| `qemu-system-x86_64` 不存在 | host 是否安裝 QEMU system emulator |
| `-accel kvm` 不支援 | ARM host 跑 x86_64 guest 通常只能用 `tcg` |
| SSH 連不上 | guest 是否還在跑 cloud-init、host port `2222` 是否被 QEMU 監聽 |
| SSH host key changed | 是否重建過 VM；確認後移除舊的 `[127.0.0.1]:2222` known_hosts |
| guest 看不到 `1234:11e8` | QEMU 啟動參數是否有 `-device edu` |
| 缺 `/lib/modules/$(uname -r)/build` | guest 內安裝對應 kernel headers |
