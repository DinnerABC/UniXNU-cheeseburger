# P4 续接点：2026-10-22

所有新产物在项目目录；保留 P1–P3。手机未刷写任何分区。

## 已完成

- Xcode 27.1 / 27A9275，iPhoneOS SDK 27.1，iig/migcom 可用，不再需要用户安装模拟器。
- **M1 内核真实编译、链接及验证通过**：out/cheeseburger/kernel/kernel.macho，18895248 字节，SHA256 5f39023168437d4cca9fde54ab07cbda2a50577b4479a04169fc24c4c0d1ae49；ARM64 MH_EXECUTE，LC_UNIXTHREAD=__start=0xfffffff0073e8488，未解析符号 0。
- build-xnu-06 的 make 成功，随后导出脚本错误寻找 _start 导致整体返回 1；已修正为真实 __start，validate-kernel-01 返回 0。没有掩盖原失败退出码。
- M2：从真实 Recovery FDT 生成最小 ADT（5208 字节），实际 DarwinBoot AdtNode C 解析器宿主验证通过；尚未证明内核消费该树。
- 实机 OCR：EL1，MPIDR=0x80000000，MIDR=0x51AF8014，CNTFRQ=19200000。config/p4/observed-registers.json。OCR 中 HPIDR/FU2 等拼写需视作识别误差，保留来源，不声称原始电子日志。
- 自动临时启动与自动返回 Android 已实测。out/p4/logs/automatic-roundtrip-02.json；run_id=20261021T194215Z-9f486d60。主机观察到 RETURNED_ANDROID_ADB，用户确认未按键。第一次误分类保留并有 correction 文件。
- P3 全部 58 分区/12 GPT 区间备份复核通过，见 out/p4/logs/p3-backup-integrity.log。

## 主线下一步

尚未跳转 XNU，尚无内核初始化/根卷/launchd 证据。正在实现 Android ramdisk 只读内存载荷，提供 kernel.macho、sidecar ADT 与 UniXNU 配置给 DarwinBoot，不写手机分区。

ArmCpu 的 timer 27 硬编码已改为读取实际 ADT 的 PPI19，patches/p4/armcpu-timer-from-adt.patch；尚未编译驱动。当前内核没有预链接 ArmCpu/ArmDTPlatform，也没有接入已编译 crypto provider，均为运行阻塞候选，不能声称完整平台可用。

唯一 XNU 工作树 .work/p4-unixnu/.cache/xnu-work。主机16GiB：MAKEJOBS=-j2，大规模 XNU 编译时停用 Linux VM。当前内核不需要重编。P4 UEFI 复用 guest /home/dinnerabc.guest/unixnu-p3/diagnostic，不能修改上游原目录。

UEFI 自动返回定时器仅 BootServices 有效，ExitBootServices 后不能恢复内核挂死。环形日志仍为易失内存，不声称跨重启保留或电子日志自动回传。失联停止重试，允许请求一次硬件重启。

## 2026-10-22 07:00 无人值守续接

- 已确认无旧编译/测试进程；设备为 ANDROID_ADB，见 out/p4/logs/device-status-7am.log。无需人工恢复。
- RAM payload UEFI 完整构建 build-rampayload-04 返回0；boot-ram-probe.img 回读验证后实测自动返回 Android，run_id=20261021T200317Z-8210007d。没有 RAM FS 被实机读取的屏幕/电子证据；默认只返回固件，不跳内核。
- 真实只读 EFI 文件回调宿主测试通过；CRC/越界/截断/哈希门禁5项测试通过。
- ArmCpu（已修复 timer PPI19）、ArmDTPlatform 和完整 POST crypto bootstrap 均实际编译成库。上文“尚未编译驱动”已被此项新结果取代。
- 首次平台候选链接因沙箱 sysctl 失败；7点以授权本机环境重试 link-platform-kernel-02 成功，94.58秒，退出0。
- 独立候选 out/p4/kernel-platform/kernel.macho：19261032字节，SHA256 244706016549da5a9e3ef927a94faa564c93cb83a3322e8ec8252fb04003ab80，入口0xfffffff0073fc488，未解析符号0，POST HMAC已按正常构建约定填充，但未执行运行自检。基线内核保留原路径及 out/p4/kernel-baseline/。
- **尚未跳转 XNU**。候选仍缺 KernelConfigTables 的 ArmCpu/ArmDTPlatform 匹配项，ADT 缺平台驱动要求的 CHRP NVRAM proxy。下一步修复这两项，明确 proxy 为易失软件策略，不能冒称手机真实 NVRAM；然后验证初期控制台及 DarwinBoot 加载。
- 5份 P4 文档已齐。pipeline 已补齐 payload/UEFI/bootimg 真实命令和依赖，静态校验通过；尚未完整端到端验收。
- 用户要求暂不问问题。无通信/高风险时只保存现场、停止该测试路径；继续独立可编译工作。不要把缺少自动日志的内核挂死当成功。
- 7点一次性自动任务已触发，处理后删除，不保留每日重复唤醒。VM已停止，主机编译限制为2任务。
