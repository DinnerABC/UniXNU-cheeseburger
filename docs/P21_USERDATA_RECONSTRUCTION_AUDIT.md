# iPadOS userdata 重建材料审计（2026-11-07）

目的：未来清空 userdata 后重建 iPadOS 启动环境，不以保留当前个人文件或当前 Data 状态为目标。本文是审计，不构成设备写入授权，也不是已验证的空白重装记录。

## 保留材料

- `out/p12/reference/cheeseburger-p12-reference/userdata/userdata-root.img`：9,382,658,048 字节，包含系统 OS/SystemOS 窗口；元数据同目录 `userdata-root.json`。2026-10-29 曾写入设备前缀并回读匹配；历史元数据标注 offline reference，不单独代表官方签名检查或未来安装授权。
- `out/p21/p21-lean-rebuild-20261107/uefi/boot-p21-up1.img`：2026-11-07 用户确认显示系统界面，SHA-256 `e4da20f65a5ee37816b77718983c8dbb038f80ebe17858e4fa0af16e8e715e83`。保留整个候选及其编译、门禁、预链接、用户态和固件材料。
- `out/p21/gpu-smmu-tlb/dev-seed.dbr`：2,331,088 字节，SHA-256 `1e8b97fbc0a2020c5e359e4a1acc93f9e435a45dcf79bf5f415d7fbd3db81993`；打包清单表明同一数据种子已作为 `scratch-data.img` 放入启动载荷，候选目录通过绝对软链接引用此源文件。不要删除该文件或让链接失效。
- `out/p21/p21-lean-rebuild-20261107/userland/kernel.macho.gpuaddon`、`kernel.macho.trustcache` 及 `userland/build.json`：用户态插件与信任缓存；同一启动载荷也包含二者。
- `out/p21/r14/log-window.json` 与 `scripts/logging/read-cheeseburger-log.py`：日志窗口规格和恢复读取工具。已有日志位于 `out/p21/persistent-logs/`，新日志不自动重建旧内容。
- 根目录 `AGENTS.md`、`HANDOFF.md`、`docs/P21_BUILD_AND_BOOT_GUIDE.md`、本机 `/Users/dinnerabc/unixnu-p21/atos-gate/source-tree`、NAS 其他固定上游输入以及 p2 VM overlay：构建与修改依赖。不要仅保留 boot 镜像就删除这些路径。

## 布局和限制

已核验设备为 OnePlus 5 / `3b25cc7c`，历史 userdata 容量 47,938,797,568 字节。系统根卷镜像从分区起点占 9,382,658,048 字节；日志窗口从 userdata 相对 10 GiB 起，长度 32 MiB。当前设备日志显示 10 GiB Data 种子、持久 store 绝对 4 KiB LBA 11261615、2621521 块（10,737,750,016 字节），按历史 userdata 首 LBA 2441472 算，相对偏移 36,127,305,728 字节；尾端约 1 GiB 是预留区域。上述值只对应本设备和本轮参数，未来操作前必须重新核验 GPT、单位、窗口和设备身份。

代码 `PersistOverlay.h` 在可写模式可对空白/不匹配的 store 创建新 overlay，`NativeRamBlock.cpp` 在挂载 Data 前使用 seed 和 store；当前镜像启动参数含 `sheng-data-persist=2`。旧的 1 GiB Data 初始化和跨重启有实机记录；本轮 10 GiB Data 日志为 `state=2`（恢复现有 store），用户确认进入系统。**尚未实机验证清空 userdata 后以此镜像创建全新 10 GiB Data 并进入系统**；因此现有材料支持恢复方案，但不能宣称重装已完全验证。

## 将来的重建顺序

1. 保留有用的运行日志；确认用户再次明确授权 userdata 写入、手机序列号、Recovery、分区几何、根镜像 SHA-256 和空间边界。
2. 只写已核准的 userdata 根卷前缀，写后按精确长度回读核验；不要覆写 GPT 或其他分区。确定日志窗口和 10 GiB Data store 的初始化方式与边界；不要把早期 1 GiB 布局照搬到当前镜像。
3. 用同一已验证镜像临时启动，让 Data 以 seed 建立新的持久状态；回收新 boot ID 日志，确认 Data 初始化和错误计数，确认 UI 可见，再做一次重启以验证持久性。
4. Data 恢复为空白初始状态，不保留清空前的设置；日志窗口从新运行开始产生。原有 logs 如需保留，要在 NAS 上保存现有回收目录。

旧 `data-forensics/data-reconstructed.raw`、早期 `atos-dev-job/private` 中的 10 GiB 文件是独立数据重建材料，不是整分区备份；是否删除应在完整核对用途和重建验证后再定。
