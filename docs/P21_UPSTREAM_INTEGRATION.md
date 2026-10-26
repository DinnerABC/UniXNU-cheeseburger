# P21 上游用户空间集成与差异

更新：2026-10-26。本轮固定来源，选择性核对，不整仓更新。

| 来源 | 固定提交 | 本轮用途及边界 |
| --- | --- | --- |
| UniXNU/UniXNU | 16e592abee8a550dec8154ed1f38b5ebc4f16213 | 已选官方框架、Stage7、Data工具、component/profile；不换固定XNU |
| UniXNU/UniXNU-sheng | 0c2046b84dbee545e7e3923eb74d7bb1ce020978 | 当前固定组件和BootLog报告来源 |
| xVanTuring/UniXNU-qemu | 97b75b1f63c6b7e895cd7646029db741d5fe5488 | 只读参考；具体文件保存在out/p21/userland-audit；固定提交不代表完整仓库均完成审计 |
| IssacL04/UniXNU-Pagani_SM8750 | c60da26abbe5de6439aaa9c94a72fc5da4b7512e | 只读参考；具体文件保存在out/p21/userland-audit；固定提交不代表完整仓库均完成审计 |
| dipa-studio/ios-sweet | 45263bee51df4a0f55cdf9c649af886399488ddb | 只读参考；具体文件保存在out/p21/userland-audit；固定提交不代表完整仓库均完成审计 |
| TrungNguyen1909/qemu-t8030 | fd4b0f790903044d90b8a35fcf03758401252063 | 只读参考；具体文件保存在out/p21/userland-audit；固定提交不代表完整仓库均完成审计 |

## QEMU同版本正向案例

UniXNU-qemu README的“从屏幕向前追踪”明确记录：没有definesDisplayBounds上下文时内建显示忽略更新；SpringBoard初始化图标管理器时同步等待chronod；chronod在ActivityKit中等liveactivitiesd；后者在SessionSyncEngine/Keybag.shared对MKBEventsRegister的NULL结果force-unwrap崩溃；加入sheng AppleKeyStore stand-in后链条完成，CPU绘制welcome。

本机KeyStore核心与已选官方AppleKeyStoreState逐字节相同；QEMU KeyStoreProbe差异主要为本机128项计数、零初始化及诊断导出。不能把“上游曾缺服务”当作“本机也缺”。R17 D2 liveactivitiesd原PID96仍在，未看到对应崩溃循环。

## 已有集成/当前不合入项

| 项目 | 现状 | 本轮决策 |
| --- | --- | --- |
| AppleKeyStoreState非密钥服务 | 已集成，实际状态查询返回0 | 保留，不复制重复provider |
| 官方skip-setup/no-auto-dim | R17 seed已包含，21H461、501:501/0600 | 保留，不重做Data |
| 原始Persona/APFS/pthread/Sandbox | 当前候选仍有编译/预链接证据 | 不以别的版本结构/编号替换 |
| GopFramebuffer及原始图形family | 已集成；当前零提交 | 不用GPU代码替代服务初始化问题 |
| sheng d4c299b289b3 coreduetd/大小核优化 | 针对持续CPU占用和A510迁移 | 没有本机因果证据，未合入 |
| ios-sweet HFS RENAME_SWAP兼容 | HFS特定失败案例 | 本机APFS没有相同失败证据，未合入 |
| Pagani build39实机UI | 维护者README报告CPU framebuffer、RAM Data | 参考通用机制；其build48主机产物明确尚未实机，不借用SM8750硬件 |

## 当前实际增量

本轮新增批量只读输入/接口检查工具及兼容性矩阵；静态解析共享缓存ObjC分类方法列表、服务端布局和Swift PushServer调用路径。没有形成有依据的运行修复，未编译/启动新候选，不将这些分析工具记为UI修复。
