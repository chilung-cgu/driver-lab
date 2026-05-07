# Debug Checklist

- 前一關 `05` 是否已經穩定？如果 `probe` 都不穩，先不要 debug IRQ。
- interrupt 是否真的被 raise？
- handler 是否被呼叫？
- acknowledge register 是否有清乾淨？
- 如果改成 MSI，teardown path 是否仍正確？
- IRQ path 是否避免睡眠與不必要工作？
