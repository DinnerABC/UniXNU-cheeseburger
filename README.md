# UniXNU-cheeseburger

面向 **OnePlus 5（一加 5，cheeseburger / Qualcomm MSM8998）** 的独立 UniXNU 设备移植仓库，最终系统目标为 **iPhone iOS**。

截至 **2026-10-22**，已完成原生 UEFI 与集成固件构建、实机 EFI Shell / UniXNU 菜单验证、XNU ARM64 内核编译及临时启动实验。最深实机证据是 P5 中用户观察到真实 XNU `__start` 的入口色条；**尚未证实 MMU 启用、C 初始化、根卷挂载或 iOS 启动**。P6-v1 已试启动，但该轮屏幕证据尚待补充。

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
| DarwinBoot 加载器 | P5 人工屏幕证据确认文件哈希、Mach-O 装载、ADT / BootArgs / 内存图准备与 EBS 后交接；主机解析器含损坏输入负例 | 尚未完成内核初始化；EBS 后加载器绿条与 XNU 入口标记分开判读 | [P5 启动报告](docs/P5_BOOT_REPORT.md) |
| RAM payload / 只读文件系统 | RAM bundle、文件表 / CRC / 边界检查、镜像回读及主机文件回调测试通过；P5 屏幕证据确认读取内核 / ADT | 只用于 RAM 中的启动文件，不提供持久化存储或 iOS 根卷 | [P4 测试](docs/P4_BOOT_TESTS.md)、[P5 报告](docs/P5_BOOT_REPORT.md) |
| XNU 内核 | 独立 MSM8998 ARM64 目标完成编译 / 链接；P5 入口色条获人工实机观察证据；P6 诊断内核已增量链接并打包 | 未确认 MMU、`arm_init` 或更深初始化；P6 需补屏幕证据定位 | [P4 构建](docs/P4_XNU_BUILD.md)、[P6 记录](docs/P6_EARLY_BOOT_REPORT.md) |
| ADT / BootArgs / NVRAM proxy | 基于实际 FDT 与寄存器观察建立独立 ADT；真实 DarwinBoot 解析器宿主测试通过；P5 补充 8192 字节易失 CHRP proxy | proxy 为软件易失状态；尚未证明 XNU 平台初始化正确消费全部参数 | [P4 ADT](docs/P4_ADT_BRINGUP.md)、[P5 报告](docs/P5_BOOT_REPORT.md) |
| ArmCpu / ArmDTPlatform / GIC / timer | 静态库已编译、链接，P5 补齐匹配项；定时器配置沿用实测 19.2 MHz / PPI19 | CPU 注册、中断、调度及多核均未实机验证 | [P6 平台状态](docs/P6_PLATFORM_PROGRESS.md) |
| Crypto bootstrap / POST | corecrypto 及真实 STARTUP 回调已编译；内核保留 POST，并完成构建期 HMAC sealing | 尚无运行时 POST 执行或通过证据 | [P4 构建](docs/P4_XNU_BUILD.md)、[P6 平台状态](docs/P6_PLATFORM_PROGRESS.md) |
| 早期异常与日志 | P6 加入 MMU 前检查点、无栈异常记录器；最终 ARM64 指令及页表构造的主机合成输入测试通过 | 未证明实机 MMU 开启；无真实 ESR / ELR / FAR 回传，异常覆盖不贯穿整个启动期，电子日志自动回收未实现 | [P6 MMU 诊断](docs/P6_MMU_DIAGNOSIS.md) |
| 显示 / 输入 / GPU | UEFI GOP 与输入相关协议已有探针记录，屏幕菜单 / 色条可见 | XNU 显示驱动、GPU / Metal、触控和系统输入未验证 | [P3 实测](docs/P3_UNIXNU_BOOT_TEST.md) |
| UFS / IOKit / APFS / 根卷 | UEFI Block I/O 协议已观察；设备备份与 GPT 核验已有本地记录 | XNU 根设备枚举、UFS 驱动、APFS 挂载及根卷均未验证 | [P6 根卷准备](docs/P6_IOS_ROOT_PREP.md) |
| iPhone iOS / dyld / launchd | 目标类型已明确，保留输入身份与兼容性门禁 | 尚未选择 / 下载 IPSW；ABI、Trust Cache、根 UUID 未确定，未进入用户态 | [P6 根卷准备](docs/P6_IOS_ROOT_PREP.md) |
| 设备测试自动化 | 主机命令、USB / ADB / Fastboot 状态与镜像门禁可记录；P4 有自动返回 Android 记录 | EBS 后失联无法自动恢复；P6-v1 观察超时后已停止，不能据此判定具体停机指令 | [P4 自动化](docs/P4_AUTOMATION.md)、[P6 续接](docs/P6_HANDOFF.md) |
| GitHub Actions | 已提交 P1 macOS / P2 Linux 工作流，已有一次 P1 远程运行 | P1 在 Python 安装步骤失败；P2 尚无远程运行记录 | 见下方 CI 状态 |

当前优先顺序：补充 P6 屏幕 / 异常证据 → 定位 MMU 切换与 C 入口 → 验证 CPU / VM / Platform Expert → 验证存储及根卷 → 推进 iOS 用户态。`drivers/`、`kits/`、`package/`、`firmware/` 中的早期 TODO 是 P1 骨架记录；后续阶段实现主要位于 `config/`、`patches/`、`scripts/` 和 `tools/`，以本表及对应阶段报告为准。

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

本工程没有执行刷写、分区修改或用户数据删除，也未生成使用未确认设备参数的刷写命令。

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

最终系统目标已更正为 **iPhone iOS**。P6 新诊断内核与独立 UEFI 已真实编译并临时启动一次；观察超时后控制器停止，当前尚待本轮屏幕证据，不能确认 MMU 或 C 初始化。未写入设备分区。

- [P6 精确续接](docs/P6_HANDOFF.md)
- [P6 真实执行记录](docs/P6_EARLY_BOOT_REPORT.md)
- [MMU 与诊断覆盖](docs/P6_MMU_DIAGNOSIS.md)
- [平台初始化状态](docs/P6_PLATFORM_PROGRESS.md)
- [iPhone iOS 根卷准备](docs/P6_IOS_ROOT_PREP.md)
