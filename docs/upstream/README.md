# 上游文档与参考源码（本地副本）

来源与抓取时间：

- `zh/porting/*.md` — GitHub `UniXNU/UniXNU` 仓库 `main` 分支的 `docs/zh/porting/`，2026-10-31 抓取，对应官网 <https://unixnu.com/zh/porting/>。
- `gop-framebuffer/` — `UniXNU/GopFramebuffer` 提交 `cf3b42979a9c44219e7bb149543e6fc3388b58ed`（官方“显示与亮度”页固定的版本）。
- `sheng/` — `UniXNU-sheng` 提交 `516a780473a2ac59c8b438e132bb60204dda9e22` 的 `package/DisplayTimingProbe.cpp`、`package/LegacyDisplayProbe.cpp`、`scripts/build-gop-legacy-display.py`。

用途：对照官方适配顺序与已核对源码，避免重复实现上游已有的机制。真正改代码前仍以
仓库内固定的框架版本（`out/p21/official-adaptation/source-inventory.json`）为准。
