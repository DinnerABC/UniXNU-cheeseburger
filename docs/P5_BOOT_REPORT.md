# P5 首次内核交接实验

## 当前状态

平台匹配与易失 CHRP NVRAM proxy 已修复。首次临时启动中，用户提供的真实屏幕 OCR 确认内核/ADT 的 SHA256 校验、Mach-O解析、BootArgs和内存映射准备全部完成；随后用户观察到紫色细条和宽绿条，与真实 XNU `__start` 品红标记及 EBS 后加载器绿条一致。结合已保存的入口反汇编，已获得首次 XNU 入口执行证据，证据级别为人工屏幕观察，尚非电子PC/寄存器跟踪。

设备随后保持该页面，观察窗口内没有恢复ADB/Fastboot；状态机已停止同镜像重试。尚未证实后续CPU/VM/platform初始化、POST运行自检、根卷或launchd。

没有执行 flash、erase、GPT、Bootloader 或校准分区写入。P1–P4 产物保留。

## 已完成的实际开发

1. `scripts/p5-prepare-platform.py` 从实际 ArmCpu / ArmDTPlatform Info.plist 提取 IOClass、IOProviderClass、IONameMatch、IOProbeScore，添加到 KernelConfigTables。bundle ID 分别为 org.blu-sharky.ArmCpu 和 org.blu-sharky.ArmDTPlatform。它们作为静态链接类使用上游 built-in personality 结构，不伪造独立已加载 kext 记录。具体结构与来源哈希：`out/p5/platform-matching.json`。
2. `scripts/p5-prepare-entry.py` 仅为 MSM8998 目标在真实 XNU `__start` 中添加品红色顶条。地址来自已验证 BootArgs Video 字段，有32bpp、非零基址、行宽/高度边界检查；保留 x0，不使用猜测的硬件地址。`out/p5/kernel/entry-disassembly.log` 证明该指令确实位于 LC_UNIXTHREAD 指向的入口，不在独立替代载荷中。
3. 复用既有 ArmCpu（PPI19）、ArmDTPlatform、crypto 档案和 POST bootstrap，仅重编 KernelConfigTables 和启动汇编并重链接。两次增量内核构建日志在 `out/p5/logs/link-kernel-*`；保留完整 POST，正常填充构建期 HMAC，尚未证明运行自检完成。
4. `scripts/p5-make-adt.py` 复用锁定 UniXNU 的 empty_nvram_proxy 通用函数，创建8192字节空 CHRP bank，验证头校验、分区边界、Adler32、CPU拓扑与PPI19。明确为易失软件状态，没有持久化控制器，不冒称 Apple 或手机实际 NVRAM。没有添加猜测的 Apple chip-id、Root UUID、Trust Cache。
5. P5Probe 从真实 EFI SimpleFS 打开内核和 ADT，验证文件大小及 SHA256；DarwinBoot 自己再次验证内核与 ADT 的预期大小/哈希，不匹配则拒绝继续。预期哈希由已验证产物生成进 `config/p5/Expected.h`。
6. DarwinBoot 加入 Stage1/2/3 屏幕信息，并在 EBS 前停留20秒；纠正上游“绿条意味着内核入口执行”的文字，绿条仅表示加载器在 EBS 后执行，品红条才来自真实 XNU 入口。

## 产物与宿主证据

- `out/p5/kernel/kernel.macho`：19261032字节，SHA256 `a849889308cb67fafe26f1002be4d5957c6b2dd5dc1f2810eb80611a9b3f53b7`，ARM64 MH_EXECUTE，入口 `0xfffffff0073fc488`，未解析符号0。
- `out/p5/adt/kernel.macho.adt`：13616字节，SHA256 `cec5013d014ea9a5c659a388691ab6446e889a43a4e0111e24a5f0e0075f0369`。
- `out/p5/adt-parser/validation.json`：实际锁定 DarwinBoot AdtNode C 函数宿主解析通过。
- `out/p5/loader-parser/validation.json`：实际 ParseKernel 函数接受10个非空 segment、跨度0x1334000；负例损坏 Mach-O 被拒绝。首次宿主 harness 缺 SIZE_256MB 定义失败，修复后 check-loader-02 返回0，未修改加载器接受条件。
- `out/p5/uefi/boot-p5-xnu.img`：完整 SHA256、大小、FD/DTB/载荷回读结果及各文件哈希见 packaging.json。
- 固件中 P5Probe、RamPayload、DarwinBoot 的 AArch64 PE 哈希与 FV section 一致。build-uefi-01 缺 RngLib 映射失败；补齐明确返回不支持的 Null RNG 依赖后 build-uefi-02 实际成功。此路径仅调用不需要熵的 SHA256；没有替换 XNU 的 entropy 或跳过 crypto 自检。

## 隔离与资源

唯一 XNU 工作树保持不变，内核编译期间 VM 停止，MAKEJOBS=-j2。UEFI 在既有 VM 中采用 overlayfs：P3/P4 diagnostic 为只读下层，P5 upper/work/source/build 位于 VM 项目磁盘内，没有重复克隆源码，没有修改 P3 原诊断树。构建产物导出到 `out/p5/`。P5 源码准备、增量构建及打包脚本位于 scripts/p5-*，补丁位于 patches/p5。

自动测试复用既有身份/镜像/备份门禁，但日志独立保存在 `out/p5/device-loop/`。每镜像只允许一次自动测试；失联或不能恢复时返回非零并停止，不盲目重试。

## 实机结果与当前现场

- run_id：`20261022T011901Z-228b8fa4`，唯一一次临时启动。
- 镜像：25411584字节，SHA256 `06af9d6c08e8e7308d81a6a07095553aa6692e816919faa5ac24090e7c9f51b7`。
- 传输结果：Fastboot接受镜像，随后240秒观察内无ADB/Fastboot；`temporary-boot-01.json`实际退出2，分类 `NO_TRANSPORT_MANUAL_RECOVERY`。没有将命令接受误判为XNU成功。
- 人工运行证据：`screen-ocr-stage3.txt`保留用户OCR原文；`screen-colors-user.txt`保留颜色观察。地址/分辨率误识别没有猜改进配置。
- 确认Stage0–5：真实UEFI、RAM文件读取和SHA256、Mach-O装载、ADT/BootArgs/内存图、EBS后加载器路径及真实XNU入口标记。不能把未解释的黄色细条认定为新的初始化阶段。
- 仍未知Stage6–8：入口之后最早CPU/页表路径、C初始化和真实日志。没有PC/ESR/FAR/ELR回收，不能判定具体fault或panic原因。
- 主机状态/命令日志自动回收已完成；固件/XNU电子日志自动回收未实现。当前采用用户OCR和颜色观察。
- 已到达本轮“无法自动恢复设备状态”的停止条件。下一轮设备测试前需要一次硬件恢复；应先保留当前屏幕，不能自动再启动相同失联镜像。
