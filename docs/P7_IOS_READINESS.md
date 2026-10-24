# P7 iPhone iOS 就绪状态

最终目标为 iPhone iOS。实机已确认 MMU→arm_init→早期VM返回→PE(TRUE)返回；P7按用户要求收尾，下一轮转kernel bootstrap、IOKit和UFS，没有将离线镜像准备当成启动成果。

- 当前内核 XNU10063.141.1 / Darwin23.6，普通 ARM64；尚未选择已核实匹配的 iPhone ProductType / IPSW build。
- 没有混入 sheng 的 iPad 身份、DeviceTree、Trust Cache、预链接偏移或 kernelcache。
- 没有下载新的大型IPSW或制作声称可启动的根卷；节省下载流量，等待早期初始化和存储路径实证。
- PE_init_platform的前后两阶段均已实机确认返回，但IOKit平台驱动匹配、XNU UFS驱动、根块设备、APFS挂载仍未验证。UEFI的Block I/O存在不能代替XNU设备识别。
- 后续必须实际核对 iPhone BuildManifest、Darwin/用户态ABI、arm64/arm64e、KernelCache、DeviceTree、Trust Cache、OS/SystemOS组件和系统卷结构。未选择输入时保持未配置，不能编造UUID或哈希。
- 目前没有 root mount、BSD、launchd 或持久化安装技术条件。只读备份与恢复记录继续保留，不执行 flash、erase 或 GPT 修改。

P6 的 ABI/来源分析仍适用，见 `P6_IOS_ROOT_PREP.md`。MMU/C/早期VM及PE调用已有人工观察证据；下一轮以IOKit和根设备的实际结果为门禁，再验证存储和系统输入。没有持久化刷写就绪结论。
