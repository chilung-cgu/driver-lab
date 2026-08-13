# `stress-03-reload.sh` 詳解

## 結論

`stress-03-reload.sh` 對 Lab03 的 `driver_lab_ioctl_poll_mmap` 做 repeated load/unload stress。每一輪都驗證 `/dev`、sysfs 和 `/proc/devices` 出現，再確認 unload 後 `/dev` 與 sysfs 消失；預設跑 20 輪，可用 `ITERATIONS` 增加。

它也使用 [`dmesg-gate.sh`](dmesg-gate.sh) 把本輪 kernel log 限縮在唯一 marker 後，拒絕已知 warning/sanitizer signature。這仍是正常路徑的 stress，並不是 allocation 或 usercopy fault injection suite。

## 第一輪先懂

- `ITERATIONS` 必須是正整數，預設為 `20`。
- script 拒絕 pre-loaded module，只卸載自己載入的 instance。
- 每一輪 `insmod` 後檢查 char-device filesystem surface，`rmmod` 後檢查它們已消失。
- 先完成所有工作負載與 unload，dmesg gate 通過後才印出 passed。
- 如果 workload 已經失敗，cleanup 仍收集 marker 後的 log，但保留原本的 exit status。

## 不確定處 / 查證範圍

本文件對照了：

- [`stress-03-reload.sh`](stress-03-reload.sh)；
- [`dmesg-gate.sh`](dmesg-gate.sh)；
- [`../../scripts/fs-surface-checks.sh`](../../scripts/fs-surface-checks.sh)；
- Lab03 的 [`../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md`](../03-ioctl-poll-mmap/driver_lab_ioctl_poll_mmap.c.md)。

它說明的是 repeated reload 的可觀測行為，不把有限輪數的 stress pass 說成完整 lifetime proof 或 fault injection。

## 測試主線

```text
confirm Linux and ITERATIONS
→ refuse a pre-loaded Lab03 module
→ build Lab03
→ write a unique kernel-log marker
→ repeat N times:
     insmod
     verify /dev + sysfs + /proc/devices
     rmmod
     verify /dev + sysfs disappear
→ inspect dmesg after the marker
→ print passed only when every gate succeeded
```

## 參數與 target

```sh
LAB_DIR="$ROOT_DIR/labs/03-ioctl-poll-mmap"
MODULE_NAME=driver_lab_ioctl_poll_mmap
DEVICE=/dev/driver_lab_ctl0
ITERATIONS=${ITERATIONS:-20}
```

用法：

```sh
ITERATIONS=100 ./stress-03-reload.sh
```

空值、`0`、負數或非數字都會在任何 module 動作前被拒絕。`ITERATIONS` 很大只會增加同一組 normal-path sequence 的重複次數，不會建立新的 fault path。

## Module ownership 與 filesystem surface

script 先確認 module 未載入：

```sh
if lsmod | grep -q "^${MODULE_NAME} "; then
    printf 'ERROR: %s is already loaded; unload it before this isolated test.\n' \
        "$MODULE_NAME" >&2
    exit 1
fi
```

載入後由 `fs_expect_char_device` 同時驗證：

```text
/dev/driver_lab_ctl0
/sys/class/driver_lab_ctl/driver_lab_ctl0
/proc/devices -> driver_lab_ctl
```

卸載後 `fs_expect_absent` 驗證 `/dev` node 與 sysfs class device 都已消失。這避免「`insmod` exit 0」被誤當作完整使用者可見 surface 正確。

## Reload loop

核心 loop：

```sh
while [ "$i" -lt "$ITERATIONS" ]; do
    $SUDO insmod "$LAB_DIR/${MODULE_NAME}.ko"
    loaded_by_test=1
    fs_expect_char_device "$DEVICE" \
        /sys/class/driver_lab_ctl/driver_lab_ctl0 \
        driver_lab_ctl

    $SUDO rmmod "$MODULE_NAME"
    loaded_by_test=0
    fs_expect_absent "$DEVICE" "device node"
    fs_expect_absent /sys/class/driver_lab_ctl/driver_lab_ctl0 \
        "sysfs class device"
    i=$((i + 1))
done
```

`loaded_by_test` 是 cleanup ownership flag。任何一輪在 `insmod` 後失敗時，EXIT trap 只會嘗試移除 script 自己成功載入的 module；它不會刪除一開始就存在的其他 session module。

## Marker-scoped dmesg gate

在第一個 `insmod` 前，script 先開始 gate：

```sh
dmesg_gate_begin "stress-03-reload"
```

正常路徑在所有 unload checks 後完成 gate：

```sh
if ! dmesg_gate_check_and_cleanup; then
    exit 1
fi
printf 'stress-03-reload passed (%s iterations).\n' "$ITERATIONS"
```

詳細的 marker、ring-buffer 與診斷 signature 行為見 [`dmesg-gate.sh.md`](dmesg-gate.sh.md)。這裡的重要契約是：不使用 `dmesg -C`，marker 遺失就失敗，且不會在 gate 失敗前印出 passed。

## Cleanup 與原始失敗碼

```sh
cleanup() {
    status=$?

    trap - EXIT INT TERM
    # unload only a module loaded by this test; clean build output
    if [ "$DMESG_GATE_STARTED" -eq 1 ] && ! dmesg_gate_check_and_cleanup; then
        if [ "$status" -eq 0 ]; then
            status=1
        fi
    fi
    exit "$status"
}
```

`status=$?` 先保存主工作負載的結果。若主工作負載失敗，dmesg capture 仍會嘗試執行，但不能把這個第一個失敗碼替換成 cleanup 或 gate 的結果。若主工作負載成功而 gate 失敗，最終 status 為 `1`。

`INT` 和 `TERM` trap 會先以 `130`、`143` 結束，然後走同一個 EXIT cleanup。因此 Ctrl-C 也不會被 cleanup 意外改成成功。

## 限制 / 例外

- 這不會在 active fd 或 active VMA 存在時測試 unload；那需要專門的 lifetime case。
- marker 後的無關 kernel warning 也會讓測試失敗；請在受控 guest 中執行並保留 stdout/stderr。
- 20 次或更多次的 pass 只能增加觀察到 cleanup 問題的機率，不能證明沒有資源或競態 bug。

## 讀完後你應該能回答

| 問題 | 答案 |
|---|---|
| `ITERATIONS=100` 改變什麼？ | 把同一個 load/verify/unload/verify sequence 重複 100 次。 |
| 為什麼拒絕 pre-loaded module？ | script 無法保證那個 instance 的來源與狀態，也不應卸載別人的 debug session。 |
| 為什麼 passed 在 dmesg gate 後才印？ | 避免 warning、sanitizer report 或 marker 遺失時仍輸出誤導性成功。 |
| 這是 fault injection 嗎？ | 不是；目前是 normal-path repeated reload stress。 |
