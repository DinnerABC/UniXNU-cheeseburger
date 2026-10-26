# P21-UP1 上游启动策略实验

2026-10-26。A=冻结R17 D2，B=out/p21/upstream-a。用户明确授权可信、版本匹配且可撤销的上游补丁先行实验，不要求先证明完整Chrono根因。

## 来源与范围

来源IssacL04/UniXNU-Pagani_SM8750，提交02c7ccd3286b661bef4513a7de50dba5153f8e6d，实际文件scripts/prepare-ram-data.py的DISABLED_JOBS与prepare_launch_policy，说明STAGE6.md。该提交存在，原文件保存在候选upstream目录。该版本属于维护者后来报告达到UI的build39组合；不能将组合成功单独归因于本次选出的两条规则。

只移植identityservicesd、locationd；watchdogd规则保留。不引入CommCenter、bluetoothd、PerfPowerServices、iomfb_fdr_loader、iconservicesagent，也不禁用chronod/liveactivitiesd/图形核心服务。原始21H461的三个Label和完整ProgramArguments均通过原镜像只读核对，记录upstream/original-jobs.json。

## 实际修改

- BootWatchdogPolicy.c：复用同一个发布入口；新增cheeseburger-upstream-userland=1，DEVELOPMENT/DEBUG显式启用时生成完整三作业配置。
- BootUserlandPolicy.h：保存原watchdog XML和三作业XML、纯策略选择函数。不开新参数时原watchdog开关行为保持；新参数启用时也保留watchdog隔离。RELEASE关闭实验。
- BootLog.c：新增UP1 requested/status/written摘要；written仅指文件发布，不等于launchd采用。
- BootProcessProbe.c：将两个迁移服务重复别名槽换为identityservicesd/locationd，保留原datamigrator/migrationpluginwrapper名称槽及S/B/C栈，行数不增加。

仍只在RAM Data创建disabled.plist和空disabled.migrated，O_EXCL/O_NOFOLLOW、root:root0644、fsync/fstat与大小检查保留，冲突拒绝覆盖。原OS、launchd、签名、内核异常处理、硬件watchdog均未改。

源码前后快照保存在out/p21/upstream-a/source-snapshot；来源及哈希在patch-provenance.json。关闭新参数即可退回原策略；正式保留与否等待B运行证据，不默认长期启用。

## 输入与验证

R17 Data/ADT/AKS/显示/Trust Cache/日志窗口保留；CPU1/CPU显示，无Metal。核对kern.maxvnodes=17408，本轮不改。四项官方text_fixups通过继承原ADT保留。

三种构建模式的策略选择、合法plist、规则精确集合检查通过，见tests/policy-check.json。独立只读审查确认默认关闭、RELEASE关闭、原watchdog规则和排他创建边界保留。只运行必要构建门禁，不新增大规模模拟。

首次构建因归档生成后补入两个进程观察槽，被源码哈希门禁正确拒绝；未打包/启动该失败输入。固定源码后重新生成受影响归档；只有通过最终哈希门禁的镜像可发送。

## 实机判定

先核对UP1文件发布状态、watchdog状态，再看新增作业是否出现/是否有launchd配置采用证据。对比同485秒窗口的SpringBoard/chronod等待、liveactivitiesd、display/swap/transfer和首帧。不能用PID总数下降或文件写入成功代替功能进展。

若无收益，标记tested-no-demonstrated-benefit，不再发送同SHA；不叠加其它作业规则。候选运行结果另见P21_UPSTREAM_BOOT_COMPARISON.md。
