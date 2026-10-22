# P1 构建审计

审计日期：2026-10-21，Asia/Shanghai。范围为 OnePlus 5（cheeseburger / MSM8998）的构建工程准备，不以 iPadOS 成功运行为验收条件。

## 1. 结论与证据边界

在恢复后的项目磁盘内，从干净源码重新编译通过 Mu_Basecore BaseTools 和 UniXNU 通用 AArch64 EFI package；5 个 EFI 文件均通过 MZ、PE 签名和 AArch64 machine（0xAA64）检查。命令及实际退出码见 `evidence/current-run/basetools.json`、`package.json`、`package-result.json`。

kernel、cheeseburger UEFI firmware、payload、bootimg、userdata **未编译通过**。对应目标由设备依赖门禁返回 BLOCKED/78。上游不完整设备门禁按预期拒绝 kernel，原始退出码 1；这是门禁验证成功，不是 kernel 编译成功。没有实机启动验证。

前次使用系统临时目录的源码、日志和产物在重启后消失，不能作为本次可审计证据。当前证据全部来自恢复磁盘后、项目路径内的新构建。

## 2. 固定源码

完整清单在 `sources.lock.json`；本地源码位于项目 `../upstream/`。UniXNU 下载时分支已前进到另一个提交，本工程显式切回选定 SHA，未跟随移动的 main。

| 源码 | 固定提交 | 用途 |
| --- | --- | --- |
| UniXNU/UniXNU | `b6ae99c17194e6a18fd819d3148e69fe405e91fc` | 构建驱动、package、文档 |
| UniXNU/UniXNU-sheng | `0c2046b84dbee545e7e3923eb74d7bb1ce020978` | 只读参考设备结构 |
| UniXNU/uefi-sheng | `0eb4514db0744b8136a3897ab1af93b713c2c27b` | 只读参考 Mu-Silicium 集成 |
| edk2-porting/edk2-msm8998 | `ace27110b9b9b6c591387d50cf748ec646402b4a` | cheeseburger 已有 UEFI |
| microsoft/mu_basecore | `cfb93724a06ff86ff5ba971413368a7809ee42a5` | package 实际编译依赖 |
| UniXNU/DarwinBoot | `85332b317ee80898fc78939ea64e6ca52d3837a3` | 协议源码审计，与 UniXNU gitlink 一致 |

参考文件均按上述提交审计，不代表分支最新版本。主要依据：[官方 macOS 工作流](https://github.com/UniXNU/UniXNU/blob/b6ae99c17194e6a18fd819d3148e69fe405e91fc/.github/workflows/macos.yml)、[构建文档](https://github.com/UniXNU/UniXNU/blob/b6ae99c17194e6a18fd819d3148e69fe405e91fc/docs/building.md)、[设备仓库文档](https://github.com/UniXNU/UniXNU/blob/b6ae99c17194e6a18fd819d3148e69fe405e91fc/docs/porting/device-repository.md)、[UEFI 文档](https://github.com/UniXNU/UniXNU/blob/b6ae99c17194e6a18fd819d3148e69fe405e91fc/docs/porting/uefi-firmware.md)。本项目 `evidence/upstream-macos.yml` 保存被审计工作流。

## 3. 官方 macos.yml 的实际行为

工作流只有 workflow_dispatch，默认设备为 sheng。Runner 为 `macos-15`，Python 为 `3.12`；没有指定 Xcode 版本或选择 Xcode 的步骤。它递归检出主仓库及设备仓库，安装 `cmake ninja unifdef llvm mtools dtc`，将 Homebrew LLVM bin 放入 PATH。

实际执行顺序：

1. `./unixnu defconfig <输入的 defconfig> -D <设备仓库>`。
2. `./unixnu fetch`，获取 feeds、模块子仓库，写设备 `unixnu.lock`。
3. 按 URL 下载可选 Trust Cache 和 Data seed；可增长 seed。
4. 始终 `./unixnu build kernel`。
5. 根据 steps 选择 `./unixnu build userdata`、`./unixnu build payload`。
6. 上传 kernel、payload、logs；可选上传 userdata。

**官方 YAML 不调用 build package、build firmware 或 bootimg。** 文档 Quick start 中这些命令是单独步骤，不能将其算入官方 Actions 已做的事情。默认 `unixnu build` 在 CLI 中展开 kernel/package/firmware/payload；userdata 只在显式请求时执行，bootimg 是独立子命令。实现见 [cli.py](https://github.com/UniXNU/UniXNU/blob/b6ae99c17194e6a18fd819d3148e69fe405e91fc/scripts/unixnu/cli.py)。

官方 artifact 上传步骤未使用 `if: always()`，先前步骤失败时可能不上传日志。本工程改为失败也上传；安装、子模块、BaseTools 和 package 日志各自保存，并使用 pipefail 保留真实错误。

## 4. 工具链要求与当前主机

| 项 | 官方/源码要求 | 本次环境与处理 |
| --- | --- | --- |
| macOS runner | 官方 `macos-15` | 本地为 macOS 27.0.1 / arm64；与 GitHub runner 不同 |
| Python | CLI 最低 3.11，官方 workflow 用 3.12 | 本次 3.12.14，CI 固定 3.12.14 |
| Xcode | kernel 调用 xcodebuild，构建脚本默认 iphoneos SDK | 当前只有 CLT；xcodebuild 和 iphoneos SDK 检查失败，原始结果已记录 |
| Xcode 精确版本 | 官方未固定 | 本阶段不能推出一个官方必需版本；后续 kernel CI 必须选择并记录实际 Xcode/SDK build |
| LLVM / LLD | CLANGPDB 需 clang、lld-link、llvm-lib、llvm-rc | 本次 LLVM 23.1.3、LLD 23.1.3；CI 编译前检查相同版本 |
| GNU make | Mu 的 VFR makefile 使用 grouped targets `&:` | 要求 >=4.3；本次 4.4.1；macOS 系统 make 3.81 不满足 |
| EDK2 | feeds 固定 Mu_Basecore release/202608 的提交 | 使用上述 cfb9372 完整 SHA，不混用 msm8998 的旧 edk2 |
| 其他 | kernel: cmake/ninja/unifdef；payload: mtools；DTB: dtc；镜像: macOS 工具 | 全量版本/退出码见 environment.json；package 本身不需要 IPSW、mtools、dtc |

官方 brew 列表没有显式安装 lld、GNU make，也没把 keg-only unifdef 加入 PATH。本工程补上，LLVM 与 LLD 分属不同 bin 时，在项目 `out/toolchain` 建立完整工具链接集。没有将旧 make 并行生成结果当作有效构建。

当前 Xcode 缺失不会阻塞通用 package；它会阻塞 XNU。没有依据把 macOS 27 的本地成功等同于 macos-15 的远程成功。

## 5. 每个目标的真实路径与依赖

| 目标 | 实际实现/主要动作 | 输出 | 当前门槛 |
| --- | --- | --- | --- |
| kernel | scripts/unixnu/kernel.py → build-xnu-4k-baseline.sh；先 bootstrap 编译记录，再按 board.archives 构建归档，然后正式链接和 NativePrelink | out/cheeseburger/kernel/kernel.macho | 完整 Xcode、iphoneos SDK、Apple 引用源码、合格归档配置、原 kernelcache 与 prelink 策略 |
| package | scripts/unixnu/package.py → package/build.sh → EDK2 UniXNUPkg.dsc，AARCH64/DEBUG/CLANGPDB | package/Build/Out/AARCH64-DEBUG 下 5 个 EFI | 通用依赖已满足，本次直接调用 build.sh 完成 |
| firmware | scripts/unixnu/firmware.py；读 Mu config，初始化子模块、preflight、临时应用补丁、EDK2 build、还原补丁，校验 FD 尺寸 | board.firmware.fd 指定的外部 UEFI checkout 路径 | 要求 Mu-Silicium 目录合同，现有 MSM8998 需要适配 |
| payload | scripts/unixnu/payload.py；调用设备 ADT 工具，暂存内核、配置、缓存/seed，以 mtools 写 FAT | out/cheeseburger/payload/payload.img + payload.json | 合格内核、MSM8998 ADT、真实内存/CPU、独立缓存策略，缺一不可 |
| bootimg | scripts/unixnu/bootimg.py；编译 BootShim、拼接 FD、gzip、附 DTB、mkbootimg，再解包比对 | out/cheeseburger/boot/boot.img + boot.json | Mu 目录及地址合同、合格 FD/DTB/Android header；不能套用 msm8998 FD |
| userdata | scripts/unixnu/userdata.py；IPSW 的 OS/SystemOS 镜像只读挂载，按 userdata 窗口生成 raw image | out/cheeseburger/userdata/userdata-root.img + JSON | 精确 GPT/LBA、镜像哈希/UUID，且不属本次执行范围 |

上游 kernel 的构建名仍是 `ARM64 / BCM2837 / DEVELOPMENT`，对应 XNU 10063.141.1、Darwin 23.6、iOS 17 的交叉构建约定；这里的 BCM2837 不是已支持 MSM8998 的证据。XNU、libplatform、libdispatch、AvailabilityVersions、libpthread、corecrypto 的版本来自固定上游 `feeds.toml`；本阶段未下载或编译全部 kernel 引用源码和驱动子模块。

`kernel.prelink` 不只是编译：它保留原 Apple kernelcache 内的组件，依赖所选 IPSW。原 cache 经 IM4P/LZFSE/arm64 提取并可检查哈希。缺少这份输入或匹配策略，不能产出合格 kernel.macho。

## 6. Secrets、专有输入与真实设备参数

| 输入 | 官方用途 | cheeseburger P1 处理 |
| --- | --- | --- |
| UNIXNU_DEVICE_TOKEN | 私有设备仓库读取，可回退 github.token | 公开设备仓库不需要；保留可选参数 |
| UNIXNU_UNIQUE_CHIP_ID | payload 平台种子；sheng 将其标为 required | 不能认定适用于 OnePlus；P1 不读取、不伪造 |
| IPSW | Apple KernelCache、DeviceTree、TrustCache、OS/SystemOS | 需要独立选择身份、校验哈希及匹配；没有复制 sheng 的 iPad 身份 |
| Trust Cache bundle | 可选下载；owner bundle 跳过 board allow-list | 不继承哈希，不默认启用 owner 绕过路径 |
| Data seed | 用户提供的 data.dbr；可 grow/skip_setup | P1 不下载、不修改；不能假设从源码自动生成 |
| 额外 kernel .a | sheng config 指定 VerifiedRoot / Virtio verified bundle | 上游 archives 不生成全部这些文件，需合格卷策略及额外生成流程；新设备没有引用这些归档 |
| UEFI blobs/配置 | vendor DXE、ACPI、firmware partition/config 文件 | MSM8998 仓库已带若干二进制，存在不代表可用于当前手机；见独立清单 |
| 设备证据 | RAM/保留区、CPU regs、GIC/PSCI、显示、分区、boot header、UUID | 全部待只读取得和交叉核对；没有填猜测值或生成刷写命令 |

sheng 的 preflight `scripts/check-sheng.py` 还依赖编译后的 `build/sheng/sheng.dtb`、相对路径 `uefi-sheng/` 与该设备 vendor 文件。代码仅把 `missing_vendor_inputs` 记入 JSON，退出状态取决于 static_errors，因此不能把它返回 0 解读为固件输入全部齐备。这一脚本未复制给 cheeseburger。

## 7. 本次实际执行记录

重建后的命令全部以项目路径为 cwd/输出路径；`scripts/p1.py` 设置 TMPDIR 到设备 `out/tmp`。EFI 的完整 SHA256 和字节数在 `evidence/current-run/package-artifacts.json`，实际文件保留在 `out/package/Build/Out/AARCH64-DEBUG/`，证据副本在 `evidence/efi/`。

| 执行项 | 实际状态 | 证据 |
| --- | --- | --- |
| 项目磁盘写入/读取 | 通过 | evidence/write-check.txt |
| 上游 defconfig 解析 | exit 0 | current-run/defconfig.log + JSON |
| 不完整设备 kernel 拒绝 | 原命令 exit 1，符合预期 | current-run/incomplete-board-refusal.log + JSON |
| GNU make 4.4.1 BaseTools 冷构建 | exit 0 | current-run/basetools.log + JSON |
| UniXNU package AARCH64 DEBUG | exit 0，5 个 EFI | current-run/package.log、package-result.json |
| Actions 静态校验 | actionlint exit 0 | validation.json、actionlint.log |
| 未就绪设备阶段 | 各 exit 78 / BLOCKED | validation.json 和对应 *-result.json |
| Xcode / iphoneos SDK 可用性 | 检查失败 | current-run/environment.json |
| GitHub 远程 Actions | 未运行 | 无 run URL，不宣称已通过 |
| 手机启动/刷写 | 未执行 | 没有硬件成功声明 |

实际命令形式（从本设备仓库目录执行，路径由脚本解析为绝对项目路径）：

```sh
python3 scripts/p1.py audit --upstream ../upstream/UniXNU
bash scripts/build-package.sh --upstream ../upstream/UniXNU --edk2 ../upstream/Mu_Basecore
```

本机系统 Python 3.9 不够；实际构建通过 PYTHON 指向已安装的 3.12.14，绝对解释器位置保存在 environment.json。

## 8. 从零复现与可复现性限制

使用 Git 将 `sources.lock.json` 的 unixnu/edk2 完整 SHA 检出至设备仓库内 `.work/sources/`，初始化下列 Mu 子模块；它们各自固定到父仓库 gitlink，而不是最新分支：

```text
MdePkg/Library/BaseFdtLib/libfdt
MdePkg/Library/MipiSysTLib/mipisyst
BaseTools/Source/C/BrotliCompress/brotli
MdeModulePkg/Library/BrotliCustomDecompressLib/brotli
SecurityPkg/DeviceSecurity/SpdmLib/libspdm
```

具体下载命令和 SHA 已写在 `.github/workflows/macos.yml`；本地可用 `scripts/fetch-sources.py` 按锁文件获取，输出也在设备仓库内。运行 package 前，脚本核对源提交、源文件未改动、必要子模块匹配、LLVM/LLD 精确版本及 make 最低版本；版本漂移会失败，不静默宣称可复现。

Actions 自身固定完整提交，设备 ref 要求完整 SHA；同仓库默认用事件 SHA。SOURCE_DATE_EPOCH 固定为 UniXNU 提交时间。日志记录 host、SDK、工具和源提交；每次 CI 上传真实产物哈希。

`macos-15` 是会更新的托管镜像，Homebrew 包及其依赖也会变更；当前工具版本 guard 能检出 LLVM/LLD 漂移，但未锁定整个 runner/SDK/Homebrew bottle 环境。因此本交付是**固定源码、带环境记录与版本门禁的可重放构建流程**，不承诺不同主机或路径间字节完全相同。要进一步达到位级复现，需要固定 runner image、自托管环境/工具包、所有构建工具及 debug 路径。

## 9. P1 CI 的失败语义

- audit：配置身份、上游 SHA、配置解析或预期拒绝不符时失败；成功仅说明骨架和安全门禁正确。
- package：依赖/版本不符、任一编译命令非零、缺少任一 EFI 或 PE 架构不符时失败；完整上传日志，不使用 continue-on-error。
- 请求 kernel/firmware/payload/bootimg/userdata：在通用 package 后输出该阶段依赖并以 78 失败；没有创建假镜像或成功占位文件。
- 前置 job 失败会阻止后续 job；BLOCKED 在 GitHub 上表现为失败，不被重新编码为成功。
- 默认 package 成功也不代表设备编译全链路成功；每份结果和 job summary 都明确 hardware_validated=false。
