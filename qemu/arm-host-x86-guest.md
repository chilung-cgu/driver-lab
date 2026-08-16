# ARM host 跑 x86_64 QEMU EDU guest

> 目標：在`arm64/aarch64` host上，用`qemu-system-x86_64`建立可執行Lab05～Lab07的Linux x86_64 guest。VM image、seed、SSH key、runtime log都留在repo外。

## 結論

- QEMU binary後綴表示**guest architecture**，不是host architecture。
- ARM host可以模擬x86_64 guest，但通常只能用TCG軟體翻譯；KVM只加速相同CPU architecture，Apple HVF也不會把x86_64 guest硬體加速在arm64 host上。
- Host負責啟QEMU與掛`-device edu`；guest負責kernel headers、build、`insmod/rmmod`與tests。
- 第一個gate是guest內：

```sh
uname -m
lspci -Dnn | grep 1234:11e8
```

期望architecture為`x86_64`且可列舉EDU。

## 不進版控的檔案

- `*.qcow2` / `*.img`
- cloud-init seed ISO
- 含帳號、password hash或SSH key的`user-data`
- serial log、pid file、monitor socket
- known_hosts或私鑰

建議放在：

```text
$HOME/vm/driver-lab-x86/
```

## 1. Host先決條件

確認binary與EDU model：

```sh
uname -m
command -v qemu-system-x86_64
qemu-system-x86_64 --version
qemu-system-x86_64 -accel help
qemu-system-x86_64 -device help | grep -w edu
command -v qemu-img
command -v cloud-localds
```

若缺`cloud-localds`，Debian/Ubuntu host通常安裝`cloud-image-utils`；macOS可使用其他cloud-init seed工具，或先以console登入，不必強迫使用相同套件。

在ARM host只看到`tcg`通常合理。不要設定`QEMU_ACCEL=kvm`；若QEMU明確回報accelerator不支援，改用`tcg`。

## 2. 準備可信的x86_64 guest image

從發行版官方cloud-image頁取得`amd64/x86_64` image，核對官方checksum。不要只根據檔名猜architecture。

以下假設：

```sh
VM_DIR="$HOME/vm/driver-lab-x86"
BASE_IMAGE="$HOME/vm/images/ubuntu-24.04-server-cloudimg-amd64.img"
mkdir -p "$VM_DIR"
```

可建立overlay，保留base image：

```sh
qemu-img info "$BASE_IMAGE"
qemu-img create \
  -f qcow2 \
  -F qcow2 \
  -b "$BASE_IMAGE" \
  "$VM_DIR/driver-lab.qcow2" \
  20G
```

如果`qemu-img info`顯示base不是qcow2，`-F`必須改成實際format；不要照抄錯誤backing format。

## 3. 建立cloud-init seed

`meta-data`：

```sh
cat > "$VM_DIR/meta-data" <<'EOF'
instance-id: driver-lab-x86-01
local-hostname: driver-lab-x86
EOF
```

`user-data`範例；只放public key：

```sh
cat > "$VM_DIR/user-data" <<'EOF'
#cloud-config
hostname: driver-lab-x86
users:
  - name: ubuntu
    groups: sudo
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    lock_passwd: true
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

建立seed：

```sh
cloud-localds "$VM_DIR/cloud-init.iso" \
  "$VM_DIR/user-data" "$VM_DIR/meta-data"
```

Cloud image第一次開機安裝套件可能較久。Serial console或`cloud-init status --wait`比猜測固定等待秒數可靠。

## 4. 啟動QEMU EDU guest

使用repo script：

```sh
QEMU_IMAGE="$VM_DIR/driver-lab.qcow2" \
QEMU_ACCEL=tcg \
QEMU_EXTRA_ARGS="-machine q35 -cpu qemu64 -drive file=$VM_DIR/cloud-init.iso,if=virtio,format=raw,readonly=on" \
./qemu/launch-edu-vm.sh
```

啟動前先讀[`README.md`](README.md)與script本身，確認它已包含：

- `-device edu`
- guest network與host port forwarding
- image format與interface
- machine/console/display選項

環境變數裡含空白的`QEMU_EXTRA_ARGS`由script如何展開很重要；若script不支援任意quoted arguments，改成明確的array/wrapper，而不是把複雜shell quoting繼續疊上去。

## 5. 登入guest

若script把host TCP 2222轉給guest 22：

```sh
ssh -i ~/.ssh/id_ed25519 -p 2222 ubuntu@127.0.0.1
```

重建guest後host key改變，先確認port背後確實是新VM，再移除舊entry：

```sh
ssh-keygen -f ~/.ssh/known_hosts -R '[127.0.0.1]:2222'
```

## 6. Guest內驗證

```sh
uname -m
uname -r
lspci -Dnn | grep 1234:11e8
command -v gcc make lspci insmod rmmod git
ls -ld "/lib/modules/$(uname -r)/build"
cloud-init status --long
```

可能看到類似：

```text
x86_64
0000:00:04.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
```

BDF不保證固定是`00:03.0`或`00:04.0`；每次machine topology與其他devices都可能影響enumeration。腳本與文件應match ID，不應寫死BDF。

如果`/lib/modules/$(uname -r)/build`不存在，`linux-headers-generic`不一定剛好對應目前正在跑的kernel。安裝`linux-headers-$(uname -r)`，或boot到已安裝headers的kernel。

## 7. 把repo同步進guest

在host repo root執行，別寫死成某個使用者的`/home/...`：

```sh
rsync -a --delete \
  --exclude .git \
  -e 'ssh -i ~/.ssh/id_ed25519 -p 2222' \
  ./ ubuntu@127.0.0.1:/home/ubuntu/driver-lab/
```

`--delete`會刪除destination中source沒有的檔案；不確定時先拿掉，或加`--dry-run`。也可直接在guest`git clone/fetch`，避免手動同步產生版本不一致。

## 8. 依序執行Labs

```sh
cd ~/driver-lab
./scripts/check-kernel-env.sh
./labs/05-pci-edu-mmio/test.sh
./labs/06-pci-edu-irq/test.sh
./labs/07-pci-edu-dma/test.sh
```

不要在Lab05未通過時直接追Lab07。先保留：

```sh
uname -a
lspci -Dnnvv
sudo dmesg
cat /proc/interrupts
```

作為可重現證據。

## 9. 關機

優先從guest正常關機：

```sh
ssh -i ~/.ssh/id_ed25519 -p 2222 ubuntu@127.0.0.1 'sudo poweroff'
```

若QEMU foreground運作，等待process正常退出。只有guest無回應時才強制kill，之後應檢查filesystem/image狀態。

## 常見卡點

| 現象 | 先查 |
|---|---|
| 找不到`qemu-system-x86_64` | host是否安裝system emulator，不只是user-mode QEMU |
| `-accel kvm/hvf`失敗 | guest/host architecture是否不同；ARM host跑x86_64改用TCG |
| 開機極慢 | TCG跨architecture正常較慢；減少vCPU/GUI、看serial log，不要誤判hang |
| SSH連不上 | cloud-init、guest network、port 2222衝突、QEMU是否仍在跑 |
| `lspci`看不到EDU | launch arguments是否有`-device edu`，以及你登入的是正確guest |
| headers缺失或build失敗 | `uname -r`與`/lib/modules/.../build`是否一致 |
| BDF與文件不同 | BDF是enumeration結果；依Vendor/Device ID找device |
| overlay建立失敗 | base image實際format與`-F`是否一致 |

## Self-check

1. 為什麼ARM host可以跑x86_64 guest，卻通常不能用KVM？
2. Lab05 driver應在host還是guest build/load？
3. 為什麼不能把EDU BDF寫死為`00:03.0`？
4. `linux-headers-generic`已安裝，為什麼external module仍可能無法build？
5. rsync範例為什麼使用repo root的`./`而不是固定`/home/ubuntu/...`？

<details>
<summary>參考答案</summary>

1. QEMU TCG可做跨ISA動態翻譯；KVM使用host CPU virtualization extension，通常要求guest與host ISA相容，不能直接把x86_64 guest硬體加速在arm64 CPU上。
2. 在能列舉EDU且kernel/headers匹配的Linux guest中；host只負責QEMU、disk與network。
3. BDF由當次PCI topology/enumeration決定，加入其他device或改machine type都可能改變；應用`1234:11e8`與sysfs查找。
4. Running kernel可能不是generic metapackage目前安裝的版本；external module需要與`uname -r`完全匹配的build tree/config/generated headers。
5. Host repo位置依使用者與OS不同；從repo root同步`./`才可攜。固定Linux路徑在macOS或其他帳號會錯。

</details>

## 參考

- QEMU invocation: <https://www.qemu.org/docs/master/system/invocation.html>
- QEMU TCG: <https://www.qemu.org/docs/master/devel/tcg.html>
- QEMU EDU: <https://www.qemu.org/docs/master/specs/edu.html>
- External modules: <https://docs.kernel.org/kbuild/modules.html>
