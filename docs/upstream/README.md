# 上游文档与参考源码（本地副本）

来源与抓取时间：

- `zh/porting/*.md` — GitHub `UniXNU/UniXNU` 仓库 `main` 分支的 `docs/zh/porting/`，2026-10-31 抓取，对应官网 <https://unixnu.com/zh/porting/>。
- `gop-framebuffer/` — `UniXNU/GopFramebuffer` 提交 `cf3b42979a9c44219e7bb149543e6fc3388b58ed`（官方“显示与亮度”页固定的版本）。
- `sheng/` — `UniXNU-sheng` 提交 `516a780473a2ac59c8b438e132bb60204dda9e22` 的 `package/DisplayTimingProbe.cpp`、`package/LegacyDisplayProbe.cpp`、`scripts/build-gop-legacy-display.py`。
- `zh/guide/mobilegestalt.md` — 同一官方仓库 `docs/zh/guide/`，2026-10-31 抓取。
- `reference/sheng-ufs/` — `UniXNU/ShengUfs` 提交 `e2b32cc429e8cf1501161c1d8c517f8994ceb9b2` 的 `NativeUfs.cpp`、`RootLayout.h`、`RootPublication.h`、`RootCommands.h`、`RootWindowRead.h`；用于对照持久 Data 窗口（`native_ufs_store_*`）的官方策略。
- `reference/sagit/` — `MC-XiaoXiao/ios_sagit` 提交 `898998cf1b74ea103e0c16d6dc0016822d8d644b` 的 QcomGPU/GPUCC/SMMU/RPM 参考文件（`AdrenoStartup.h`、`AdrenoFirmware.h`、`AdrenoSubmit.h`、`AdrenoBackend.h`、`QcomGPUHardware.cpp`、`QcomGPUPlatform.*`、`ClockTables.h`、`ContextTLB.h`、`RPMTransport.h`、`RPMRegulators.h`）；同 MSM8998/Adreno 540，用于 A540 后端与电源/固件输入。
- 这些目录只保存源码/文档文本，不含任何厂商固件、签名材料或设备镜像。

用途：对照官方适配顺序与已核对源码，避免重复实现上游已有的机制。真正改代码前仍以
仓库内固定的框架版本（`out/p21/official-adaptation/source-inventory.json`）为准。
