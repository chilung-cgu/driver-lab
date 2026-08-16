# Markdown link audit — 2026-08

## 結論

本次audit掃描repository中所有剩餘Markdown文件，重新導向已被集中化取代的文件參照，並新增永久CI，阻止broken local links、missing anchors與superseded filenames回歸。

## 執行範圍

- 掃描Markdown files：96
- 自動重寫link destinations：1（`lab-04-walkthrough.md` → `lab-04-study-order.md`）
- 自動重寫plain／code-span references：12
- 內容有變動的Markdown files：9（後續由人工修正 label 語意並補修其他 superseded 參照）
- Superseded document set：22份，由canonical documentation architecture管理。

## 驗證

`scripts/check_docs_links_and_references.py`會檢查所有local Markdown targets與anchors，並掃描普通文字中的舊檔名。外部HTTP links不在此checker的可用性保證範圍。

此audit只證明文件導航完整，不證明教材technical semantics或runtime。
