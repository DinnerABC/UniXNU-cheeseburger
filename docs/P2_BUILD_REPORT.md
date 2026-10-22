# P2 真实构建报告

日期：2026-10-22（Asia/Shanghai）；环境搭建开始于 10 月 21 日。本轮最低目标及最小集成目标均已完成。本报告依据实际文件和命令记录，不把静态检查或编译通过当作实机启动通过。

## 实际结果

| 阶段 | 结果 | 证据 |
| --- | --- | --- |
| 原版 cheeseburger 全量 UEFI | PASS，原源码及固定子模块未改动，SimpleInit 未跳过 | out/p2/logs/uefi-original-attempt2-ext4.log / .json，exit 0 |
| 原版 Android boot image | PASS，上游原脚本完成打包，FD/DTB/ramdisk 回读一致 | out/p2/baseline/image-verification.json |
| P2 通用 UniXNU package | PASS，5 个 AArch64 EFI；未覆盖 P1 | out/p2/unixnu-package/result.json |
| UniXNUDxe + DarwinBoot 源编译/FV 集成 | PASS，完整固件及 boot image 生成 | out/p2/logs/uefi-integration-attempt2-asm-compat.log / .json，exit 0 |
| 集成内容验证 | PASS，FV 提取出的三个 PE32 section 与应用输入/驱动编译产物 SHA256 一致 | out/p2/integrated/compiled-efi-manifest.json |
| 独立原生 boot image 打包阶段 | PASS，使用已编译 FD，解压回读 FD/DTB 一致 | out/p2/repacked/packaging.json，logs/native-bootimg-stage-final.json |
| CI YAML / shell 脚本 | actionlint、shellcheck 通过 | out/p2/logs/p2-actionlint.*、p2-shellcheck.* |
| XNU | BLOCKED，未编译 | out/p2/xnu/readiness.json，exit 78 |
| 手机启动 / GitHub 远程 Actions | 未执行 | 无硬件/远程成功声明 |

原版构建产出 50 个可验证的 AArch64 PE；集成版产出 52 个（新增源编译 UniXNUDxe、DarwinBoot）。源编译模块之外，还把本轮实际编译的 UniXNU.efi 作为预编译应用嵌入 FV。不是把整个 UniXNU 应用重新适配到旧 EDK2 源编译。

## 固定输入与环境

源仓库继续采用 `sources.lock.json` 中的 SHA，未更新到新版本：MSM8998 `ace27110b9b9b6c591387d50cf748ec646402b4a`，UniXNU `b6ae99c17194e6a18fd819d3148e69fe405e91fc`，DarwinBoot `85332b317ee80898fc78939ea64e6ca52d3837a3`。sheng 两个仓库作为既有参考保留，未拿它们的硬件配置参与编译。MSM8998 共 17 项递归 gitlink 的实际状态保存在 `out/p2/submodules.lock.txt`。

macOS 原生预检不通过：系统 Bash 3.2 不支持上游 typeset 的大小写转换选项；还有 GNU/Linux 脚本及 ELF/GNU 工具要求。因此使用项目内 Lima 2.2.1 / Apple Virtualization.framework 原生 ARM64 VM，没有引入 x86 模拟。宿主 arm64、guest aarch64；Ubuntu 22.04.5，内核 5.15.0-198；本机 GCC 和 `aarch64-linux-gnu-gcc -dumpmachine` 都为 aarch64-linux-gnu，实际 GCC 11.4.0。EDK2 的 GCC5 是工具链配置名，**不要求 GCC 版本为 5**。

Linux Python 3.10.12，IASL 20200925，NASM 2.15.05，GNU make 4.3，binutils 2.38；完整 dpkg 版本与架构见 `out/p2/logs/linux-setup.log`。macOS UniXNU package 采用 P1 已验证的 LLVM/LLD 23.1.3、GNU make 4.4.1、Python 3.12.14、固定 Mu_Basecore。macOS 无完整 Xcode / iphoneos SDK 的事实未改变。

Lima tar SHA256：`9e9eacce88f37e185c346bad73aa6136f738d8cdf8c3bb23cd42b071824bc66e`。
Ubuntu ARM64 镜像 SHA256：`e720ae083f93baae47ac9b76a096243fe1027f60998cf1127a47a74c880f64ed`。
镜像为 700955648 字节，已经按官方清单校验；下载缓存保留在 `.work/downloads/`，不需要再次下载。所有大文件与源码均直连下载；发现 Lima 会继承系统代理后，显式禁用代理继承并重启；APT 使用 Acquire::http::Proxy=DIRECT / https::Proxy=DIRECT。具体命令见 download、vm-start-direct、linux-setup 日志。

## 两个真实失败及修复

1. **大小写不敏感文件系统**：首次在 macOS virtiofs 共享源树编译，SimpleInit 的 `"uefi.h"` 被解析为 MdePkg 的 `Uefi.h`，导致 `efi_file_get_file_info` 隐式声明错误。依赖文件证据见 `out/p2/diagnostics/case-insensitive-uefi.obj.deps`。没有关闭 -Werror；将完整工作副本同步到 VM ext4 后，原始源码直接全量编译成功。构建入口已添加文件系统预检。
2. **DarwinBoot 汇编头版本差异**：第一轮集成构建的 UniXNUDxe 已编过，但旧 EDK2 没有新 Mu 的 AsmMacroLib.h。改用旧树已有的 AsmMacroIoLibV8.h，并在 DarwinBoot.inf 添加 ArmPkg.dec；ASM_FUNC 保持旧树原生实现，没有修改 handoff 指令、硬件地址或内存图。补丁在 `patches/p2/darwinboot-old-edk2-asm.patch`，仅应用于独立集成副本。第二轮通过。

失败日志全部保留，没有忽略编译退出码、伪造文件或跳过关键原模块。最终工具导出曾因自动审批额度耗尽未执行；后续已重新审批并真实完成，最终 `verify-integrated.json` 为 exit 0。

## 关键产物

| 产物 | 字节数 | SHA256 |
| --- | ---: | --- |
| baseline/MSM8998PKG_UEFI.fd | 6291456 | 94fe96cb7ba556d3d4659d5953b8a40c66a81e8da4b33cedc162c6cad3a1d682 |
| baseline/boot-cheeseburger.img | 6070272 | c52e0ab0100a8bfb46c6d66c888455147d28634b6b88ab1d93599d7b87ca6433 |
| integrated/MSM8998PKG_UEFI.fd | 6291456 | 7ef5f4e35d9f9cb27814a1a03f1a4f225258eed97a4569c29b91e2f554dc8c4d |
| integrated/boot-cheeseburger.img | 6113280 | dc599569ca8a9edd5f1d6c3a6edf37bc61e8e5b1636220b4b3c00e73a87d12e8 |

以上路径均相对 `out/p2/`。两版都保持原始 6 MiB FD、ARM64 image header、Fv offset 0x8000；boot image 是原方案的 Android header v1、2048-byte page、1-byte ramdisk（上游 echo 产生的换行），解压 kernel 后 FD 与追加的原 DTB 完全一致。原版 FVMAIN_COMPACT 约 90% 使用，集成版约 91%，没有扩大固件区域或修改地址。对应 uncompressed FV、映射、AArch64 EFI 样本、哈希清单都已导出到各自目录。

这些是编译和格式验证过的实验产物，`hardware_validated=false`、`safe_to_flash=false`，不能据此宣称可以安全刷入。没有执行手机连接、刷写、分区修改或数据删除。

## 目录、复现和 CI

全部主机源码、镜像、缓存、脚本、补丁、日志和导出产物都在 `/Volumes/NAS-MacData/Project/IOS/` 内。原始管理源码仍在 `../upstream/`；VM 所有文件存于本设备仓库 `.l/p2/disk`。因为共享盘大小写问题，编译副本在 guest ext4 的 `/home/dinnerabc.guest/unixnu-p2/original`、`integrated`；它们物理上位于项目内的 VM 磁盘，未搬到主机 /tmp 或外部目录。

P1 的 evidence 及五个 EFI 在 `out/p2/P1-preservation.json` 中逐文件留存初始哈希，最终校验见 `out/p2/final-checks.json`。P1 文档和成功产物没有覆盖或删除。

可执行入口：`scripts/p2-vm.sh`、`p2-linux-setup.sh`、`p2-linux-build.sh`、`p2-package.py`、`p2-integrate.py`、`p2-darwinboot-compat.py`、`p2-pack-bootimg.py`、`p2-verify-images.py`、`p2-export.py`。`p2run.py` 为每条调用保存完整 stdout/stderr、cwd、退出码与起止时间，并在 CI 生成摘要。

`.github/workflows/p2.yml` 新增 build-uefi、build-unixnu-package、integrate-uefi、build-bootimg、条件请求的 build-xnu-kernel / build-payload。原版与集成版走已验证 Linux 路径；package 走 macOS。bootimg 阶段只打包原生 UEFI，**不代表 XNU payload 已就绪**。缺少 XNU/ADT 输入的请求明确失败，默认不伪造这些阶段成功。Actions 上传完整日志与实际产物；仅完成本地静态检查，未运行 GitHub 远程工作流。

托管 runner 和 APT 仓库仍会更新：源 SHA、VM 镜像摘要、当前软件版本已记录，但未声称整个环境跨主机位级复现。下载 URL 漂移时必须保留摘要检查，不能静默接受新镜像。

后续精确续接方式见 `P2_HANDOFF.md`；不需要重跑 P1 或重新下载大文件。
