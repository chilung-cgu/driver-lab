# Debug Checklist

- 問題是 process context 還是 IRQ context？
- 應該用 mutex 還是 spinlock？
- 是否需要 waitqueue / completion？
- KCSAN / lockdep 是否能重現問題？
- resource lifetime 是否和 worker / thread 協調一致？

