# `dmesg-gate.sh` 詳解

## 結論

`dmesg-gate.sh` 是 Lab09 私有的 kernel-log gate。兩支 Lab03 stress script 都先在 `/dev/kmsg` 寫入唯一 marker，結束後只檢查 marker 之後的 `dmesg`。它不執行 `dmesg -C`，因此不會刪除同一台機器上其他人的 kernel log。

它只判斷正常路徑 stress 期間是否出現已知 kernel warning 或 sanitizer signature；不是 fault-injection framework，也不會主動讓 allocation、usercopy、IRQ 或 DMA 路徑失敗。

## 第一輪先懂

- 呼叫端先設定 `DMESG_GATE_SUDO`，再 source 本 helper。
- `dmesg_gate_begin` 寫入 marker，必須在第一個 `insmod` 前完成。
- `dmesg_gate_check_and_cleanup` 取得一個完整 `dmesg` snapshot，只留下 marker 後的區段並檢查診斷字串。
- marker 遺失時直接失敗；不能用舊 log 或 `dmesg -C` 取代。
- 呼叫端的 `EXIT` trap 保存原本失敗碼。只有原本工作負載成功、但 gate 失敗時，整支 script 才以 gate failure 結束。

## 不確定處 / 查證範圍

本文件對照了：

- [`dmesg-gate.sh`](dmesg-gate.sh) 本身；
- 使用它的 [`stress-03-reload.sh`](stress-03-reload.sh) 與 [`stress-03-parallel.sh`](stress-03-parallel.sh)；
- Lab09 的 [`README.md`](README.md)；
- 同 repo 的 Lab05–07 marker-scoped `dmesg` test pattern。

它沒有把字串 gate 宣稱為完整 kernel diagnostics parser；實際驗證仍應在受控 guest 中進行並保存 script 的 stdout/stderr。

## 使用方式

呼叫端在建立 cleanup trap 前先 source helper：

```sh
DMESG_GATE_SUDO=$SUDO
. "$SCRIPT_DIR/dmesg-gate.sh"
```

然後在第一個 driver 動作前開始 gate：

```sh
dmesg_gate_begin "stress-03-reload"
```

正常路徑完成 unload 與 filesystem cleanup 後，才允許印出 passed：

```sh
if ! dmesg_gate_check_and_cleanup; then
    exit 1
fi
printf 'stress passed\n'
```

這個順序避免 kernel-log gate 失敗時仍先印出誤導性的成功訊息。

## `dmesg_gate_begin`

核心程式：

```sh
DMESG_GATE_MARKER="driver-lab-lab09: ${dmesg_gate_label} marker pid=$$ epoch=$(date +%s)"

printf '%s\n' "$DMESG_GATE_MARKER" |
    dmesg_gate_sudo tee /dev/kmsg >/dev/null
```

`pid` 與 epoch 讓同一個 script invocation 的起點可辨識。helper 同時建立兩個 `mktemp` 檔：一份完整 snapshot、一份 marker 後的切片；失敗時會清掉自己建立的暫存檔。

`DMESG_GATE_SUDO` 是空字串時直接執行；非 root test 會把它設為 `sudo`，讓寫 `/dev/kmsg` 和讀 `dmesg` 都走同一個權限邊界。

## `dmesg_gate_finish`

流程是：

```text
sudo dmesg -> complete snapshot
marker still present?
  no  -> fail: this run cannot be isolated
  yes -> awk extracts only later lines
          print the slice to stdout
          reject known warning/sanitizer signatures
```

切片使用 marker 的文字比對，而不是「先量 `dmesg` 行數」。行數 baseline 無法可靠區分 ring buffer wrap；marker 本身不在 snapshot 中時，gate 明確失敗。

目前拒絕的 signature 是：

```text
BUG:
WARNING:
KASAN:
KCSAN:
Oops:
use-after-free
general protection fault
```

這些是高訊號的 diagnostics，不是 Linux 所有可能錯誤的完整清單。

## 原始失敗碼怎麼保留

`dmesg_gate_check_and_cleanup` 只做 gate 並移除暫存檔。呼叫端的 trap 才負責保存原始結果：

```sh
cleanup() {
    status=$?

    # stop workers, unload only this test's module, clean artifacts
    if [ "$DMESG_GATE_STARTED" -eq 1 ] && ! dmesg_gate_check_and_cleanup; then
        if [ "$status" -eq 0 ]; then
            status=1
        fi
    fi
    exit "$status"
}
```

因此 `insmod`、worker 或 filesystem assertion 已經失敗時，dmesg 的第二個錯誤不會把第一個失敗碼改成另一個值。反過來，正常 stress 做完但 marker 遺失或出現 warning 時，gate 會讓最終結果失敗。

## 限制 / 例外

- kernel log 是全系統共用的；受控 marker 之後的無關 warning 仍會讓 gate 失敗。這是刻意採取較保守的判斷。
- script 只把 marker 後切片印到 stdout。要保存證據時，請保存完整 script stdout/stderr；它不會代替集中式 test artifact 收集。
- 這裡沒有強制任何 failure path，所以通過只代表 normal-path stress 與所列 diagnostics gate 都通過。

## 讀完後你應該能回答

| 問題 | 答案 |
|---|---|
| 為什麼不用 `dmesg -C`？ | kernel log 是共享診斷資料；清空會刪除不屬於本 test 的證據。 |
| marker 不在 snapshot 時怎麼辦？ | 直接失敗，因為本次區段已無法可靠隔離。 |
| 為什麼不能讓 gate 覆蓋原失敗碼？ | 第一個 workload failure 是最重要的診斷訊號，cleanup/gate 的附加失敗不能把它藏起來。 |
| 通過 gate 是否等於完成 fault injection？ | 不等於；它只檢查正常壓力路徑。 |
