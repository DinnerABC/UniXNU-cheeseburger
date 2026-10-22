# P4 实机测试记录

## 已证实的最深阶段

既有 P3：原版 UEFI Shell、集成版 UniXNU 0.1.0 图形菜单、P3/P4 只读探针真实执行。DarwinBoot Loader 协议存在不等于已经加载或跳转 XNU。

P4 新增主机端自动临时启动与返回 Android：

| run_id | 镜像/测试 | 实际结果 |
|---|---|---|
| 首次自动返回试验 | AutoReturn | 用户确认未按键自行返回；主机残留 Fastboot 导致误分类，原记录保留并有 classification-correction.json；观察器加入20秒宽限 |
| 20261021T194215Z-9f486d60 | register-probe | RETURNED_ANDROID_ADB；约186秒，主机完整状态记录；用户随后提供寄存器 OCR |
| 20261021T200317Z-8210007d | RAM probe | RETURNED_ANDROID_ADB；186.25秒；没有本次屏幕或电子日志证明 RAM 文件系统被读取 |

每轮 `out/p4/device-loop/<run_id>/` 保存 events.jsonl、summary.json/md、私有原始命令日志；总索引 JSONL/CSV。日志中 Fastboot 返回 OKAY 仅证明接收镜像，不证明 UEFI/XNU 执行。

## RAM probe 镜像

- `out/p4/ram-payload-uefi/boot-ram-probe.img`，25034752字节。
- SHA256：`f6a94dc955196b2698696f24028b874d7a1912785e34d4ff09ce6b030319a581`。
- UEFI 新模块真实完整构建 `build-rampayload-04` 返回0；前3轮类型/头文件/库依赖错误均保留。
- AArch64 P4RamPayload.efi 的哈希与 FV 对应 GUID 970879CB-72F0-4876-A365-4BA3E9BA99A0 中 PE section 匹配。
- 使用实际 P3 stock/recovery boot header 的 v0/page4096/base0/offsets(kernel0x8000,ramdisk0x1000000,tags0x100)；这不意味着运行时固定加载到这些地址。内存地址只从 bootloader FDT initrd-start/end 读取。
- 从镜像回读 FD、设备 DTB、RAM bundle 与原始输入逐字节一致。打包日志 pack-rampayload-02 返回0；首次脚本误期望构建目录已导出FD失败，已修复，未替换为假文件。
- RAM bundle 18915328字节，SHA256 `42df52df93cb44ca68deaf9c6b6faa6814b711d27308352503d8f9dfd12319d9`；包含实际 M1 kernel、ADT、UniXNU 配置。默认仅 Return to firmware，**不会自动进入 XNU**。

PEI 只在 bootloader 范围合法、完全处于真实 RAM/平台 conventional descriptor、不与固件/MMIO 冲突且 magic 匹配时保留内存。DXE 要求存在匹配的 reserved allocation HOB，再验证文件表及各文件 CRC。CRC 检测传输损坏，不能代替主机 SHA256 或代码真实性验证。

只读 EFI 文件系统没有 Block I/O/分区写操作，拒绝创建、写入和删除；仅用于限定文件路径读取。实际回调的宿主执行测试 `ramfs-callback-tests-02` 通过，覆盖路径、文件信息、分段读取、EOF、seek、写/删拒绝；不是固件运行证明。载荷坏 CRC、越界、截断和镜像哈希门禁等5项测试 `qualification-tests-01` 通过。

7点只读设备检测已执行，见 `device-status-7am` 及对应 summary。未做任何刷写、擦除、解锁或关键固件/GPT 写入。尚未跳转 XNU、进入内核初始化、挂根卷或 launchd。
