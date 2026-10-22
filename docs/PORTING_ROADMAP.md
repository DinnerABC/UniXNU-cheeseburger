# cheeseburger 移植路线与验收门槛

## 当前交付

- 独立设备骨架、固定源提交、macOS 分阶段工作流。
- BaseTools 与通用 AArch64 UniXNU package 已在项目内真实编译通过，5 个 EFI、完整日志和哈希已保存。
- 配置解析、上游不完整设备门禁及 CI YAML 静态检查已验证。
- 原 MSM8998 UEFI 的设备支持、协议候选和 Mu 结构差异已审计。
- 没有 kernel、cheeseburger firmware、payload、bootimg 或硬件启动成功声明；没有执行刷写或修改分区。

## 最小下一目标

**在固定 Linux/GCC5 环境中离线编译原 edk2-msm8998 的 cheeseburger 固件，并保存完整证据。** 不加入 XNU，不改内存图，不制作 userdata，不连接手机。这能独立确认现有 UEFI 基线是否可重现，减少同时排查平台与 UniXNU 的变量。

同时，若要优先验证托管 CI，可将本设备仓库推送到明确的 GitHub 目标仓库后，仅运行默认 audit/package。当前没有远程仓库 owner/name 或 Actions run URL，不能报告 GitHub 已通过。发布位置由用户明确后再推送；本地工作已完成，不以发布确认阻塞文档和编译交付。

## 阶段与退出条件

| 阶段 | 必须具备的输入 | 通过条件 | 失败/阻塞条件 | 当前状态 |
| --- | --- | --- | --- | --- |
| P1-A 骨架/审计 | 锁定 UniXNU、设备身份 | defconfig 可解析；无继承的板级数据；上游按预期拒绝未完成 board | 错误 SHA、解析失败、门禁失效 | 通过 |
| P1-B 通用 EFI | Mu_Basecore + gitlinks；LLVM/LLD 23.1.3；make >=4.3 | 5 个 AArch64 EFI，所有命令 exit 0，哈希/日志齐全 | 工具漂移、编译失败、产物缺失或架构错误 | 本地通过；远程未运行 |
| P1-C 原 UEFI 基线 | 固定 Linux/GCC5、MSM8998 源码及所有依赖 | 原 cheeseburger FD 和原格式 boot image 编译完成；尺寸/解包/哈希有记录 | 源码/子模块/工具缺失或任何编译、打包错误 | 未执行 |
| P2-A EFI 集成 | 原 UEFI 基线、独立 UniXNU EFI | 离线集成可编译；随后在单独批准的只读硬件实验中看到菜单/日志 | 只编译无运行证据不能算硬件通过 | 待完成 |
| P2-B 协议验证 | 只读协议探针/日志通路 | 记录 GOP、输入、文件系统、BlockIO、FV2 等真实 handle；必要自定义 loader 协议 revision 正确 | 仅 INF 声明、仅 ACPI、无 UART 证据 | 待完成 |
| P3-A XNU 源码编译 | 完整 Xcode/SDK；合格 MSM8998 archives/flags | ARM64 对象/归档/原生链接成功，按选定版本记录 | CLT-only、缺驱动、缺 archive、链接失败 | 阻塞 |
| P3-B prelink/ADT | 独立 IPSW/哈希、真实 RAM/CPU/GIC/PSCI 和 ADT 工具 | kernel.macho 及 ADT 验证通过，未继承 sheng 地址/身份 | 不匹配 cache、未知 topology、猜测值 | 阻塞 |
| P3-C payload/bootimg | 合格内核/ADT、固件/打包适配、Trust Cache 策略 | FAT 文件可读、报告哈希齐全；镜像解包和输入一致 | 任一输入缺失、错误尺寸/重叠/双重 shim | 阻塞 |
| P4 kernel entry | 已单独审查的硬件实验、只读可观测日志 | DarwinBoot 校验/ExitBootServices/EL 路径、XNU entry 的实际日志 | 只有 EFI 启动或编译通过 | 未开始 |
| 后续存储/用户态 | exact GPT/UUID/卷策略、驱动、合法取得的 OS 输入 | 分阶段证明 root、launchd、用户态 | 不以无法启动为由猜分区、改 userdata | 不属 P1 |

## 解除各 CI 门禁需要提交什么

设备仓库当前 `example = true` 是上游真正执行的拒绝机制。只有完成对应板级审计后才能评估移除；不能单独删掉它以获得绿灯。`scripts/p1.py` 中的设备阶段暂时全部返回 BLOCKED/78，需要随实际实现及审计一起改动，不能以新建成功占位文件替代。

- kernel：提供 Xcode/SDK 精确版本、源及归档构建记录、驱动选择理由、独立 IPSW/prelink 配置与哈希。
- firmware：先提供原 EDK2/GCC5 冷构建记录，再提供最小集成补丁、vendor 清单、库类兼容性和输出尺寸检查。
- payload：提供专属 ADT 生成器、参数来源记录、真实内存和 CPU 拓扑、可接受缓存策略；若需要种子，只在 Secret 中配置，日志不输出值。
- bootimg：提供原 stock 镜像/DTB 的只读解析、参数来源、入口包装说明和 round-trip 验证；不得把 sheng 的 boot 参数替换名称后使用。
- userdata：P1 不解除；后续必须先有准确只读 GPT/LBA/UUID 及卷布局证据，再另行审查数据影响。本项目不生成刷写命令。

## 证据规则

每次构建至少保存：源 SHA/gitlink、已修改文件情况、主机/工具/SDK 版本、完整命令/cwd、实际退出码、全 stdout/stderr、文件大小和 SHA256。失败也上传。输入专有文件只记录允许公开的元数据/哈希，不把 Secret 值写日志或 artifact。

“编译通过”必须指真实编译器/链接器执行且产物验证通过；“镜像格式通过”必须有解包验证；“实机通过”必须有设备日志和测试条件。静态 YAML 检查、预期失败检查及 BLOCKED 报告各自独立，不跨级计分。

所有构建输出、下载源码和临时工作文件留在项目内。已有系统编译工具可作为外部依赖使用；不把系统 `/tmp` 当作产物存放位置。
