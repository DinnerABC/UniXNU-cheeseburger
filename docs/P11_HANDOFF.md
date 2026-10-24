# P11 接续记录：已按用户要求停止

## 停止现场

- 状态：STOPPED_AT_USER_REQUEST，机器可读记录 `out/p11/STOPPED.json`。
- 最后 run：`20261022T184732Z-8356a95a`，一次临时发送，240 秒观察后 NO_TRANSPORT_MANUAL_RECOVERY。
- 手机最后照片为 First Fatal：`Failed to mount root device @IOKitBSDInit.cpp:921`，Caller `0xfffffff007a07b7c`，Last bootstrap bsd_init。
- 不再编译、重启或重发。本轮未修改设备分区。下一次实机测试必须先由用户恢复 Android/Recovery 并确认在线。
- 项目构建 VM 已停止，证据 `out/p11/logs/stop-vm-after-v5-01.json`。设备测试进程已退出，未安排后台重试。

## 最后合格候选

| 文件 | SHA256 |
| --- | --- |
| out/p11/kernel/kernel.macho | 40a9449209885028c0b0c09d728aa8d79f2db1826a55948a61be8747d18760dd |
| out/p11/uefi/boot-p11-xnu.img | 57ce685b94469091fff85a4a8c782bd9c47b30561b69b150677ac4c157f894fe |

官方 prelink-v5；kernel 51544064 字节，boot image 57753600 字节。冻结源码、二进制、manifest 在 `out/p11/milestones/v5-read10/`。最终照片及转录在 `out/p11/evidence/v5-photos/`；v3/v4 的照片与全部历史版本也已保留。

## 已确认与未确认

AMFI 已完成可发现 PMGR 服务上的兼容调用，No PMGR 已越过；这不是 Qualcomm 硬件电源后端。原始 Image4/AMFI/Trust Cache、pthread 和固定普通 ARM64 XNU 保留。

UFS 实机完成 NOP、REPORT LUNS、6 个容量结果和只读 LUN 服务发布；LUN0 四次 READ10 传输（LBA 0、1、2、0，各 4096 字节）有直接照片。没有证明上层 IOMedia/GPT、根卷或 launchd。完整电子日志仍不可用，不把照片转录称作电子回收。

## 下一轮最小目标

沿实际 IOBlockStorageDriver → IOMedia → 分区识别链查找第一个未完成环节；必要时做只读结构/哈希交叉验证，不再重做早期 MMU、CPU、VM、安全组件或 PMGR 审计。当前最终 panic 来自根匹配/准备路径，尚无具体 APFS 挂载错误。核对根服务匹配及真实、版本一致的 iOS 根卷输入；不要简单把 Apple_HFS 字符串改为 APFS 就宣称可启动，也不编造 UUID。

先保留当前成功候选；没有具体新修复和人工恢复，不重试同一镜像。根卷/iOS 镜像准备仅限本地验证，持续禁止分区刷写。

## 构建接续

唯一 XNU 工作树 `.work/p4-unixnu/.cache/xnu-work`，保持 -j2，编译前停止 Linux VM。不要重新克隆或全量重编。UFS canonical 源码 `drivers/p11/NativeUfs/`，构建器不再从 P8 覆盖。

主要脚本：p11-build-pmgr.py、p11-build-ufs.py、p11-link-native.sh、p11-official-prelink.py、p11-audit-registration.py、p11-qualify-prelink.py、p11-check-integration.py、p11-build-payload.py、p11-prepare-uefi.py、p11-pack-image.py、p11-finalize.py。每次显式使用新 P11_PRELINK_VARIANT，勿误用旧默认 prelink-v2；命令、编译日志、退出码都在 out/p11/logs。控制器 tools/p11/device_loop.py 保留身份/备份/哈希和同镜像重复测试门禁。

不得修改 P1–P10、已测试快照或 P3 原始诊断工程。正式结果见 P11_BOOT_RESULT.md，实现与限制见 P11_IMPLEMENTATION.md。
