# P21 按官方流程适配：执行记录

官方源码已完整拉取到 `.work/p21-official-framework`，固定提交
`16e592abee8a550dec8154ed1f38b5ebc4f16213`。所需 ArmCpu、ArmDTPlatform、
SoftwareAES、DarwinBoot、GopFramebuffer、AppleKeyStoreState、VerifiedRoot、
XnuPrelink 子模块已按父仓库固定版本初始化。禁止把未初始化的其他硬件组件
误记为已审计或已适配。

## 当前完成的顺序

1. 保存根配置、defconfig、PMGR 源码与 R7 hash、工作区状态；未修改历史镜像。
2. 根配置改为官方 schema_version=2，解析 ARM64→本地 MSM8998→匹配 iPadOS→设备
   四层。官方 defconfig/status 已成功执行；记录在
   `out/p21/official-adaptation/{defconfig.log,status.json}`。
   **仍保留 example=true**：声明性配置解析成功不代表所有构建/固件/硬件绑定已完成。
3. 新增 `soc/msm8998` 本地支持输入与板卡 DTS，所有地址/拓扑来自捕获 FDT 和
   经验证 R7 ADT。DTC 编译通过。它是迁移库存，不是已经可替换运行 ADT 的声明。
4. 按官方 format-1 兼容说明，在转换器完整适配前，复用官方 typed_properties/node
   实现应用 OS profile，并对 CPU、GIC/timer、UFS、RAM/保留区、root/memory-map
   做严格语义不变检查。新增 UUID seed 是本项目稳定私有软件身份，不冒充设备 ECID；
   chip-id 输入取捕获 qcom,msm-id 第一 cell 0x124，不取其他板卡值。
5. PMGR 板级匹配改为显式 unixnu,board-id；旧 ADT 仅在新标记缺席时回退旧型号。
   新标记存在但错误时拒绝。保留已验证生命周期与 340 项 ABI/原始绑定检查，
   新 archive 编译通过。尚未将它声明为实机验证。
6. **直接运行官方** seed-data-skip-setup.py --no-auto-dim，保留基础种子。
   管理员授权后成功。随后所有权开启挂载发现官方新偏好实际 UID=0、GID=501、0600，
   已在输出副本中修正为501:501/0600；只读重新挂载确认五份 plist、版本、目录、
   UUID，fsck_apfs -n 退出0。最终 Data 在
   `out/p21/official-adaptation/data-seed/owner-data-1g.dbr`，
   encoded SHA256 `f1a23b6ba8e1a5ed13e1b3e36c3666e5eb76c3a2d54ac1e1803ff2b1e52a06c4`。
   原 seed 未变；没有设备分区操作。
7. 官方 arch/OS 共56条 patch 已逐条正反向检查固定历史 XNU 源树：15已存在、32可正向应用、
   9需上下文/重叠审计。**这不是R7缺失32项修复的结论**，后续阶段有替换对象/源码。
   不修改旧源树，不把上下文检查当语义适用性检查。

8. 官方 fetch 已完整成功，生成 unixnu.lock；新 tree 已组装。曾遇到 GitHub TLS
   中断，使用已有缓存并切换直连完成，不改变固定提交。
9. 全新官方 source preparation 暴露 zzz-boot-progress.patch 引用了前序栈不存在的
   XNU_SHENG_PMGR 代码块。已保存原始与适配 hash，并在
   patches/framework/0001-boot-progress-remove-unprovided-sheng-context.patch 中移除
   仅该专用进度标记 hunk。随后官方顺序的全部56条补丁成功应用到独立 Apple 源树。
   这不是未修改上游的结果：准确来源是固定官方提交加上述显式本地上下文修正。
   source-pthread 关闭；SOURCE_CORECRYPTO14 是构建命名空间 crypto 支持的选择，
   CRYPTO_BOOTSTRAP=prelinked 则在 prng_random 编译处关闭源提供者注册。
   这两个开关不能混为一谈：原始 crypto 提供者保留，而编译支持按官方 recipe 开启。

## 必须完成后才能出新镜像的项目

- 选择并记录连续交接 RAM 与完整硬件 RAM 库存的关系，不能抹掉第二 RAM bank 来让工具通过。
- 官方 DT binding 补齐已验证 p8,timer-intid/UFS 合约；检查统一 arm-io window 的地址翻译。
- 解决 common generator 发出的 CPU compatible 与现有原始 AppleARMCPU 的匹配边界；
  不重复 R13 的 CPU 配置 panic。
- 把本端实际使用的 CPU/UFS/PMGR/验证根/Data/显示/HID/观察器构建和初始化顺序，
  纳入完整的组件/辅助 archive 计划。当前根声明组件列表尚不覆盖所有板级自有 recipe。
- 按最后实际编译源码逐个对照 patch 语义。source pthread/corecrypto 与原始提供者保持互斥；
  A740 GPU 特定方案不当作 MSM8998 通用修复。
- 官方 OS 的四条 text_fixups 需要原字节与 kernel consumer 验证，当前输出未启用。
- 把已验证 edk2-msm8998 固件适配到官方 firmware/payload/bootimg 入口；当前不是 Mu-Silicium
  模板板卡，不能填写不存在的 DSC/配置名来假装成功。
- 核验最终合并 kernel/prelink、原始 Apple code hash、Data/ADT/参数、loader decode、
  固件/boot header 与所有者策略，再解除 guard，打包并建立新的离线门禁。

本次不启动新镜像。R7 是独立保留的运行实验；新的 UUID/身份/Data/PMGR 尚未合并为
可启动候选。不要将这里的 seed 或单独 ADT 直接替换进入旧镜像启动。

## 可重复命令

```sh
python3.13 .work/p21-official-framework/unixnu defconfig cheeseburger_defconfig -D . --out out/p21/official-adaptation/resolved
python3.13 .work/p21-official-framework/unixnu status --json -D . --out out/p21/official-adaptation/resolved
python3.13 scripts/p21-prepare-official-adt.py
python3.13 scripts/p21-build-pmgr.py
python3.13 scripts/p21-audit-official-patches.py
python3.13 .work/p21-official-framework/scripts/porting-record.py check --device .
```

Data 准备/所有权核验需要本机管理员授权，只操作上述项目输出；脚本拒绝覆盖现有输出。
本地 SoC override 在忽略的 local.toml；其尚未发布，不伪造 git pin。CI 保持手动。

## 2026-10-26 新候选打包（替代上文“尚未完成”状态）

新候选使用固定官方源码和56条 arch/OS 补丁，加两条显式板级集成补丁。
源码内核和全部板级对象重新编译；原始 Apple 组件由固定官方 XnuPrelink 合入。
UFS/RAM 共享的绝对父类绑定统一生成一次，并拒绝地址冲突。新预链接共核对
67793个指针绑定、263个绝对别名，原始组件代码哈希保持一致。另用官方
patch-xnu-entry-probe.py --panic-screen --no-reset 加入无自动重启的错误屏幕。

两个 RAM 描述符实际相邻，合并保持同一物理集合，未增加或丢失内存；EFI
可分配内存图仍负责排除保留区，交接窗口1GiB。中央 ADT 生成器的 GIC、UFS、
PHY物理翻译、19号timer、CPU0、root UUID、PackedRAMDisk及TrustCache占位符
已通过语义检查。四条官方 text-fixups 已在只读挂载的21H461 OS/SystemCryptex
原始文件对应偏移逐字节匹配；内核仅在签名页验证通过且原字节匹配时应用。

固件沿用本板实际 edk2-msm8998 适配，不伪装为Mu-Silicium。独立overlay中
接入固定官方DarwinBoot，保留候选SHA256检查；Entry.S仅将ASM_FUNC头文件映射
到本板ArmPkg提供的等价头，缓存维护与跳转指令保持。BootArgs和loader protocol
字节不变。已有P13只读载荷发现/解压及内存保留桥接继续使用，Data本身独立为
DBRDISK1，新skip-setup/no-auto-dim种子已接入。最终包的hash/输入/门禁以
out/p21/official-adaptation/offline-gate.json为准，不能引用R7旧门禁。

本板使用显式构建/打包适配脚本；通用Mu入口仍保留example guard，未宣称通用
firmware/bootimg CLI迁移已全部完成。此次新候选满足本板离线实验检查后，按用户
明确授权执行一次身份绑定的fastboot boot。此处所有检查是主机证据，实际系统
启动与交互结果另记boot-result.json及用户反馈；不刷写分区，不自动重复启动。

## R9 实机首个错误与 R10 修复

R9 已打印 PE_init_platform returned。随后用户记录的未定义指令 PC 对应本版
arm_init 中的 0xd500419f，即 msr PAN,#1。此前“三条色带”不是 MMU 失败的证明；
可见日志表明已进入内核平台初始化之后。官方 ARM64 common.config 中
CONFIG_XNU_NATIVE_PAN=y 被本板继承，这是迁移时遗漏的 CPU 能力覆盖项。
现已在 SoC 片段和设备 defconfig 显式禁用，通过官方现有非 PAN 分支编译，
不以 NOP 替换某一条指令。新 native __TEXT_EXEC 中不再有 MSR PAN,#0/#1。

按用户要求，R10只做本次修复相关编译/预链接/打包和PAN指令检查，不重跑完整审计。
保留R9可见日志实现（检查点文字仍带R9），新固件菜单标题为R10。
R10实机验证另行记录，不把代码消除或打包成功视为系统已启动。

## R11 控制台内部停顿与 R12

用户两次观察到PE_create_console enter后黑屏。实际pe_init编译记录显示
XNU_DT_PANIC_REGION=0，因此check_for_panic_log选择BCM构建模板的固定物理
地址0x3c0fc000/16KiB，并会读、清空该区。本板RAM库存从0x40000000开始，
不能把这个模板地址用于MSM8998。R12在设备/SoC配置启用官方已有DT panic
路径；ADT没有pram区域时直接返回，不另造缓冲区。新增函数内部进入/返回日志。
这是一项明确的配置纠正，是否解释全部黑屏仍需实机验证。

## R15 主显示对象缺失与入口修正

R14持久日志已通过Recovery取回（20261026T044653Z，62次提交、写入错误0）。
SpringBoard首次SIGTRAP以匹配21H461的cache符号定位到FrontBoardServices
`-[FBSDisplayMonitor _initWithBookendObserver:transformer:]`，断言为
`failed to find the main CADisplay`。不是由corpse提示推断的根因。

实际bsd_init的官方`native_mobile_graphics_load_probe`需要
`offline-mobile-graphics-load=1`，其内部才会在`offline-legacy-register=1`时
调用早期display probe。R14仅有后者，早期入口返回；晚期probe没有
pendingLegacyDisplay便返回。R15通过官方现有参数恢复这一入口，
未改写断言、CADisplay或虚构显示对象；保持原始ABI与GOP资源不变。
根board.toml同步当前已选运行参数，通用firmware guard继续保留。

持久快照新增DISPLAY stage/meta/reg/clients/frames和SWAP
check/reject/why/transfer/rc。先前运行日志独立保存；R15实际修复效果
须以本轮屏幕和下次Recovery取回的日志为准。

## R16 watchdog策略条件边界

R15回收日志20261026T050651Z：66次提交、错误0，显示stage10/reg1/clients3，
SpringBoard与backboardd无致命退出，但swap/transfer仍0。watchdogd两次SIGTRAP。
这证明R15显示入口已恢复，不证明watchdog是零帧唯一原因。

发现板级watchdog隔离调用错误嵌在XNU_BOOT_PROGRESS条件内，而配置值0；
R16将该调用移至进度条件之后，仍在Data成功挂载范围内。沿用开发版与显式
参数限制和安全文件创建逻辑，不触碰硬件watchdog，不关闭任何全局panic。
持久日志新增WATCHDOG requested/status/applied，供实机验证策略是否执行。

## R17 零提交诊断

R16日志20261026T052949Z有396次提交、错误0；watchdog requested1/status0/applied1，
watchdogd、SpringBoard、backboardd未记录致命退出。全部快照swap检查和transfer为0。
因此恢复watchdog策略后仍有独立的提交前等待。R17仅将已有客户端selector/通知端口、
vsync/prepare、原始显示状态字和AKS行接入持久日志，不更改调用返回值。

## R18 参考R4产帧证据恢复启动路径

用户要求直接制作修复并参考曾渲染版本。逐字节对比确认当前
NativeLegacyDisplay.cpp与R4相同；R4照片证据为6次成功传输、0次拒绝，
显示过升级UI，不代表桌面启动完成。R18保留当前官方内核/硬件适配、
显示加载门、watchdog修复和按键；恢复R4使用的P20R8原Data种子及
不启用AKS状态服务的选择。没有移植旧内核、改调试许可或放宽swap校验。
这是一组回归恢复候选，不宣称已找到零帧唯一原因；可能回到升级/设置UI，
而非直接桌面，且旧种子不含新skip-setup/no-auto-dim偏好。

补齐每UI角色最多24线程、每线程8个返回地址的有界采样；状态复制在
进程发布锁内完成，栈内存读取在解锁task/thread后由独立worker执行。
写入既有日志槽，满时优先保留线程/显示信息、截断旧控制台尾部。

## R17 D1 综合诊断（替代已停止的R18方向）

用户明确恢复R17诊断路线，R18候选未启动，不采用其旧Data/AKS关闭方案。
本次以R17的Data、ADT和全部原参数为基线，一次接入UI线程栈、IOKit请求/真实
迭代结果、24个系统服务、平台计数和上游实际可用的日志汇总。
范围、固定来源、未选择原因及日志边界见P21_R17_COMPLETE_DIAGNOSTICS.md。
只有诊断，不声称已修复零帧；实机结果由本轮Recovery日志另证。
