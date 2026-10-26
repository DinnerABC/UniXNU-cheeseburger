# P21 当前候选与实机验收

更新：2026-10-26。历史P19/P20镜像与哈希不改。此前曾显示Press home to upgrade的R4属于历史UI证据，不能移植为当前R17成功。

| 候选 | SHA256 | 主机/传输证据 | 实机证据 |
| --- | --- | --- | --- |
| R17 D1 | 776d35e9607ac5b737968928a6ab6fe378c054b053d2b7deac9769489f812972 | 编译/打包通过，一次临时boot接受 | 回收100次日志、写错0，零swap，采到Chrono等待 |
| R17 D2 | e60f3d0823fbdf5089aa1659fe6c7d7c5b9ed8077dc9c5f90d02a1787da2e85a | 编译/打包及Chrono解析通过，一次临时boot接受 | 回收96次/485秒、写错0，零swap，采到ActivityKit同步等待 |

R17 D2最终kernel SHA256：b25e238fad807a010768a5564573b4ecd09fc321cef796aebc1e1152d72e6a6e。
源构建使用固定XNU10063.141.1与原始21H461 kernelcache；源码及脚本清单以构建目录和官方来源库存为准。本轮不声称脏工作区存在单一覆盖所有本地适配的Git commit。

## U0–U4

- U0：当前日志支持原始用户空间、Data/Root路径继续；未对全磁盘重新做读回验证，不能宣称整机回归全部通过。
- U1：usermanagerd运行，早期manifest崩溃未复现；完整Persona创建清单不足，部分验证。
- U2：容器/基础服务运行并有容器路径缓存；不是所有服务调用语义已验证。
- U3：backboardd已连接显示，stage10/reg1/clients4；IOSurface有效提交和软件首帧未证实，部分验证。
- U4：当前候选未达到，无当前Hello/Setup/SpringBoard照片，不宣称成功。

## 当前本轮

只读集中审计与静态分析，没有新候选，没有新boot命令。设备保持Recovery。仍可沿用identity-pinned fastboot boot机制；下一候选必须有明确增量和独立门禁，不能重复发送已失败SHA。持久日志窗口写入授权保持既有32MiB范围，不写GPT/OS/SystemOS/userdata其他区域。

证据：out/p21/r17-diag2/{boot-result,runtime-result}.json；out/p21/persistent-logs/20261026T074907Z/；out/p21/userland-audit/。

## UP1 原始界面实机进展

2026-10-26，用户确认UP1出现原始系统界面及时间。镜像SHA256 `7cf2fef9dea9103e860db4358697b4d2e67d550928b5deb3afac67c5d4309323`，一次临时启动。该证据区别于历史R4升级界面：这是UP1本轮的直接用户确认，但未提供照片或等同像素捕获，因此U4原始界面里程碑为用户确认、正式照片证据待补；不将其提升为触摸可用或Home交互完成。下一阶段修复实际触摸输入，保留有效启动策略。
