# P21-R5 与 ios_sagit 同芯片输入/唤醒对照

用户提供仓库 https://github.com/MC-XiaoXiao/ios_sagit ，本轮读取固定 commit `898998cf1b74ea103e0c16d6dc0016822d8d644b`。源码与资料位于 out/p21/input-research/ios_sagit。

实际读取 QcomButtons.cpp、ButtonState.h、SPMIArbiter.h、QcomSPMI.cpp、button-routing.dts、HomeProtocol.h、gpio README 和 logical-wake-review-1.0.23.md。tests/buttons_test.cpp 两次公开下载失败，未声称运行该仓库测试；本机 R5 自身测试已运行。

| 项目 | sagit | 一加5 R5 |
|---|---|---|
| SPMI | v2/v3，通过PPID动态查APID；observer读命令 | 同一读取公式，EE0、动态查APID，限界observer映射；不引入IRQ/PMIC配置 |
| 电源 | SID0 PON 0x810 bit0，Consumer Power 0x30 | 相同硬件状态语义和HID usage，官方NativeHidInput报文bit1 |
| 音量加 | GPIO6，低有效，0xe9 | 一加5资料同为GPIO6，报文bit2 |
| 音量减 | PON RESIN bit1 | 一加5Linux设备树为GPIO5，低有效，报文bit3；不能照搬sagit |
| 防抖 | 每键15ms及真实中断/恢复 | 10ms轮询，两次稳定采样；不声明深度休眠IRQ唤醒 |
| Home | Consumer Menu 0x40；指纹QSEE确认状态，IRQ不能代替按压状态 | 官方HID已有同usage；实体指纹/电容Home未适配，以双音量组合测试一次Home |
| 目标ABI | iOS18 / 22H374私有类及硬件DPU | iPadOS17 / 21H461原始IOHIDDevice207槽/208字节已核验；不搬私有偏移 |

## 对黑屏判断的作用

逻辑唤醒评审明确区分 UI screen-off 与面板供电：面板/DPU/DSI保持供电时，CoverSheet仍可进入逻辑关屏。此为该仓库基于iOS18固件静态分析的结论，可支持分层诊断，但不是本机黑屏因果证明。

该报告的1.0.23修改是同一worker中 PP_DONE/VBL 合并事件的时序：先记录真实VBL，再完成旧传输，再通知订阅者，排除完成回调耗时污染。报告明确主机模型/交叉编译通过，未新做实机测试。本机目前CPU写GOP+软件vsync，没有对应硬件PP_DONE/VBL事件，故不直接合并，也不伪造硬件VBL。

## 本轮决定

R5现有电源/音量读取协议、HID usage与同芯片实现相符；保留一加5接线及目标ABI。无需改变已完成镜像。先按用户已确认的Recovery执行一次临时启动，用KEY raw/edges/delivered、HID report结果和frames验证。后续若KEY变化而HID不响应，查原生匹配/策略；若HID成功但UI不醒，查逻辑PM/vsync及最后帧，而非直接接管背光或伪造完成。
