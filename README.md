# UniXNU-cheeseburger

面向 **OnePlus 5（一加 5，cheeseburger / Qualcomm MSM8998）** 的独立 UniXNU 设备移植仓库，最终系统目标为 **iPhone iOS**。

截至 **2026-10-24（P18）**，最深已人工核准的实机成果为 **P17-R4：真实 APFS Data 可写挂载，tzinit 时区软链调用返回 0**。此前已确认 MMU / C / VM / PE、只读 UFS 根卷与 owner 授权、SystemOS 挂载、Owner Boot Manifest 发布读回，以及 PID1 真实 EL0 执行。P16 已越过 P15 的 mount exit(66)；P17-R4 已越过 P16 的 timezone EROFS。

P17-R4 后续画面出现 IPC ports / zalloc 元数据检查 panic，完整归因仍有证据缺口。P18 已应用上游 PGZ 对齐校验最小修复，通过离线门禁并仅临时启动一次；**当前状态为 `TRANSPORT_ACCEPTED_SCREEN_PENDING`，尚不能宣称实机修复成功或达到更深阶段**。最新状态见 [P18 启动结果](docs/P18_BOOT_RESULT.md)、[IPC / PGZ 分析](docs/P18_IPC_ROOT_CAUSE.md)及[交接](docs/P18_HANDOFF.md)。

当前实测用户空间仍来自 **iPad7,1 / iPadOS 17.7.11 / 21H461 参考系统**。可写 Data 为 RAM scratch，不代表生产加密 Data 或 Data Protection 成功。tzinit 退出完成、launchd `LC_MAIN` 首指令直接观察、Keybag / Sandbox / FIPS 整体成功与图形界面均未独立证实，最终 iPhone iOS 目标尚未完成。

**设备状态变更：** P13 已在当轮明确授权下将参考根卷写入 userdata，原 Android 用户数据不再可用；没有有效的 userdata 回滚备份。该历史授权不扩展到后续刷写。详见 [P13 写入与回读记录](docs/P13_FLASH_RESULT.md)。本次仓库更新只添加文件、更新文档并推送，没有执行设备操作。

## 原仓库与来源

本项目基于 [UniXNU/UniXNU](https://github.com/UniXNU/UniXNU) 的构建框架和设备仓库约定，为 cheeseburger 增加设备配置、集成补丁、诊断代码、构建脚本和实验记录。感谢上游项目及各依赖的贡献者。

这是独立设备仓库；Git 历史从本地移植文件的首次导入开始。通用上游源码在构建时从固定版本获取，并未作为完整源码树提交到本仓库。主要来源如下：

| 原仓库 | 用途 | 固定提交 |
| --- | --- | --- |
| [UniXNU/UniXNU](https://github.com/UniXNU/UniXNU) | 上游构建框架、通用 UEFI package、补丁及模块入口 | [`b6ae99c17194`](https://github.com/UniXNU/UniXNU/commit/b6ae99c17194e6a18fd819d3148e69fe405e91fc) |
| [edk2-porting/edk2-msm8998](https://github.com/edk2-porting/edk2-msm8998) | OnePlus 5 原生 UEFI 平台，作为本设备固件集成基础 | [`ace27110b9b9`](https://github.com/edk2-porting/edk2-msm8998/commit/ace27110b9b9b6c591387d50cf748ec646402b4a) |
| [UniXNU/DarwinBoot](https://github.com/UniXNU/DarwinBoot) | Mach-O / ADT 加载、BootArgs 构造及内核交接 | [`85332b317ee8`](https://github.com/UniXNU/DarwinBoot/commit/85332b317ee80898fc78939ea64e6ca52d3837a3) |
| [microsoft/mu_basecore](https://github.com/microsoft/mu_basecore) | P1 通用 EFI package 的 EDK II / Mu Basecore 基础 | [`cfb93724a06f`](https://github.com/microsoft/mu_basecore/commit/cfb93724a06ff86ff5ba971413368a7809ee42a5) |
| [UniXNU/UniXNU-sheng](https://github.com/UniXNU/UniXNU-sheng) | 设备仓库布局与移植流程参考 | [`0c2046b84dbe`](https://github.com/UniXNU/UniXNU-sheng/commit/0c2046b84dbee545e7e3923eb74d7bb1ce020978) |
| [UniXNU/uefi-sheng](https://github.com/UniXNU/uefi-sheng) | UEFI 集成方式参考 | [`0eb4514db074`](https://github.com/UniXNU/uefi-sheng/commit/0eb4514db0744b8136a3897ab1af93b713c2c27b) |
| [apple-oss-distributions/xnu](https://github.com/apple-oss-distributions/xnu) | XNU 10063.141.1 / Darwin 23.6.0 内核源码 | [`d8b80295118e`](https://github.com/apple-oss-distributions/xnu/commit/d8b80295118ef25ac3a784134bcf95cd8e88109f) |

前六项完整 SHA 见 [sources.lock.json](sources.lock.json)，初始来源审计见 [source-provenance.json](evidence/source-provenance.json)，XNU 与后续组件构建来源见 [P4 构建报告](docs/P4_XNU_BUILD.md)及对应脚本。P4 使用的 corecrypto 为固定第三方快照，尚未对 Apple 原始归档认证，不能称为认证加密实现。

`UniXNU-sheng` 与 `uefi-sheng` 用作流程参考；其硬件地址、设备身份、根 UUID、Trust Cache 及驱动可用性不能直接用于一加 5。上游及依赖的许可证以各自仓库和文件声明为准，已保留的上游许可证副本见 [UPSTREAM_LICENSE](evidence/UPSTREAM_LICENSE)。

## 各模块工作情况

下表按截至上述日期的已有记录区分构建、主机测试与实机证据；编译通过不等于硬件功能可用。

| 模块 | 已完成与验证结果 | 当前限制 / 下一步 | 记录 |
| --- | --- | --- | --- |
| 板级配置与构建入口 | OnePlus 5 身份、defconfig、源码锁定、审计脚本已建立 | `board.toml` 保留 `example = true`；通用完整设备 build/bootimg 流程仍受门禁限制 | [P1 审计](docs/P1_BUILD_AUDIT.md) |
| 原生 UEFI / boot image | 原版及 UniXNU 集成版真实构建；镜像回读、FV 内容校验通过；原版实机进入 EFI Shell | 后续诊断镜像按阶段独立构建，尚无完整系统启动镜像 | [P2 集成](docs/P2_UNIXNU_INTEGRATION.md)、[P3 实测](docs/P3_UEFI_BOOT_TEST.md) |
| UniXNU UEFI 应用 / UniXNUDxe | 通用 EFI package、源编译 UniXNUDxe 已构建；实机确认 UniXNU 0.1.0 图形菜单执行 | UEFI 界面可见不代表 iOS 图形栈运行 | [P3 UniXNU](docs/P3_UNIXNU_BOOT_TEST.md) |
| DarwinBoot / RAM payload | 内核与 ADT 校验、Mach-O 装载、BootArgs、EBS 交接已有实机证据；P13–P15 使用经过固定解码器与回读测试的压缩载荷 | 不代表完整系统可用；当前候选仅用于受控启动实验 | [P5](docs/P5_BOOT_REPORT.md)、[P13](docs/P13_ROOT_BOOT_RESULT.md)、[P15](docs/P15_BOOT_RESULT.md) |
| XNU / MMU / C / VM / PE | P7 人工观察确认 MMU、高地址、栈、`arm_init`、CPU topology、VM 与 PE 路径；后续已进入 BSD 及用户空间 | 多核、完整系统稳定性与服务初始化尚未验证 | [P7](docs/P7_REAL_BOOT_REPORT.md)、[P15](docs/P15_BOOT_RESULT.md) |
| ADT / NVRAM proxy | 基于实采 FDT 与寄存器建立设备 ADT；后续修正 DRAM 描述并参与真实启动；易失 CHRP proxy 保留 | 不冒称 Apple NVRAM；P16 已真实发布并读回 Owner Boot Manifest，mount PID3 退出0，越过 P15 阻塞；该摘要使用明确 owner 域，不是 Apple 固件身份 | [P7 根因](docs/P7_MMU_ROOT_CAUSE.md)、[P16](docs/P16_BOOT_RESULT.md) |
| ArmCpu / ArmDTPlatform / timer | 静态链接与匹配项已集成，采用实测 19.2 MHz / PPI19；后续启动已推进至用户空间 | 不据此推定全部中断、多核、调度或电源管理合格 | [P8 实现](docs/P8_IMPLEMENTATION.md)、[P15](docs/P15_BOOT_RESULT.md) |
| Crypto / AMFI / PMGR 兼容 | 集成真实 crypto 启动回调、安全组件及 PMGR 兼容接口；P11 观察到 AMFI READ/OR/WRITE 调用并越过 No PMGR 阻塞 | PMGR 接口不代表 Qualcomm 电源控制或硬件安全锁存器实现；不声称取得认证加密资质 | [P10](docs/P10_SECURITY_INTEGRATION.md)、[P11](docs/P11_BOOT_RESULT.md) |
| UFS / IOKit / 根窗口 | P11 发布 6 个只读 LUN，LUN0 四次 READ10 成功；P12 集成真实 RootLayout、NativeUfsBlock 与 VerifiedRoot，后续根设备身份已有实机证据 | 初步成功不代表全部 LUN、完整磁盘读取与长期稳定性验证通过 | [P11](docs/P11_BOOT_RESULT.md)、[P12](docs/P12_ROOT_INTEGRATION.md) |
| APFS / 根卷 / owner 授权 | P13 部署参考 OS/SystemOS 根镜像并回读校验；P14/P15 只读根卷的设备号、UUID、签名策略与 owner 接受链有屏幕证据；P15 SystemOS 挂载 / 授权成功 | Apple 原始 EAUTH=80 保留，owner 接受不等于 Apple 原厂根封印认证成功 | [P13](docs/P13_FLASH_RESULT.md)、[P14](docs/P14_ROOT_AUTH.md)、[P15](docs/P15_BOOT_RESULT.md) |
| 原始压缩文件 / 签名缓存 | P15 原始类型 3/4 处理器真实注册，越过 exec ENOTSUP45；实际块与页面校验、ready blobs 有屏幕计数 | 未独立记录全部 Sandbox handler / shared_region 返回值 | [P15 根因](docs/P15_EXEC_ROOT_CAUSE.md)、[实机结果](docs/P15_BOOT_RESULT.md) |
| dyld / PID1 / mount / tzinit | P16 mount PID3 退出0；P17-R4 PID1 有3879次EL0调用，tzinit 时区软链 errno0 | tzinit 退出完成与 LC_MAIN 首指令直接证据仍缺；完整服务初始化未完成 | [P16](docs/P16_BOOT_RESULT.md)、[P17](docs/P17_BOOT_RESULT.md) |
| 可写 Data / RAM scratch | P17-R4 Data stage4/status0/readonly0，RAM reads89/writes5/failures0；真实APFS挂载成功 | 不代表持久化加密Data、Data Protection或生产系统可用；P18尚待新屏幕证据 | [P17 实现](docs/P17_DATA_IMPLEMENTATION.md)、[实机结果](docs/P17_BOOT_RESULT.md) |
| IPC / PGZ 校验 | P18 应用上游两行最小修复，完成实际机器码核验和10项提取源码host测试；临时启动一次 | 手机对象属于PGZ alias仍缺直接记录；未运行完整HVF回归，实机修复结果待确认 | [P18 分析](docs/P18_IPC_ROOT_CAUSE.md)、[结果](docs/P18_BOOT_RESULT.md) |
| 显示 / 输入 / GPU | UEFI GOP、菜单与内核诊断屏幕已有证据 | iOS 图形界面、GPU / Metal、触控及完整系统输入未验证 | [P3](docs/P3_UNIXNU_BOOT_TEST.md)、[P15 续接](docs/P15_HANDOFF.md) |
| iPhone iOS 目标 | 当前参考系统推进了启动链验证 | 实测仍为 iPad7,1 / 21H461；未完成最终 iPhone 用户空间集成；已集成实验用RAM Data，Keybag / Sandbox / FIPS无整体成功结论 | [P17](docs/P17_BOOT_RESULT.md)、[P18](docs/P18_HANDOFF.md) |
| 诊断 / 自动化 | 主机命令与镜像门禁、屏幕首 fatal、符号核对及 EL0 观察逐阶段建立 | 屏幕证据与主机传输状态分开记录；未实现通用电子日志回收或失联自动恢复，不自动重试实机镜像 | [P9](docs/P9_ROOT_CAUSE.md)、[P15](docs/P15_HANDOFF.md) |
| GitHub Actions | P1 / P2 均仅保留手动入口 | push / PR 不自动运行；仅在明确要求时手动触发 | 见下方 CI 状态 |

当前优先顺序：取得 P18 屏幕与执行证据 → 核对 P17 已确认的 owner / SystemOS / Manifest / 可写 Data / tzinit 节点是否保持 → 判断 IPC / PGZ 旧 panic 是否消失或记录新的首 fatal → 按真实阻塞继续定位。不能用 Fastboot 接受镜像代替执行成功，不重发已测镜像，也不绕过 PGZ 或其他安全检查。

`drivers/`、`kits/`、`package/`、`firmware/` 中部分早期 TODO 属于 P1 历史记录。公开阶段实现主要位于 `config/p7`–`p14`、`drivers/`、`patches/`、`scripts/` 和 `tools/`。P15–P18 的源码副本、脚本及完整证据 / 产物目前位于 Git 忽略的 `out/p15/`–`out/p18/`，本次提交其阶段报告，尚不是 P18 完整公开复现包。独立上游 Git 检出及设备备份不随本次提交。

## 文件位置

本次所有源码和产物都位于项目 `/Volumes/NAS-MacData/Project/IOS` 内：

- 本设备工程：`UniXNU-cheeseburger/`
- 固定版本源码与 BaseTools 中间产物：`../upstream/`
- EFI：`out/package/Build/Out/AARCH64-DEBUG/`
- 完整构建日志、命令、退出码与哈希：`out/`
- 可随设备仓库保留的本次证据快照：`evidence/`

`out/` 被 Git 忽略，但保留在项目磁盘。后续脚本使用项目内 `out/tmp`，不把编译产物放到系统临时目录。此前临时目录的内容在重启后消失；本次交付基于项目内的重新编译。

## 状态与门禁

`board.toml` 只填写已知设备身份，并保留 `example = true`，使用上游现有机制拒绝不完整的设备 build/bootimg。`cheeseburger_defconfig` 可以解析，但不启用未验证驱动。地址、GPT、IPSW 身份、Trust Cache 哈希及 UUID 均未借用 sheng。

通用 EFI package 不需要设备地址或 IPSW，因此直接调用锁定上游的 `package/build.sh`。这不是移除设备门禁，也不是 MSM8998 硬件支持证明。

## 本地复现

需要 Python 3.11+（本次及 CI 为 3.12.14）、LLVM/LLD 23.1.3、GNU make 4.3+。macOS Command Line Tools 足够编译本阶段 package；XNU 另外需要完整 Xcode 和 iPhoneOS SDK。

在本设备仓库目录中，使用本项目已下载且固定的源码：

```sh
python3 scripts/p1.py audit --upstream ../upstream/UniXNU
bash scripts/build-package.sh --upstream ../upstream/UniXNU --edk2 ../upstream/Mu_Basecore
```

若系统 `python3` 过旧，通过 `PYTHON=/absolute/path/to/python3.12` 指定构建解释器。脚本自动将已安装 Homebrew LLVM、LLD 和 GNU make 放到 PATH 前面，不自动修改 shell 配置或安装软件。

重新获取源码的固定提交、EDK2 子模块及工具要求见 [构建审计](docs/P1_BUILD_AUDIT.md)。不能用移动分支替代 `sources.lock.json` 中的完整 SHA。

## GitHub Actions

[macos.yml](.github/workflows/macos.yml) 使用独立 `device_repository` 参数和完整设备提交 SHA，固定上游及 Actions 提交，默认运行 audit → package。选择 kernel/firmware/payload/bootimg/userdata 会输出对应依赖清单并以 78 退出，工作流显示失败。

公开仓库不需要自定义 Secret；私有设备仓库可用 `UNIXNU_DEVICE_TOKEN`。本阶段没有读取平台种子或固件下载 Secret。

设备仓库已发布至 [DinnerABC/UniXNU-cheeseburger](https://github.com/DinnerABC/UniXNU-cheeseburger)。截至 2026-10-22 核对，P1 已有一次[远程运行](https://github.com/DinnerABC/UniXNU-cheeseburger/actions/runs/39721491702)，对应初始提交 `6b8ff92`，结果为失败：`actions/setup-python` 无法为 macOS 15.7.9 / arm64 提供指定的 Python 3.12.14，审计及 package 构建未执行，随后因没有 `device/out/` 而上传产物失败。P2 尚无远程运行记录。

工作流曾通过本地 actionlint 检查；当前没有远程构建通过结论。P1 与 P2 均仅保留 `workflow_dispatch` 手动入口，push 和 pull request 不自动运行 Actions；需要运行时再明确手动触发。

## 审计与后续

- [P1_BUILD_AUDIT.md](docs/P1_BUILD_AUDIT.md)：官方执行路径、输入依赖和真实编译证据。
- [MSM8998_UEFI_GAP.md](docs/MSM8998_UEFI_GAP.md)：协议差距及复用现有 UEFI 的集成路径。
- [PORTING_ROADMAP.md](docs/PORTING_ROADMAP.md)：分阶段验收与下一步最小目标。

P1–P12 未执行设备分区写入；P13 在当轮明确授权后写入 userdata 并完成已部署镜像范围的回读校验。原 Android 用户数据已不可用，且没有有效 userdata 回滚副本；boot、GPT 等其他分区未写入。P14–P18 采用临时启动。阶段授权与执行范围见对应报告，不将历史授权当作后续设备操作许可。

## P2 已完成（2026-10-22）

原版 cheeseburger UEFI 与原生 boot image 已真实构建；独立集成版包含 UniXNU 应用、源编译 UniXNUDxe 和 DarwinBoot，且通过 FV 内容与镜像回读校验。产物在 `out/p2/`；P2 阶段本身没有硬件启动证明，后续 P3 已补充 UEFI 与菜单实机验证。

- [P2 构建报告](docs/P2_BUILD_REPORT.md)
- [P2 集成结果](docs/P2_UNIXNU_INTEGRATION.md)
- [下一次精确续接](docs/P2_HANDOFF.md)
- [P2 Actions](.github/workflows/p2.yml)（本地检查通过，未远程运行）

## P3 实机进展（2026-10-22）

已识别真实 OnePlus 5，利用现有 root Recovery 校验备份58个分区及6个UFS LUN的双GPT。原版临时启动已进入EFI Shell；集成版照片确认 UniXNU 0.1.0 图形应用执行。新增只读诊断驱动已编译并实机运行，确认EL1、DarwinBoot Loader安装成功及GOP/文件系统/块设备协议存在。P3 镜像没有内核启动项，默认返回 firmware 后进入 Shell；该阶段尚未启动 XNU，阶段结束时已恢复 Android。后续内核入口进展见上方模块表及 P5/P6 报告。

- [设备清单](docs/P3_DEVICE_INVENTORY.md)
- [私有备份清单](docs/P3_BACKUP_MANIFEST.md)
- [原版 UEFI 实测](docs/P3_UEFI_BOOT_TEST.md)
- [UniXNU 实测](docs/P3_UNIXNU_BOOT_TEST.md)
- [XNU 状态](docs/P3_XNU_BUILD_STATUS.md)
- [P3 续接检查点](docs/P3_HANDOFF.md)

备份仅在项目 backups/p3，不提交或上传；P3 不使用 Actions。未执行刷写、自动解锁或数据清除。

## P6 本地早期启动定位

最终系统目标已更正为 **iPhone iOS**。P6 新诊断内核与独立 UEFI 已真实编译并临时启动一次；观察超时后控制器停止；随后照片确认已执行到 S05（BootArgs 检查、页表构造完成，MMU 写入前），尚不能确认 MMU 或 C 初始化。未写入设备分区。

- [P6 精确续接](docs/P6_HANDOFF.md)
- [P6 真实执行记录](docs/P6_EARLY_BOOT_REPORT.md)
- [MMU 与诊断覆盖](docs/P6_MMU_DIAGNOSIS.md)
- [平台初始化状态](docs/P6_PLATFORM_PROGRESS.md)
- [iPhone iOS 根卷准备](docs/P6_IOS_ROOT_PREP.md)

## P7 实机突破与收尾（2026-10-22）

三轮仅临时启动，用户逐轮确认诊断标记：MMU开启、高地址代码、栈、arm_init、CPU topology、cpu_bootstrap、VM初始化返回和PE(FALSE/TRUE)返回均已验证。证据为人工观察，尚未证明IOKit、UFS根设备或iOS启动。手机已恢复Android，VM停止；按用户要求结束本轮。

- [P7 实机结果](docs/P7_REAL_BOOT_REPORT.md)
- [MMU与具体缺项分析](docs/P7_MMU_ROOT_CAUSE.md)
- [下一轮精确续接](docs/P7_HANDOFF.md)
- [iPhone iOS就绪状态](docs/P7_IOS_READINESS.md)

最终产物在out/p7，三份成功版本完整保存在out/p7/milestones。下一轮集中kernel bootstrap、IOKit和UFS，不重复MMU定位。


## P8–P18 阶段索引（2026-10-22 至 2026-10-24）

以下为历史推进顺序，早期阶段的限制不代表当前状态；各轮失败、证据边界及本地产物位置保留在原报告。

| 阶段 | 工作与结果 | 文档 |
| --- | --- | --- |
| P8 | IOKit / 存储组件与平台集成，出现设备树处理日志及重启阻塞 | [实现](docs/P8_IMPLEMENTATION.md)、[结果](docs/P8_BOOT_RESULT.md)、[续接](docs/P8_HANDOFF.md) |
| P9 | 首 fatal 与日志诊断，定位后续安全初始化问题 | [根因](docs/P9_ROOT_CAUSE.md)、[结果](docs/P9_BOOT_RESULT.md)、[续接](docs/P9_HANDOFF.md) |
| P10 | 原始安全组件与 PMGR 依赖集成 / 审计 | [安全集成](docs/P10_SECURITY_INTEGRATION.md)、[结果](docs/P10_BOOT_RESULT.md)、[续接](docs/P10_HANDOFF.md) |
| P11 | 越过 No PMGR，6 个只读 UFS LUN 与初步 READ10 成功，尚停于根设备匹配 | [实现](docs/P11_IMPLEMENTATION.md)、[结果](docs/P11_BOOT_RESULT.md)、[续接](docs/P11_HANDOFF.md) |
| P12 | 离线参考根卷、签名策略、UFS 窗口与 APFS 组件集成 | [根卷](docs/P12_ROOT_INTEGRATION.md)、[就绪状态](docs/P12_INSTALL_READINESS.md)、[续接](docs/P12_HANDOFF.md) |
| P13 | 经授权写 userdata，回读一致；压缩临时启动推进到根认证阻塞 | [写入](docs/P13_FLASH_RESULT.md)、[根启动](docs/P13_ROOT_BOOT_RESULT.md)、[续接](docs/P13_HANDOFF.md) |
| P14 | owner 根授权实机通过，原始 Apple 错误保留；首进程 exec ENOTSUP45 | [根授权](docs/P14_ROOT_AUTH.md)、[结果](docs/P14_BOOT_RESULT.md)、[续接](docs/P14_HANDOFF.md) |
| P15 | 原始类型3/4处理器注册、SystemOS 挂载、PID1 EL0 执行；新阻塞为 mount exit(66) | [exec 根因](docs/P15_EXEC_ROOT_CAUSE.md)、[结果](docs/P15_BOOT_RESULT.md)、[续接](docs/P15_HANDOFF.md) |
| P16 | Owner Boot Manifest 实机发布读回，mount退出0；新阻塞为tzinit在只读路径symlink返回EROFS | [用户空间](docs/P16_USERLAND_BOOT.md)、[结果](docs/P16_BOOT_RESULT.md)、[续接](docs/P16_HANDOFF.md) |
| P17 | R4真实可写APFS Data、RAM写入与tzinit symlink返回0；后续IPC ports / zalloc检查panic待定位 | [Data实现](docs/P17_DATA_IMPLEMENTATION.md)、[结果](docs/P17_BOOT_RESULT.md)、[续接](docs/P17_HANDOFF.md) |
| P18 | 上游PGZ最小修复、离线门禁通过；一次临时启动已被Fastboot接受，实机屏幕证据待补 | [IPC根因](docs/P18_IPC_ROOT_CAUSE.md)、[结果](docs/P18_BOOT_RESULT.md)、[续接](docs/P18_HANDOFF.md) |
