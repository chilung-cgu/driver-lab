# Debug Checklist

- repeated load / unload 是否穩定？
- parallel access 是否暴露 race？
- fault injection 後 cleanup 是否完整？
- 測試失敗時，能否從 log 判斷是哪一條 path 出錯？

