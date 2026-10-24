# P11 实机结果（本轮已停止）

2026-10-23，按用户要求完成证据归档和报告后停止。本轮没有执行任何 flash、erase、GPT 或设备分区写入。

## 最终结论

1. **No PMGR 已越过**：AMFI 实际调用 PMGR 兼容 READ/OR/WRITE。原始“不支持硬件 AON”返回及无硬件写语义，不代表 Qualcomm 电源控制或安全锁存器已实现。
2. **UFS 枚举与初步读取已取得实机证据**：6 个只读 LUN 服务发布；LUN0 上四次 READ10 响应传输成功，每次 4096 字节，LBA 依次为 0、1、2、0。
3. **未进入 iOS 用户空间**：最终仍在 bsd_init 的根设备匹配/准备路径触发 `Failed to mount root device @IOKitBSDInit.cpp:921`。
4. **尚未证明** IOMedia 发布、GPT 解析、APFS 根卷挂载、launchd 或完整磁盘读取稳定性。成功传输不等于已核验盘上内容；日志点也早于目标缓冲 writeBytes，不能据此宣称所有上层读取完成。

## 最终候选和测试

- kernel：`out/p11/kernel/kernel.macho`，51544064 字节。
- SHA256：`40a9449209885028c0b0c09d728aa8d79f2db1826a55948a61be8747d18760dd`。
- boot image：`out/p11/uefi/boot-p11-xnu.img`，57753600 字节。
- SHA256：`57ce685b94469091fff85a4a8c782bd9c47b30561b69b150677ac4c157f894fe`。
- run：`20261022T184732Z-8356a95a`；temporary-boot 退出 0。
- 240 秒观察：NO_TRANSPORT_MANUAL_RECOVERY，整体测试退出 2。屏幕证据确认了内核执行深度；发送成功、失联、执行成功分别记录，不互相替代。
- 原照片和 SHA256、转录：`out/p11/evidence/v5-photos/`。
- 最终冻结源码与二进制：`out/p11/milestones/v5-read10/`。

## 照片中的 LUN

| LUN | 块字节数 | 最后 LBA | 只读服务发布 |
| --- | --- | --- | --- |
| 0 | 4096 | 14145535 | 是 |
| 1 | 4096 | 1023 | 是 |
| 2 | 4096 | 1023 | 是 |
| 3 | 4096 | 32767 | 是 |
| 4 | 4096 | 1048575 | 是 |
| 5 | 4096 | 393215 | 是 |

NOP、REPORT LUNS received=56/entries=6，以及 enumeration complete usable=6 均有照片。仅 LUN0 的上述四次读取有直接成功照片，不推定其他 LUN 已读成功。

## 当前错误的含义

最终 Caller=`0xfffffff007a07b7c`，Last bootstrap=`bsd_init`。固定源码在 root matching 的等待/失败分支触发此 panic（条件包含无匹配 service 或重试次数达到上限），不是已经取得 APFS 挂载错误码。源码节选保存在 `out/p11/evidence/v5-photos/root-match-source.txt`。

此前照片显示等待 IOMedia Content=Apple_HFS；当前参数没有指定经过核验的 iOS 根卷。必须进一步区分 IOBlockStorageDriver/IOMedia、分区识别、根匹配及真实系统卷输入，不能仅凭 panic 断言 APFS 驱动坏了，不能编造 UUID 或写盘“修复”。未验证的实时 EL1 寄存器仍不作为异常原因。

## 迭代记录

| 版本 | 真实结果与修复 |
| --- | --- |
| v1 | PMGR Nub 误接管 DT 根，平台匹配失败；改为独立描述节点 |
| v2 | PMGR 兼容调用与 BSD 有证据；REPORT LUNS 失败 |
| v3 | 修复有效短响应被拒绝；实机返回 6 条目，发布 LUN0/3；读取仍失败 |
| v4 | 全部容量查询完成后才发布服务，消除共享 DMA 并发缺陷；设备仍明确拒绝 READ16，标识匹配，key=5/ASC=24/00 |
| v5 | 按固定 EDK2 布局使用有界 READ10；6 个 LUN 发布及四次块传输成功；根设备问题未解决 |

全部版本、失败日志和命令退出码保留。构建 VM 已停止，测试工具已结束；未再发送镜像，没有自动复位或电子内核日志回收成功声明。
