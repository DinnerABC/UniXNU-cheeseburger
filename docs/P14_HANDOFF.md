# P14 续接

先读取 out/p14/logs/*.json、candidate-check.json、uefi/packaging.json 和 boot-result.json（若存在）。只依据实际退出码继续。保留全部 P13 冻结产物，userdata 已部署且回读SHA匹配，禁止重刷、erase、GPT/boot等分区写入。

当前新增设备代码 drivers/p14/RootAuth 和 Capture；唯一 XNU 工作树中 BSD 认证位置已接入正式 owner trust。原 Apple ioctl 保留非零返回，与独立 owner 签名授权决定分开。真实 UFS 策略和块验证不变；不使用空接口、假 UUID 或假验签。

P14 原始中间构建移至 native-v1、kernel-v1、payload-v1、qualification-v1、uefi-v1，未发送。正式版本保留认证字段在 First Fatal 屏幕，正在完成最终增量链接/预链接及身份哈希更新。继续使用 P13 同样压缩协议，配置 pin 必须对应最终 payload，不能发送旧 pin 固件。

手机用户已恢复 Recovery。候选通过静态/真实签名及压缩回读门禁后，使用身份固定的单次 fastboot boot；不自动重刷或重复失败镜像。新 First Fatal 先保留屏幕。失联后停止该实机路径，需人工恢复才可下一轮。

此工程仍是 iPad7,1/17.7.11/21H461 技术参考用户空间，不等于最终 iPhone iOS。无 Data seed；SystemOS 上层准备及后续exec路径可能仍有真实阻塞，必须按首个实际错误处理。

## 正式候选已就绪并执行一次临时启动

boot 镜像：`out/p14/uefi/boot-p14-root-auth.img`，80,883,712 bytes，SHA256 `0afefc9dde0b3e58b9a9fa5d6ce660cff96fdaf75f90a7b42c8713c30d54d059`。正式构建日志：link-native-04、prelink-security-02、qualify-prelink-02、check-candidate-02、build-payload-02、build-uefi-02、pack-compressed-01 均退出0。冻结 P13 校验不变，原始 Apple 组件代码哈希保持一致。

首次启动前门禁因 ADB daemon 启动提示混入 `id -u` 查询结果而停止，未进入 Bootloader，也未发送镜像。只读核实 Recovery/root/cheeseburger 后，仅让 ADB 查询返回 stdout、仍保存完整 stderr 并检查失败信息，随后继续一次正式临时启动。记录 boot-root-auth-once-01/02；实际发送次数以boot-result.json为准，禁止盲目重复。

当前等待真实屏幕证据；不得提前报告 rootauth、SystemOS 或 launchd 成功。构建 VM 已正常停止。

最新实际状态：boot-root-auth-once-02退出0，Fastboot发送/Booting成功；唯一一次正式发送完成，之后一次USB观察ADB/Fastboot均离线。此状态不代表执行成功。已请求用户保留P14 First Fatal与ROOT AUTH字段，当前等待屏幕证据，不自动重复发送或恢复设备。

## 实机照片确认：owner 根认证通过，首个新阻塞为 exec ENOTSUP

照片已保存至 `out/p14/evidence/root-auth-accepted-launchd-enotsup.jpg`，SHA256 `8a2eb77e50fc7abe2807e1e8b8605a1abccf50203f83c08b412491feb9846e16`；逐项记录见 screen-result-01.json。

| 字段 | 实机值 |
| --- | --- |
| Apple FSIOC_KERNEL_ROOTAUTH | 0x50 = 80 = EAUTH，原值保留 |
| owner / accepted | 1 / 1 |
| 只读挂载 | 1 |
| mounted / selected / verified dev_t | 均为0x0100004f |
| IOMedia匹配数 / Provider深度 | 1 / 9 |
| 实际UUID / 策略UUID | 均为04769D26-2CA0-4825-B443-E2E5F88300F2 |
| UUID match | 1 |

这证明本轮设备根授权链实际成立，已越过P13根认证panic；不代表原始Apple根封印认证成功。原始Apple错误没有改为0，当前使用经过真实签名、块验证及实际挂载身份约束的owner路径。

新的首个致命错误：`Process 1 exec of /sbin/launchd failed, errno 45 @kern_exec.c:7197`。照片Caller为0xfffffff00bcbab4c，固定最终内核符号表对应 `_load_init_program + 0x1c0`。本分支errno.h确认45为ENOTSUP，80为EAUTH。源码说明此入口由BSD初始化完成后的首进程AST路径进入，因此BSD后续已运行到首次用户程序exec尝试；launchd自身尚未成功执行，没有SystemOS挂载或dyld运行的新证据。

不能从ENOTSUP单独认定是4KiB代码签名、AMFI或SystemOS某一项错误。当前 plain execve 调用不使用posix_spawn属性，kern_exec.c中两个spawn专用ENOTSUP赋值不能直接套用于本次。exec激活、原始安全策略及文件系统回调均可能传递错误；下一步应定位首个返回ENOTSUP的实际边界。固定上游也记录过owner根授权之后的launchd exec ENOTSUP，但其历史QEMU结果不是本机错误来源的独立证明。

当前停机现场已记录，未执行第二次实机启动、重刷或自动恢复。继续测试前需人工恢复原Recovery。保留当前已通过owner认证的P14候选，后续软件修复使用新变体和独立日志，不覆盖它。
