# `test.sh` 詳解

## 結論

`labs/08-runtime-library/test.sh` 是 Lab08 的 userspace runtime build/CLI smoke test。它不載入 kernel module，也不操作 `/dev/driver_lab_*`。它只確認：

```text
runtime/ 可以 build
tests/driver_lab_char_cli binary 真的存在且可執行
CLI 不帶參數時會印 Usage 並回傳非 0
```

這支 test 的目的，是確認 Lab08 的 runtime/CLI 包裝層至少有最小可執行入口。真正 driver 行為仍由 Lab02/Lab03 的 Linux smoke tests 驗證。

## 不確定處 / 查證範圍

這份 companion doc 已查過：

- [`test.sh`](test.sh) 本身。
- Lab08 Makefile：[`Makefile.md`](Makefile.md)。
- runtime source 旁讀：[`../../runtime/src/driver_lab_runtime.c.md`](../../runtime/src/driver_lab_runtime.c.md)、[`../../runtime/include/driver_lab_runtime.h.md`](../../runtime/include/driver_lab_runtime.h.md)、[`../../runtime/include/driver_lab_uapi.h.md`](../../runtime/include/driver_lab_uapi.h.md)。
- CLI source 旁讀：[`../../tests/driver_lab_char_cli.c.md`](../../tests/driver_lab_char_cli.c.md)。

這裡只解釋 Lab08 的 build/CLI smoke test，不重新解釋每個 runtime helper。

## 測試主線

整支 script 的流程是：

```text
compute repo root
define CLI path: tests/driver_lab_char_cli
create temp output log
build runtime
assert CLI is executable
run CLI without arguments
expect non-zero exit
grep Usage:
cleanup temp log
```

## 一、`set -eu`

原始碼：

```sh
set -eu
```

Lab08 test 使用 POSIX shell 風格：

| option | 意義 |
|---|---|
| `-e` | 簡單命令失敗時讓 script 停下來。 |
| `-u` | 使用未設定變數時直接失敗。 |

這讓 build/test failure 不會被悄悄吞掉。

## 二、找 repo root 與 CLI path

原始碼：

```sh
ROOT_DIR=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
CLI="$ROOT_DIR/tests/driver_lab_char_cli"
OUTPUT_LOG=$(mktemp)
```

| 變數 | 用途 |
|---|---|
| `ROOT_DIR` | repo 根目錄，不依賴你從哪個 cwd 執行 script。 |
| `CLI` | runtime build 後應該出現的 CLI binary。 |
| `OUTPUT_LOG` | 保存 CLI 無參數時的 stdout/stderr。 |

`CDPATH=` 是為了避免某些 shell 設定 `CDPATH` 時，`cd` 額外輸出路徑干擾 command substitution。

## 三、cleanup 與 trap

原始碼：

```sh
cleanup() {
    rm -f "$OUTPUT_LOG"
}

trap cleanup EXIT INT TERM
```

Lab08 沒有 load module，所以 cleanup 只需要刪掉暫存 log。

`trap` 確保正常結束、Ctrl-C 或 termination 時都會清掉 `mktemp` 產生的檔案。

## 四、build runtime

原始碼：

```sh
make -C "$ROOT_DIR/runtime"
test -x "$CLI"
```

這裡做兩件事。

第一，呼叫 runtime build：

```text
make -C runtime
  -> build tests/driver_lab_char_cli
```

第二，確認 CLI binary 真的存在且可執行：

```sh
test -x "$CLI"
```

這比只看 `make` exit code 多一層保護：build 系統如果改壞產物路徑，這裡會抓到。

## 五、故意用錯方式呼叫 CLI

原始碼：

```sh
if "$CLI" >"$OUTPUT_LOG" 2>&1; then
    printf 'ERROR: CLI without arguments should print usage and exit non-zero.\n' >&2
    exit 1
fi

grep -q 'Usage:' "$OUTPUT_LOG"
```

這段看起來反直覺，但很重要：CLI 沒有參數時應該失敗並印 usage。

測試邏輯是：

```text
run CLI with no args
  -> expected: non-zero exit
  -> expected: output contains Usage:
```

如果 CLI 無參數卻回傳 0，script 會印錯誤並失敗。這代表 CLI argument validation 壞掉。

如果 CLI 回傳非 0 但沒印 `Usage:`，`grep` 會失敗。這代表使用者看不到基本說明。

## 六、這支 test 沒有驗什麼？

它沒有驗：

- kernel module build。
- `insmod` / `rmmod`。
- `/dev/driver_lab_char0` 或 `/dev/driver_lab_ctl0` 是否存在。
- runtime 呼叫真 driver 的 read/write/ioctl/poll/mmap 行為。

這些仍要回：

- [`../02-char-device/test.sh.md`](../02-char-device/test.sh.md)
- [`../03-ioctl-poll-mmap/test.sh.md`](../03-ioctl-poll-mmap/test.sh.md)

## test 和 runtime/CLI 的對照

| test 片段 | 驗證什麼 |
|---|---|
| `make -C "$ROOT_DIR/runtime"` | runtime build glue 可用。 |
| `test -x "$CLI"` | CLI binary 存在且可執行。 |
| `"$CLI"` 無參數 | CLI argument validation 會拒絕錯誤用法。 |
| `grep -q 'Usage:'` | CLI 會印最小 usage。 |

## 常見卡點

- `make` 失敗：先看 `runtime/Makefile` compile command 與 include path。
- `test -x` 失敗：確認 `tests/driver_lab_char_cli` 是否真的被 build 出來。
- CLI 無參數回傳 0：CLI argument handling 壞掉。
- `Usage:` grep 失敗：CLI 可能有錯誤訊息，但缺少使用提示。
- 想驗 driver 行為：不要停在 Lab08，回 Lab02/Lab03 smoke tests。

## 讀完後你應該能回答

| 問題 | 標準答案 |
|---|---|
| Lab08 `test.sh` 會載入 kernel module 嗎？ | 不會。 |
| 它主要驗證什麼？ | runtime 能 build 出 CLI，且 CLI 無參數時會印 usage 並回傳非 0。 |
| 為什麼要檢查 `test -x "$CLI"`？ | 確認 build artifact 真的存在且可執行。 |
| 為什麼無參數 CLI 應該失敗？ | 因為 CLI 需要 device path 和 subcommand；無參數是錯誤用法。 |
| 真正 runtime 對 driver 的行為在哪裡驗？ | Lab02/Lab03 的 Linux smoke test。 |
