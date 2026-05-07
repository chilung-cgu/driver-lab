# Debug Checklist

- interrupt 是否真的被 raise？
- handler 是否被呼叫？
- acknowledge register 是否有清乾淨？
- 如果改成 MSI，teardown path 是否仍正確？
- IRQ path 是否避免睡眠與不必要工作？

