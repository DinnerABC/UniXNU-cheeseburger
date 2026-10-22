# UniXNU-cheeseburger — P1

OnePlus 5 / cheeseburger / MSM8998 的独立设备移植骨架。当前真实通过的是 **macOS 上的 BaseTools 与通用 AArch64 EFI package 编译**，尚未通过 cheeseburger 固件、XNU 或 iPadOS 启动验证。

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

这是本地新设备仓库，尚未创建远程 GitHub 仓库、推送或运行远程 Actions。YAML 已在本地用 actionlint 检查，不能据此宣称远程运行通过。

## 审计与后续

- [P1_BUILD_AUDIT.md](docs/P1_BUILD_AUDIT.md)：官方执行路径、输入依赖和真实编译证据。
- [MSM8998_UEFI_GAP.md](docs/MSM8998_UEFI_GAP.md)：协议差距及复用现有 UEFI 的集成路径。
- [PORTING_ROADMAP.md](docs/PORTING_ROADMAP.md)：分阶段验收与下一步最小目标。

本工程没有执行刷写、分区修改或用户数据删除，也未生成使用未确认设备参数的刷写命令。

## P2 已完成（2026-10-22）

原版 cheeseburger UEFI 与原生 boot image 已真实构建；独立集成版包含 UniXNU 应用、源编译 UniXNUDxe 和 DarwinBoot，且通过 FV 内容与镜像回读校验。产物在 `out/p2/`；没有硬件启动证明。

- [P2 构建报告](docs/P2_BUILD_REPORT.md)
- [P2 集成结果](docs/P2_UNIXNU_INTEGRATION.md)
- [下一次精确续接](docs/P2_HANDOFF.md)
- [P2 Actions](.github/workflows/p2.yml)（本地检查通过，未远程运行）

## P3 实机进展（2026-10-22）

已识别真实 OnePlus 5，利用现有 root Recovery 校验备份58个分区及6个UFS LUN的双GPT。原版临时启动已进入EFI Shell；集成版照片确认 UniXNU 0.1.0 图形应用执行。新增只读诊断驱动已编译并实机运行，确认EL1、DarwinBoot Loader安装成功及GOP/文件系统/块设备协议存在。没有内核启动项，默认返回 firmware 后进入Shell；尚未启动XNU。手机已恢复Android。

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
