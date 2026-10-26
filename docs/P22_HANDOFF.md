# UniXNU-cheeseburger 交接：下一步切换 iOS

更新：2026-10-27

项目：`/Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger`
仓库：https://github.com/DinnerABC/UniXNU-cheeseburger

## 当前状态

设备为一加5 / MSM8998 / Snapdragon835，6GB内存，1080×1920。
当前系统是 **iPadOS17.7.11 / 21H461，XNU10063.141.1**，CPU软件渲染，无GPU加速。

**确认成功的基线是P21-UP1：单CPU、1GiB，用户确认原始iPadOS界面和时间可见。**
触摸和八核还没有确认成功，不要把最新镜像当作已验证基线。

| 版本 | 结果 |
| --- | --- |
| UP1 | 原始界面和时间可见；日志transfer75/reject0 |
| P22初版 | 集成QUP5/S3320/RMI4/HID；日志显示触摸在时钟检查error=-51停止，未进行I²C/HID通信 |
| R2 / R3 | 4GiB / 2GiB均报`NO WINDOW`，未进入XNU |
| R4 | 恢复1GiB，8核；用户报告ticket lock，地址对应`pset0+0x100`区域，未取得完整调用栈 |
| R5 | 保留8核、1GiB、vnode33792；改通用单SMP调度域、保留完整MPIDR；已临时启动，**实机结果待用户确认** |

最后发送的镜像：
`out/p22/r5/uefi/boot-p22-r5-native-smp-1g.img`

SHA256：`b0657d32460b7961e3ed73fb4a86c690967855e6645263d727ef83dfb7ad083e`

可回退的UP1镜像：`out/p21/upstream-a/uefi/boot-p21-up1.img`

SHA256：`7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`

## 已做的修改与文件位置

- `package/BoardRuntime/CheeseburgerTouch.cpp`、`SynapticsRMI4.h`：原生QUP5 PIO、
  RMI4/F01/F12运行时查询、多点快照、方向处理、40Hz轮询，接官方HID消费者。
- `NativeHidInput.cpp`、`HidInputProbe.cpp`、`LegacyDisplayProbe.cpp`：触摸HID接入。
  不再使用sheng的30480/20320坐标常量。
- `BootLog.c`、`CompactObserver.c`：触摸摘要、实际CPU在线数及每核状态。
- `DisplayObservation.c`：诊断绘屏与系统scanout的跨核排斥。
- `patches/xnu/0007-cheeseburger-eight-core-topology.patch`：R5的通用八核容量，
  用独立`XNU_NATIVE_AFFINITY16`处理次核入口，未启用Apple专用`HAS_CLUSTER`。
- `soc/msm8998/patches/components/ArmCpu-smp-bootstrap-controller.patch`：
  修正中断控制器等待全部CPU、但次核尚未启动的顺序问题。

触摸实际接线：S3320，QUP5 `0x0c179000`，I²C地址0x20，SDA87/SCL88、reset89、IRQ125。
R2以后已修正合法XO时钟配置0被误判的问题；后续触点和界面响应尚待验证。

固定官方framework：`16e592abee8a550dec8154ed1f38b5ebc4f16213`。
ArmCpu官方提交：`9066b53bbc82e92a98ec44a11dd50fd13eaf1a1b`，核对时没有更新修复。
Linux触摸参考固定为`6b4b543fa18757297ac756a6fe6e64f506b7844a`，新协议/总线文件为GPL-2.0-only。

构建入口：`scripts/p22-compile-r5.py`；板级archive：`scripts/p21-build-board-runtime.py`；
本地打包脚本：`out/p22/r5/scripts/finish-build.py`。
使用Python3.13、Xcode、Homebrew LLVM/GNU Make、P2 Lima虚拟机，重型构建`-j2`。
`out/`、`.work/`和私有输入不进Git；当前不是干净clone后一条命令即可重建的完整分发。
历史R2/R3/R4脚本依赖当时共享树，不要重跑并覆盖历史镜像。

## 日志与证据

必须保留：启动可见文字、first-fatal/no-reset、蓝字观察器、RAM ring、持久日志、Recovery回收。
现有日志窗口仍在userdata相对10GiB处，32MiB；具体布局用
`out/p21/r14/log-window.json`，新系统镜像不能覆盖它。

Recovery回收：

```sh
cd /Volumes/NAS-MacData/Project/IOS/UniXNU-cheeseburger
/opt/homebrew/opt/python@3.13/bin/python3.13 scripts/logging/read-cheeseburger-log.py
```

关键日志：`out/p21/persistent-logs/20261026T130815Z`是UP1；
`20261026T134118Z`是P22触摸-51；最后回收`20261026T164846Z`仍是旧boot_id/230条，
没有记录新R4 panic。不要把旧日志或Fastboot接收当成新系统成功。

## 给下一个模型的提示词（直接复制）

你接手上述项目。下一步先将系统内容从iPadOS换成真正的iPhone iOS，准备对应的
系统镜像和userdata部署产物，并保留现有日志系统。先读本文件、AGENTS.md和官方文档，
以本地实际代码、镜像和用户最新反馈为准，不要覆盖已有产物。

**用户明确要求：只操作userdata分区，不修改其他分区和参数。**
不要改boot/recovery/固件、内核、ADT、启动参数、CPU、内存、vnode或分区几何。
这轮“编译打包iOS”指从真实固件提取、准备和组装可供现有启动栈使用的系统内容，
不是顺便替换内核，也不是改个iPhone名称来伪装iOS。

1. 先盘点本地固件和现有启动输入，核对官方已有案例，选择能与冻结内核、原始Apple
   组件ABI、签名/owner验证、root匹配和窗口布局相容的真实iPhone固件。
   当前固定上游只有iPadOS17.7.11 profile，没有已验证的iPhone iOS profile。
   检查实际BuildManifest、arm64/arm64e、PAC、dyld cache及IOKit接口，不要按相近版本号猜兼容。
2. 用户没有指定iOS版本。优先选择有源码/二进制依据、改动最少的版本。
   [Apple iOS17.7.2](https://support.apple.com/en-ca/121754)适用iPhoneXS及以后；
   [iOS16.7.11](https://support.apple.com/en-la/122346)包含iPhone8/8Plus/X。
   这些仅是筛选线索，不代表与本机内核兼容，不要编造“iPhone版17.7.11”。
3. 在`out/p23/`准备原始Root/SystemOS/Cryptex等所需内容、匹配Data和userdata镜像/补丁，
   保存来源、哈希、实际覆盖范围及可恢复的旧系统。不要直接套旧Data、Persona、
   TrustCache、签名fixup偏移或机型参数。保持现有外部root匹配和启动输入不变。
4. 保留全部日志功能和32MiB日志窗口。userdata更新前回收当前日志并备份实际覆盖范围；
   使用已绑定身份的Recovery连接，仅更新userdata内明确的目标窗口，不格式化整盘。
   本任务允许userdata范围内迁移，不必对常规读取、下载、备份和镜像准备反复提问。
5. 如果目标iOS必须修改冻结的内核、ABI绑定、信任输入或其他参数才能启动，
   清楚说明具体冲突和所需最小额外变更，不能擅自扩大范围，也不能宣称已迁移成功。
6. 准备完成后做一次受控验证，记录原始iPhone UI和日志。当前R5多核仍未确认，
   不要把它当已修复，也不要在本任务顺手展开SMP、GPU、USB串口或触摸固件研究。

尽快推进实际产物，不只写方案；优先复用官方实现。只做与迁移直接相关的检查，
不用逐组件多镜像实验；不要自动重启循环，不运行GitHub Actions。
如需子代理，最多两名低档且任务有边界，禁止再派生。

最终给出选定iOS版本、userdata产物、修改范围、部署/恢复记录、日志位置和真实结果。
若现有启动栈不兼容，就交付具体阻塞与已准备产物，不通过改名或修改其他参数“完成”任务。
